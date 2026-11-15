package com.genymobile.scrcpy.web;

import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Minimal HTTP + WebSocket server streaming the video to web browsers on the local network.
 * <pre>
 *   GET /        the player page
 *   GET /status  JSON (clients, video size, counters)
 *   GET /ws      WebSocket
 * </pre>
 * WebSocket messages sent to the browser:
 * <ul>
 *   <li>text: a JSON "hello" (video size, Quest parameters), sent on connection and whenever something changes;</li>
 *   <li>binary: {@code [type:1][flags:1][pts:8 (µs, big-endian)][payload]}, where type is 1 for the codec config (SPS/PPS) or 2 for a frame
 *   (flags bit 0: key frame), and the payload is H.264 Annex B as produced by MediaCodec.</li>
 * </ul>
 * Text messages accepted from the browser: {@code p} (keep-alive, answered by a "pong" JSON), {@code key} (request a key frame),
 * {@code set <key>=<value>} (change a Quest parameter).
 * <p>
 * This class has no Android dependency (it is unit-tested on the JVM).
 */
public final class WebServer {

    public interface Logger {
        void log(String message);
    }

    public interface Listener {
        /** The last client has left: the capture should be suspended. */
        void onIdle();

        /** A client needs a key frame as soon as possible. */
        void onKeyFrameRequested();

        /** A parameter changed, the video size may be different: the capture should be restarted. */
        void onRestartRequested();
    }

    private static final String WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    public static final int TYPE_CONFIG = 1;
    public static final int TYPE_FRAME = 2;
    public static final int FLAG_KEY_FRAME = 1;
    private static final int MESSAGE_HEADER_SIZE = 10;

    private static final int MAX_CLIENTS = 8;
    private static final int MAX_REQUEST_SIZE = 8192;
    private static final int MAX_QUEUED_BYTES = 6 * 1024 * 1024;
    private static final int MAX_QUEUED_PACKETS = 180;
    // The page sends a keep-alive message every 3 seconds
    private static final int CLIENT_TIMEOUT_MS = 20_000;
    private static final long STALLED_TIMEOUT_MS = 10_000;
    private static final long KEY_FRAME_REQUEST_INTERVAL_MS = 500;

    private static final byte[] PONG = encodeText("{\"type\":\"pong\"}");

    private final int port;
    private final byte[] page;
    private final QuestParams questParams; // may be null
    private final Logger logger;

    private volatile Listener listener;

    private ServerSocket serverSocket;
    private Thread acceptThread;
    private volatile boolean stopped;

    private final Object lock = new Object();
    private final List<Client> clients = new ArrayList<>(); // guarded by lock
    private int width; // guarded by lock
    private int height; // guarded by lock
    private byte[] configMessage; // guarded by lock
    private long lastKeyFrameRequest; // guarded by lock

    private final long startTime = System.currentTimeMillis();
    private volatile long frameCount;
    private volatile long byteCount;

    public WebServer(int port, byte[] page, QuestParams questParams, Logger logger) {
        this.port = port;
        this.page = page;
        this.questParams = questParams;
        this.logger = logger;
    }

    public void setListener(Listener listener) {
        this.listener = listener;
    }

    public void start() throws IOException {
        serverSocket = new ServerSocket();
        serverSocket.setReuseAddress(true);
        serverSocket.bind(new InetSocketAddress(port));
        acceptThread = new Thread(this::acceptLoop, "web-accept");
        acceptThread.setDaemon(true);
        acceptThread.start();
        logger.log("Web server listening on port " + getPort());
    }

    public int getPort() {
        return serverSocket != null ? serverSocket.getLocalPort() : port;
    }

    public void stop() {
        stopped = true;
        try {
            if (serverSocket != null) {
                serverSocket.close();
            }
        } catch (IOException e) {
            // ignore
        }
        List<Client> copy;
        synchronized (lock) {
            copy = new ArrayList<>(clients);
            lock.notifyAll();
        }
        for (Client client : copy) {
            client.close();
        }
    }

    public int getClientCount() {
        synchronized (lock) {
            return clients.size();
        }
    }

    /**
     * Block until at least one client is connected.
     *
     * @return {@code false} if the server has been stopped
     */
    public boolean awaitClient() {
        synchronized (lock) {
            while (clients.isEmpty() && !stopped) {
                try {
                    lock.wait();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return false;
                }
            }
            return !stopped;
        }
    }

    /**
     * To be called when a capture session starts (before its first packet).
     */
    public void setVideoSize(int width, int height) {
        List<Client> copy;
        synchronized (lock) {
            this.width = width;
            this.height = height;
            configMessage = null;
            copy = new ArrayList<>(clients);
        }
        byte[] hello = encodeText(buildHello());
        for (Client client : copy) {
            client.restart(hello);
        }
    }

    /**
     * Send a packet produced by the encoder to all the clients.
     */
    public void writePacket(ByteBuffer buffer, long pts, boolean config, boolean keyFrame) {
        int size = buffer.remaining();
        byte[] message = new byte[frameHeaderSize(MESSAGE_HEADER_SIZE + size) + MESSAGE_HEADER_SIZE + size];
        int offset = writeFrameHeader(message, 0x2, MESSAGE_HEADER_SIZE + size);
        message[offset] = (byte) (config ? TYPE_CONFIG : TYPE_FRAME);
        message[offset + 1] = (byte) (keyFrame ? FLAG_KEY_FRAME : 0);
        for (int i = 0; i < 8; ++i) {
            message[offset + 2 + i] = (byte) (pts >>> (56 - 8 * i));
        }
        buffer.get(message, offset + MESSAGE_HEADER_SIZE, size);

        List<Client> copy;
        byte[] currentConfig;
        synchronized (lock) {
            if (config) {
                configMessage = message;
            }
            currentConfig = configMessage;
            copy = new ArrayList<>(clients);
        }

        if (config) {
            // The clients receive the config with the next key frame
            for (Client client : copy) {
                client.waitKeyFrame();
            }
            return;
        }

        ++frameCount;
        byteCount += size;

        boolean requestKeyFrame = false;
        for (Client client : copy) {
            requestKeyFrame |= client.offer(message, keyFrame, currentConfig);
        }
        if (requestKeyFrame) {
            requestKeyFrame();
        }
    }

    private void requestKeyFrame() {
        // Several clients (or a slow one) may ask for a key frame repeatedly: do not flood the encoder
        long now = System.currentTimeMillis();
        synchronized (lock) {
            if (now - lastKeyFrameRequest < KEY_FRAME_REQUEST_INTERVAL_MS) {
                return;
            }
            lastKeyFrameRequest = now;
        }
        Listener l = listener;
        if (l != null) {
            l.onKeyFrameRequested();
        }
    }

    private String buildHello() {
        int w;
        int h;
        synchronized (lock) {
            w = width;
            h = height;
        }
        return "{\"type\":\"hello\",\"version\":1,\"codec\":\"h264\",\"width\":" + w + ",\"height\":" + h + ",\"quest\":"
                + (questParams != null ? questParams.toJson() : "null") + "}";
    }

    private void broadcastHello() {
        byte[] hello = encodeText(buildHello());
        List<Client> copy;
        synchronized (lock) {
            copy = new ArrayList<>(clients);
        }
        for (Client client : copy) {
            client.sendControl(hello);
        }
    }

    private void acceptLoop() {
        while (!stopped) {
            Socket socket;
            try {
                socket = serverSocket.accept();
            } catch (IOException e) {
                if (!stopped) {
                    logger.log("Web server accept error: " + e.getMessage());
                }
                break;
            }
            Thread thread = new Thread(() -> handleConnection(socket), "web-conn");
            thread.setDaemon(true);
            thread.start();
        }
    }

    private void handleConnection(Socket socket) {
        try {
            socket.setTcpNoDelay(true);
            socket.setSoTimeout(CLIENT_TIMEOUT_MS);
            InputStream in = socket.getInputStream();
            OutputStream out = socket.getOutputStream();

            String request = readRequest(in);
            if (request == null) {
                return;
            }
            String[] lines = request.split("\r\n");
            String[] requestLine = lines[0].split(" ");
            if (requestLine.length < 2 || !"GET".equals(requestLine[0])) {
                writeResponse(out, "405 Method Not Allowed", "text/plain", "Method not allowed\n".getBytes(StandardCharsets.UTF_8));
                return;
            }
            String path = requestLine[1];
            int query = path.indexOf('?');
            if (query != -1) {
                path = path.substring(0, query);
            }

            switch (path) {
                case "/":
                case "/index.html":
                    writeResponse(out, "200 OK", "text/html; charset=utf-8", page);
                    break;
                case "/status":
                    writeResponse(out, "200 OK", "application/json", buildStatus().getBytes(StandardCharsets.UTF_8));
                    break;
                case "/ws":
                    String key = getHeader(lines, "sec-websocket-key");
                    if (key == null) {
                        writeResponse(out, "400 Bad Request", "text/plain", "WebSocket expected\n".getBytes(StandardCharsets.UTF_8));
                        break;
                    }
                    out.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
                            + computeAccept(key) + "\r\n\r\n").getBytes(StandardCharsets.US_ASCII));
                    out.flush();
                    runClient(socket, in, out);
                    break;
                default:
                    writeResponse(out, "404 Not Found", "text/plain", "Not found\n".getBytes(StandardCharsets.UTF_8));
                    break;
            }
        } catch (IOException e) {
            // connection closed
        } finally {
            try {
                socket.close();
            } catch (IOException e) {
                // ignore
            }
        }
    }

    private static String readRequest(InputStream in) throws IOException {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        int state = 0; // number of consecutive characters of "\r\n\r\n" matched
        while (state < 4) {
            int c = in.read();
            if (c == -1 || buffer.size() >= MAX_REQUEST_SIZE) {
                return null;
            }
            buffer.write(c);
            if (c == (state % 2 == 0 ? '\r' : '\n')) {
                ++state;
            } else {
                state = c == '\r' ? 1 : 0;
            }
        }
        return new String(buffer.toByteArray(), StandardCharsets.ISO_8859_1);
    }

    private static String getHeader(String[] lines, String lowerCaseName) {
        for (int i = 1; i < lines.length; ++i) {
            int sep = lines[i].indexOf(':');
            if (sep != -1 && lines[i].substring(0, sep).trim().toLowerCase(Locale.ENGLISH).equals(lowerCaseName)) {
                return lines[i].substring(sep + 1).trim();
            }
        }
        return null;
    }

    private static void writeResponse(OutputStream out, String status, String contentType, byte[] body) throws IOException {
        String headers = "HTTP/1.1 " + status + "\r\nContent-Type: " + contentType + "\r\nContent-Length: " + body.length
                + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        out.write(headers.getBytes(StandardCharsets.US_ASCII));
        out.write(body);
        out.flush();
    }

    private String buildStatus() {
        int w;
        int h;
        int count;
        synchronized (lock) {
            w = width;
            h = height;
            count = clients.size();
        }
        return "{\"clients\":" + count + ",\"width\":" + w + ",\"height\":" + h + ",\"frames\":" + frameCount + ",\"bytes\":" + byteCount
                + ",\"uptime_s\":" + (System.currentTimeMillis() - startTime) / 1000 + ",\"quest\":"
                + (questParams != null ? questParams.toJson() : "null") + "}\n";
    }

    static String computeAccept(String key) {
        try {
            MessageDigest sha1 = MessageDigest.getInstance("SHA-1");
            return base64(sha1.digest((key + WS_GUID).getBytes(StandardCharsets.US_ASCII)));
        } catch (NoSuchAlgorithmException e) {
            throw new AssertionError(e);
        }
    }

    // java.util.Base64 requires API 26
    static String base64(byte[] data) {
        final String chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < data.length; i += 3) {
            int b0 = data[i] & 0xff;
            int b1 = i + 1 < data.length ? data[i + 1] & 0xff : 0;
            int b2 = i + 2 < data.length ? data[i + 2] & 0xff : 0;
            builder.append(chars.charAt(b0 >> 2));
            builder.append(chars.charAt((b0 & 0x3) << 4 | b1 >> 4));
            builder.append(i + 1 < data.length ? chars.charAt((b1 & 0xf) << 2 | b2 >> 6) : '=');
            builder.append(i + 2 < data.length ? chars.charAt(b2 & 0x3f) : '=');
        }
        return builder.toString();
    }

    private static int frameHeaderSize(int payloadSize) {
        return payloadSize < 126 ? 2 : payloadSize < 65536 ? 4 : 10;
    }

    // Write the header of an unmasked WebSocket frame (FIN set), return its size
    private static int writeFrameHeader(byte[] out, int opcode, int payloadSize) {
        out[0] = (byte) (0x80 | opcode);
        if (payloadSize < 126) {
            out[1] = (byte) payloadSize;
            return 2;
        }
        if (payloadSize < 65536) {
            out[1] = 126;
            out[2] = (byte) (payloadSize >> 8);
            out[3] = (byte) payloadSize;
            return 4;
        }
        out[1] = 127;
        for (int i = 0; i < 4; ++i) {
            out[2 + i] = 0;
            out[6 + i] = (byte) (payloadSize >>> (24 - 8 * i));
        }
        return 10;
    }

    private static byte[] encodeFrame(int opcode, byte[] payload) {
        byte[] frame = new byte[frameHeaderSize(payload.length) + payload.length];
        int offset = writeFrameHeader(frame, opcode, payload.length);
        System.arraycopy(payload, 0, frame, offset, payload.length);
        return frame;
    }

    private static byte[] encodeText(String text) {
        return encodeFrame(0x1, text.getBytes(StandardCharsets.UTF_8));
    }

    private void runClient(Socket socket, InputStream in, OutputStream out) {
        Client client = new Client(socket, out);
        byte[] currentConfig;
        synchronized (lock) {
            if (stopped || clients.size() >= MAX_CLIENTS) {
                return;
            }
            clients.add(client);
            currentConfig = configMessage;
            lock.notifyAll();
            logger.log("Web client connected: " + socket.getRemoteSocketAddress() + " (" + clients.size() + " connected)");
        }
        client.sendControl(encodeText(buildHello()));
        if (currentConfig != null) {
            // A capture is running: the client can only start on a key frame
            requestKeyFrame();
        }
        client.startWriter();

        try {
            readMessages(client, in);
        } catch (IOException e) {
            // connection closed or timeout
        } finally {
            client.close();
            boolean idle;
            synchronized (lock) {
                clients.remove(client);
                idle = clients.isEmpty();
                logger.log("Web client disconnected: " + socket.getRemoteSocketAddress() + " (" + clients.size() + " connected)");
            }
            Listener l = listener;
            if (idle && l != null && !stopped) {
                l.onIdle();
            }
        }
    }

    private static void readFully(InputStream in, byte[] buffer, int length) throws IOException {
        int offset = 0;
        while (offset < length) {
            int r = in.read(buffer, offset, length - offset);
            if (r == -1) {
                throw new EOFException();
            }
            offset += r;
        }
    }

    private void readMessages(Client client, InputStream in) throws IOException {
        byte[] header = new byte[8];
        byte[] payload = new byte[1024];
        while (true) {
            try {
                readFully(in, header, 2);
            } catch (SocketTimeoutException e) {
                logger.log("Web client timeout");
                return;
            }
            int opcode = header[0] & 0xf;
            boolean masked = (header[1] & 0x80) != 0;
            int length = header[1] & 0x7f;
            if (length == 126) {
                readFully(in, header, 2);
                length = (header[0] & 0xff) << 8 | (header[1] & 0xff);
            } else if (length == 127) {
                // Not expected from the page
                return;
            }
            if (!masked || length > payload.length) {
                // A client must always mask its frames; the messages from the page are short
                return;
            }
            byte[] mask = new byte[4];
            readFully(in, mask, 4);
            readFully(in, payload, length);
            for (int i = 0; i < length; ++i) {
                payload[i] ^= mask[i & 3];
            }

            switch (opcode) {
                case 0x1: // text
                    handleCommand(client, new String(payload, 0, length, StandardCharsets.UTF_8));
                    break;
                case 0x8: // close
                    return;
                case 0x9: // ping
                    byte[] data = new byte[length];
                    System.arraycopy(payload, 0, data, 0, length);
                    client.sendControl(encodeFrame(0xA, data));
                    break;
                default:
                    // ignore
                    break;
            }
        }
    }

    private void handleCommand(Client client, String command) {
        if ("p".equals(command)) {
            // keep-alive: answer, so that the page can detect a dead connection even if the picture is static (no frames)
            client.sendControl(PONG);
            return;
        }
        if ("key".equals(command)) {
            client.waitKeyFrame();
            requestKeyFrame();
            return;
        }
        if (command.startsWith("set ") && questParams != null) {
            int sep = command.indexOf('=');
            if (sep == -1) {
                return;
            }
            String key = command.substring(4, sep).trim();
            String value = command.substring(sep + 1).trim();
            boolean wasDevice = questParams.isDevice();
            int result = questParams.set(key, value);
            if (result == QuestParams.UNCHANGED) {
                return;
            }
            logger.log("Quest parameter set from the web page: " + key + "=" + value);
            // The video size changes only if the processing is (or was) done on the device
            boolean restart = result == QuestParams.CHANGED_SIZE && (wasDevice || questParams.isDevice());
            Listener l = listener;
            if (restart && l != null) {
                // The new parameters are sent with the new video size
                l.onRestartRequested();
            } else {
                broadcastHello();
            }
        }
    }

    private final class Client {
        private final Socket socket;
        private final OutputStream out;
        private final ArrayDeque<byte[]> queue = new ArrayDeque<>();
        private int queuedBytes;
        private boolean waitKeyFrame = true;
        private boolean closed;
        private long lastProgress = System.currentTimeMillis();
        private Thread writer;

        Client(Socket socket, OutputStream out) {
            this.socket = socket;
            this.out = out;
        }

        void startWriter() {
            writer = new Thread(this::writeLoop, "web-writer");
            writer.setDaemon(true);
            writer.start();
        }

        private synchronized void enqueue(byte[] message) {
            queue.add(message);
            queuedBytes += message.length;
            notifyAll();
        }

        synchronized void sendControl(byte[] message) {
            if (!closed) {
                enqueue(message);
            }
        }

        synchronized void waitKeyFrame() {
            waitKeyFrame = true;
        }

        // A new capture session starts: forget the pending packets of the previous one
        synchronized void restart(byte[] hello) {
            if (closed) {
                return;
            }
            queue.clear();
            queuedBytes = 0;
            waitKeyFrame = true;
            enqueue(hello);
        }

        /**
         * Queue a frame for this client.
         *
         * @return {@code true} if a key frame must be requested
         */
        synchronized boolean offer(byte[] message, boolean keyFrame, byte[] config) {
            if (closed) {
                return false;
            }
            if (queuedBytes > MAX_QUEUED_BYTES || queue.size() > MAX_QUEUED_PACKETS) {
                // The client is too slow (or gone): drop everything, it will resume from a key frame
                queue.clear();
                queuedBytes = 0;
                waitKeyFrame = true;
                if (System.currentTimeMillis() - lastProgress > STALLED_TIMEOUT_MS) {
                    logger.log("Web client stalled, closing");
                    close();
                    return false;
                }
            }
            if (waitKeyFrame) {
                if (!keyFrame) {
                    return true;
                }
                waitKeyFrame = false;
                if (config != null) {
                    enqueue(config);
                }
            }
            enqueue(message);
            return false;
        }

        private void writeLoop() {
            try {
                while (true) {
                    byte[] message;
                    synchronized (this) {
                        while (queue.isEmpty() && !closed) {
                            wait();
                        }
                        if (closed) {
                            return;
                        }
                        message = queue.poll();
                        queuedBytes -= message.length;
                    }
                    out.write(message);
                    out.flush();
                    synchronized (this) {
                        lastProgress = System.currentTimeMillis();
                    }
                }
            } catch (IOException e) {
                // connection closed
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            } finally {
                close();
            }
        }

        void close() {
            synchronized (this) {
                if (closed) {
                    return;
                }
                closed = true;
                queue.clear();
                queuedBytes = 0;
                notifyAll();
            }
            try {
                // Unblock the reader and the writer
                socket.close();
            } catch (IOException e) {
                // ignore
            }
        }
    }
}

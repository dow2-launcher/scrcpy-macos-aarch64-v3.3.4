package com.genymobile.scrcpy.web;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class WebServerTest {

    private static final byte[] PAGE = "<html>questview</html>".getBytes(StandardCharsets.UTF_8);

    private WebServer server;
    private QuestParams params;
    private final AtomicInteger keyFrameRequests = new AtomicInteger();
    private final AtomicInteger restartRequests = new AtomicInteger();
    private CountDownLatch idle;

    @Before
    public void setUp() throws IOException {
        params = new QuestParams();
        idle = new CountDownLatch(1);
        server = new WebServer(0, PAGE, params, message -> { });
        server.setListener(new WebServer.Listener() {
            @Override
            public void onIdle() {
                idle.countDown();
            }

            @Override
            public void onKeyFrameRequested() {
                keyFrameRequests.incrementAndGet();
            }

            @Override
            public void onRestartRequested() {
                restartRequests.incrementAndGet();
            }
        });
        server.start();
    }

    @After
    public void tearDown() {
        server.stop();
    }

    private String httpGet(String path) throws IOException {
        try (Socket socket = new Socket("127.0.0.1", server.getPort())) {
            socket.getOutputStream().write(("GET " + path + " HTTP/1.1\r\nHost: test\r\n\r\n").getBytes(StandardCharsets.US_ASCII));
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            InputStream in = socket.getInputStream();
            byte[] buffer = new byte[4096];
            int r;
            while ((r = in.read(buffer)) != -1) {
                out.write(buffer, 0, r);
            }
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    // Minimal WebSocket client
    private static final class Client implements AutoCloseable {
        private final Socket socket;
        private final DataInputStream in;
        private final OutputStream out;

        Client(int port) throws IOException {
            socket = new Socket("127.0.0.1", port);
            socket.setSoTimeout(5000);
            in = new DataInputStream(socket.getInputStream());
            out = socket.getOutputStream();
            out.write(("GET /ws HTTP/1.1\r\nHost: test\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                    + "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n").getBytes(StandardCharsets.US_ASCII));
            StringBuilder response = new StringBuilder();
            while (response.indexOf("\r\n\r\n") == -1) {
                response.append((char) in.readUnsignedByte());
            }
            Assert.assertTrue(response.toString().startsWith("HTTP/1.1 101 "));
            // Value from RFC 6455
            Assert.assertTrue(response.toString().contains("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"));
        }

        // Return the payload, prefixed by the opcode
        byte[] read() throws IOException {
            int opcode = in.readUnsignedByte() & 0xf;
            int length = in.readUnsignedByte();
            Assert.assertEquals("server frames must not be masked", 0, length & 0x80);
            if (length == 126) {
                length = in.readUnsignedShort();
            } else if (length == 127) {
                length = (int) in.readLong();
            }
            byte[] message = new byte[1 + length];
            message[0] = (byte) opcode;
            in.readFully(message, 1, length);
            return message;
        }

        String readText() throws IOException {
            byte[] message = read();
            Assert.assertEquals(1, message[0]);
            return new String(message, 1, message.length - 1, StandardCharsets.UTF_8);
        }

        void sendText(String text) throws IOException {
            byte[] payload = text.getBytes(StandardCharsets.UTF_8);
            byte[] frame = new byte[6 + payload.length];
            frame[0] = (byte) 0x81;
            frame[1] = (byte) (0x80 | payload.length);
            frame[2] = 1;
            frame[3] = 2;
            frame[4] = 3;
            frame[5] = 4;
            for (int i = 0; i < payload.length; ++i) {
                frame[6 + i] = (byte) (payload[i] ^ frame[2 + (i & 3)]);
            }
            out.write(frame);
            out.flush();
        }

        @Override
        public void close() throws IOException {
            socket.close();
        }
    }

    @Test
    public void testHttp() throws IOException {
        String page = httpGet("/");
        Assert.assertTrue(page.startsWith("HTTP/1.1 200 OK\r\n"));
        Assert.assertTrue(page.endsWith("\r\n\r\n<html>questview</html>"));
        Assert.assertTrue(httpGet("/?player=mse").startsWith("HTTP/1.1 200 OK\r\n"));

        String status = httpGet("/status");
        Assert.assertTrue(status.contains("\"clients\":0"));
        Assert.assertTrue(status.contains("\"process\":\"device\""));

        Assert.assertTrue(httpGet("/nothing").startsWith("HTTP/1.1 404 "));
        Assert.assertTrue(httpGet("/ws").startsWith("HTTP/1.1 400 "));
    }

    @Test
    public void testBase64() {
        Assert.assertEquals("", WebServer.base64(new byte[0]));
        Assert.assertEquals("Zg==", WebServer.base64("f".getBytes(StandardCharsets.US_ASCII)));
        Assert.assertEquals("Zm8=", WebServer.base64("fo".getBytes(StandardCharsets.US_ASCII)));
        Assert.assertEquals("Zm9vYmFy", WebServer.base64("foobar".getBytes(StandardCharsets.US_ASCII)));
    }

    private static void assertPacket(byte[] message, int type, boolean keyFrame, long pts, byte[] payload) {
        Assert.assertEquals("binary message", 2, message[0]);
        ByteBuffer buffer = ByteBuffer.wrap(message, 1, message.length - 1);
        Assert.assertEquals(type, buffer.get());
        Assert.assertEquals(keyFrame ? 1 : 0, buffer.get());
        Assert.assertEquals(pts, buffer.getLong());
        byte[] data = new byte[buffer.remaining()];
        buffer.get(data);
        Assert.assertArrayEquals(payload, data);
    }

    @Test
    public void testStream() throws Exception {
        byte[] config = {0, 0, 0, 1, 0x67, 1, 2, 3, 0, 0, 0, 1, 0x68, 4};
        byte[] key = new byte[70000]; // more than 65535 bytes: 64-bit WebSocket length
        key[4] = 0x65;
        byte[] delta = {0, 0, 0, 1, 0x41, 9};

        Assert.assertEquals(0, server.getClientCount());
        try (Client client = new Client(server.getPort())) {
            Assert.assertTrue(server.awaitClient());
            Assert.assertEquals(1, server.getClientCount());
            Assert.assertTrue(client.readText().contains("\"width\":0"));

            // A capture session starts
            server.setVideoSize(1200, 1080);
            String hello = client.readText();
            Assert.assertTrue(hello.contains("\"type\":\"hello\""));
            Assert.assertTrue(hello.contains("\"width\":1200,\"height\":1080"));
            Assert.assertTrue(hello.contains("\"eye\":\"left\""));

            server.writePacket(ByteBuffer.wrap(config), 0, true, false);
            // A frame which is not a key frame cannot be decoded by a new client: it is dropped, and a key frame is requested
            server.writePacket(ByteBuffer.wrap(delta), 1000, false, false);
            Assert.assertEquals(1, keyFrameRequests.get());
            server.writePacket(ByteBuffer.wrap(key), 2000, false, true);
            server.writePacket(ByteBuffer.wrap(delta), 0x123456789AL, false, false);

            assertPacket(client.read(), WebServer.TYPE_CONFIG, false, 0, config);
            assertPacket(client.read(), WebServer.TYPE_FRAME, true, 2000, key);
            assertPacket(client.read(), WebServer.TYPE_FRAME, false, 0x123456789AL, delta);

            // Keep-alive
            client.sendText("p");
            Assert.assertTrue(client.readText().contains("pong"));

            // A parameter applied without restart is sent to all the clients
            client.sendText("set angle=21.3");
            Assert.assertTrue(client.readText().contains("\"angle\":21.3000"));
            Assert.assertEquals(0, restartRequests.get());

            // A parameter changing the picture size restarts the capture (the new hello is sent with the new size)
            client.sendText("set preset=A");
            client.sendText("p");
            Assert.assertTrue(client.readText().contains("pong"));
            Assert.assertEquals(1, restartRequests.get());

            // A second client joins while the capture is running: it starts on the next key frame, with the config
            try (Client second = new Client(server.getPort())) {
                Assert.assertTrue(second.readText().contains("\"preset\":\"A\""));
                long deadline = System.currentTimeMillis() + 5000;
                while (server.getClientCount() < 2 && System.currentTimeMillis() < deadline) {
                    Thread.sleep(10);
                }
                Assert.assertEquals(2, server.getClientCount());
                server.writePacket(ByteBuffer.wrap(delta), 3000, false, false);
                server.writePacket(ByteBuffer.wrap(key), 4000, false, true);
                assertPacket(second.read(), WebServer.TYPE_CONFIG, false, 0, config);
                assertPacket(second.read(), WebServer.TYPE_FRAME, true, 4000, key);
                // The first client receives everything
                assertPacket(client.read(), WebServer.TYPE_FRAME, false, 3000, delta);
                assertPacket(client.read(), WebServer.TYPE_FRAME, true, 4000, key);
            }
        }

        // The last client has left: the capture must be suspended
        Assert.assertTrue(idle.await(5, TimeUnit.SECONDS));
        Assert.assertEquals(0, server.getClientCount());
    }
}

// Fake device for the web mode: run the real web server of scrcpy-server (com.genymobile.scrcpy.web, no Android dependency) on this
// machine, and feed it with a recorded H.264 stream (with access unit delimiters) in a loop, like the video encoder does on the device.
//
//   javac -d /tmp/webfeed server/src/main/java/com/genymobile/scrcpy/web/*.java release/quest/e2e/WebFeed.java
//   java -cp /tmp/webfeed WebFeed 8090 release/quest/e2e/test.h264 2064 1104 30 page \
//       server/src/main/resources/questweb/index.html
//
// then open http://localhost:8090/ (see release/quest/e2e/web_test.js for the automated check).

import com.genymobile.scrcpy.web.QuestParams;
import com.genymobile.scrcpy.web.WebServer;

import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

public final class WebFeed {

    private WebFeed() {
    }

    private static int find(byte[] data, byte[] pattern, int from) {
        outer:
        for (int i = from; i + pattern.length <= data.length; ++i) {
            for (int j = 0; j < pattern.length; ++j) {
                if (data[i + j] != pattern[j]) {
                    continue outer;
                }
            }
            return i;
        }
        return -1;
    }

    public static void main(String[] args) throws Exception {
        int port = Integer.parseInt(args[0]);
        byte[] data = Files.readAllBytes(Paths.get(args[1]));
        int width = Integer.parseInt(args[2]);
        int height = Integer.parseInt(args[3]);
        double fps = Double.parseDouble(args[4]);
        QuestParams params = new QuestParams();
        params.set("process", args[5]);
        byte[] page = Files.readAllBytes(Paths.get(args[6]));

        byte[] aud = {0, 0, 0, 1, 9};
        byte[] sps = {0, 0, 1, 0x67};
        byte[] pps = {0, 0, 1, 0x68};
        byte[] startCode = {0, 0, 1};
        byte[] idr = {0, 0, 1, 0x65};
        List<byte[]> units = new ArrayList<>();
        for (int pos = find(data, aud, 0); pos != -1;) {
            int next = find(data, aud, pos + aud.length);
            int end = next == -1 ? data.length : next;
            byte[] unit = new byte[end - pos];
            System.arraycopy(data, pos, unit, 0, unit.length);
            units.add(unit);
            pos = next;
        }
        // The codec config is the SPS and the PPS of the first access unit
        byte[] first = units.get(0);
        int spsPos = find(first, sps, 0);
        int ppsPos = find(first, pps, 0);
        int configEnd = ppsPos == -1 ? -1 : find(first, startCode, ppsPos + pps.length);
        if (spsPos == -1 || configEnd == -1 || find(first, idr, 0) == -1) {
            throw new IllegalArgumentException("The stream must start with SPS, PPS and an IDR frame");
        }
        byte[] config = new byte[configEnd - spsPos];
        System.arraycopy(first, spsPos, config, 0, config.length);
        System.out.println(units.size() + " access units, config " + config.length + " bytes");

        AtomicBoolean restart = new AtomicBoolean();
        WebServer server = new WebServer(port, page, params, System.out::println);
        server.setListener(new WebServer.Listener() {
            @Override
            public void onIdle() {
                restart.set(true);
            }

            @Override
            public void onKeyFrameRequested() {
                // A recorded stream cannot produce a key frame on demand: restart from the beginning
                restart.set(true);
            }

            @Override
            public void onRestartRequested() {
                restart.set(true);
            }
        });
        server.start();

        long pts = 0;
        while (true) {
            if (server.getClientCount() == 0) {
                System.out.println("No web client: capture suspended");
            }
            server.awaitClient();
            restart.set(false);
            server.setVideoSize(width, height);
            server.writePacket(ByteBuffer.wrap(config), 0, true, false);
            long start = System.nanoTime();
            long count = 0;
            session:
            while (true) {
                for (byte[] unit : units) {
                    boolean key = find(unit, idr, 0) != -1;
                    if (restart.get() && key && count > 0) {
                        break session;
                    }
                    server.writePacket(ByteBuffer.wrap(unit), pts, false, key);
                    pts += (long) (1e6 / fps);
                    ++count;
                    long delay = start + (long) (count * 1e9 / fps) - System.nanoTime();
                    if (delay > 0) {
                        Thread.sleep(delay / 1_000_000, (int) (delay % 1_000_000));
                    }
                    if (server.getClientCount() == 0) {
                        break session;
                    }
                }
            }
        }
    }
}

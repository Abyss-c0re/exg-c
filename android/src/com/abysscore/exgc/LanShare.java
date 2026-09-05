package com.abysscore.exgc;

import java.nio.charset.Charset;

/** Map/settings copy over LAN HTTP. No radio. */
final class LanShare {
    private static final Charset UTF8 = Charset.forName("UTF-8");

    private LanShare() {}

    static void sendKit() {
        byte[] kit = kitBytes();
        if (kit == null) {
            return;
        }
        httpKit(true, new String(kit, UTF8));
    }

    static void takeKit() {
        httpKit(false, null);
    }

    static void copyBoth() {
        sendKit();
        takeKit();
    }

    private static byte[] kitBytes() {
        String s = ExgNative.kitExport();
        if (s == null || s.length() < 1) {
            return null;
        }
        return s.getBytes(UTF8);
    }

    private static String[] destParts() {
        String d = ExgNative.linkDest();
        if (d == null || d.length() < 1 || d.startsWith("bt:")) {
            return null;
        }
        int sl = d.lastIndexOf('/');
        if (sl > 0) {
            d = d.substring(0, sl);
        }
        int c = d.lastIndexOf(':');
        if (c < 1) {
            return new String[] {d, "8765"};
        }
        if (c + 1 >= d.length() || d.charAt(c + 1) < '0' || d.charAt(c + 1) > '9') {
            return new String[] {d, "8765"};
        }
        return new String[] {d.substring(0, c), d.substring(c + 1)};
    }

    private static void httpKit(final boolean post, final String body) {
        final String[] p = destParts();
        if (p == null) {
            return;
        }
        new Thread(() -> {
            try {
                int hp = Integer.parseInt(p[1]);
                java.net.Socket s = new java.net.Socket();
                s.connect(new java.net.InetSocketAddress(p[0], hp), 800);
                s.setSoTimeout(1500);
                String tok = ExgNative.linkToken();
                String req;
                if (post) {
                    byte[] b = body.getBytes(UTF8);
                    req = "POST /kit HTTP/1.0\r\nHost: x\r\nContent-Length: " + b.length
                            + "\r\n";
                    if (tok != null && tok.length() > 0) {
                        req += "X-EXG-Token: " + tok + "\r\n";
                    }
                    req += "\r\n";
                    s.getOutputStream().write(req.getBytes(UTF8));
                    s.getOutputStream().write(b);
                } else {
                    req = "GET /kit HTTP/1.0\r\nHost: x\r\n";
                    if (tok != null && tok.length() > 0) {
                        req += "X-EXG-Token: " + tok + "\r\n";
                    }
                    req += "\r\n";
                    s.getOutputStream().write(req.getBytes(UTF8));
                }
                java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
                byte[] buf = new byte[512];
                int r;
                java.io.InputStream in = s.getInputStream();
                while ((r = in.read(buf)) > 0) {
                    bos.write(buf, 0, r);
                }
                s.close();
                if (!post) {
                    String all = bos.toString("UTF-8");
                    int sp = all.indexOf("\r\n\r\n");
                    if (sp >= 0) {
                        ExgNative.kitImport(all.substring(sp + 4));
                    }
                }
            } catch (Exception ignored) {
            }
        }, "kit-lan").start();
    }
}

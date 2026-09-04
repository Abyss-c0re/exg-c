package com.abysscore.exgc;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothServerSocket;
import android.bluetooth.BluetoothSocket;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;

/**
 * Bluetooth handshake, then EXG on the same link.
 * Wifi address is optional spare. Names are device names.
 */
public final class BtPair {
    static final UUID SVC = UUID.fromString("c0de8e6c-6578-4000-8000-6578672d6331");
    private static final Charset UTF8 = Charset.forName("UTF-8");

    public interface ScanSink {
        void found(String name, BluetoothDevice dev);
        void done();
    }

    public interface FollowSink {
        void ok(String name);
        void no(String why);
    }

    private static volatile boolean listen;
    private static volatile boolean followLive;
    private static volatile byte[] pendingKit;
    private static volatile boolean pendingWant;
    private static Thread listenThr;
    private static BluetoothServerSocket server;
    private static volatile BluetoothSocket followSock;
    private static BroadcastReceiver scanRx;
    private static final Map<String, BluetoothDevice> found = new LinkedHashMap<String, BluetoothDevice>();

    private BtPair() {}

    static String selfName() {
        String m = Build.MODEL;
        if (m == null || m.length() < 1) {
            return "exg";
        }
        return m.replace(' ', '_');
    }

    static String wifiHost(Context c) {
        try {
            WifiManager wm = (WifiManager) c.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm == null) {
                return "";
            }
            int ip = wm.getConnectionInfo().getIpAddress();
            if (ip == 0) {
                return "";
            }
            return String.format(Locale.US, "%d.%d.%d.%d", ip & 255, (ip >> 8) & 255,
                    (ip >> 16) & 255, (ip >> 24) & 255);
        } catch (RuntimeException e) {
            return "";
        }
    }

    static void shareStart() {
        if (listen) {
            return;
        }
        BluetoothAdapter ad = BluetoothAdapter.getDefaultAdapter();
        if (ad == null) {
            return;
        }
        listen = true;
        listenThr = new Thread(() -> {
            try {
                server = ad.listenUsingInsecureRfcommWithServiceRecord("exg", SVC);
            } catch (Exception e) {
                listen = false;
                return;
            }
            while (listen && server != null) {
                try {
                    final BluetoothSocket s = server.accept();
                    new Thread(() -> {
                        try {
                            handleIn(s);
                        } catch (Exception ignored) {
                        } finally {
                            try {
                                s.close();
                            } catch (Exception ignored) {
                            }
                        }
                    }, "exg-out").start();
                } catch (Exception ignored) {
                }
            }
        }, "exg-share");
        listenThr.setDaemon(true);
        listenThr.start();
    }

    static void shareStop() {
        listen = false;
        if (server != null) {
            try {
                server.close();
            } catch (Exception ignored) {
            }
            server = null;
        }
    }

    private static void handleIn(BluetoothSocket s) throws Exception {
        BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), UTF8));
        OutputStream out = s.getOutputStream();
        String name = "exg";
        String grantIn = "";
        String line;
        while ((line = in.readLine()) != null) {
            if (line.startsWith("NAME ")) {
                name = line.substring(5).trim();
            } else if (line.startsWith("GRANT ")) {
                grantIn = line.substring(6).trim();
            } else if (line.equals("PAIR") || line.equals("GRANT")) {
                break;
            }
        }
        if (grantIn.length() > 0 && ExgNative.grantOk(grantIn)) {
            writeOk(out, grantIn);
            new Thread(() -> {
                try {
                    readPeer(s.getInputStream(), out);
                } catch (Exception ignored) {
                }
            }, "exg-in").start();
            pumpOut(out);
            return;
        }
        int st = ExgNative.pairBegin(name);
        long t0 = System.currentTimeMillis();
        while (st == 1 && System.currentTimeMillis() - t0 < 60000) {
            try {
                Thread.sleep(200);
            } catch (InterruptedException ignored) {
            }
            st = ExgNative.pairState();
        }
        if (st == 2) {
            writeOk(out, ExgNative.pairGrant());
            new Thread(() -> {
                try {
                    readPeer(s.getInputStream(), out);
                } catch (Exception ignored) {
                }
            }, "exg-in").start();
            pumpOut(out);
        } else {
            out.write("NO\n".getBytes(UTF8));
        }
    }

    private static void writeOk(OutputStream out, String grant) throws Exception {
        String host = wifiHost(ExgNativeApp.ctx);
        int http = ExgNative.apiHttp();
        int udp = ExgNative.apiUdp();
        if (http < 1) {
            http = 8765;
        }
        if (udp < 1) {
            udp = http + 1;
        }
        String msg = "OK\nGRANT " + (grant == null ? "" : grant) + "\nHOST " + host + "\nPORT "
                + http + "\nUDP " + udp + "\nLIVE\n";
        out.write(msg.getBytes(UTF8));
    }

    private static void pumpOut(OutputStream out) throws Exception {
        byte[] frame = new byte[68];
        long lastCfg = 0;
        int lastSeq4 = -1;
        while (listen) {
            int n = ExgNative.copyExg1(frame);
            if (n >= 68) {
                int seq = (frame[4] & 255) | ((frame[5] & 255) << 8);
                if (seq != lastSeq4) {
                    out.write(frame, 0, n);
                    lastSeq4 = seq;
                }
            }
            long now = System.currentTimeMillis();
            if (now - lastCfg >= 250) {
                String js = ExgNative.viewJson();
                if (js != null && js.length() > 0) {
                    byte[] jsb = js.getBytes(UTF8);
                    int ln = jsb.length;
                    if (ln > 1400) {
                        ln = 1400;
                    }
                    out.write(new byte[] {'C', 'F', 'G', '1', (byte) (ln & 255),
                            (byte) ((ln >> 8) & 255)});
                    out.write(jsb, 0, ln);
                }
                lastCfg = now;
            }
            if (pendingWant) {
                pendingWant = false;
                pendingKit = kitBytes();
            }
            if (pendingKit != null) {
                byte[] kit = pendingKit;
                pendingKit = null;
                writeKit(out, kit);
            }
            try {
                Thread.sleep(8);
            } catch (InterruptedException e) {
                return;
            }
        }
    }

    private static byte[] kitBytes() {
        String k = ExgNative.kitExport();
        return (k == null || k.length() < 8) ? null : k.getBytes(UTF8);
    }

    private static void writeKit(OutputStream out, byte[] kit) throws Exception {
        if (kit == null || kit.length < 1) {
            return;
        }
        int ln = kit.length;
        if (ln > 8000) {
            ln = 8000;
        }
        out.write(new byte[] {'K', 'I', 'T', '1', (byte) (ln & 255), (byte) ((ln >> 8) & 255)});
        out.write(kit, 0, ln);
    }

    private static void readPeer(java.io.InputStream in, OutputStream out) throws Exception {
        byte[] mag = new byte[4];
        while (listen) {
            if (!readFull(in, mag, 4)) {
                return;
            }
            if (mag[0] == 'W' && mag[1] == 'A' && mag[2] == 'N' && mag[3] == 'T') {
                writeKit(out, kitBytes());
            } else if (mag[0] == 'K' && mag[1] == 'I' && mag[2] == 'T' && mag[3] == '1') {
                applyKit(in);
            } else {
                return;
            }
        }
    }

    private static void applyKit(java.io.InputStream in) throws Exception {
        byte[] ln = new byte[2];
        if (!readFull(in, ln, 2)) {
            return;
        }
        int n = (ln[0] & 255) | ((ln[1] & 255) << 8);
        if (n < 8 || n > 8000) {
            return;
        }
        byte[] kit = new byte[n];
        if (!readFull(in, kit, n)) {
            return;
        }
        ExgNative.kitImport(new String(kit, UTF8));
    }

    static void scan(Context c, ScanSink sink) {
        found.clear();
        BluetoothAdapter ad = BluetoothAdapter.getDefaultAdapter();
        if (ad == null) {
            sink.done();
            return;
        }
        final boolean[] finished = { false };
        final ScanSink once = new ScanSink() {
            @Override
            public void found(String name, BluetoothDevice dev) {
                sink.found(name, dev);
            }

            @Override
            public void done() {
                synchronized (finished) {
                    if (finished[0]) {
                        return;
                    }
                    finished[0] = true;
                }
                try {
                    ad.cancelDiscovery();
                } catch (SecurityException ignored) {
                }
                if (scanRx != null) {
                    try {
                        c.unregisterReceiver(scanRx);
                    } catch (RuntimeException ignored) {
                    }
                    scanRx = null;
                }
                sink.done();
            }
        };
        try {
            for (BluetoothDevice d : ad.getBondedDevices()) {
                String n = d.getName();
                if (n != null && n.length() > 0) {
                    found.put(n, d);
                    once.found(n, d);
                }
            }
        } catch (SecurityException ignored) {
        }
        if (scanRx != null) {
            try {
                c.unregisterReceiver(scanRx);
            } catch (RuntimeException ignored) {
            }
        }
        scanRx = new BroadcastReceiver() {
            @Override
            public void onReceive(Context ctx, Intent i) {
                String a = i.getAction();
                if (BluetoothDevice.ACTION_FOUND.equals(a)) {
                    BluetoothDevice d = i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                    if (d == null) {
                        return;
                    }
                    String n;
                    try {
                        n = d.getName();
                    } catch (SecurityException e) {
                        return;
                    }
                    if (n == null || n.length() < 1 || found.containsKey(n)) {
                        return;
                    }
                    found.put(n, d);
                    once.found(n, d);
                } else if (BluetoothAdapter.ACTION_DISCOVERY_FINISHED.equals(a)) {
                    once.done();
                }
            }
        };
        IntentFilter f = new IntentFilter();
        f.addAction(BluetoothDevice.ACTION_FOUND);
        f.addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);
        if (Build.VERSION.SDK_INT >= 33) {
            c.registerReceiver(scanRx, f, Context.RECEIVER_EXPORTED);
        } else {
            c.registerReceiver(scanRx, f);
        }
        boolean started = false;
        try {
            started = ad.startDiscovery();
        } catch (SecurityException e) {
            once.done();
            return;
        }
        if (!started) {
            once.done();
            return;
        }
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                once.done();
            }
        }, 12000);
    }

    static void follow(final BluetoothDevice dev, final String shown, final FollowSink sink) {
        new Thread(() -> {
            BluetoothSocket s = null;
            try {
                BluetoothAdapter ad = BluetoothAdapter.getDefaultAdapter();
                if (ad != null) {
                    try {
                        ad.cancelDiscovery();
                    } catch (SecurityException ignored) {
                    }
                }
                s = dev.createInsecureRfcommSocketToServiceRecord(SVC);
                s.connect();
                OutputStream out = s.getOutputStream();
                java.io.InputStream raw = s.getInputStream();
                String grant = ExgNative.followGrant(shown);
                String req = "NAME " + selfName() + "\n";
                if (grant != null && grant.length() > 0) {
                    req += "GRANT " + grant + "\n";
                } else {
                    req += "PAIR\n";
                }
                out.write(req.getBytes(UTF8));
                String host = "", g = grant == null ? "" : grant;
                int port = 8765, udp = 8766;
                boolean ok = false;
                String line;
                while ((line = readLine(raw)) != null) {
                    if (line.equals("NO")) {
                        sink.no("refused");
                        return;
                    }
                    if (line.equals("OK")) {
                        ok = true;
                    } else if (line.startsWith("GRANT ")) {
                        g = line.substring(6).trim();
                    } else if (line.startsWith("HOST ")) {
                        host = line.substring(5).trim();
                    } else if (line.startsWith("PORT ")) {
                        try {
                            port = Integer.parseInt(line.substring(5).trim());
                        } catch (NumberFormatException ignored) {
                        }
                    } else if (line.startsWith("UDP ")) {
                        try {
                            udp = Integer.parseInt(line.substring(5).trim());
                        } catch (NumberFormatException ignored) {
                        }
                    }
                    if (line.equals("LIVE") && ok && g.length() > 0) {
                        break;
                    }
                    if (ok && g.length() > 0 && host.length() > 0) {
                        /* older share without LIVE — wifi only */
                        break;
                    }
                }
                if (!ok || g.length() < 1) {
                    sink.no("EXG refused");
                    return;
                }
                String dest = host.length() > 0 ? (host + ":" + port + "/" + udp)
                        : ("bt:" + shown.replace(' ', '_'));
                ExgNative.followRemember(shown.replace(' ', '_'), dest, g);
                ExgNative.setLinkApi(true);
                ExgNative.linkWire(true);
                followSock = s;
                followLive = true;
                sink.ok(shown);
                pumpIn(s);
            } catch (Exception e) {
                sink.no("could not reach EXG");
            } finally {
                followLive = false;
                followSock = null;
                ExgNative.linkWire(false);
                if (s != null) {
                    try {
                        s.close();
                    } catch (Exception ignored) {
                    }
                }
            }
        }, "exg-follow").start();
    }

    private static String readLine(java.io.InputStream in) throws Exception {
        StringBuilder b = new StringBuilder();
        for (;;) {
            int c = in.read();
            if (c < 0) {
                return b.length() == 0 ? null : b.toString();
            }
            if (c == '\n') {
                return b.toString();
            }
            if (c != '\r') {
                b.append((char) c);
            }
        }
    }

    private static void pumpIn(BluetoothSocket s) throws Exception {
        java.io.InputStream in = s.getInputStream();
        byte[] mag = new byte[4];
        byte[] frame = new byte[68];
        while (followLive) {
            if (!readFull(in, mag, 4)) {
                return;
            }
            if (mag[0] == 'E' && mag[1] == 'X' && mag[2] == 'G' && mag[3] == '1') {
                System.arraycopy(mag, 0, frame, 0, 4);
                if (!readFull(in, frame, 4, 64)) {
                    return;
                }
                ExgNative.feedExg1(frame);
            } else if (mag[0] == 'W' && mag[1] == 'A' && mag[2] == 'N' && mag[3] == 'T') {
                writeKit(s.getOutputStream(), kitBytes());
            } else if (mag[0] == 'K' && mag[1] == 'I' && mag[2] == 'T' && mag[3] == '1') {
                applyKit(in);
            } else if (mag[0] == 'C' && mag[1] == 'F' && mag[2] == 'G' && mag[3] == '1') {
                byte[] ln = new byte[2];
                if (!readFull(in, ln, 2)) {
                    return;
                }
                int n = (ln[0] & 255) | ((ln[1] & 255) << 8);
                if (n < 1 || n > 1600) {
                    return;
                }
                byte[] js = new byte[n];
                if (!readFull(in, js, n)) {
                    return;
                }
                ExgNative.applyCfgJson(new String(js, UTF8));
            } else {
                return;
            }
        }
    }

    private static boolean readFull(java.io.InputStream in, byte[] b, int n) throws Exception {
        return readFull(in, b, 0, n);
    }

    private static boolean readFull(java.io.InputStream in, byte[] b, int off, int n)
            throws Exception {
        int g = 0;
        while (g < n) {
            int r = in.read(b, off + g, n - g);
            if (r < 0) {
                return false;
            }
            g += r;
        }
        return true;
    }

    static void followStop() {
        followLive = false;
        if (followSock != null) {
            try {
                followSock.close();
            } catch (Exception ignored) {
            }
            followSock = null;
        }
    }

    static boolean followLive() {
        return followLive;
    }

    static void sendKit() {
        byte[] kit = kitBytes();
        if (kit == null) {
            return;
        }
        if (followLive && followSock != null) {
            try {
                writeKit(followSock.getOutputStream(), kit);
                return;
            } catch (Exception ignored) {
            }
        }
        pendingKit = kit;
        lanPostKit(kit);
    }

    static void takeKit() {
        if (followLive && followSock != null) {
            try {
                followSock.getOutputStream().write(new byte[] {'W', 'A', 'N', 'T'});
                return;
            } catch (Exception ignored) {
            }
        }
        pendingWant = true;
        lanGetKit();
    }

    static void copyBoth() {
        sendKit();
        takeKit();
    }

    private static String[] destParts() {
        String d = ExgNative.linkDest();
        if (d == null || d.length() < 3 || d.startsWith("bt:")) {
            return null;
        }
        int sl = d.lastIndexOf('/');
        if (sl > 0) {
            d = d.substring(0, sl);
        }
        int c = d.lastIndexOf(':');
        if (c < 1) {
            return null;
        }
        return new String[] {d.substring(0, c), d.substring(c + 1)};
    }

    private static void lanPostKit(byte[] kit) {
        String[] p = destParts();
        if (p == null) {
            return;
        }
        httpKit(p[0], p[1], true, new String(kit, UTF8));
    }

    private static void lanGetKit() {
        String[] p = destParts();
        if (p == null) {
            return;
        }
        httpKit(p[0], p[1], false, null);
    }

    private static void httpKit(final String host, final String port, final boolean post,
            final String body) {
        new Thread(() -> {
            try {
                int hp = Integer.parseInt(port);
                java.net.Socket s = new java.net.Socket();
                s.connect(new java.net.InetSocketAddress(host, hp), 800);
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

    static void followName(final String shown, final FollowSink sink) {
        BluetoothDevice d = found.get(shown);
        if (d == null) {
            BluetoothAdapter ad = BluetoothAdapter.getDefaultAdapter();
            if (ad != null) {
                try {
                    for (BluetoothDevice b : ad.getBondedDevices()) {
                        String n = b.getName();
                        if (n != null && (n.equals(shown) || n.replace(' ', '_').equals(shown))) {
                            d = b;
                            break;
                        }
                    }
                } catch (SecurityException ignored) {
                }
            }
        }
        if (d == null) {
            sink.no("not nearby");
            return;
        }
        follow(d, shown, sink);
    }

    static ArrayList<String> foundNames() {
        return new ArrayList<String>(found.keySet());
    }

    static BluetoothDevice device(String name) {
        return found.get(name);
    }
}

/** Holds app context for the share reply. Set from the activity. */
final class ExgNativeApp {
    static Context ctx;
}

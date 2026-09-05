package com.abysscore.exgc;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothServerSocket;
import android.bluetooth.BluetoothSocket;
import android.bluetooth.le.AdvertiseCallback;
import android.bluetooth.le.AdvertiseData;
import android.bluetooth.le.AdvertiseSettings;
import android.bluetooth.le.BluetoothLeAdvertiser;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import android.util.Log;

import java.io.IOException;
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
    private static BluetoothLeScanner leScan;
    private static ScanCallback leCb;
    private static ScanSink scanOnce;
    private static BluetoothLeAdvertiser leAdv;
    private static AdvertiseCallback leAdvCb;
    private static final int EXG_MFG = 0xC0DE;
    private static final Map<String, ArrayList<BluetoothDevice>> found =
            new LinkedHashMap<String, ArrayList<BluetoothDevice>>();

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
        offerInquiry(ad);
        startAdvert(ad);
        listenThr = new Thread(() -> {
            try {
                server = ad.listenUsingInsecureRfcommWithServiceRecord("exg", SVC);
                Log.i("exg-c", "rfcomm listen");
            } catch (Exception e) {
                listen = false;
                Log.e("exg-c", "rfcomm listen failed", e);
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
        stopAdvert();
        if (server != null) {
            try {
                server.close();
            } catch (Exception ignored) {
            }
            server = null;
        }
    }

    private static void handleIn(BluetoothSocket s) throws Exception {
        java.io.InputStream rawIn = s.getInputStream();
        OutputStream out = s.getOutputStream();
        String name = "exg";
        String grantIn = "";
        String line;
        while ((line = readLine(rawIn)) != null) {
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

    /* Classic inquiry if the adapter allows it. No Settings activity. */
    private static void offerInquiry(BluetoothAdapter ad) {
        if (ad == null) {
            return;
        }
        try {
            java.lang.reflect.Method m = BluetoothAdapter.class.getMethod(
                    "setScanMode", int.class, int.class);
            m.invoke(ad, BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE, 300);
        } catch (Exception e) {
            try {
                java.lang.reflect.Method m = BluetoothAdapter.class.getMethod(
                        "setScanMode", int.class, long.class);
                m.invoke(ad, BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE, 300L);
            } catch (Exception ignored) {
            }
        }
    }

    private static String classicMac() {
        String[] paths = {
                "/sys/class/bluetooth/hci0/address",
                "/sys/class/bluetooth/hci1/address"
        };
        for (int i = 0; i < paths.length; i++) {
            java.io.BufferedReader r = null;
            try {
                r = new java.io.BufferedReader(new java.io.FileReader(paths[i]));
                String s = r.readLine();
                if (s != null) {
                    s = s.trim().toUpperCase(Locale.US);
                    if (s.length() >= 17 && !s.startsWith("00:00:00") && !s.startsWith("02:00:00")) {
                        return s;
                    }
                }
            } catch (Exception ignored) {
            } finally {
                if (r != null) {
                    try {
                        r.close();
                    } catch (Exception ignored) {
                    }
                }
            }
        }
        try {
            Context c = ExgNativeApp.ctx;
            if (c != null) {
                String s = android.provider.Settings.Secure.getString(
                        c.getContentResolver(), "bluetooth_address");
                if (s != null) {
                    s = s.trim().toUpperCase(Locale.US);
                    if (s.length() >= 17 && !s.startsWith("00:00:00") && !s.startsWith("02:00:00")) {
                        return s;
                    }
                }
            }
        } catch (RuntimeException ignored) {
        }
        return "";
    }

    private static byte[] macBytes(String mac) {
        byte[] b = new byte[6];
        if (mac == null || mac.length() < 17) {
            return b;
        }
        try {
            String[] p = mac.split(":");
            for (int i = 0; i < 6 && i < p.length; i++) {
                b[i] = (byte) Integer.parseInt(p[i], 16);
            }
        } catch (RuntimeException ignored) {
        }
        return b;
    }

    private static String macString(byte[] b, int off) {
        if (b == null || off + 6 > b.length) {
            return "";
        }
        return String.format(Locale.US, "%02X:%02X:%02X:%02X:%02X:%02X",
                b[off] & 255, b[off + 1] & 255, b[off + 2] & 255,
                b[off + 3] & 255, b[off + 4] & 255, b[off + 5] & 255);
    }

    private static void startAdvert(BluetoothAdapter ad) {
        stopAdvert();
        if (ad == null) {
            return;
        }
        try {
            leAdv = ad.getBluetoothLeAdvertiser();
            if (leAdv == null) {
                Log.w("exg-c", "no BLE advertiser");
                return;
            }
            String mac = classicMac();
            byte[] pay = new byte[10];
            pay[0] = 'E';
            pay[1] = 'X';
            pay[2] = 'G';
            pay[3] = '1';
            byte[] mb = macBytes(mac);
            System.arraycopy(mb, 0, pay, 4, 6);
            AdvertiseSettings st = new AdvertiseSettings.Builder()
                    .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
                    .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
                    .setConnectable(false)
                    .setTimeout(0)
                    .build();
            AdvertiseData data = new AdvertiseData.Builder()
                    .addManufacturerData(EXG_MFG, pay)
                    .build();
            AdvertiseData rsp = new AdvertiseData.Builder()
                    .setIncludeDeviceName(true)
                    .build();
            leAdvCb = new AdvertiseCallback() {
                @Override
                public void onStartSuccess(AdvertiseSettings s) {
                    Log.i("exg-c", "BLE advert EXG1 mac=" + (mac.length() > 0 ? "yes" : "none"));
                }

                @Override
                public void onStartFailure(int err) {
                    Log.e("exg-c", "BLE advert fail " + err);
                }
            };
            leAdv.startAdvertising(st, data, rsp, leAdvCb);
        } catch (RuntimeException e) {
            Log.e("exg-c", "BLE advert", e);
        }
    }

    private static void stopAdvert() {
        if (leAdv != null && leAdvCb != null) {
            try {
                leAdv.stopAdvertising(leAdvCb);
            } catch (RuntimeException ignored) {
            }
        }
        leAdv = null;
        leAdvCb = null;
    }

    private static int rank(BluetoothDevice d) {
        if (d == null) {
            return -1;
        }
        int t = d.getType();
        if (t == BluetoothDevice.DEVICE_TYPE_CLASSIC) {
            return 3;
        }
        if (t == BluetoothDevice.DEVICE_TYPE_DUAL) {
            return 2;
        }
        if (t == BluetoothDevice.DEVICE_TYPE_UNKNOWN) {
            return 1;
        }
        return 0;
    }

    private static void hear(BluetoothDevice d, String n, ScanSink sink) {
        if (d == null || n == null || n.length() < 1) {
            return;
        }
        ArrayList<BluetoothDevice> list = found.get(n);
        boolean first = list == null;
        if (first) {
            list = new ArrayList<BluetoothDevice>();
            found.put(n, list);
        }
        String addr = d.getAddress();
        for (int i = 0; i < list.size(); i++) {
            if (addr.equals(list.get(i).getAddress())) {
                return;
            }
        }
        list.add(d);
        Log.i("exg-c", "hear " + n + " type=" + d.getType() + " n=" + list.size());
        if (first && sink != null) {
            sink.found(n, d);
        }
    }

    private static void hearAdvert(ScanResult r, ScanSink sink) {
        if (r == null) {
            return;
        }
        ScanRecord rec = r.getScanRecord();
        BluetoothDevice d = r.getDevice();
        String n = null;
        try {
            n = d != null ? d.getName() : null;
        } catch (SecurityException ignored) {
        }
        if ((n == null || n.length() < 1) && rec != null) {
            n = rec.getDeviceName();
        }
        if (rec != null) {
            byte[] md = rec.getManufacturerSpecificData(EXG_MFG);
            if (md != null && md.length >= 10 && md[0] == 'E' && md[1] == 'X'
                    && md[2] == 'G' && md[3] == '1') {
                String mac = macString(md, 4);
                if (mac.length() == 17 && !mac.startsWith("00:00:00")) {
                    try {
                        BluetoothDevice classic = BluetoothAdapter.getDefaultAdapter()
                                .getRemoteDevice(mac);
                        if (n == null || n.length() < 1) {
                            n = "EXG";
                        }
                        hear(classic, n, sink);
                    } catch (RuntimeException ignored) {
                    }
                }
            }
        }
        hear(d, n, sink);
    }

    static void scanStop() {
        ScanSink s = scanOnce;
        if (s != null) {
            s.done();
        }
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
                scanOnce = null;
                try {
                    ad.cancelDiscovery();
                } catch (SecurityException ignored) {
                }
                if (leScan != null && leCb != null) {
                    try {
                        leScan.stopScan(leCb);
                    } catch (RuntimeException ignored) {
                    }
                }
                leScan = null;
                leCb = null;
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
        scanOnce = once;
        try {
            for (BluetoothDevice d : ad.getBondedDevices()) {
                String n;
                try {
                    n = d.getName();
                } catch (SecurityException e) {
                    continue;
                }
                hear(d, n, once);
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
                    hear(d, n, once);
                }
            }
        };
        IntentFilter f = new IntentFilter();
        f.addAction(BluetoothDevice.ACTION_FOUND);
        if (Build.VERSION.SDK_INT >= 33) {
            c.registerReceiver(scanRx, f, Context.RECEIVER_EXPORTED);
        } else {
            c.registerReceiver(scanRx, f);
        }
        try {
            ad.startDiscovery();
        } catch (SecurityException ignored) {
        }
        try {
            leScan = ad.getBluetoothLeScanner();
            if (leScan != null) {
                leCb = new ScanCallback() {
                    @Override
                    public void onScanResult(int ct, ScanResult r) {
                        if (r == null || r.getDevice() == null) {
                            return;
                        }
                        String n;
                        try {
                            n = r.getDevice().getName();
                        } catch (SecurityException e) {
                            return;
                        }
                        if ((n == null || n.length() < 1) && r.getScanRecord() != null) {
                            n = r.getScanRecord().getDeviceName();
                        }
                        hearAdvert(r, once);
                    }
                };
                leScan.startScan(leCb);
            }
        } catch (RuntimeException ignored) {
        }
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                once.done();
            }
        }, 8000);
    }

    private static BluetoothSocket openRfcomm(BluetoothDevice dev) throws IOException {
        BluetoothAdapter ad = BluetoothAdapter.getDefaultAdapter();
        if (ad != null) {
            try {
                ad.cancelDiscovery();
            } catch (SecurityException ignored) {
            }
        }
        IOException last = null;
        try {
            BluetoothSocket s = dev.createInsecureRfcommSocketToServiceRecord(SVC);
            s.connect();
            return s;
        } catch (IOException e) {
            last = e;
            Log.w("exg-c", "rfcomm insecure uuid: " + e.getMessage());
        }
        try {
            BluetoothSocket s = dev.createRfcommSocketToServiceRecord(SVC);
            s.connect();
            return s;
        } catch (IOException e) {
            last = e;
            Log.w("exg-c", "rfcomm uuid: " + e.getMessage());
        }
        try {
            java.lang.reflect.Method m = dev.getClass().getMethod(
                    "createInsecureRfcommSocket", int.class);
            BluetoothSocket s = (BluetoothSocket) m.invoke(dev, 1);
            s.connect();
            return s;
        } catch (Exception e) {
            Log.w("exg-c", "rfcomm ch1: " + e.getMessage());
        }
        throw last != null ? last : new IOException("rfcomm failed");
    }

    static void follow(final BluetoothDevice dev, final String shown, final FollowSink sink) {
        new Thread(() -> {
            BluetoothSocket s = null;
            try {
                s = openRfcomm(dev);
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
                String why = e.getMessage();
                if (why == null || why.length() < 1) {
                    why = e.getClass().getSimpleName();
                }
                Log.e("exg-c", "follow failed: " + why);
                sink.no("could not reach EXG — " + why);
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

    static ArrayList<BluetoothDevice> cands(String shown) {
        ArrayList<BluetoothDevice> list = new ArrayList<BluetoothDevice>();
        if (shown == null) {
            return list;
        }
        ArrayList<BluetoothDevice> have = found.get(shown);
        if (have != null) {
            list.addAll(have);
        }
        BluetoothAdapter ad = BluetoothAdapter.getDefaultAdapter();
        if (ad != null) {
            try {
                for (BluetoothDevice b : ad.getBondedDevices()) {
                    String n = b.getName();
                    if (n == null) {
                        continue;
                    }
                    if (!n.equals(shown) && !n.replace(' ', '_').equals(shown)) {
                        continue;
                    }
                    boolean dup = false;
                    for (int i = 0; i < list.size(); i++) {
                        if (b.getAddress().equals(list.get(i).getAddress())) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) {
                        list.add(b);
                    }
                }
            } catch (SecurityException ignored) {
            }
        }
        for (int i = 0; i < list.size(); i++) {
            int best = i;
            for (int j = i + 1; j < list.size(); j++) {
                if (rank(list.get(j)) > rank(list.get(best))) {
                    best = j;
                }
            }
            if (best != i) {
                BluetoothDevice t = list.get(i);
                list.set(i, list.get(best));
                list.set(best, t);
            }
        }
        return list;
    }

    static void followAny(final String shown, final FollowSink sink) {
        final ArrayList<BluetoothDevice> list = cands(shown);
        if (list.isEmpty()) {
            sink.no("not nearby");
            return;
        }
        followAt(list, 0, shown, sink);
    }

    private static void followAt(final ArrayList<BluetoothDevice> list, final int i,
            final String shown, final FollowSink sink) {
        if (i >= list.size()) {
            sink.no("could not reach EXG — RFCOMM failed");
            return;
        }
        Log.i("exg-c", "follow try " + i + "/" + list.size() + " type=" + list.get(i).getType());
        follow(list.get(i), shown, new FollowSink() {
            @Override
            public void ok(String n) {
                sink.ok(n);
            }

            @Override
            public void no(String why) {
                Log.w("exg-c", "follow try " + i + " no: " + why);
                if (why != null && why.contains("refus")) {
                    sink.no(why);
                    return;
                }
                followAt(list, i + 1, shown, sink);
            }
        });
    }

    static void followName(final String shown, final FollowSink sink) {
        followAny(shown, sink);
    }

    static ArrayList<String> foundNames() {
        return new ArrayList<String>(found.keySet());
    }

    static BluetoothDevice device(String name) {
        ArrayList<BluetoothDevice> list = cands(name);
        return list.isEmpty() ? null : list.get(0);
    }
}

/** Holds app context for the share reply. Set from the activity. */
final class ExgNativeApp {
    static Context ctx;
}

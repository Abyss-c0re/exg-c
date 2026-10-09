package com.abysscore.exgc;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.SystemClock;

import com.abysscore.mindstorm.IMindStormBridge;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.Arrays;

/** Ball link. The token comes from the MindStorm app and is not logged. */
public final class MindStormLink {
    private static final int TCP_PORT = 8744;
    private static final int MTU_ASK = 247;
    private static final int MTU_MIN = 185;
    private static final java.util.UUID SVC =
            java.util.UUID.fromString("8f3a0001-1c3e-4b2a-9a01-6d696e647374");
    private static final java.util.UUID RX =
            java.util.UUID.fromString("8f3a0002-1c3e-4b2a-9a01-6d696e647374");
    private static final java.util.UUID TX =
            java.util.UUID.fromString("8f3a0003-1c3e-4b2a-9a01-6d696e647374");
    private static final java.util.UUID CCCD =
            java.util.UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    private static final Object gate = new Object();

    private static volatile int epoch;
    private static volatile String fault = "";
    private static volatile boolean bound;
    private static volatile boolean bleReady;
    private static volatile boolean writeBusy;
    private static int writeGen;
    private static Handler bleHandler;
    private static byte[] held;
    private static volatile boolean expectClose;
    private static volatile boolean mtuRejected;
    private static volatile String pendingHost = "";
    private static Activity host;
    private static ServiceConnection conn;
    private static IMindStormBridge bridge;
    private static BluetoothGatt gatt;
    private static BluetoothGattCharacteristic rxCh;
    private static Socket tcp;

    /** Exists so the class is not instantiated. */
    private MindStormLink() {}

    /** True when the MindStorm package is installed and visible to this app. */
    public static boolean installed(Context context) {
        if (context == null) {
            return false;
        }
        try {
            if (Build.VERSION.SDK_INT >= 33) {
                context.getPackageManager().getPackageInfo(
                        "com.abysscore.mindstorm",
                        PackageManager.PackageInfoFlags.of(0));
            } else {
                context.getPackageManager().getPackageInfo("com.abysscore.mindstorm", 0);
            }
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }

    /** Binds the MindStorm client bridge, then BLE or TCP port 8744. A null activity returns. */
    public static void connect(Activity activity) {
        if (activity == null) {
            return;
        }
        try {
            open(activity);
        } catch (RuntimeException e) {
            fault = "ball link failed";
        }
    }

    /** Closes BLE and TCP and drops the native session back to disconnected. */
    public static void stop() {
        halt();
        fault = "";
        dropSession();
    }

    /** Transport fault, or empty when the row should show the native status. Never null. */
    public static String note() {
        String s = fault;
        return s == null ? "" : s;
    }

    /** Asks for Bluetooth permission when needed, then binds the bridge. */
    private static void open(Activity activity) {
        if (!installed(activity)) {
            fault = "";
            return;
        }
        halt();
        dropSession();
        mtuRejected = false;
        expectClose = false;
        pendingHost = "";
        if (Build.VERSION.SDK_INT >= 31) {
            boolean connectOk = activity.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
                    == PackageManager.PERMISSION_GRANTED;
            if (!connectOk) {
                fault = "allow bluetooth";
                activity.requestPermissions(new String[] {
                        android.Manifest.permission.BLUETOOTH_CONNECT,
                        android.Manifest.permission.BLUETOOTH_SCAN
                }, 84);
                return;
            }
        }
        fault = "";
        final int ep = epoch;
        host = activity;
        Intent intent = new Intent();
        intent.setClassName("com.abysscore.mindstorm",
                "com.abysscore.mindstorm.ClientBridgeService");
        conn = new ServiceConnection() {
            /** Starts the grant thread when this bind is still current. A null binder is a dropped bridge. */
            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {
                if (ep != epoch) {
                    return;
                }
                bridge = IMindStormBridge.Stub.asInterface(service);
                if (bridge == null) {
                    fault = "MindStorm bridge dropped";
                    return;
                }
                Thread t = new Thread(() -> pullGrant(ep), "mindstorm-grant");
                t.setDaemon(true);
                t.start();
            }

            /** Clears the bridge. A stale bind does not change the fault line. */
            @Override
            public void onServiceDisconnected(ComponentName name) {
                bridge = null;
                if (ep == epoch) {
                    fault = "MindStorm bridge dropped";
                }
            }
        };
        boolean ok;
        try {
            ok = activity.bindService(intent, conn, Context.BIND_AUTO_CREATE);
        } catch (SecurityException e) {
            fault = "MindStorm bind refused";
            conn = null;
            return;
        }
        if (!ok) {
            fault = "ball link failed";
            conn = null;
            return;
        }
        bound = true;
    }

    /** Bumps the epoch, closes the radio and the socket, and unbinds the bridge. */
    private static void halt() {
        Handler h;
        epoch++;
        bleReady = false;
        synchronized (gate) {
            h = bleHandler;
        }
        if (h != null) {
            h.post(() -> held = null);
        } else {
            held = null;
        }
        closeGatt();
        closeTcp();
        Activity a = host;
        ServiceConnection c = conn;
        if (a != null && c != null && bound) {
            try {
                a.unbindService(c);
            } catch (RuntimeException ignored) {
                /* already unbound */
            }
        }
        bound = false;
        conn = null;
        bridge = null;
        host = null;
    }

    /** HELLO resets auth when an identity is already stored. A missing identity returns -1. */
    private static void dropSession() {
        try {
            ExgNative.mindstormHello();
        } catch (Throwable ignored) {
            /* native library not ready */
        }
    }

    /** Polls until the user grants or denies. The token is wiped after it is copied into native code. */
    private static void pullGrant(int ep) {
        IMindStormBridge b = bridge;
        if (b == null || ep != epoch) {
            return;
        }
        try {
            int st = b.requestAccess("exg-c");
            if (st == 0) {
                fault = "asking MindStorm";
            }
            long t0 = SystemClock.uptimeMillis();
            while (ep == epoch && st == 0 && SystemClock.uptimeMillis() - t0 < 120000L) {
                try {
                    Thread.sleep(250);
                } catch (InterruptedException e) {
                    return;
                }
                st = b.status();
            }
            if (ep != epoch) {
                return;
            }
            if (st != 0 && st != 1 && st != 2) {
                st = b.status();
            }
            if (st == 2) {
                fault = "ball access denied";
                return;
            }
            if (st != 1) {
                fault = "ball access pending";
                return;
            }
            byte[] blob = b.token();
            if (blob == null) {
                fault = "ball token missing";
                return;
            }
            if (blob.length < 48) {
                wipe(blob);
                fault = "ball grant has no id";
                return;
            }
            byte[] id = Arrays.copyOfRange(blob, 0, 16);
            byte[] key = Arrays.copyOfRange(blob, 16, 48);
            wipe(blob);
            int rc = ExgNative.mindstormSetIdentity(id, key);
            wipe(id);
            wipe(key);
            if (ep != epoch) {
                return;
            }
            if (rc != 0) {
                fault = "ball identity refused";
                return;
            }
            ExgNative.mindstormSetWind(true);
            String ble = b.bleAddress();
            String hostName = b.deviceHost();
            pendingHost = hostName == null ? "" : hostName;
            if (ep != epoch) {
                return;
            }
            if (ble != null && ble.trim().length() > 0) {
                openBle(ep, ble.trim());
            } else if (hostOnly(pendingHost).length() > 0) {
                openTcp(ep, pendingHost);
            } else {
                fault = "no ball address";
            }
        } catch (android.os.RemoteException e) {
            if (ep == epoch) {
                fault = "MindStorm bridge dropped";
            }
        }
    }

    /** Connects LE, asks for MTU 247, and refuses a link under 185. */
    private static void openBle(int ep, String address) {
        Activity a = host;
        if (a == null || ep != epoch) {
            return;
        }
        BluetoothManager bm = (BluetoothManager) a.getSystemService(Context.BLUETOOTH_SERVICE);
        BluetoothAdapter adapter = bm == null ? null : bm.getAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            if (hostOnly(pendingHost).length() > 0) {
                openTcp(ep, pendingHost);
            } else {
                fault = "bluetooth is off";
            }
            return;
        }
        BluetoothDevice device;
        try {
            device = adapter.getRemoteDevice(address);
        } catch (IllegalArgumentException e) {
            fault = "bad ball address";
            return;
        }
        BallGatt cb = new BallGatt(ep);
        Handler h = bleHandler();
        BluetoothGatt g = device.connectGatt(a.getApplicationContext(), false, cb,
                BluetoothDevice.TRANSPORT_LE, BluetoothDevice.PHY_LE_1M_MASK, h);
        synchronized (gate) {
            if (ep != epoch) {
                if (g != null) {
                    h.post(() -> shut(g));
                }
                return;
            }
            gatt = g;
            rxCh = null;
        }
        if (g == null) {
            fault = "ball link failed";
        }
    }

    /** One thread for connect, notify, and write. Callbacks are posted here. */
    private static Handler bleHandler() {
        synchronized (gate) {
            if (bleHandler == null) {
                HandlerThread t = new HandlerThread("mindstorm-ble");
                t.setDaemon(true);
                t.start();
                bleHandler = new Handler(t.getLooper());
            }
            return bleHandler;
        }
    }

    /** Disconnects and closes. Safe to call more than once. */
    private static void shut(BluetoothGatt g) {
        if (g == null) {
            return;
        }
        try {
            g.disconnect();
            g.close();
        } catch (RuntimeException ignored) {
            /* already closed */
        }
    }

    /** Clears the GATT client. The close runs on the BLE thread when that thread exists. */
    private static void closeGatt() {
        final BluetoothGatt g;
        final Handler h;
        synchronized (gate) {
            g = gatt;
            h = bleHandler;
            gatt = null;
            rxCh = null;
            bleReady = false;
            writeBusy = false;
        }
        if (g == null) {
            return;
        }
        if (h != null) {
            h.post(() -> shut(g));
        } else {
            shut(g);
        }
    }

    /** Writes one frame to the RX characteristic. False when the radio is down. */
    private static boolean writeRx(byte[] frame) {
        BluetoothGatt g;
        BluetoothGattCharacteristic ch;
        synchronized (gate) {
            g = gatt;
            ch = rxCh;
        }
        if (g == null || ch == null || frame == null || frame.length < 1) {
            return false;
        }
        ch.setValue(frame);
        ch.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
        try {
            return g.writeCharacteristic(ch);
        } catch (RuntimeException e) {
            return false;
        }
    }

    /** Sends at most one frame. Runs on the BLE thread. A refused write keeps the frame. */
    private static void pumpOne(int ep) {
        byte[] frame;
        if (ep != epoch || !bleReady || writeBusy) {
            return;
        }
        frame = held;
        if (frame == null) {
            frame = ExgNative.mindstormTx();
            if (frame == null || frame.length < 1) {
                if (ExgNative.mindstormAuthed()) {
                    fault = "";
                }
                bleHandler().postDelayed(() -> pumpOne(ep), 8);
                return;
            }
            held = frame;
        }
        if (ep != epoch) {
            return;
        }
        final int gen = ++writeGen;
        writeBusy = true;
        if (!writeRx(frame)) {
            writeBusy = false;
            bleHandler().postDelayed(() -> pumpOne(ep), 20);
            return;
        }
        held = null;
        bleHandler().postDelayed(() -> {
            if (ep == epoch && writeBusy && writeGen == gen) {
                writeBusy = false;
                pumpOne(ep);
            }
        }, 80);
    }

    /** TCP port 8744. Clears the fault line once HELLO is queued. */
    private static void openTcp(int ep, String host) {
        final String h = hostOnly(host);
        if (h.length() < 1) {
            fault = mtuRejected ? "MTU under 185 — use Wi-Fi" : "no ball address";
            return;
        }
        Thread t = new Thread(() -> tcpSession(ep, h), "mindstorm-tcp");
        t.setDaemon(true);
        t.start();
    }

    /** Connects, queues HELLO, then reads and writes until the epoch changes. */
    private static void tcpSession(int ep, String host) {
        Socket s = new Socket();
        try {
            s.connect(new InetSocketAddress(host, TCP_PORT), 5000);
            s.setTcpNoDelay(true);
            s.setSoTimeout(400);
            synchronized (gate) {
                if (ep != epoch) {
                    s.close();
                    return;
                }
                tcp = s;
            }
            if (ExgNative.mindstormHello() != 0) {
                fault = "ball identity refused";
                s.close();
                return;
            }
            fault = "";
            InputStream in = s.getInputStream();
            OutputStream out = s.getOutputStream();
            Thread reader = new Thread(() -> tcpRead(ep, in), "mindstorm-tcp-rx");
            reader.setDaemon(true);
            reader.start();
            tcpWrite(ep, out);
        } catch (IOException e) {
            if (ep == epoch) {
                fault = mtuRejected ? "MTU under 185 — use Wi-Fi" : "ball TCP failed";
            }
        } finally {
            try {
                s.close();
            } catch (IOException ignored) {
                /* already closed */
            }
        }
    }

    /** Copies socket bytes into the native parser. A timeout only checks auth. */
    private static void tcpRead(int ep, InputStream in) {
        byte[] buf = new byte[512];
        while (ep == epoch) {
            try {
                int n = in.read(buf);
                if (n < 0) {
                    break;
                }
                if (n < 1) {
                    continue;
                }
                byte[] slice = new byte[n];
                System.arraycopy(buf, 0, slice, 0, n);
                ExgNative.mindstormRx(slice);
                if (ExgNative.mindstormAuthed()) {
                    fault = "";
                }
            } catch (SocketTimeoutException e) {
                if (ExgNative.mindstormAuthed()) {
                    fault = "";
                }
            } catch (IOException e) {
                break;
            }
        }
    }

    /** Writes each native frame. Sleeps a few milliseconds when the queue is empty. */
    private static void tcpWrite(int ep, OutputStream out) {
        while (ep == epoch) {
            byte[] frame = ExgNative.mindstormTx();
            if (frame != null && frame.length > 0) {
                try {
                    out.write(frame);
                    out.flush();
                } catch (IOException e) {
                    break;
                }
            } else if (!sleepMs(5)) {
                return;
            }
            if (ExgNative.mindstormAuthed()) {
                fault = "";
            }
        }
    }

    /** Closes the TCP socket. Safe when none is open. */
    private static void closeTcp() {
        Socket s;
        synchronized (gate) {
            s = tcp;
            tcp = null;
        }
        if (s != null) {
            try {
                s.close();
            } catch (IOException ignored) {
                /* already closed */
            }
        }
    }

    /** Host without a scheme or a trailing :port. Empty when h is null or blank. */
    private static String hostOnly(String h) {
        if (h == null) {
            return "";
        }
        h = h.trim();
        if (h.startsWith("tcp://")) {
            h = h.substring(6);
        }
        int colon = h.lastIndexOf(':');
        if (colon > 0 && h.indexOf(':') == colon) {
            String tail = h.substring(colon + 1);
            boolean digits = tail.length() > 0;
            int i;
            for (i = 0; i < tail.length(); i++) {
                char c = tail.charAt(i);
                if (c < '0' || c > '9') {
                    digits = false;
                    break;
                }
            }
            if (digits) {
                h = h.substring(0, colon);
            }
        }
        return h;
    }

    /** Zeroes a secret buffer. A null array returns. */
    private static void wipe(byte[] b) {
        if (b != null) {
            Arrays.fill(b, (byte) 0);
        }
    }

    /** Sleeps ms. False when the thread is interrupted. */
    private static boolean sleepMs(int ms) {
        try {
            Thread.sleep(ms);
            return true;
        } catch (InterruptedException e) {
            return false;
        }
    }

    /** LE callbacks for one bind generation. A stale epoch ignores the event. */
    private static final class BallGatt extends BluetoothGattCallback {
        private final int ep;
        private boolean notifyQueued;

        /** Remembers which connect() this callback belongs to. */
        BallGatt(int ep) {
            this.ep = ep;
        }

        /** On connect, asks for MTU 247. A later close does not overwrite an MTU fault. */
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int status, int newState) {
            if (ep != epoch) {
                return;
            }
            if (newState != BluetoothProfile.STATE_CONNECTED) {
                bleReady = false;
                if (!expectClose && fault.length() == 0) {
                    fault = "ball link failed";
                }
                return;
            }
            if (status != BluetoothGatt.GATT_SUCCESS) {
                fault = "ball link failed";
                return;
            }
            g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
            if (!g.requestMtu(MTU_ASK)) {
                giveUpMtu(g);
            }
        }

        /** Below 185 the link is closed and the row says to use Wi-Fi. */
        @Override
        public void onMtuChanged(BluetoothGatt g, int mtu, int status) {
            if (ep != epoch) {
                return;
            }
            if (status != BluetoothGatt.GATT_SUCCESS || mtu < MTU_MIN) {
                giveUpMtu(g);
                return;
            }
            if (!g.discoverServices()) {
                fault = "ball service missing";
            }
        }

        /** Enables notify on TX, then HELLO is queued from the descriptor callback. */
        @Override
        public void onServicesDiscovered(BluetoothGatt g, int status) {
            if (ep != epoch) {
                return;
            }
            if (status != BluetoothGatt.GATT_SUCCESS) {
                fault = "ball service missing";
                return;
            }
            BluetoothGattService svc = g.getService(SVC);
            if (svc == null) {
                fault = "ball service missing";
                return;
            }
            BluetoothGattCharacteristic rx = svc.getCharacteristic(RX);
            BluetoothGattCharacteristic tx = svc.getCharacteristic(TX);
            if (rx == null || tx == null) {
                fault = "ball service missing";
                return;
            }
            synchronized (gate) {
                if (ep != epoch) {
                    return;
                }
                rxCh = rx;
            }
            if (!g.setCharacteristicNotification(tx, true)) {
                fault = "ball notify failed";
                return;
            }
            BluetoothGattDescriptor desc = tx.getDescriptor(CCCD);
            if (desc == null) {
                fault = "ball notify failed";
                return;
            }
            /* The one-argument write drops its callback on this phone. */
            final BluetoothGattDescriptor cccd = desc;
            bleHandler().postDelayed(() -> writeNotify(g, cccd, true), 400L);
        }

        /** Enables notify. Retries once when the stack is still settling. */
        private void writeNotify(BluetoothGatt g, BluetoothGattDescriptor desc, boolean retry) {
            if (ep != epoch || bleReady) {
                return;
            }
            boolean wrote;
            if (Build.VERSION.SDK_INT >= 33) {
                wrote = g.writeDescriptor(desc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                        == BluetoothGatt.GATT_SUCCESS;
            } else {
                desc.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                wrote = g.writeDescriptor(desc);
            }
            if (wrote) {
                notifyQueued = true;
                if (retry) {
                    bleHandler().postDelayed(() -> {
                        if (ep == epoch && !bleReady) {
                            writeNotify(g, desc, false);
                        }
                    }, 700L);
                }
                return;
            }
            if (retry) {
                bleHandler().postDelayed(() -> writeNotify(g, desc, false), 500L);
            } else if (ep == epoch && !notifyQueued) {
                fault = "ball notify failed";
            }
        }

        /** Notify is on. Queue HELLO. The pump sends it. */
        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor descriptor, int status) {
            if (ep != epoch || bleReady) {
                return;
            }
            if (status != BluetoothGatt.GATT_SUCCESS) {
                fault = "ball notify failed";
                return;
            }
            bleReady = true;
            if (ExgNative.mindstormHello() != 0) {
                fault = "ball identity refused";
                return;
            }
            bleReady = true;
            fault = "";
            pumpOne(ep);
        }

        /** Device bytes on API 32 and older. API 33+ delivers the array overload instead. */
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic ch) {
            if (Build.VERSION.SDK_INT >= 33) {
                return;
            }
            takeRx(ch == null ? null : ch.getValue());
        }

        /** Device bytes on API 33+. The array is copied by the native parser before return. */
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic ch, byte[] value) {
            takeRx(value);
        }

        /** Feeds one notify into the session. A null or empty value returns. */
        private void takeRx(byte[] v) {
            if (ep != epoch || v == null || v.length < 1) {
                return;
            }
            ExgNative.mindstormRx(v);
            if (ExgNative.mindstormAuthed()) {
                fault = "";
            }
        }

        /** Lets the pump send the next frame. Runs on the BLE thread. */
        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic ch, int status) {
            writeBusy = false;
            pumpOne(ep);
        }

        /** Closes this GATT and opens TCP when a host is known. The row names Wi-Fi. */
        private void giveUpMtu(BluetoothGatt g) {
            boolean mine;
            mtuRejected = true;
            expectClose = true;
            fault = "MTU under 185 — use Wi-Fi";
            synchronized (gate) {
                mine = g != null && gatt == g;
                if (mine) {
                    gatt = null;
                    rxCh = null;
                }
                bleReady = false;
                writeBusy = false;
            }
            if (mine) {
                shut(g);
            }
            if (ep == epoch && hostOnly(pendingHost).length() > 0) {
                openTcp(ep, pendingHost);
            }
        }
    }
}

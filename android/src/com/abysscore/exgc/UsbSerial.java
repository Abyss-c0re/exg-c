package com.abysscore.exgc;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.util.Log;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Knight board is FTDI FT232R (0403:6001). Also tries CDC ACM / CH340 / CP210x.
 */
public final class UsbSerial {
    private static final String TAG = "exg-c";
    private static final String ACTION_PERM = "com.abysscore.exgc.USB_PERMISSION";
    private static final int VID_FTDI = 0x0403;
    private static final int VID_CH340 = 0x1a86;
    private static final int VID_CP210 = 0x10c4;

    private static final int FTDI_RESET = 0;
    private static final int FTDI_MODEM = 1;
    private static final int FTDI_FLOW = 2;
    private static final int FTDI_BAUD = 3;
    private static final int FTDI_DATA = 4;
    private static final int FTDI_HOST = 0x40;

    private static Context app;
    private static boolean inited;
    private static UsbManager mgr;
    private static UsbDeviceConnection conn;
    private static UsbInterface iface;
    private static UsbEndpoint epIn, epOut;
    private static int kind; /* 1 ftdi 2 cdc 3 other */
    private static final Object lock = new Object();
    private static CountDownLatch permLatch;
    private static boolean permOk;
    private static int sTick, sPay;

    private UsbSerial() {}

    public static void init(Context ctx) {
        app = ctx.getApplicationContext();
        mgr = (UsbManager) app.getSystemService(Context.USB_SERVICE);
        if (inited) {
            return;
        }
        inited = true;
        IntentFilter f = new IntentFilter(ACTION_PERM);
        if (Build.VERSION.SDK_INT >= 33) {
            app.registerReceiver(permRx, f, Context.RECEIVER_NOT_EXPORTED);
        } else {
            app.registerReceiver(permRx, f);
        }
    }

    private static final BroadcastReceiver permRx = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!ACTION_PERM.equals(intent.getAction())) {
                return;
            }
            permOk = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
            if (permLatch != null) {
                permLatch.countDown();
            }
        }
    };

    public static String[] listPorts() {
        List<String> out = new ArrayList<String>();
        if (mgr == null) {
            return new String[0];
        }
        HashMap<String, UsbDevice> map = mgr.getDeviceList();
        if (map == null) {
            return new String[0];
        }
        for (UsbDevice d : map.values()) {
            if (supported(d)) {
                out.add(label(d));
            }
        }
        return out.toArray(new String[0]);
    }

    public static int open(String path) {
        synchronized (lock) {
            close();
            if (mgr == null) {
                return -1;
            }
            UsbDevice dev = find(path);
            if (dev == null) {
                Log.e(TAG, "no usb device for " + path);
                return -1;
            }
            if (!mgr.hasPermission(dev) && !requestPerm(dev)) {
                Log.e(TAG, "usb permission denied");
                return -1;
            }
            conn = mgr.openDevice(dev);
            if (conn == null) {
                Log.e(TAG, "openDevice failed");
                return -1;
            }
            if (!claim(dev)) {
                close();
                return -1;
            }
            if (!configure()) {
                close();
                return -1;
            }
            Log.i(TAG, "opened " + label(dev) + " kind=" + kind);
            return 0;
        }
    }

    public static void close() {
        synchronized (lock) {
            if (conn != null && iface != null) {
                try {
                    conn.releaseInterface(iface);
                } catch (Exception ignored) {
                }
            }
            if (conn != null) {
                conn.close();
            }
            conn = null;
            iface = null;
            epIn = null;
            epOut = null;
            kind = 0;
            sTick = 0;
            sPay = 0;
        }
    }

    public static int read(byte[] buf, int n) {
        synchronized (lock) {
            if (conn == null || epIn == null || n <= 0) {
                return 0;
            }
            int chunk = Math.min(n + (kind == 1 ? 2 : 0), epIn.getMaxPacketSize());
            if (chunk < 3) {
                chunk = kind == 1 ? 64 : Math.min(n, 64);
            }
            byte[] tmp = new byte[Math.max(chunk, 64)];
            int out = 0;
            int loops = kind == 1 ? 8 : 1;
            for (int li = 0; li < loops && out < n; li++) {
                int got = conn.bulkTransfer(epIn, tmp, tmp.length, li == 0 ? 80 : 2);
                if (got < 0) {
                    if (out == 0) {
                        Log.w(TAG, "bulk IN " + got);
                    }
                    break;
                }
                if (got == 0) {
                    break;
                }
                if (kind == 1) {
                    if (got <= 2) {
                        if (sTick++ == 0) {
                            Log.i(TAG, "ftdi status-only packet (uart idle)");
                        }
                        continue;
                    }
                    int payload = got - 2;
                    if (payload > n - out) {
                        payload = n - out;
                    }
                    System.arraycopy(tmp, 2, buf, out, payload);
                    out += payload;
                } else {
                    int take = got > n - out ? n - out : got;
                    System.arraycopy(tmp, 0, buf, out, take);
                    out += take;
                    break;
                }
            }
            if (out > 0 && sPay == 0) {
                Log.i(TAG, "ftdi payload " + out + " bytes");
                sPay = 1;
            }
            return out;
        }
    }

    public static int write(byte[] buf, int n) {
        synchronized (lock) {
            if (conn == null || epOut == null || n <= 0) {
                return -1;
            }
            int off = 0;
            while (off < n) {
                int w = conn.bulkTransfer(epOut, buf, off, n - off, 200);
                if (w <= 0) {
                    return off > 0 ? off : -1;
                }
                off += w;
            }
            return off;
        }
    }

    public static void pulseDtr() {
        synchronized (lock) {
            if (conn == null) {
                return;
            }
            if (kind == 1) {
                conn.controlTransfer(FTDI_HOST, FTDI_MODEM, 0x0100, 0, null, 0, 200);
                try {
                    Thread.sleep(100);
                } catch (InterruptedException ignored) {
                }
                conn.controlTransfer(FTDI_HOST, FTDI_MODEM, 0x0101, 0, null, 0, 200);
            } else if (kind == 2) {
                byte[] line = new byte[] {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08};
                conn.controlTransfer(0x21, 0x22, 0x00, 0, null, 0, 200);
                try {
                    Thread.sleep(100);
                } catch (InterruptedException ignored) {
                }
                conn.controlTransfer(0x21, 0x22, 0x03, 0, null, 0, 200);
                conn.controlTransfer(0x21, 0x20, 0, 0, line, line.length, 200);
            }
        }
    }

    public static void flush() {
        synchronized (lock) {
            if (conn == null || epIn == null) {
                return;
            }
            byte[] dump = new byte[64];
            while (conn.bulkTransfer(epIn, dump, dump.length, 5) > 0) {
                /* drain */
            }
        }
    }

    private static boolean requestPerm(UsbDevice dev) {
        permOk = false;
        permLatch = new CountDownLatch(1);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) {
            flags |= PendingIntent.FLAG_MUTABLE;
        }
        PendingIntent pi = PendingIntent.getBroadcast(app, 0, new Intent(ACTION_PERM), flags);
        mgr.requestPermission(dev, pi);
        try {
            if (!permLatch.await(20, TimeUnit.SECONDS)) {
                return false;
            }
        } catch (InterruptedException e) {
            return false;
        }
        return permOk || mgr.hasPermission(dev);
    }

    private static UsbDevice find(String path) {
        HashMap<String, UsbDevice> map = mgr.getDeviceList();
        if (map == null) {
            return null;
        }
        UsbDevice first = null;
        for (UsbDevice d : map.values()) {
            if (!supported(d)) {
                continue;
            }
            if (first == null) {
                first = d;
            }
            if (path == null || path.length() == 0 || label(d).equals(path)
                    || d.getDeviceName().equals(path)) {
                return d;
            }
        }
        return first;
    }

    private static String label(UsbDevice d) {
        return String.format(Locale.US, "usb:%04x:%04x", d.getVendorId(), d.getProductId());
    }

    private static boolean supported(UsbDevice d) {
        int vid = d.getVendorId();
        if (vid == VID_FTDI || vid == VID_CH340 || vid == VID_CP210) {
            return true;
        }
        for (int i = 0; i < d.getInterfaceCount(); i++) {
            UsbInterface ui = d.getInterface(i);
            if (ui.getInterfaceClass() == UsbConstants.USB_CLASS_CDC_DATA
                    || ui.getInterfaceClass() == UsbConstants.USB_CLASS_COMM) {
                return true;
            }
        }
        return false;
    }

    private static boolean claim(UsbDevice dev) {
        UsbInterface data = null;
        UsbInterface comm = null;
        for (int i = 0; i < dev.getInterfaceCount(); i++) {
            UsbInterface ui = dev.getInterface(i);
            int cls = ui.getInterfaceClass();
            if (dev.getVendorId() == VID_FTDI) {
                data = ui;
                kind = 1;
                break;
            }
            if (cls == UsbConstants.USB_CLASS_CDC_DATA) {
                data = ui;
                kind = 2;
            } else if (cls == UsbConstants.USB_CLASS_COMM) {
                comm = ui;
                if (kind == 0) {
                    kind = 2;
                }
            } else if (data == null) {
                data = ui;
                kind = 3;
            }
        }
        if (data == null) {
            return false;
        }
        if (comm != null) {
            conn.claimInterface(comm, true);
        }
        if (!conn.claimInterface(data, true)) {
            return false;
        }
        iface = data;
        for (int i = 0; i < data.getEndpointCount(); i++) {
            UsbEndpoint ep = data.getEndpoint(i);
            if (ep.getType() != UsbConstants.USB_ENDPOINT_XFER_BULK) {
                continue;
            }
            if (ep.getDirection() == UsbConstants.USB_DIR_IN) {
                epIn = ep;
            } else {
                epOut = ep;
            }
        }
        return epIn != null && epOut != null;
    }

    private static boolean configure() {
        if (kind == 1) {
            conn.controlTransfer(FTDI_HOST, FTDI_RESET, 0, 0, null, 0, 200);
            conn.controlTransfer(FTDI_HOST, FTDI_RESET, 1, 0, null, 0, 200); /* purge RX */
            conn.controlTransfer(FTDI_HOST, FTDI_RESET, 2, 0, null, 0, 200); /* purge TX */
            conn.controlTransfer(FTDI_HOST, 9, 1, 0, null, 0, 200); /* latency 1 ms */
            /* 115200 on FT232R: divisor 26 */
            conn.controlTransfer(FTDI_HOST, FTDI_BAUD, 26, 0, null, 0, 200);
            conn.controlTransfer(FTDI_HOST, FTDI_DATA, 8, 0, null, 0, 200);
            conn.controlTransfer(FTDI_HOST, FTDI_FLOW, 0, 0, null, 0, 200);
            /* Leave DTR/RTS as the chip has them. Toggling DTR here resets
             * the Nano and the UI thread loses its GL surface. */
            Log.i(TAG, "ftdi configured 115200 8N1 in=" + epIn + " out=" + epOut);
            return true;
        }
        if (kind == 2) {
            byte[] line = new byte[] {(byte) 0x00, (byte) 0xc2, 0x01, 0x00, 0x00, 0x00, 0x08};
            conn.controlTransfer(0x21, 0x20, 0, 0, line, line.length, 200);
            conn.controlTransfer(0x21, 0x22, 0x03, 0, null, 0, 200);
            return true;
        }
        if (kind == 3) {
            /* CH340: set baud 115200 */
            conn.controlTransfer(0x40, 0x9a, 0x1312, 0xcc83, null, 0, 200);
            conn.controlTransfer(0x40, 0xa1, 0, 0, null, 0, 200);
            return true;
        }
        return true;
    }
}

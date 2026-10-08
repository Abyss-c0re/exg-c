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
import android.os.SystemClock;
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
    private static int kind; /* 1 ftdi 2 cdc 3 ch340 4 cp210 */
    private static final Object lock = new Object();
    private static CountDownLatch permLatch;
    private static boolean permOk;
    private static int sTick, sPay, sRaw, sDataLog;
    /* Set for the upload pulse. Full-size all-zero reads are padding, not UART. */
    private static boolean flashRx;
    private static final int RD_MAX = 1024;
    private static final byte[] rdTmp = new byte[RD_MAX];
    /* Bytes past the caller's request stay here. Dropping them ate the
     * tail of a bootloader reply that shared a packet with a short read. */
    private static final byte[] hold = new byte[256];
    private static int holdN;

    /** No instances. The port is process-wide static state. */
    private UsbSerial() {}

    /** Stores the application context and UsbManager. Returns if the permission receiver is already registered; API 33+ registers it not exported. */
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
        /** Main-thread permission result. Another action returns without changing permOk; a match stores the grant and counts the latch down when one is waiting. */
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

    /** Labels of FTDI, CH340, CP210x, and CDC devices, on the USB caller. Empty array if the manager or the device list is null. */
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

    /** Closes any current port, then claims and configures on the USB caller, often the native reader. Returns 0, or −1 if the manager, device, permission, open, claim, or setup fails; the permission wait can block 20 s. */
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
            try {
                if (dev.getConfigurationCount() > 0) {
                    conn.setConfiguration(dev.getConfiguration(0));
                }
            } catch (Exception e) {
                Log.w(TAG, "setConfiguration: " + e.getMessage());
            }
            kind = kindOf(dev);
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

    /** Drops the interface, endpoints, kind, counters, flash-read flag, and hold count, under the USB lock. A missing connection still clears that state. */
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
            sRaw = 0;
            sDataLog = 0;
            flashRx = false;
            holdN = 0;
        }
    }

    /** Appends unread bytes to the 256-byte hold. Returns if src is null, off < 0, or len ≤ 0; a longer chunk keeps only its tail and drops bytes already held. */
    private static void holdAdd(byte[] src, int off, int len) {
        if (src == null || len <= 0 || off < 0) {
            return;
        }
        if (len > hold.length) {
            off += len - hold.length;
            len = hold.length;
            holdN = 0;
        }
        if (holdN + len > hold.length) {
            int drop = holdN + len - hold.length;
            System.arraycopy(hold, drop, hold, 0, holdN - drop);
            holdN -= drop;
        }
        System.arraycopy(src, off, hold, holdN, len);
        holdN += len;
    }

    /** Copies min(n, held) bytes into buf and slides the rest to the front. Returns 0 when nothing is held or n ≤ 0. */
    private static int holdTake(byte[] buf, int n) {
        int k = holdN < n ? holdN : n;
        if (k <= 0) {
            return 0;
        }
        System.arraycopy(hold, 0, buf, 0, k);
        holdN -= k;
        if (holdN > 0) {
            System.arraycopy(hold, k, hold, 0, holdN);
        }
        return k;
    }

    /** Forwards to readFor with an 80 ms timeout, on the USB caller (often the native reader). */
    public static int read(byte[] buf, int n) {
        return readFor(buf, n, 80);
    }

    /** Bootloader sync needs a short wait. FTDI status-only packets are not payload. */
    public static int readFor(byte[] buf, int n, int timeoutMs) {
        synchronized (lock) {
            if (conn == null || epIn == null || buf == null || n <= 0) {
                return 0;
            }
            if (timeoutMs < 5) {
                timeoutMs = 5;
            }
            if (timeoutMs > 80) {
                timeoutMs = 80;
            }
            int maxp = epIn.getMaxPacketSize();
            if (maxp < 8 || maxp > RD_MAX) {
                maxp = 64;
            }
            int out = holdTake(buf, n);
            if (out >= n) {
                return out;
            }
            int idle = 0;
            for (int li = 0; li < 4 && out < n; li++) {
                int cap = kind == 1 ? maxp : Math.min(n - out, RD_MAX);
                if (flashRx && kind == 1) {
                    int z = cap < rdTmp.length ? cap : rdTmp.length;
                    for (int zi = 0; zi < z; zi++) {
                        rdTmp[zi] = 0;
                    }
                }
                int got = conn.bulkTransfer(epIn, rdTmp, cap, li == 0 ? timeoutMs : 2);
                if (got < 0) {
                    if (out == 0 && sTick++ % 40 == 0) {
                        Log.w(TAG, "bulk IN " + got + " (timeout — no serial packet)");
                    }
                    break;
                }
                if (got == 0) {
                    break;
                }
                if (flashRx && (sRaw < 80 || (got > 2 && sDataLog < 16))) {
                    Log.i(TAG, "ftdi rx got=" + got + " " + hexPrefix(rdTmp, got));
                    sRaw++;
                    if (got > 2) {
                        sDataLog++;
                    }
                }
                if (kind == 1) {
                    int src = 0;
                    int produced = 0;
                    while (src < got) {
                        int len = Math.min(maxp, got - src);
                        if (len <= 2) {
                            if (sTick++ == 0) {
                                Log.i(TAG, "ftdi status-only packet (uart idle)");
                            }
                        } else if (flashRx && len == maxp && allZero(rdTmp, src + 2, len - 2)) {
                            /* A full packet of zeros is padding from a short status read. */
                            if (sTick++ == 0) {
                                Log.i(TAG, "ftdi phantom zeros got=" + got);
                            }
                        } else {
                            int pay = len - 2;
                            int space = n - out;
                            int take = pay < space ? pay : space;
                            if (take > 0) {
                                System.arraycopy(rdTmp, src + 2, buf, out, take);
                                out += take;
                                produced += take;
                            }
                            if (pay > take) {
                                holdAdd(rdTmp, src + 2 + take, pay - take);
                            }
                        }
                        if (len < maxp) {
                            break;
                        }
                        src += maxp;
                    }
                    if (produced == 0 && holdN == 0) {
                        if (++idle >= 2) {
                            break;
                        }
                        continue;
                    }
                } else {
                    int space = n - out;
                    int take = got < space ? got : space;
                    if (take > 0) {
                        System.arraycopy(rdTmp, 0, buf, out, take);
                        out += take;
                    }
                    if (got > take) {
                        holdAdd(rdTmp, take, got - take);
                    }
                }
            }
            if (out > 0 && sPay == 0) {
                Log.i(TAG, "serial payload " + out + " bytes kind=" + kind);
                sPay = 1;
            }
            return out;
        }
    }

    /** Bulk OUT on the USB caller, 200 ms per chunk. Returns −1 if closed, n ≤ 0, or the first chunk sends nothing; a later failure returns how many bytes were already written. */
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

    /** Reset on the USB caller. Returns at once if the port is closed.
     * FTDI marks the following reads as a flash, drives DTR and RTS high, waits 20 ms, holds both low for 100 ms, purges, discards RX, then raises both and waits 30 ms.
     * CDC, CH340, and CP210x hold both lines low for 250 ms, then high. CDC also sends a line-coding block of zeros. */
    public static void pulseDtr() {
        synchronized (lock) {
            int lo, hi;
            if (conn == null) {
                return;
            }
            /* High, then low, so a stuck-low DTR still falls. One request for both lines. */
            if (kind == 1) {
                int arm, purge;
                flashRx = true;
                sRaw = 0;
                sDataLog = 0;
                sTick = 0;
                holdN = 0;
                arm = conn.controlTransfer(FTDI_HOST, FTDI_MODEM, 0x0303, 0, null, 0, 200);
                pauseMs(20);
                lo = conn.controlTransfer(FTDI_HOST, FTDI_MODEM, 0x0300, 0, null, 0, 200);
                pauseMs(100);
                purge = ctrl(FTDI_RESET, 1, 0);
                ctrl(FTDI_RESET, 2, 0);
                discardRx();
                hi = conn.controlTransfer(FTDI_HOST, FTDI_MODEM, 0x0303, 0, null, 0, 200);
                Log.i(TAG, "ftdi reset arm=" + arm + " low=" + lo + " purge=" + purge + " high=" + hi);
                pauseMs(30);
            } else if (kind == 2) {
                byte[] line = new byte[] {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08};
                conn.controlTransfer(0x21, 0x22, 0x00, 0, null, 0, 200);
                pauseMs(250);
                conn.controlTransfer(0x21, 0x22, 0x03, 0, null, 0, 200);
                conn.controlTransfer(0x21, 0x20, 0, 0, line, line.length, 200);
                pauseMs(50);
            } else if (kind == 3) {
                ch340Lines(false, false);
                pauseMs(250);
                ch340Lines(true, true);
                pauseMs(50);
            } else if (kind == 4) {
                cpOut(0x07, 0x0300, 0); /* DTR and RTS low */
                pauseMs(250);
                cpOut(0x07, 0x0303, 0);
                pauseMs(50);
            }
        }
    }

    /** 115200 (running sketch and optiboot) or 57600 (old Nano bootloader). */
    public static int setBaud(int baud) {
        synchronized (lock) {
            if (conn == null || (baud != 115200 && baud != 57600)) {
                return -1;
            }
            holdN = 0;
            if (kind == 1) {
                int div = baud == 57600 ? 52 : 26; /* 3 MHz / baud */
                int r = ctrl(FTDI_BAUD, div, 0);
                Log.i(TAG, "ftdi baud " + baud + " div " + div + " -> " + r);
                return r < 0 ? -1 : 0;
            }
            if (kind == 3) {
                return ch340Baud(baud) ? 0 : -1;
            }
            if (kind == 4) {
                byte[] b = new byte[] {
                        (byte) (baud & 0xff),
                        (byte) ((baud >> 8) & 0xff),
                        (byte) ((baud >> 16) & 0xff),
                        (byte) ((baud >> 24) & 0xff)
                };
                int r = conn.controlTransfer(0x41, 0x1e, 0, 0, b, 4, 200);
                Log.i(TAG, "cp210 baud " + baud + " -> " + r);
                return r < 0 ? -1 : 0;
            }
            return -1;
        }
    }

    /** True if the span is all zeros, including a length of 0. readFor uses that to drop a full FTDI packet of padding after the two status bytes. */
    private static boolean allZero(byte[] b, int off, int len) {
        int i;
        for (i = 0; i < len; i++) {
            if (b[off + i] != 0) {
                return false;
            }
        }
        return true;
    }

    /** Up to 12 bytes as upper-case hex, separated by spaces, in the US locale. */
    private static String hexPrefix(byte[] b, int n) {
        StringBuilder sb = new StringBuilder();
        int i;
        if (n > 12) {
            n = 12;
        }
        for (i = 0; i < n; i++) {
            if (i > 0) {
                sb.append(' ');
            }
            sb.append(String.format(Locale.US, "%02X", b[i] & 0xFF));
        }
        return sb.toString();
    }

    /** Drop USB packets already queued from the running sketch. */
    private static void discardRx() {
        int i;
        holdN = 0;
        if (conn == null || epIn == null) {
            return;
        }
        for (i = 0; i < 16; i++) {
            int got = conn.bulkTransfer(epIn, rdTmp, 64, 2);
            if (got <= 0) {
                break;
            }
        }
    }

    /** Sleeps ms milliseconds on the USB caller. InterruptedException is ignored, which also clears the interrupt status. */
    private static void pauseMs(int ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }

    /** Clears the hold, then returns if closed; otherwise drains bulk IN on the USB caller. Stops at 30 ms, 6 packets, a non-positive read, or an FTDI read of 2 bytes or fewer. */
    public static void flush() {
        synchronized (lock) {
            holdN = 0;
            if (conn == null || epIn == null) {
                return;
            }
            byte[] dump = new byte[64];
            int spins = 0;
            long t0 = SystemClock.uptimeMillis();
            /* FTDI sends a 2-byte status packet whenever the UART is idle.
             * An unbounded drain never returns and freezes the app. */
            while (spins < 6 && SystemClock.uptimeMillis() - t0 < 30) {
                int got = conn.bulkTransfer(epIn, dump, dump.length, 2);
                if (got <= 0) {
                    break;
                }
                spins++;
                if (kind == 1 && got <= 2) {
                    break;
                }
            }
        }
    }

    /** Blocks the USB caller up to 20 s for the dialog (mutable pending intent on API 31+) and returns false on timeout or interrupt. The wait must not be the main thread, or the receiver cannot count the latch down. */
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

    /** Supported device whose label or device name equals path. A null, empty, or unknown path still returns the first supported device, or null when the list is missing or empty. */
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

    /** "usb:vvvv:pppp" from the vendor and product ids, four lower-case hex digits each. */
    private static String label(UsbDevice d) {
        return String.format(Locale.US, "usb:%04x:%04x", d.getVendorId(), d.getProductId());
    }

    /** FTDI 0403, CH340 1a86, CP210x 10c4, or a device with a CDC data or comm interface. */
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

    /** Claims a data interface and keeps its bulk IN and OUT; FTDI takes the first interface as kind 1. Returns false when no data interface exists, the claim fails, or either bulk endpoint is missing. */
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
                if (kind == 0) {
                    kind = 2;
                }
            } else if (cls == UsbConstants.USB_CLASS_COMM) {
                comm = ui;
                if (kind == 0) {
                    kind = 2;
                }
            } else if (data == null) {
                data = ui;
                if (kind == 0) {
                    kind = 3;
                }
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

    /** One FTDI vendor control OUT, type 0x40, 200 ms, empty data stage. A negative status is logged and returned. */
    private static int ctrl(int req, int value, int index) {
        int r = conn.controlTransfer(FTDI_HOST, req, value, index, null, 0, 200);
        if (r < 0) {
            Log.w(TAG, "ftdi ctrl req=" + req + " val=" + value + " idx=" + index + " -> " + r);
        }
        return r;
    }



    /** Programs 115200 8N1 with DTR and RTS high on the USB caller; 57600 is not applied here, and the Knight bootloader is 115200. FTDI uses divisor 26 and one 0x0303 write, because a second RTS-only request can drop DTR, and that path returns true even if a control fails. */
    private static boolean configure() {
        if (kind == 1) {
            ctrl(FTDI_RESET, 0, 0);
            ctrl(FTDI_RESET, 1, 0); /* purge RX */
            ctrl(FTDI_RESET, 2, 0); /* purge TX */
            if (ctrl(9, 1, 0) < 0) {
                ctrl(9, 16, 0); /* latency 16 ms if 1 ms is refused */
            }
            /* 115200 on FT232R: divisor 26 */
            ctrl(FTDI_BAUD, 26, 0);
            ctrl(FTDI_DATA, 8, 0);
            ctrl(FTDI_FLOW, 0, 0);
            /* One write. A second RTS-only request can drop DTR on this chip. */
            ctrl(FTDI_MODEM, 0x0303, 0);
            Log.i(TAG, "ftdi configured 115200 8N1 DTR/RTS on in="
                    + epIn.getAddress() + " out=" + epOut.getAddress()
                    + " max=" + epIn.getMaxPacketSize());
            return true;
        }
        if (kind == 4) {
            return configureCp210();
        }
        if (kind == 3) {
            return configureCh340();
        }
        if (kind == 2) {
            byte[] line = new byte[] {(byte) 0x00, (byte) 0xc2, 0x01, 0x00, 0x00, 0x00, 0x08};
            conn.controlTransfer(0x21, 0x20, 0, 0, line, line.length, 200);
            conn.controlTransfer(0x21, 0x22, 0x03, 0, null, 0, 200);
            return true;
        }
        return true;
    }

    /** 1 for FTDI, 3 for CH340, 4 for CP210x, 2 when a CDC interface is present, otherwise 3. */
    private static int kindOf(UsbDevice dev) {
        int vid = dev.getVendorId();
        if (vid == VID_FTDI) {
            return 1;
        }
        if (vid == VID_CH340) {
            return 3;
        }
        if (vid == VID_CP210) {
            return 4;
        }
        for (int i = 0; i < dev.getInterfaceCount(); i++) {
            int cls = dev.getInterface(i).getInterfaceClass();
            if (cls == UsbConstants.USB_CLASS_CDC_DATA
                    || cls == UsbConstants.USB_CLASS_COMM) {
                return 2;
            }
        }
        return 3;
    }

    /** Vendor OUT, request type 0x40, 200 ms, no data. Returns the transfer status, negative on failure. */
    private static int vendOut(int req, int value, int index) {
        return conn.controlTransfer(0x40, req, value, index, null, 0, 200);
    }

    /** CP210x vendor OUT, request type 0x41, 200 ms, no data. Returns the transfer status, negative on failure. */
    private static int cpOut(int req, int value, int index) {
        return conn.controlTransfer(0x41, req, value, index, null, 0, 200);
    }

    /* 115200 → factor 0xCC09, divisor 0x83. Both registers are required.
     * Writing only 0x1312/0xCC83 leaves the factor's low byte unset, so the
     * UART is not 115200 and Knight frames never lock. */
    private static boolean ch340Baud(int baud) {
        long factor = 1532620800L / baud;
        long divisor = 3;
        int val1, val2;
        while (factor > 0xfff0L && divisor > 0) {
            factor >>= 3;
            divisor--;
        }
        factor = 0x10000L - factor;
        divisor |= 0x80L; /* else a CH341 waits until the buffer is full */
        val1 = (int) ((factor & 0xff00L) | divisor);
        val2 = (int) (factor & 0xffL);
        return vendOut(0x9a, 0x1312, val1) >= 0 && vendOut(0x9a, 0x0f2c, val2) >= 0;
    }

    /** CH340 DTR (bit 0x20) and RTS (bit 0x40), inverted, on request 0xa4. False when that control transfer fails. */
    private static boolean ch340Lines(boolean dtr, boolean rts) {
        int bits = (dtr ? 0x20 : 0) | (rts ? 0x40 : 0);
        return vendOut(0xa4, (~bits) & 0xffff, 0) >= 0;
    }

    /** CH340 115200 8N1 with DTR and RTS high, releasing reset. Returns true when the first baud setup or the final line write succeeded, and does not check the second baud write. */
    private static boolean configureCh340() {
        boolean baud, lines;
        vendOut(0xa1, 0, 0);
        baud = ch340Baud(115200);
        vendOut(0x9a, 0x2518, 0x00c3); /* RX | TX | 8N1 */
        vendOut(0xa1, 0x501f, 0xd90a);
        ch340Baud(115200);
        lines = ch340Lines(true, true); /* low DTR holds the Nano in reset */
        Log.i(TAG, "ch340 115200 8N1 DTR/RTS " + (lines ? "on" : "fail"));
        return baud || lines;
    }

    /** Enables the CP210x, then 115200 little-endian, 8N1, and DTR/RTS high (0x0303). Returns false only when the enable request fails. */
    private static boolean configureCp210() {
        byte[] baud = new byte[] {0x00, (byte) 0xc2, 0x01, 0x00}; /* 115200 LE */
        int en = cpOut(0x00, 0x0001, 0);
        int rate, mhs;
        if (en < 0) {
            Log.w(TAG, "cp210 enable failed");
            return false;
        }
        rate = conn.controlTransfer(0x41, 0x1e, 0, 0, baud, 4, 200);
        cpOut(0x03, 0x0800, 0); /* 8N1 */
        mhs = cpOut(0x07, 0x0303, 0); /* DTR and RTS high */
        Log.i(TAG, "cp210 115200 8N1 DTR/RTS " + (mhs >= 0 ? "on" : "fail")
                + " baud=" + rate);
        return true;
    }
}

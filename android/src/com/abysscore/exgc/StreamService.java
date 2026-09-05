package com.abysscore.exgc;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

/** Keeps the API alive when the 2D panel is closed. */
public class StreamService extends Service {
    private static final String CH = "exg-stream";
    private static final String PAIR_CH = "exg-pair";
    private static final int NOTE = 31;
    private final Handler h = new Handler(Looper.getMainLooper());
    private PowerManager.WakeLock wake;
    private WifiManager.WifiLock wifi;
    private boolean ticking;
    private long lastNoteMs;
    private int lastPair;

    public static void ensure(Context c, boolean on) {
        Intent i = new Intent(c, StreamService.class);
        if (on) {
            /* From a visible activity, startService — not startForegroundService.
             * Horizon delays service onCreate; the FGS 5s deadline then kills us. */
            c.startService(i);
        } else {
            c.stopService(i);
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel ch = new NotificationChannel(CH, "EXG share",
                    NotificationManager.IMPORTANCE_LOW);
            ch.setDescription("Sharing EXG");
            ch.setShowBadge(false);
            NotificationChannel pair = new NotificationChannel(PAIR_CH, "EXG ask",
                    NotificationManager.IMPORTANCE_HIGH);
            pair.setDescription("Someone wants EXG");
            pair.setShowBadge(true);
            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null) {
                nm.createNotificationChannel(ch);
                nm.createNotificationChannel(pair);
            }
        }
        if (!goForeground(bootNote())) {
            return;
        }
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        if (pm != null) {
            wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "exgc:stream");
            wake.setReferenceCounted(false);
            wake.acquire();
        }
        try {
            WifiManager wm = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            if (wm != null) {
                wifi = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "exgc:stream");
                wifi.setReferenceCounted(false);
                wifi.acquire();
            }
        } catch (RuntimeException ignored) {
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (!goForeground(note())) {
            return START_NOT_STICKY;
        }
        if (!ticking) {
            ticking = true;
            h.post(tick);
        }
        return START_STICKY;
    }

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (!ticking) {
                return;
            }
            ExgNative.tick();
            long now = android.os.SystemClock.uptimeMillis();
            int pst = ExgNative.pairState();
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            if (nm != null && pst != lastPair) {
                lastPair = pst;
                if (pst == 1) {
                    nm.notify(PairReceiver.NOTE, pairNote());
                } else {
                    nm.cancel(PairReceiver.NOTE);
                }
            }
            if (now - lastNoteMs >= 1000) {
                lastNoteMs = now;
                if (nm != null) {
                    nm.notify(NOTE, note());
                    if (pst == 1) {
                        nm.notify(PairReceiver.NOTE, pairNote());
                    }
                }
            }
            if (!ExgNative.apiOn() && !ExgNative.connected()) {
                stopSelf();
                return;
            }
            h.postDelayed(this, 8);
        }
    };

    private boolean goForeground(Notification n) {
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(NOTE, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            } else {
                startForeground(NOTE, n);
            }
            return true;
        } catch (RuntimeException e) {
            stopSelf();
            return false;
        }
    }

    private Notification bootNote() {
        return buildNote("EXG stream", "starting");
    }

    private Notification note() {
        String line = ExgNative.apiLine();
        String title = ExgNative.connected() ? "EXG on" : "Sharing EXG";
        return buildNote(title, line);
    }

    private Notification pairNote() {
        String who = ExgNative.pairName();
        if (who == null || who.length() < 1) {
            who = "Someone";
        }
        Intent yes = new Intent(this, PairReceiver.class).setAction(PairReceiver.ALLOW);
        Intent no = new Intent(this, PairReceiver.class).setAction(PairReceiver.NO);
        int fl = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) {
            fl |= PendingIntent.FLAG_MUTABLE;
        } else if (Build.VERSION.SDK_INT >= 23) {
            fl |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent py = PendingIntent.getBroadcast(this, 2, yes, fl);
        PendingIntent pn = PendingIntent.getBroadcast(this, 3, no, fl);
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            b = new Notification.Builder(this, PAIR_CH);
        } else {
            b = new Notification.Builder(this);
        }
        b.setContentTitle("EXG")
                .setContentText(who.replace('_', ' ') + " wants EXG")
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setOngoing(true)
                .addAction(0, "Allow", py)
                .addAction(0, "No", pn);
        if (Build.VERSION.SDK_INT >= 21) {
            b.setVisibility(Notification.VISIBILITY_PUBLIC);
            b.setPriority(Notification.PRIORITY_HIGH);
        }
        return b.build();
    }

    private Notification buildNote(String title, String line) {
        Intent open = new Intent(this, ExgActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        int piFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 23) {
            piFlags |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent pi = PendingIntent.getActivity(this, 0, open, piFlags);
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            b = new Notification.Builder(this, CH);
        } else {
            b = new Notification.Builder(this);
        }
        b.setContentTitle(title)
                .setContentText(line)
                .setSmallIcon(android.R.drawable.ic_media_play)
                .setContentIntent(pi)
                .setOngoing(true)
                .setOnlyAlertOnce(true);
        if (Build.VERSION.SDK_INT >= 21) {
            b.setVisibility(Notification.VISIBILITY_PUBLIC);
        }
        return b.build();
    }

    @Override
    public void onDestroy() {
        ticking = false;
        h.removeCallbacks(tick);
        if (wake != null && wake.isHeld()) {
            wake.release();
        }
        if (wifi != null && wifi.isHeld()) {
            wifi.release();
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}

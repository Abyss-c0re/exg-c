package com.abysscore.exgc;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.app.NotificationManager;

public class PairReceiver extends BroadcastReceiver {
    public static final String ALLOW = "com.abysscore.exgc.PAIR_ALLOW";
    public static final String NO = "com.abysscore.exgc.PAIR_NO";
    public static final int NOTE = 32;

    @Override
    public void onReceive(Context c, Intent i) {
        String a = i.getAction();
        if (ALLOW.equals(a)) {
            ExgNative.pairAccept();
        } else if (NO.equals(a)) {
            ExgNative.pairReject();
        }
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) {
            nm.cancel(NOTE);
        }
    }
}

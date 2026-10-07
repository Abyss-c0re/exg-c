package com.abysscore.exgc;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** Boot ASCII and host commands. The binary sample stream stays off this page. */
public final class DebugActivity extends Activity {
    private final Handler h = new Handler(Looper.getMainLooper());
    private TextView head;
    private TextView log;
    private boolean alive;
    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            if (!alive) {
                return;
            }
            ExgNative.tick();
            String warm = ExgNative.streamCold() ? "  warming" : "";
            head.setText("design " + ExgNative.designSps() + " sps" + warm
                    + "    measured " + (int) ExgNative.sps());
            String text = ExgNative.debugLog();
            log.setText(text == null || text.length() == 0 ? "no serial text yet" : text);
            h.postDelayed(this, 250);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xFF16181E);
        root.setPadding(16, 16, 16, 16);

        head = new TextView(this);
        head.setTextColor(0xFFE8EAF0);
        head.setTextSize(16);
        root.addView(head);

        TextView note = new TextView(this);
        note.setTextColor(0xFF8B93A0);
        note.setTextSize(13);
        note.setText("Serial lines from the Knight (EXG-FW, EXG-MODE, EXG-SWITCH, commands). Sample bytes are not printed.");
        root.addView(note);

        log = new TextView(this);
        log.setTextColor(0xFFBAC3A8);
        log.setTextSize(13);
        log.setTypeface(android.graphics.Typeface.MONOSPACE);
        log.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(log);
        root.addView(scroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);
    }

    @Override
    protected void onResume() {
        super.onResume();
        alive = true;
        h.post(poll);
    }

    @Override
    protected void onPause() {
        alive = false;
        h.removeCallbacks(poll);
        super.onPause();
    }
}

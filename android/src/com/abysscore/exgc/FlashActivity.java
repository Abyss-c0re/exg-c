package com.abysscore.exgc;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** One Knight image. Upload is two taps. The banner stays on FLASHED or FAILED. */
public final class FlashActivity extends Activity {
    private final Handler h = new Handler(Looper.getMainLooper());
    private TextView head;
    private TextView banner;
    private TextView log;
    private boolean alive;
    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            if (!alive) {
                return;
            }
            ExgNative.tick();
            head.setText(headline());
            applyBanner(ExgNative.flashState());
            log.setText(ExgNative.flashLog());
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

        head = label(0xFFE8EAF0, 16);
        root.addView(head);

        banner = label(0xFFE8EAF0, 28);
        banner.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        banner.setText("READY");
        root.addView(banner);

        Button upload = button("Upload");
        upload.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        upload.setOnClickListener(v -> ExgNative.flashUpload());
        root.addView(upload);

        TextView note = label(0xFF8B93A0, 13);
        note.setText("One image. Electrodes off. Tap Upload again to write knight.hex. "
                + "The banner stays on FLASHED or FAILED. Mode is in Settings.");
        root.addView(note);

        log = label(0xFFBAC3A8, 13);
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

    private String headline() {
        return "fw " + ExgNative.fwHave() + " / " + ExgNative.fwNeed()
                + "   seen " + ExgNative.fwSeen();
    }

    private void applyBanner(String state) {
        String text = state == null ? "" : state;
        int nl = text.indexOf('\n');
        String tag = nl >= 0 ? text.substring(0, nl) : text;
        String line = nl >= 0 ? text.substring(nl + 1) : "";
        if ("ok".equals(tag)) {
            banner.setText(line.length() == 0 ? "FLASHED" : line);
            banner.setTextColor(0xFF7DFFB0);
        } else if ("err".equals(tag)) {
            banner.setText(line.length() == 0 ? "FAILED" : line);
            banner.setTextColor(0xFFFF6B6B);
        } else if ("run".equals(tag)) {
            banner.setText(line.length() == 0 ? "FLASHING" : line);
            banner.setTextColor(0xFFFFD36B);
        } else if ("arm".equals(tag)) {
            banner.setText(line.length() == 0 ? "TAP AGAIN" : line);
            banner.setTextColor(0xFFFFD36B);
        } else {
            banner.setText("READY");
            banner.setTextColor(0xFFE8EAF0);
        }
    }

    private LinearLayout row() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        return row;
    }

    private Button button(String name) {
        Button b = new Button(this);
        b.setText(name);
        b.setTextColor(0xFFE8EAF0);
        b.setAllCaps(false);
        b.setBackgroundColor(0xFF2A3038);
        b.setLayoutParams(new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        return b;
    }

    private TextView label(int color, int sp) {
        TextView t = new TextView(this);
        t.setTextColor(color);
        t.setTextSize(sp);
        t.setPadding(0, 8, 0, 8);
        return t;
    }
}

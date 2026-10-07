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

/** Knight flasher. Upload is two taps in the host. Electrodes stay off. */
public final class FlashActivity extends Activity {
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
            head.setText(headline());
            log.setText(ExgNative.status() + "\n" + ExgNative.flashLog());
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

        LinearLayout modes = row();
        modes.addView(modeButton(0, "125 + IMU"));
        modes.addView(modeButton(1, "250 EEG"));
        modes.addView(modeButton(2, "500 EEG"));
        root.addView(modes);

        LinearLayout acts = row();
        Button upload = button("Upload");
        upload.setOnClickListener(v -> ExgNative.flashUpload());
        Button write = button("Write mode");
        write.setOnClickListener(v -> ExgNative.flashModeOnly(ExgNative.fwMode(), false));
        acts.addView(upload);
        acts.addView(write);
        root.addView(acts);

        TextView note = label(0xFF8B93A0, 13);
        note.setText("Electrodes off. Tap Upload again to write knight.hex and the mode byte. "
                + "Write mode changes only the EEPROM byte, after firmware "
                + ExgNative.fwNeed() + " is already on the board.");
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
                + "   seen " + ExgNative.fwSeen()
                + "   " + ExgNative.fwLabel(ExgNative.fwMode());
    }

    private Button modeButton(int mode, String name) {
        Button b = button(name);
        b.setOnClickListener(v -> {
            ExgNative.setFwMode(mode);
            head.setText(headline());
        });
        return b;
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

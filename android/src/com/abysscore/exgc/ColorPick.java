package com.abysscore.exgc;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/** RGB sliders + a few presets. Not a 12-step cycle. */
final class ColorPick {
    interface Done {
        void onColor(int rgb);
    }

    private static final int[] PRESET = {
        0x50C8FF, 0xFFB446, 0x78DC8C, 0xF06E8C,
        0xB496FF, 0xFFE65A, 0x5AE6D2, 0xE68CFF,
        0xFF5A5A, 0x5AFF8C, 0xFFFFFF, 0xFF8C28
    };

    private ColorPick() {}

    static void show(Activity act, String title, int startRgb, Done done) {
        int rgb = startRgb & 0x00FFFFFF;
        LinearLayout root = new LinearLayout(act);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(act, 16);
        root.setPadding(pad, pad, pad, pad);

        View swatch = new View(act);
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(act, 56));
        slp.bottomMargin = dp(act, 12);
        GradientDrawable gd = new GradientDrawable();
        gd.setCornerRadius(dp(act, 8));
        gd.setColor(0xFF000000 | rgb);
        swatch.setBackground(gd);
        root.addView(swatch, slp);

        TextView hex = new TextView(act);
        hex.setGravity(Gravity.CENTER);
        hex.setTextColor(0xFFE8EAF0);
        hex.setText(String.format("#%06X", rgb));
        root.addView(hex);

        int[] ch = { (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255 };
        String[] names = { "R", "G", "B" };
        int[] cols = { 0xFFFF6666, 0xFF66FF66, 0xFF6699FF };
        SeekBar[] bars = new SeekBar[3];
        TextView[] labs = new TextView[3];
        Runnable paint = () -> {
            int c = (ch[0] << 16) | (ch[1] << 8) | ch[2];
            gd.setColor(0xFF000000 | c);
            hex.setText(String.format("#%06X", c));
        };
        for (int i = 0; i < 3; i++) {
            final int ix = i;
            LinearLayout row = new LinearLayout(act);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(0, dp(act, 6), 0, dp(act, 6));
            TextView n = new TextView(act);
            n.setText(names[i]);
            n.setTextColor(cols[i]);
            n.setWidth(dp(act, 28));
            SeekBar sb = new SeekBar(act);
            sb.setMax(255);
            sb.setProgress(ch[i]);
            LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            TextView v = new TextView(act);
            v.setText(Integer.toString(ch[i]));
            v.setTextColor(0xFFE8EAF0);
            v.setWidth(dp(act, 48));
            v.setGravity(Gravity.END);
            sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override
                public void onProgressChanged(SeekBar s, int p, boolean fromUser) {
                    ch[ix] = p;
                    v.setText(Integer.toString(p));
                    paint.run();
                }

                @Override
                public void onStartTrackingTouch(SeekBar s) {}

                @Override
                public void onStopTrackingTouch(SeekBar s) {}
            });
            row.addView(n);
            row.addView(sb, blp);
            row.addView(v);
            root.addView(row);
            bars[i] = sb;
            labs[i] = v;
        }

        TextView plab = new TextView(act);
        plab.setText("presets");
        plab.setTextColor(0xFF8B93A0);
        plab.setPadding(0, dp(act, 10), 0, dp(act, 6));
        root.addView(plab);
        LinearLayout presets = new LinearLayout(act);
        presets.setOrientation(LinearLayout.HORIZONTAL);
        for (int p : PRESET) {
            View chip = new View(act);
            GradientDrawable cd = new GradientDrawable();
            cd.setShape(GradientDrawable.OVAL);
            cd.setColor(0xFF000000 | p);
            chip.setBackground(cd);
            LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(dp(act, 28), dp(act, 28));
            clp.setMargins(dp(act, 4), 0, dp(act, 4), 0);
            final int pick = p;
            chip.setOnClickListener(v -> {
                ch[0] = (pick >> 16) & 255;
                ch[1] = (pick >> 8) & 255;
                ch[2] = pick & 255;
                bars[0].setProgress(ch[0]);
                bars[1].setProgress(ch[1]);
                bars[2].setProgress(ch[2]);
                paint.run();
            });
            presets.addView(chip, clp);
        }
        root.addView(presets);

        new AlertDialog.Builder(act)
                .setTitle(title)
                .setView(root)
                .setPositiveButton("Set", (d, w) -> {
                    if (done != null) {
                        done.onColor((ch[0] << 16) | (ch[1] << 8) | ch[2]);
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private static int dp(Activity a, int d) {
        return Math.round(d * a.getResources().getDisplayMetrics().density);
    }
}

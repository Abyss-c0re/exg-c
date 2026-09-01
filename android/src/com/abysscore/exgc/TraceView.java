package com.abysscore.exgc;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;

public class TraceView extends View {
    private static final int NCHAN = 8;
    /* 8 s at 125 SPS, same cap as the desktop window cycle. */
    private static final int NSAMP = 1024;
    private final float[][] wave = new float[NCHAN][NSAMP];
    private final int[] got = new int[NCHAN];
    private final int[] col = new int[NCHAN];
    private final boolean[] clip = new boolean[NCHAN];
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint grid = new Paint();
    private final Paint lab = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private int scaleUv = 200;

    public TraceView(Context c) {
        super(c);
        init();
    }

    public TraceView(Context c, AttributeSet a) {
        super(c, a);
        init();
    }

    private void init() {
        setBackgroundColor(0xFF101218);
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(2.2f);
        line.setStrokeJoin(Paint.Join.ROUND);
        grid.setColor(0x22FFFFFF);
        grid.setStrokeWidth(1f);
        lab.setColor(0xFFB8C0CC);
        lab.setTextSize(28f);
        for (int i = 0; i < NCHAN; i++) {
            col[i] = 0xFF80C8FF;
        }
    }

    public void pull() {
        scaleUv = Math.max(20, ExgNative.scaleUv());
        for (int c = 0; c < NCHAN; c++) {
            got[c] = ExgNative.copyWave(c, wave[c]);
            col[c] = ExgNative.color(c);
            clip[c] = ExgNative.clipped(c);
        }
        postInvalidateOnAnimation();
    }

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);
        int w = getWidth();
        int h = getHeight();
        if (w < 8 || h < 8) {
            return;
        }
        float row = h / (float) NCHAN;
        float uv = scaleUv;
        for (int ch = 0; ch < NCHAN; ch++) {
            float y0 = row * ch;
            float mid = y0 + row * 0.5f;
            c.drawLine(0, mid, w, mid, grid);
            lab.setColor(col[ch]);
            c.drawText("ch" + (ch + 1), 12, y0 + 32, lab);
            if (clip[ch]) {
                lab.setColor(0xFFE05050);
                c.drawText("CLIP", w - 140, y0 + 32, lab);
            }
            int n = got[ch];
            if (n < 2) {
                continue;
            }
            path.reset();
            float amp = (row * 0.42f) / uv;
            for (int i = 0; i < n; i++) {
                float x = i * (w - 8f) / (n - 1);
                float y = mid - wave[ch][i] * amp;
                if (y < y0 + 2) {
                    y = y0 + 2;
                }
                if (y > y0 + row - 2) {
                    y = y0 + row - 2;
                }
                if (i == 0) {
                    path.moveTo(x, y);
                } else {
                    path.lineTo(x, y);
                }
            }
            line.setColor(col[ch]);
            c.drawPath(path, line);
        }
    }
}

package com.abysscore.exgc;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

/** Thin 128-pt strip FFT. Marker only at the effective notch. */
public class FftView extends View {
    private static final int NBINS = 64;
    private final float[] mag = new float[NBINS];
    private final Paint bar = new Paint();
    private final Paint mark = new Paint();
    private final Paint lab = new Paint(Paint.ANTI_ALIAS_FLAG);
    private int peakHz;
    private int markHz;
    private float sps = 125f;
    private float labelSp = 22f;
    private boolean frozen;

    public FftView(Context c) {
        super(c);
        init();
    }

    public FftView(Context c, AttributeSet a) {
        super(c, a);
        init();
    }

    private void init() {
        setBackgroundColor(0xFF0A0C10);
        bar.setColor(0xFF46AADC);
        mark.setColor(0x66E05050);
        lab.setColor(0xFFB8C0CC);
        lab.setTextSize(labelSp);
    }

    public void setLabelScale(float f) {
        if (f < 0.8f) {
            f = 0.8f;
        }
        if (f > 2.2f) {
            f = 2.2f;
        }
        labelSp = 22f * f;
        lab.setTextSize(labelSp);
        invalidate();
    }

    public void pull() {
        frozen = ExgNative.paused();
        if (!frozen) {
            peakHz = ExgNative.copyFft(mag);
            markHz = ExgNative.notchEff();
            float s = ExgNative.sps();
            sps = s > 1f ? s : 125f;
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
        float peak = 1e-12f;
        for (int i = 1; i < NBINS; i++) {
            if (mag[i] > peak) {
                peak = mag[i];
            }
        }
        int markBin = markHz > 1 ? Math.round(markHz * 128f / sps) : -1;
        if (markBin > NBINS - 1) {
            markBin = NBINS - 1;
        }
        if (markBin >= 1) {
            float mx = (markBin - 1) * (w - 1f) / (NBINS - 1);
            c.drawRect(mx - 2f, 0, mx + 2f, h, mark);
        }
        float barW = Math.max(1f, (w - 1f) / (NBINS - 1));
        for (int i = 1; i < NBINS; i++) {
            float bh = mag[i] / peak * (h - 18f);
            if (bh < 1f) {
                bh = 1f;
            }
            float x = (i - 1) * (w - 1f) / (NBINS - 1);
            bar.setColor(i == markBin ? 0xFFE05050 : 0xFF46AADC);
            c.drawRect(x, h - bh, x + barW, h, bar);
        }
        String cap = peakHz > 0 ? ("FFT  " + peakHz + " Hz") : "FFT";
        if (frozen) {
            cap = cap + "  FROZEN";
        }
        lab.setColor(0xFFB8C0CC);
        c.drawText(cap, 8, 18, lab);
        if (markHz > 1) {
            lab.setColor(0xFFE05050);
            c.drawText(markHz + " Hz", w - 110, 18, lab);
        }
    }
}

package com.abysscore.exgc;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
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
    private float labelMul = 1f;
    private float den = 1f;
    private boolean frozen;

    /** One-argument constructor. Background and paints are set before the first draw. */
    public FftView(Context c) {
        super(c);
        init();
    }

    /** XML constructor. Background and paints are set before the first draw. */
    public FftView(Context c, AttributeSet a) {
        super(c, a);
        init();
    }

    /** Dark background and bar, marker, and caption paints. Text size follows density. */
    private void init() {
        setBackgroundColor(0xFF0A0C10);
        bar.setColor(0xFF46AADC);
        mark.setColor(0x66E05050);
        lab.setColor(0xFFB8C0CC);
        syncPaint();
    }

    /** Clamps f to 0.8..2.2, then resizes the caption and redraws. */
    public void setLabelScale(float f) {
        if (f < 0.8f) {
            f = 0.8f;
        }
        if (f > 2.2f) {
            f = 2.2f;
        }
        labelMul = f;
        syncPaint();
        invalidate();
    }

    /** Caption is 11 sp times the label scale. A density under 0.75 is treated as 1. */
    private void syncPaint() {
        DisplayMetrics m = getResources().getDisplayMetrics();
        den = m.density < 0.75f ? 1f : m.density;
        float sd = m.scaledDensity < 0.75f ? den : m.scaledDensity;
        lab.setTextSize(11f * labelMul * sd);
    }

    /** While paused, keeps the last bins and still requests a redraw. Otherwise copies up to 64 bins, the effective notch in Hz, and measured SPS, using 125 when SPS is at most 1. */
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

    /** Returns if either side is under 8 px. Bar height is magnitude over the peak, and the notch marker is omitted when the effective notch is 1 Hz or less. */
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
        syncPaint();
        Paint.FontMetrics fm = lab.getFontMetrics();
        float pad = 3f * den;
        float textH = -fm.ascent + fm.descent;
        float baseline = pad - fm.ascent;
        if (baseline + fm.descent > h - 1f) {
            baseline = h - 1f - fm.descent;
        }
        int markBin = markHz > 1 ? Math.round(markHz * 128f / sps) : -1;
        if (markBin > NBINS - 1) {
            markBin = NBINS - 1;
        }
        float markW = Math.max(1.5f, den);
        if (markBin >= 1) {
            float mx = (markBin - 1) * (w - 1f) / (NBINS - 1);
            c.drawRect(mx - markW, textH, mx + markW, h, mark);
        }
        float barW = Math.max(1f, (w - 1f) / (NBINS - 1));
        float barTop = textH + pad;
        for (int i = 1; i < NBINS; i++) {
            float bh = mag[i] / peak * (h - barTop);
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
        c.drawText(cap, pad, baseline, lab);
        if (markHz > 1) {
            lab.setColor(0xFFE05050);
            String hz = markHz + " Hz";
            c.drawText(hz, w - lab.measureText(hz) - pad, baseline, lab);
        }
    }
}

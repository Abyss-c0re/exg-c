package com.abysscore.exgc;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.view.View;

public class TraceView extends View {
    private static final int NCHAN = 8;
    /* 8 s at 125 SPS, same cap as the desktop window cycle. */
    private static final int NSAMP = 1024;
    private final float[][] wave = new float[NCHAN][NSAMP];
    private final int[] got = new int[NCHAN];
    private final int[] col = new int[NCHAN];
    private final boolean[] clip = new boolean[NCHAN];
    private final String[] site = new String[NCHAN];
    private final float[] rms = new float[NCHAN];
    private final boolean[] on = new boolean[NCHAN];
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint grid = new Paint();
    private final Paint lab = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint rule = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private int scaleUv = 200;
    private int windowS = 2;
    private float labelMul = 1f;
    private float den = 1f;
    private boolean frozen;

    /** One-argument constructor. Background and paints are set before the first draw. */
    public TraceView(Context c) {
        super(c);
        init();
    }

    /** XML constructor. Background and paints are set before the first draw. */
    public TraceView(Context c, AttributeSet a) {
        super(c, a);
        init();
    }

    /** Dark background, stroke and grid paints, and default channel colors. Site labels start as ch1 through ch8. */
    private void init() {
        setBackgroundColor(0xFF101218);
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(2.2f);
        line.setStrokeJoin(Paint.Join.ROUND);
        grid.setColor(0x22FFFFFF);
        grid.setStrokeWidth(1f);
        lab.setColor(0xFFB8C0CC);
        rule.setColor(0xFFE8EAF0);
        syncPaint();
        for (int i = 0; i < NCHAN; i++) {
            col[i] = 0xFF80C8FF;
            site[i] = "ch" + (i + 1);
        }
    }

    /** Clamps f to 0.8..2.2, resizes the labels, and redraws. */
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

    /** 12sp at the phone density, then the UI 1.0/1.5/2.0 control. */
    private void syncPaint() {
        DisplayMetrics m = getResources().getDisplayMetrics();
        den = m.density < 0.75f ? 1f : m.density;
        float sd = m.scaledDensity < 0.75f ? den : m.scaledDensity;
        lab.setTextSize(12f * labelMul * sd);
        rule.setTextSize(11f * labelMul * sd);
        line.setStrokeWidth(Math.max(1.25f, 1.15f * den));
        grid.setStrokeWidth(Math.max(1f, 0.6f * den));
    }

    /** While paused, redraws the last samples if any channel already has more than one, and returns. Otherwise copies each active channel in µV, color, clip, and RMS, appending a minus name on NEG RAIL when it is non-empty and not NONE; an inactive channel is cleared. */
    public void pull() {
        frozen = ExgNative.paused();
        if (frozen) {
            boolean have = false;
            for (int c = 0; c < NCHAN; c++) {
                if (got[c] > 1) {
                    have = true;
                    break;
                }
            }
            if (have) {
                postInvalidateOnAnimation();
                return;
            }
        }
        scaleUv = Math.max(20, ExgNative.scaleUv());
        windowS = Math.max(1, ExgNative.windowS());
        for (int c = 0; c < NCHAN; c++) {
            on[c] = ExgNative.active(c);
            if (!on[c]) {
                got[c] = 0;
                continue;
            }
            got[c] = ExgNative.copyWave(c, wave[c]);
            col[c] = ExgNative.color(c);
            clip[c] = ExgNative.clipped(c);
            String n = ExgNative.elecName(c);
            if (ExgNative.negRail()) {
                String nn = ExgNative.negName(c);
                if (n != null && nn != null && nn.length() > 0 && !"NONE".equals(nn)) {
                    n = n + "-" + nn;
                }
            }
            site[c] = (n == null || n.length() == 0) ? ("ch" + (c + 1)) : n;
            float e = 0f;
            int nSamp = got[c];
            for (int i = 0; i < nSamp; i++) {
                e += wave[c][i] * wave[c][i];
            }
            rms[c] = nSamp > 0 ? (float) Math.sqrt(e / nSamp) : 0f;
        }
        postInvalidateOnAnimation();
    }

    /** Returns if either side is under 8 px, or after "no channels on" when none are active. Each active row is a mid-line grid, an RMS label in µV or mV, and the trace kept inside the row. */
    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);
        int w = getWidth();
        int h = getHeight();
        if (w < 8 || h < 8) {
            return;
        }
        int nOn = 0;
        for (int ch = 0; ch < NCHAN; ch++) {
            if (on[ch]) {
                nOn++;
            }
        }
        syncPaint();
        Paint.FontMetrics fm = lab.getFontMetrics();
        float pad = 4f * den;
        if (nOn < 1) {
            lab.setColor(0xFF8B93A0);
            c.drawText("no channels on", pad, pad - fm.ascent, lab);
            return;
        }
        float row = h / (float) nOn;
        float uv = scaleUv;
        int rowi = 0;
        for (int ch = 0; ch < NCHAN; ch++) {
            if (!on[ch]) {
                continue;
            }
            float y0 = row * rowi;
            rowi++;
            float mid = y0 + row * 0.5f;
            c.drawLine(0, mid, w, mid, grid);
            lab.setColor(col[ch]);
            float baseline = y0 + pad - fm.ascent;
            float maxBase = y0 + row - 2f - fm.descent;
            if (baseline > maxBase) {
                baseline = Math.max(y0 - fm.ascent, maxBase);
            }
            String rmsLab = rms[ch] >= 1000f
                    ? String.format(java.util.Locale.US, "%s  %.1f mV", site[ch], rms[ch] / 1000f)
                    : String.format(java.util.Locale.US, "%s  %.0f µV", site[ch], rms[ch]);
            c.drawText(rmsLab, pad, baseline, lab);
            if (clip[ch]) {
                lab.setColor(0xFFE05050);
                String tag = "CLIP";
                c.drawText(tag, w - lab.measureText(tag) - pad, baseline, lab);
            } else if (frozen) {
                lab.setColor(0xFFF0A040);
                String tag = "FROZEN";
                c.drawText(tag, w - lab.measureText(tag) - pad, baseline, lab);
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
        drawScaleBar(c, w, h, row);
    }

    /** Corner ruler: vertical length is a round µV step, horizontal length is a round time step. */
    private void drawScaleBar(Canvas c, int w, int h, float row) {
        float pxPerUv = (row * 0.42f) / Math.max(20, scaleUv);
        float pxPerSec = (w - 8f) / Math.max(1, windowS);
        float maxH = Math.min(row * 0.85f, 72f * den);
        float maxW = w * 0.38f;
        int showUv = 10;
        int[] niceUv = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
        for (int v : niceUv) {
            if (v * pxPerUv <= maxH) {
                showUv = v;
            }
        }
        float barH = showUv * pxPerUv;
        if (barH < 10f * den) {
            barH = Math.min(maxH, 10f * den);
            showUv = Math.max(1, Math.round(barH / Math.max(1e-4f, pxPerUv)));
        }
        float showT = 0.2f;
        float[] niceT = {0.2f, 0.5f, 1f, 2f, 4f};
        for (float t : niceT) {
            if (t <= windowS + 0.01f && t * pxPerSec <= maxW) {
                showT = t;
            }
        }
        float barW = showT * pxPerSec;
        if (barW < 16f * den) {
            barW = Math.min(maxW, 28f * den);
            showT = barW / Math.max(1f, pxPerSec);
        }
        float pad = 6f * den;
        float xR = w - pad;
        float yB = h - pad;
        float yT = yB - barH;
        float xL = xR - barW;
        float sw = Math.max(1.6f, 1.3f * den);
        rule.setStyle(Paint.Style.STROKE);
        rule.setStrokeWidth(sw);
        rule.setStrokeCap(Paint.Cap.SQUARE);
        float tick = 5f * den;
        c.drawLine(xR, yT, xR, yB, rule);
        c.drawLine(xR - tick, yT, xR + tick * 0.2f, yT, rule);
        c.drawLine(xL, yB, xR, yB, rule);
        c.drawLine(xL, yB - tick, xL, yB + tick * 0.15f, rule);

        rule.setStyle(Paint.Style.FILL);
        String uvLab = showUv >= 1000
                ? (showUv % 1000 == 0 ? (showUv / 1000) + " mV" : String.format(java.util.Locale.US, "%.1f mV", showUv / 1000f))
                : (showUv + " µV");
        Paint.FontMetrics fm = rule.getFontMetrics();
        float tw = rule.measureText(uvLab);
        float uvBase = yT - 2f * den - fm.descent;
        if (uvBase < pad - fm.ascent) {
            uvBase = pad - fm.ascent;
        }
        c.drawText(uvLab, Math.max(pad, xR - tw), uvBase, rule);
        String tLab = showT >= 0.95f && Math.abs(showT - Math.round(showT)) < 0.05f
                ? ((int) Math.round(showT) + " s")
                : String.format(java.util.Locale.US, "%.1f s", showT);
        c.drawText(tLab, Math.max(pad, xL), yB - 2f * den - fm.descent, rule);
    }
}

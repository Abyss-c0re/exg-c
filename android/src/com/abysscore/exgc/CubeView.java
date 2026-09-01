package com.abysscore.exgc;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

/**
 * viz — crimson sample cube (ON cells + channel labels).
 * map — 10-10 assign: tap a channel, then a site on the scalp map or cube.
 */
public class CubeView extends View {
    private static final int NCHAN = 8;
    private static final int MAX_CELL = 40;
    private static final int MAX_SITE = 80;

    private int mode; /* 0 viz 1 map */
    private int ncell;
    private final float[] cellXyz = new float[MAX_CELL * 3];
    private final float[] cellS = new float[MAX_CELL];
    private final int[] cellRgba = new int[MAX_CELL];
    private int nsite;
    private final String[] siteName = new String[MAX_SITE];
    private final float[] siteFx = new float[MAX_SITE];
    private final float[] siteFy = new float[MAX_SITE];
    private final float[] siteX = new float[MAX_SITE];
    private final float[] siteY = new float[MAX_SITE];
    private final float[] siteZ = new float[MAX_SITE];
    private final boolean[] siteCore = new boolean[MAX_SITE];
    private final int[] siteCh = new int[MAX_SITE];
    private final float[] elecX = new float[NCHAN];
    private final float[] elecY = new float[NCHAN];
    private final float[] elecZ = new float[NCHAN];
    private final String[] elecLab = new String[NCHAN];
    private final int[] elecCol = new int[NCHAN];
    private int elecSel;
    private int siteFocus;
    private int smxSeq;
    private int smxFold;
    private float yaw = 0.55f, pitch = 0.40f, zoom = 1.0f;
    private float lastX, lastY;
    private boolean spinning;

    private final int[] siteSx = new int[MAX_SITE];
    private final int[] siteSy = new int[MAX_SITE];
    private final int[] mapSx = new int[MAX_SITE];
    private final int[] mapSy = new int[MAX_SITE];
    private final int[] elecSx = new int[NCHAN];
    private final int[] elecSy = new int[NCHAN];
    private int mapL, mapT, mapR, mapB, cubeB;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ink = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();

    public CubeView(Context c) {
        super(c);
        init();
    }

    public CubeView(Context c, AttributeSet a) {
        super(c, a);
        init();
    }

    private void init() {
        setBackgroundColor(0xFF040206);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2f);
        ink.setTextSize(26f);
        ink.setColor(0xFFF22647);
    }

    public void setLabelScale(float f) {
        if (f < 0.8f) {
            f = 0.8f;
        }
        if (f > 2.2f) {
            f = 2.2f;
        }
        ink.setTextSize(26f * f);
        invalidate();
    }

    public void resetCam() {
        yaw = 0.55f;
        pitch = 0.40f;
        zoom = 1.0f;
        invalidate();
    }

    public void nudgeZoom(int dir) {
        zoom += dir > 0 ? 0.20f : -0.20f;
        if (zoom < 0.70f) {
            zoom = 0.70f;
        }
        if (zoom > 2.80f) {
            zoom = 2.80f;
        }
        invalidate();
    }

    public void pull() {
        mode = ExgNative.cubeView();
        ncell = ExgNative.vizCells(cellXyz, cellS, cellRgba);
        smxSeq = ExgNative.smxSeq();
        smxFold = ExgNative.smxFold();
        nsite = Math.min(MAX_SITE, ExgNative.siteN());
        siteFocus = ExgNative.siteFocus();
        elecSel = ExgNative.elecSel();
        float[] xyz = new float[3];
        float[] xy = new float[2];
        for (int i = 0; i < nsite; i++) {
            siteName[i] = ExgNative.siteName(i);
            siteCore[i] = ExgNative.siteCore(i);
            siteCh[i] = ExgNative.siteCh(i);
            ExgNative.siteFlat(i, xy);
            siteFx[i] = xy[0];
            siteFy[i] = xy[1];
            ExgNative.siteXyz(i, xyz);
            siteX[i] = xyz[0];
            siteY[i] = xyz[1];
            siteZ[i] = xyz[2];
        }
        for (int c = 0; c < NCHAN; c++) {
            elecLab[c] = ExgNative.elecLabel(c);
            elecCol[c] = ExgNative.color(c) | 0xFF000000;
            ExgNative.elecXyz(c, xyz);
            elecX[c] = xyz[0];
            elecY[c] = xyz[1];
            elecZ[c] = xyz[2];
        }
        postInvalidateOnAnimation();
    }

    private void project(float x, float y, float z, float cx, float cy, float k, float[] out) {
        float cyaw = (float) Math.cos(yaw), syaw = (float) Math.sin(yaw);
        float cp = (float) Math.cos(pitch), sp = (float) Math.sin(pitch);
        float x1 = cyaw * x + syaw * z;
        float z1 = -syaw * x + cyaw * z;
        float oy = cp * y - sp * z1;
        out[0] = cx + x1 * k;
        out[1] = cy - oy * k;
        out[2] = sp * y + cp * z1;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        float x = e.getX(), y = e.getY();
        int act = e.getAction();
        if (act == MotionEvent.ACTION_DOWN) {
            if (mode == 1 && hitMap((int) x, (int) y)) {
                return true;
            }
            if (hitElec((int) x, (int) y)) {
                return true;
            }
            if (mode == 1 && hitSite((int) x, (int) y)) {
                return true;
            }
            spinning = y < cubeB || cubeB == 0;
            lastX = x;
            lastY = y;
            return true;
        }
        if (act == MotionEvent.ACTION_MOVE && spinning) {
            ExgNative.cubeSpin((x - lastX) * 0.010f, (lastY - y) * 0.010f);
            yaw += (x - lastX) * 0.010f;
            pitch += (lastY - y) * 0.010f;
            lastX = x;
            lastY = y;
            invalidate();
            return true;
        }
        if (act == MotionEvent.ACTION_UP || act == MotionEvent.ACTION_CANCEL) {
            spinning = false;
        }
        return super.onTouchEvent(e);
    }

    private boolean hitMap(int mx, int my) {
        if (mx < mapL || my < mapT || mx >= mapR || my >= mapB) {
            return false;
        }
        int best = -1, bd = 22 * 22;
        for (int i = 0; i < nsite; i++) {
            int dx = mx - mapSx[i], dy = my - mapSy[i], d = dx * dx + dy * dy;
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best >= 0) {
            ExgNative.assignSite(best);
            return true;
        }
        return true;
    }

    private boolean hitElec(int mx, int my) {
        int best = -1, bd = 28 * 28;
        for (int c = 0; c < NCHAN; c++) {
            int dx = mx - elecSx[c], dy = my - elecSy[c], d = dx * dx + dy * dy;
            if (d < bd) {
                bd = d;
                best = c;
            }
        }
        if (best >= 0) {
            ExgNative.setElecSel(best);
            return true;
        }
        return false;
    }

    private boolean hitSite(int mx, int my) {
        int best = -1, bd = 24 * 24;
        for (int i = 0; i < nsite; i++) {
            int dx = mx - siteSx[i], dy = my - siteSy[i], d = dx * dx + dy * dy;
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best >= 0) {
            ExgNative.assignSite(best);
            return true;
        }
        return false;
    }

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);
        int w = getWidth(), h = getHeight();
        if (w < 8 || h < 8) {
            return;
        }
        int mapH = mode == 1 ? Math.max(160, h / 4) : 0;
        cubeB = h - mapH;
        float cx = w * 0.5f;
        float cy = cubeB * 0.52f;
        float k = Math.min(w, cubeB) / 2.35f * zoom;
        if (k < 50) {
            k = 50;
        }
        drawWire(c, cx, cy, k);
        drawCells(c, cx, cy, k);
        if (mode == 0) {
            drawCore(c, cx, cy, k);
            ink.setColor(0xFFF22647);
            ink.setTextSize(28f);
            String bits = "";
            for (int b = 0; b < 8; b++) {
                bits += ((smxFold >> b) & 1) != 0 ? "1" : "0";
            }
            c.drawText("viz  seq " + smxSeq + "  " + bits + "  ·  drag", 16, 36, ink);
            drawSot(c, w, h);
        } else {
            drawFocusCell(c, cx, cy, k);
            drawSiteLabels(c, cx, cy, k);
            ink.setColor(0xFFF22647);
            ink.setTextSize(26f);
            c.drawText("map  tap ch, then a 10-10 site", 16, 36, ink);
        }
        drawElecLabels(c, cx, cy, k);
        if (mode == 1) {
            drawScalp(c, w, h, mapH);
        }
    }

    private void drawWire(Canvas c, float cx, float cy, float k) {
        float[][] p = {{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
                {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}};
        int[][] e = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        float[] a = new float[3], b = new float[3];
        stroke.setColor(0xFF5A1220);
        stroke.setStrokeWidth(3f);
        for (int i = 0; i < 12; i++) {
            project(p[e[i][0]][0], p[e[i][0]][1], p[e[i][0]][2], cx, cy, k, a);
            project(p[e[i][1]][0], p[e[i][1]][1], p[e[i][1]][2], cx, cy, k, b);
            c.drawLine(a[0], a[1], b[0], b[1], stroke);
        }
    }

    private void drawCells(Canvas c, float cx, float cy, float k) {
        float[] p = new float[3];
        /* farther first: sort by depth */
        int[] order = new int[ncell];
        float[] depth = new float[ncell];
        for (int i = 0; i < ncell; i++) {
            order[i] = i;
            project(cellXyz[i * 3], cellXyz[i * 3 + 1], cellXyz[i * 3 + 2], cx, cy, k, p);
            depth[i] = p[2];
        }
        for (int i = 0; i < ncell; i++) {
            for (int j = i + 1; j < ncell; j++) {
                if (depth[order[i]] > depth[order[j]]) {
                    int t = order[i];
                    order[i] = order[j];
                    order[j] = t;
                }
            }
        }
        for (int oi = 0; oi < ncell; oi++) {
            int i = order[oi];
            int rgba = cellRgba[i];
            int a = (rgba >>> 24) & 255;
            int on = a >= 160 ? 1 : 0;
            project(cellXyz[i * 3], cellXyz[i * 3 + 1], cellXyz[i * 3 + 2], cx, cy, k, p);
            float r = Math.max(on != 0 ? 14f : 6f, cellS[i] * k * (on != 0 ? 0.72f : 0.35f));
            fill.setColor(on != 0 ? rgba : 0x55F22647);
            c.drawRect(p[0] - r, p[1] - r, p[0] + r, p[1] + r, fill);
            if (on != 0) {
                stroke.setColor(0xFFFFFFFF);
                stroke.setStrokeWidth(2f);
                c.drawRect(p[0] - r, p[1] - r, p[0] + r, p[1] + r, stroke);
            }
        }
    }

    private void drawCore(Canvas c, float cx, float cy, float k) {
        float[] p = new float[3];
        project(0, 0, 0, cx, cy, k, p);
        fill.setColor(0xDCF22647);
        float r = 0.14f * k;
        c.drawRect(p[0] - r, p[1] - r, p[0] + r, p[1] + r, fill);
    }

    private void drawFocusCell(Canvas c, float cx, float cy, float k) {
        if (siteFocus < 0 || siteFocus >= nsite) {
            return;
        }
        float[] p = new float[3];
        project(siteX[siteFocus], siteY[siteFocus], siteZ[siteFocus], cx, cy, k, p);
        fill.setColor(0xFFFFD246);
        float r = 0.16f * k;
        c.drawRect(p[0] - r, p[1] - r, p[0] + r, p[1] + r, fill);
    }

    private void drawSiteLabels(Canvas c, float cx, float cy, float k) {
        float[] p = new float[3];
        ink.setTextSize(22f);
        for (int i = 0; i < nsite; i++) {
            project(siteX[i], siteY[i], siteZ[i], cx, cy, k, p);
            siteSx[i] = (int) p[0];
            siteSy[i] = (int) p[1];
            boolean show = i == siteFocus || siteCh[i] >= 0 || siteCore[i];
            if (!show || p[2] < -0.25f) {
                continue;
            }
            int col = i == siteFocus ? 0xFFFFDC50
                    : (siteCh[i] >= 0 ? elecCol[siteCh[i]] : 0xFFA02832);
            ink.setColor(col);
            c.drawText(siteName[i] != null ? siteName[i] : "?", p[0] + 6, p[1] + 6, ink);
        }
    }

    private void drawElecLabels(Canvas c, float cx, float cy, float k) {
        float[] p = new float[3];
        ink.setTextSize(24f);
        for (int ch = 0; ch < NCHAN; ch++) {
            project(elecX[ch], elecY[ch], elecZ[ch], cx, cy, k, p);
            elecSx[ch] = (int) p[0];
            elecSy[ch] = (int) p[1];
            String lab = elecLab[ch] != null ? elecLab[ch] : ("ch" + (ch + 1));
            if (ch == elecSel) {
                fill.setColor(0xCC000000);
                c.drawRect(p[0] - 36, p[1] - 28, p[0] + 36, p[1] - 6, fill);
            }
            ink.setColor(ch == elecSel ? 0xFFFFE6E6 : elecCol[ch]);
            c.drawText(lab, p[0] - ink.measureText(lab) * 0.5f, p[1] - 10, ink);
        }
    }

    private void drawScalp(Canvas c, int w, int h, int mapH) {
        mapL = 16;
        mapT = h - mapH + 8;
        mapR = w - 16;
        mapB = h - 8;
        fill.setColor(0xFF08060A);
        c.drawRect(mapL, mapT, mapR, mapB, fill);
        ink.setColor(0xFF8C2832);
        ink.setTextSize(22f);
        c.drawText("10-10  (nose up)   tap a site to assign", mapL + 8, mapT + 24, ink);
        int mw = mapR - mapL, mh = mapB - mapT;
        for (int i = 0; i < nsite; i++) {
            int sx = mapL + mw / 2 + (int) (siteFx[i] * (mw / 2 - 28));
            int sy = mapT + mh / 2 - (int) (siteFy[i] * (mh / 2 - 28));
            mapSx[i] = sx;
            mapSy[i] = sy;
            int taken = siteCh[i];
            int r = i == siteFocus ? 10 : (taken >= 0 || siteCore[i] ? 7 : 4);
            int col = i == siteFocus ? 0xFFFFD246
                    : (taken >= 0 ? elecCol[taken] : 0xFF5A1820);
            fill.setColor(col);
            c.drawCircle(sx, sy, r, fill);
            if (i == siteFocus || taken >= 0 || siteCore[i]) {
                ink.setColor(i == siteFocus ? 0xFFFFE090 : 0xFFC8B4B8);
                ink.setTextSize(20f);
                c.drawText(siteName[i] != null ? siteName[i] : "?", sx + 8, sy - 4, ink);
            }
        }
    }

    private void drawSot(Canvas c, int w, int h) {
        int cell = Math.min(48, Math.max(28, (w - 32) / 8));
        int y = h - cell - 16;
        ink.setColor(0xFFEEC8CE);
        ink.setTextSize(22f);
        c.drawText("this second", 16, y - 8, ink);
        for (int ch = 0; ch < 8; ch++) {
            int x = 16 + ch * (cell + 6);
            boolean on = ((smxFold >> ch) & 1) != 0;
            fill.setColor(on ? (elecCol[ch] | 0xFF000000) : 0xFF2A1014);
            c.drawRect(x, y, x + cell, y + cell, fill);
            ink.setColor(0xFFFFFFFF);
            ink.setTextSize(20f);
            c.drawText(String.valueOf(ch + 1), x + 8, y + cell - 8, ink);
        }
    }
}

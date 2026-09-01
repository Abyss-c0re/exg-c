package com.abysscore.exgc;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

/**
 * viz — Cube Experience / crimson lattice (N=8, spike #FF141A, levitate).
 * map — 10-10 assign: tap a channel, then a site on the scalp map or cube.
 */
public class CubeView extends View {
    private static final int NCHAN = 8;
    private static final int N = 8;
    private static final int NCELL = N * N * N;
    private static final int MAX_CELL = 40;
    private static final int MAX_SITE = 80;
    private static final int SPIKE = 0xFFFF141A;
    private static final int VOID = 0xFF060001;

    private int mode; /* 0 viz 1 map */
    private int ncell;
    private final float[] cellXyz = new float[MAX_CELL * 3];
    private final float[] cellS = new float[MAX_CELL];
    private final int[] cellRgba = new int[MAX_CELL];
    private final byte[] cubeBits = new byte[NCELL];
    private final byte[] prevBits = new byte[NCELL];
    private final float[] impulse = new float[NCELL];
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
    private float autoYaw;
    private float t;
    private long lastMs;
    private float lastX, lastY;
    private boolean spinning;
    private int onCount;

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

    public CubeView(Context c) {
        super(c);
        init();
    }

    public CubeView(Context c, AttributeSet a) {
        super(c, a);
        init();
    }

    private void init() {
        setBackgroundColor(VOID);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2f);
        ink.setTextSize(26f);
        ink.setColor(SPIKE);
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
        ExgNative.copyCube(cubeBits);
        onCount = 0;
        for (int i = 0; i < NCELL; i++) {
            boolean on = cubeBits[i] != 0;
            if (on) {
                onCount++;
            }
            if (on && prevBits[i] == 0) {
                impulse[i] = 1f;
            } else {
                impulse[i] *= 0.88f;
                if (impulse[i] < 0.02f) {
                    impulse[i] = 0f;
                }
            }
            prevBits[i] = cubeBits[i];
        }
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

    private void tickViz() {
        long now = SystemClock.uptimeMillis();
        float dt = lastMs == 0 ? 0.016f : (now - lastMs) / 1000f;
        if (dt > 0.08f) {
            dt = 0.016f;
        }
        lastMs = now;
        t += dt;
        if (!spinning) {
            /* cube_gl --levitate: 8.5 deg/s drift */
            autoYaw += 0.148f * dt;
        }
    }

    private void project(float x, float y, float z, float cx, float cy, float k, float[] out) {
        float yawUse = yaw + (mode == 0 ? autoYaw : 0f);
        float yy = y;
        if (mode == 0) {
            yy += 0.06f * (float) Math.sin(t * 1.4);
        }
        float cyaw = (float) Math.cos(yawUse), syaw = (float) Math.sin(yawUse);
        float cp = (float) Math.cos(pitch), sp = (float) Math.sin(pitch);
        float x1 = cyaw * x + syaw * z;
        float z1 = -syaw * x + cyaw * z;
        float oy = cp * yy - sp * z1;
        float oz = sp * yy + cp * z1;
        if (mode == 0) {
            float f = 3.6f / (3.6f + oz + 2.2f);
            out[0] = cx + x1 * f * k;
            out[1] = cy - oy * f * k;
            out[2] = oz;
            out[3] = f;
        } else {
            out[0] = cx + x1 * k;
            out[1] = cy - oy * k;
            out[2] = oz;
            out[3] = 1f;
        }
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
        if (mode == 0) {
            tickViz();
            drawGlow(c, w, cubeB);
            drawWire(c, cx, cy, k);
            drawLattice(c, cx, cy, k);
            drawCore(c, cx, cy, k);
            ink.setColor(SPIKE);
            ink.setTextSize(28f);
            String bits = "";
            for (int b = 0; b < 8; b++) {
                bits += ((smxFold >> b) & 1) != 0 ? "1" : "0";
            }
            c.drawText("viz  seq " + smxSeq + "  " + bits + "  ·  drag", 16, 36, ink);
            drawSot(c, w, h);
            postInvalidateOnAnimation();
        } else {
            drawWire(c, cx, cy, k);
            drawCells(c, cx, cy, k);
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

    private void drawGlow(Canvas c, int w, int h) {
        float g = 0.15f + Math.min(0.85f, onCount / 28f);
        if (g < 0.2f) {
            return;
        }
        float rad = Math.min(w, h) * (0.28f + 0.22f * g);
        int a = (int) (40 + 140 * g);
        RadialGradient grad = new RadialGradient(w * 0.5f, h * 0.5f, rad,
                new int[] {(a << 24) | 0x00FF141A, ((int) (a * 0.35f) << 24) | 0x00B40810, 0},
                new float[] {0f, 0.55f, 1f}, Shader.TileMode.CLAMP);
        fill.setShader(grad);
        c.drawRect(0, 0, w, h, fill);
        fill.setShader(null);
    }

    private void drawWire(Canvas c, float cx, float cy, float k) {
        float[][] p = {{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
                {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}};
        int[][] e = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        float[] a = new float[4], b = new float[4];
        float glow = mode == 0 ? (0.15f + Math.min(0.85f, onCount / 28f)) : 0f;
        if (mode == 0) {
            stroke.setColor(0xA08C050D);
            stroke.setStrokeWidth(1.4f + 2.2f * glow);
        } else {
            stroke.setColor(0xFF5A1220);
            stroke.setStrokeWidth(3f);
        }
        for (int i = 0; i < 12; i++) {
            project(p[e[i][0]][0], p[e[i][0]][1], p[e[i][0]][2], cx, cy, k, a);
            project(p[e[i][1]][0], p[e[i][1]][1], p[e[i][1]][2], cx, cy, k, b);
            c.drawLine(a[0], a[1], b[0], b[1], stroke);
        }
    }

    private void drawLattice(Canvas c, float cx, float cy, float k) {
        /* shell nodes + live ON voxels, far first */
        int max = 296 + NCELL;
        float[] depth = new float[max];
        float[] px = new float[max];
        float[] py = new float[max];
        float[] pf = new float[max];
        int[] kind = new int[max]; /* 0 lattice 1 on 2 impulse */
        int[] edgeN = new int[max];
        int n = 0;
        float[] p = new float[4];
        float pulse = 0.5f + 0.5f * (float) Math.sin(t * 3.2);
        float glow = 0.15f + Math.min(0.85f, onCount / 28f);
        for (int iz = 0; iz < N; iz++) {
            for (int iy = 0; iy < N; iy++) {
                for (int ix = 0; ix < N; ix++) {
                    boolean shell = ix == 0 || ix == N - 1 || iy == 0 || iy == N - 1
                            || iz == 0 || iz == N - 1;
                    int idx = ix + N * iy + N * N * iz;
                    boolean on = cubeBits[idx] != 0;
                    float imp = impulse[idx];
                    if (!shell && !on && imp < 0.08f) {
                        continue;
                    }
                    float wx = (ix - 3.5f) / 3.5f;
                    float wy = (iy - 3.5f) / 3.5f;
                    float wz = (iz - 3.5f) / 3.5f;
                    project(wx, wy, wz, cx, cy, k, p);
                    if (n >= max) {
                        continue;
                    }
                    px[n] = p[0];
                    py[n] = p[1];
                    pf[n] = p[3];
                    depth[n] = p[2];
                    edgeN[n] = (ix == 0 || ix == N - 1 ? 1 : 0)
                            + (iy == 0 || iy == N - 1 ? 1 : 0)
                            + (iz == 0 || iz == N - 1 ? 1 : 0);
                    if (imp > 0.08f) {
                        kind[n] = 2;
                    } else if (on) {
                        kind[n] = 1;
                    } else {
                        kind[n] = 0;
                    }
                    n++;
                }
            }
        }
        int[] order = new int[n];
        for (int i = 0; i < n; i++) {
            order[i] = i;
        }
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (depth[order[i]] > depth[order[j]]) {
                    int tmpi = order[i];
                    order[i] = order[j];
                    order[j] = tmpi;
                }
            }
        }
        fill.setStyle(Paint.Style.FILL);
        for (int oi = 0; oi < n; oi++) {
            int i = order[oi];
            float f = pf[i];
            if (f < 0.15f) {
                f = 0.15f;
            }
            if (kind[i] == 0) {
                boolean hot = edgeN[i] >= 2;
                float size = (2.0f + (hot ? 3.5f : 1.2f) * f) * (1.0f + 0.9f * glow);
                if (hot) {
                    size *= 1.0f + 0.35f * pulse * glow;
                }
                int alpha = (int) ((hot ? 70 + 160 * glow : 35 + 80 * glow) * f);
                if (alpha < 20) {
                    alpha = 20;
                }
                if (alpha > 230) {
                    alpha = 230;
                }
                fill.setColor((alpha << 24) | 0x00FF141A);
                c.drawCircle(px[i], py[i], size, fill);
            } else {
                float flash = kind[i] == 2 ? impulse[i] : 0.35f;
                float size = (6.5f + 10f * f) * (1.0f + 0.55f * flash);
                int alpha = kind[i] == 2 ? (int) (220 * flash + 40) : 210;
                fill.setColor((alpha << 24) | 0x00FF141A);
                c.drawCircle(px[i], py[i], size, fill);
                if (kind[i] == 2) {
                    float jit = 8f + 18f * flash;
                    float ang = t * 40f + i;
                    stroke.setColor((int) (0xF2000000 | 0x00FF0D14));
                    stroke.setStrokeWidth(2.4f);
                    c.drawLine(px[i], py[i],
                            px[i] + (float) Math.sin(ang) * jit,
                            py[i] + (float) Math.cos(ang * 1.3) * jit, stroke);
                    stroke.setStrokeWidth(1.4f);
                    c.drawLine(px[i], py[i],
                            px[i] + (float) Math.cos(ang * 1.7) * jit * 1.3f,
                            py[i] + (float) Math.sin(ang * 2.1) * jit * 1.3f, stroke);
                }
            }
        }
    }

    private void drawCells(Canvas c, float cx, float cy, float k) {
        float[] p = new float[4];
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
                    int tord = order[i];
                    order[i] = order[j];
                    order[j] = tord;
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
        float[] p = new float[4];
        project(0, 0, 0, cx, cy, k, p);
        fill.setColor(0xD0FF141A);
        float r = (mode == 0 ? 0.10f : 0.14f) * k * (mode == 0 ? p[3] : 1f);
        if (mode == 0) {
            c.drawCircle(p[0], p[1], r, fill);
        } else {
            c.drawRect(p[0] - r, p[1] - r, p[0] + r, p[1] + r, fill);
        }
    }

    private void drawFocusCell(Canvas c, float cx, float cy, float k) {
        if (siteFocus < 0 || siteFocus >= nsite) {
            return;
        }
        float[] p = new float[4];
        project(siteX[siteFocus], siteY[siteFocus], siteZ[siteFocus], cx, cy, k, p);
        fill.setColor(0xFFFFD246);
        float r = 0.16f * k;
        c.drawRect(p[0] - r, p[1] - r, p[0] + r, p[1] + r, fill);
    }

    private void drawSiteLabels(Canvas c, float cx, float cy, float k) {
        float[] p = new float[4];
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
        float[] p = new float[4];
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
            fill.setColor(on ? SPIKE : 0xFF2A0508);
            c.drawRect(x, y, x + cell, y + cell, fill);
            ink.setColor(0xFFFFFFFF);
            ink.setTextSize(20f);
            c.drawText(String.valueOf(ch + 1), x + 8, y + cell - 8, ink);
        }
    }
}

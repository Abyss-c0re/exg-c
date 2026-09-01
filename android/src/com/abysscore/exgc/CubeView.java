package com.abysscore.exgc;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

public class CubeView extends View {
    private final byte[] cube = new byte[512];
    private final Paint on = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint off = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ink = new Paint(Paint.ANTI_ALIAS_FLAG);
    private float yaw = 0.6f, pitch = 0.35f;
    private float lastX, lastY;

    public CubeView(Context c) {
        super(c);
        init();
    }

    public CubeView(Context c, AttributeSet a) {
        super(c, a);
        init();
    }

    private void init() {
        setBackgroundColor(0xFF10060A);
        on.setColor(0xFFF22647);
        off.setColor(0x33F22647);
        ink.setColor(0xFFEEC8CE);
        ink.setTextSize(30f);
    }

    public void pull() {
        ExgNative.copyCube(cube);
        postInvalidateOnAnimation();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (e.getAction() == MotionEvent.ACTION_DOWN) {
            lastX = e.getX();
            lastY = e.getY();
            return true;
        }
        if (e.getAction() == MotionEvent.ACTION_MOVE) {
            yaw += (e.getX() - lastX) * 0.008f;
            pitch += (e.getY() - lastY) * 0.008f;
            if (pitch > 1.1f) {
                pitch = 1.1f;
            }
            if (pitch < -0.2f) {
                pitch = -0.2f;
            }
            lastX = e.getX();
            lastY = e.getY();
            invalidate();
            return true;
        }
        return super.onTouchEvent(e);
    }

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);
        int w = getWidth();
        int h = getHeight();
        float cx = w * 0.5f;
        float cy = h * 0.52f;
        float s = Math.min(w, h) * 0.055f;
        float cyaw = (float) Math.cos(yaw);
        float syaw = (float) Math.sin(yaw);
        float cp = (float) Math.cos(pitch);
        float sp = (float) Math.sin(pitch);
        c.drawText("8³ cube  drag to spin", 24, 40, ink);
        for (int z = 0; z < 8; z++) {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int bit = cube[x + 8 * y + 64 * z];
                    float X = x - 3.5f;
                    float Y = y - 3.5f;
                    float Z = z - 3.5f;
                    float x1 = X * cyaw - Z * syaw;
                    float z1 = X * syaw + Z * cyaw;
                    float y1 = Y * cp - z1 * sp;
                    float px = cx + x1 * s * 1.6f;
                    float py = cy + y1 * s * 1.6f;
                    float r = bit != 0 ? 7f : 3.5f;
                    c.drawCircle(px, py, r, bit != 0 ? on : off);
                }
            }
        }
    }
}

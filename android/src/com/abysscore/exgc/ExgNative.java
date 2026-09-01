package com.abysscore.exgc;

public final class ExgNative {
    static {
        System.loadLibrary("exg");
    }

    private ExgNative() {}

    public static native int start(String filesDir);
    public static native void shutdown();
    public static native void tick();
    public static native int connect();
    public static native void disconnect();
    public static native boolean connected();
    public static native String status();
    public static native boolean statusOk();
    public static native float sps();
    public static native int frames();
    public static native int copyWave(int ch, float[] dst);
    public static native int scaleUv();
    public static native void cycleScale();
    public static native int windowS();
    public static native void cycleWindow();
    public static native void setActive(int ch, boolean on);
    public static native void setRld(int ch, boolean on);
    public static native void cycleGain(int ch);
    public static native boolean active(int ch);
    public static native boolean rld(int ch);
    public static native int gain(int ch);
    public static native int color(int ch);
    public static native void noiseArm();
    public static native void noiseOk();
    public static native void calm();
    public static native void toggleClean();
    public static native boolean calHave();
    public static native boolean calmHave();
    public static native boolean cleanOn();
    public static native void setName(String s);
    public static native String getName();
    public static native void record();
    public static native void toggleMatch();
    public static native boolean matchOn();
    public static native void setProfile(String s);
    public static native String getProfile();
    public static native int profSave();
    public static native int profLoad();
    public static native String[] profiles();
    public static native String ports();
    public static native void cyclePort();
    public static native void copyCube(byte[] dst);
    public static native void cycleNotch();
    public static native void cycleHp();
    public static native int notch();
    public static native int hp();
    public static native int lp();
    public static native void cycleLp();
    public static native boolean car();
    public static native void toggleCar();
    public static native boolean detrend();
    public static native void toggleDetrend();
    public static native boolean envelope();
    public static native void toggleEnvelope();
    public static native int band();
    public static native void cycleBand();
    public static native boolean clipped(int ch);
    public static native int algo();
    public static native void cycleAlgo();
    public static native String algoName();
    public static native void togglePause();

    public static native int cubeView();
    public static native void setCubeView(int map);
    public static native void cubeSpin(float yaw, float pitch);
    public static native void cubeZoom(int dir);
    public static native void cubeFront();
    public static native int elecSel();
    public static native void setElecSel(int ch);
    public static native String elecLabel(int ch);
    public static native void elecXyz(int ch, float[] xyz);
    public static native int siteFocus();
    public static native void siteStep(int dir);
    public static native void assignSite(int site);
    public static native int siteN();
    public static native String siteName(int i);
    public static native boolean siteCore(int i);
    public static native int siteCh(int i);
    public static native void siteFlat(int i, float[] xy);
    public static native void siteXyz(int i, float[] xyz);
    public static native String siteFocusLabel();
    public static native int vizCells(float[] xyz, float[] size, int[] rgba);
    public static native int smxSeq();
    public static native int smxFold();
    public static native int profExport(String path);
    public static native int profImport(String path);
    public static native String idLine();
    public static native int recMs();
    public static native String matchLine();
}

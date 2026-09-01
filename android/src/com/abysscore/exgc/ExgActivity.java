package com.abysscore.exgc;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class ExgActivity extends Activity {
    private final Handler h = new Handler(Looper.getMainLooper());
    private TraceView traces;
    private CubeView cube;
    private View cubePane;
    private View cubeMapTools;
    private LinearLayout cubeChRow;
    private Button cubeViz, cubeMap, cubeAlgo;
    private TextView siteLabel;
    private View settings;
    private View learnBar;
    private TextView status;
    private TextView imuLine;
    private TextView idLine;
    private TextView profList;
    private Button record;
    private Button connect;
    private Button port;
    private Button tabMain, tabCube, tabPoses, tabSet;
    private View posesPane;
    private LinearLayout poseList;
    private TextView poseHint;
    private Button clean;
    private Button match;
    private Button notch;
    private Button hp;
    private Button scale;
    private Button win;
    private Button band;
    private Button car;
    private Button detrend;
    private Button env;
    private Button lp;
    private Button algo;
    private Button uiScale;
    private Button board;
    private final float[] imu = new float[9];
    private EditText profName;
    private EditText learnName;
    private LinearLayout chGrid;
    private LinearLayout profChips;
    private LinearLayout learnChips;
    private int lastLearnN = -1;
    private int tab;
    private boolean running = true;
    private static final int REQ_EXPORT = 71;
    private static final int REQ_IMPORT = 72;

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (!running) {
                return;
            }
            ExgNative.tick();
            refreshChrome();
            if (tab == 0) {
                traces.pull();
            } else if (tab == 1) {
                cube.pull();
            }
            h.postDelayed(this, 33);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        UsbSerial.init(this);
        File dir = getFilesDir();
        if (dir != null) {
            new File(dir, "exg-c/profiles").mkdirs();
        }
        ExgNative.start(dir != null ? dir.getAbsolutePath() : getApplicationInfo().dataDir);
        setContentView(R.layout.activity_exg);
        traces = findViewById(R.id.traces);
        cube = findViewById(R.id.cube);
        cubePane = findViewById(R.id.cubePane);
        cubeMapTools = findViewById(R.id.cubeMapTools);
        cubeChRow = findViewById(R.id.cubeChRow);
        cubeViz = findViewById(R.id.cubeViz);
        cubeMap = findViewById(R.id.cubeMap);
        cubeAlgo = findViewById(R.id.cubeAlgo);
        siteLabel = findViewById(R.id.siteLabel);
        settings = findViewById(R.id.settings);
        learnBar = findViewById(R.id.learnBar);
        status = findViewById(R.id.status);
        imuLine = findViewById(R.id.imuLine);
        idLine = findViewById(R.id.idLine);
        profList = findViewById(R.id.profList);
        record = findViewById(R.id.record);
        connect = findViewById(R.id.connect);
        port = findViewById(R.id.port);
        tabMain = findViewById(R.id.tabMain);
        tabCube = findViewById(R.id.tabCube);
        tabPoses = findViewById(R.id.tabPoses);
        tabSet = findViewById(R.id.tabSet);
        posesPane = findViewById(R.id.poses);
        poseList = findViewById(R.id.poseList);
        poseHint = findViewById(R.id.poseHint);
        clean = findViewById(R.id.clean);
        match = findViewById(R.id.match);
        notch = findViewById(R.id.notch);
        hp = findViewById(R.id.hp);
        scale = findViewById(R.id.scale);
        win = findViewById(R.id.win);
        band = findViewById(R.id.band);
        car = findViewById(R.id.car);
        detrend = findViewById(R.id.detrend);
        env = findViewById(R.id.env);
        lp = findViewById(R.id.lp);
        algo = findViewById(R.id.algo);
        uiScale = findViewById(R.id.uiScale);
        board = findViewById(R.id.board);
        profName = findViewById(R.id.profName);
        learnName = findViewById(R.id.learnName);
        chGrid = findViewById(R.id.chGrid);
        profChips = findViewById(R.id.profChips);
        learnChips = findViewById(R.id.learnChips);

        connect.setOnClickListener(v -> {
            if (ExgNative.connected()) {
                ExgNative.disconnect();
            } else {
                ExgNative.connect();
            }
            refreshChrome();
        });
        port.setOnClickListener(v -> {
            ExgNative.cyclePort();
            refreshChrome();
        });
        tabMain.setOnClickListener(v -> showTab(0));
        tabCube.setOnClickListener(v -> showTab(1));
        tabPoses.setOnClickListener(v -> showTab(2));
        tabSet.setOnClickListener(v -> showTab(3));
        cubeViz.setOnClickListener(v -> {
            ExgNative.setCubeView(0);
            refreshCubeChrome();
        });
        cubeMap.setOnClickListener(v -> {
            ExgNative.setCubeView(1);
            refreshCubeChrome();
        });
        findViewById(R.id.cubeZoomOut).setOnClickListener(v -> {
            ExgNative.cubeZoom(-1);
            cube.nudgeZoom(-1);
        });
        findViewById(R.id.cubeZoomIn).setOnClickListener(v -> {
            ExgNative.cubeZoom(1);
            cube.nudgeZoom(1);
        });
        findViewById(R.id.cubeFront).setOnClickListener(v -> {
            ExgNative.cubeFront();
            cube.resetCam();
        });
        cubeAlgo.setOnClickListener(v -> {
            ExgNative.cycleAlgo();
            refreshCubeChrome();
            refreshChrome();
        });
        findViewById(R.id.sitePrev).setOnClickListener(v -> {
            ExgNative.siteStep(-1);
            refreshCubeChrome();
        });
        findViewById(R.id.siteNext).setOnClickListener(v -> {
            ExgNative.siteStep(1);
            refreshCubeChrome();
        });
        findViewById(R.id.siteAssign).setOnClickListener(v -> {
            ExgNative.assignSite(ExgNative.siteFocus());
            refreshCubeChrome();
        });
        buildCubeChannels();
        findViewById(R.id.noise).setOnClickListener(v -> ExgNative.noiseArm());
        findViewById(R.id.noiseOk).setOnClickListener(v -> ExgNative.noiseOk());
        findViewById(R.id.calm).setOnClickListener(v -> ExgNative.calm());
        clean.setOnClickListener(v -> {
            ExgNative.toggleClean();
            refreshChrome();
        });
        record.setOnClickListener(v -> {
            ExgNative.setName(learnName.getText().toString().trim());
            ExgNative.record();
            lastLearnN = -1;
            refreshChrome();
        });
        match.setOnClickListener(v -> {
            ExgNative.toggleMatch();
            refreshChrome();
        });
        learnName.addTextChangedListener(new SimpleWatch() {
            @Override
            public void afterTextChanged(Editable s) {
                ExgNative.setName(s.toString().trim());
            }
        });
        findViewById(R.id.profSave).setOnClickListener(v -> {
            ExgNative.setProfile(profName.getText().toString().trim());
            ExgNative.profSave();
            refreshProfiles();
        });
        findViewById(R.id.profLoad).setOnClickListener(v -> {
            ExgNative.setProfile(profName.getText().toString().trim());
            ExgNative.profLoad();
            refreshChannels();
            refreshChrome();
        });
        findViewById(R.id.profExport).setOnClickListener(v -> {
            String name = profName.getText().toString().trim();
            if (name.length() == 0) {
                name = "exg-profile";
            }
            Intent it = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            it.addCategory(Intent.CATEGORY_OPENABLE);
            it.setType("text/plain");
            it.putExtra(Intent.EXTRA_TITLE, name + ".ini");
            startActivityForResult(it, REQ_EXPORT);
        });
        findViewById(R.id.profImport).setOnClickListener(v -> {
            Intent it = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            it.addCategory(Intent.CATEGORY_OPENABLE);
            it.setType("*/*");
            startActivityForResult(it, REQ_IMPORT);
        });
        notch.setOnClickListener(v -> {
            ExgNative.cycleNotch();
            refreshChrome();
        });
        hp.setOnClickListener(v -> {
            ExgNative.cycleHp();
            refreshChrome();
        });
        scale.setOnClickListener(v -> {
            ExgNative.cycleScale();
            refreshChrome();
        });
        win.setOnClickListener(v -> {
            ExgNative.cycleWindow();
            refreshChrome();
        });
        band.setOnClickListener(v -> {
            ExgNative.cycleBand();
            refreshChrome();
        });
        car.setOnClickListener(v -> {
            ExgNative.toggleCar();
            refreshChrome();
        });
        detrend.setOnClickListener(v -> {
            ExgNative.toggleDetrend();
            refreshChrome();
        });
        env.setOnClickListener(v -> {
            ExgNative.toggleEnvelope();
            refreshChrome();
        });
        lp.setOnClickListener(v -> {
            ExgNative.cycleLp();
            refreshChrome();
        });
        algo.setOnClickListener(v -> {
            ExgNative.cycleAlgo();
            refreshChrome();
            refreshCubeChrome();
        });
        uiScale.setOnClickListener(v -> {
            ExgNative.cycleUiScale();
            applyUiScale();
            refreshChrome();
        });
        board.setOnClickListener(v -> {
            ExgNative.cycleBoard();
            refreshChrome();
        });
        buildChannels();
        profName.setText(ExgNative.getProfile());
        refreshProfiles();
        showTab(0);
        lastLearnN = -1;
        refreshLearnChips();
        applyUiScale();
        h.post(tick);
        h.postDelayed(() -> {
            if (!ExgNative.connected()) {
                ExgNative.connect();
                refreshChrome();
            }
        }, 400);
    }

    @Override
    protected void onDestroy() {
        running = false;
        h.removeCallbacks(tick);
        ExgNative.shutdown();
        UsbSerial.close();
        super.onDestroy();
    }

    private void showTab(int t) {
        tab = t;
        traces.setVisibility(t == 0 ? View.VISIBLE : View.GONE);
        cubePane.setVisibility(t == 1 ? View.VISIBLE : View.GONE);
        posesPane.setVisibility(t == 2 ? View.VISIBLE : View.GONE);
        settings.setVisibility(t == 3 ? View.VISIBLE : View.GONE);
        learnBar.setVisibility(t == 0 ? View.VISIBLE : View.GONE);
        tabMain.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 0 ? 0xFF24322C : 0xFF2A3038));
        tabCube.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 1 ? 0xFF3A1820 : 0xFF2A3038));
        tabPoses.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 2 ? 0xFF3A3020 : 0xFF2A3038));
        tabSet.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 3 ? 0xFF243044 : 0xFF2A3038));
        if (t == 1) {
            refreshCubeChrome();
        }
        if (t == 2) {
            lastLearnN = -1;
            rebuildPoseList();
        }
        if (t == 3) {
            refreshChannels();
            refreshProfiles();
        }
    }

    private void buildCubeChannels() {
        cubeChRow.removeAllViews();
        for (int c = 0; c < 8; c++) {
            final int ch = c;
            Button b = new Button(this);
            b.setOnClickListener(v -> {
                ExgNative.setElecSel(ch);
                refreshCubeChrome();
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            cubeChRow.addView(b, lp);
        }
        refreshCubeChrome();
        applyUiScale();
    }

    private void refreshCubeChrome() {
        int map = ExgNative.cubeView();
        cubeViz.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                map == 0 ? 0xFF5A1020 : 0xFF2A3038));
        cubeMap.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                map == 1 ? 0xFF5A2810 : 0xFF2A3038));
        cubeMapTools.setVisibility(map == 1 ? View.VISIBLE : View.GONE);
        cubeAlgo.setText("algo " + ExgNative.algoName());
        siteLabel.setText(ExgNative.siteFocusLabel());
        int sel = ExgNative.elecSel();
        for (int i = 0; i < cubeChRow.getChildCount(); i++) {
            Button b = (Button) cubeChRow.getChildAt(i);
            b.setText(ExgNative.elecLabel(i));
            b.setTextColor(ExgNative.color(i) | 0xFF000000);
            b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    i == sel ? 0xFF5A1020 : 0xFF2A3038));
        }
    }

    private void refreshChrome() {
        boolean on = ExgNative.connected();
        connect.setText(on ? "Disconnect" : "Connect");
        connect.setBackgroundTintList(android.content.res.ColorStateList.valueOf(on ? 0xFF8A3038 : 0xFF2E8A58));
        String ports = ExgNative.ports();
        port.setText(ports == null || ports.length() == 0 ? "(no port)" : ports.split("\n")[0]);
        String st = ExgNative.status();
        float sps = ExgNative.sps();
        int fr = ExgNative.frames();
        if (on && sps > 1f) {
            st = st + "   " + (int) sps + " sps   " + fr + " frames";
        }
        status.setText(st);
        status.setTextColor(ExgNative.statusOk() ? 0xFF3CB46E : 0xFFF0A040);
        clean.setText(ExgNative.cleanOn() ? "CLN" : "cln");
        boolean matching = ExgNative.matchOn();
        match.setText(matching ? "MATCH on" : "MATCH off");
        match.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                matching ? 0xFF2E8A58 : 0xFF2A3038));
        String id = ExgNative.idLine();
        String now = ExgNative.matchLine();
        int rec = ExgNative.recMs();
        int ln = ExgNative.learnN();
        if (rec > 0) {
            idLine.setText("do blink or clench…  " + ((rec + 99) / 1000) + "s  " + id);
            idLine.setTextColor(0xFFF0A040);
            record.setText("…");
        } else {
            String extra = now.length() > 0 ? "   " + now
                    : (ln == 0 ? "   type a name, Record a pose" : "");
            idLine.setText(id + extra);
            int best = ExgNative.learnBest();
            float sc = best >= 0 ? ExgNative.learnScore(best) : 0f;
            idLine.setTextColor(matching && sc >= 0.55f ? 0xFF3CB46E : 0xFF8B93A0);
            record.setText("Record");
        }
        if (ln != lastLearnN) {
            lastLearnN = ln;
            rebuildLearnChips();
            rebuildPoseList();
        }
        refreshLearnChips();
        refreshPoseList();
        int nh = ExgNative.notch();
        notch.setText(nh < 0 ? "notch AUTO" : (nh == 0 ? "notch off" : "notch " + nh));
        hp.setText(ExgNative.hp() == 0 ? "hp off" : "hp " + ExgNative.hp() + "Hz");
        scale.setText("±" + ExgNative.scaleUv() + " µV");
        win.setText("win " + ExgNative.windowS() + "s");
        int bd = ExgNative.band();
        band.setText(bd == 1 ? "band line-kill" : (bd == 2 ? "band EEG" : (bd == 3 ? "band EMG" : "band raw")));
        car.setText(ExgNative.car() ? "CAR on" : "CAR off");
        detrend.setText(ExgNative.detrend() ? "detrend" : "raw DC");
        env.setText(ExgNative.envelope() ? "envelope" : "wave");
        lp.setText(ExgNative.lp() == 0 ? "lp off" : "lp " + ExgNative.lp() + "Hz");
        algo.setText("algo " + ExgNative.algoName());
        int us = ExgNative.uiScale();
        uiScale.setText("UI " + (us == 10 ? "1.0x" : (us == 20 ? "2.0x" : "1.5x")));
        board.setText(ExgNative.boardImu() ? "8-ch + IMU" : "8-ch EXG");
        if (ExgNative.boardImu()) {
            imuLine.setVisibility(View.VISIBLE);
            if (ExgNative.imuOk()) {
                ExgNative.imu(imu);
                imuLine.setText(String.format(java.util.Locale.US,
                        "IMU  acc %+5.2f %+5.2f %+5.2f   gyr %+5.2f %+5.2f %+5.2f   mag %+5.2f %+5.2f %+5.2f",
                        imu[0], imu[1], imu[2], imu[3], imu[4], imu[5], imu[6], imu[7], imu[8]));
                imuLine.setTextColor(0xFFB4C878);
            } else {
                imuLine.setText("IMU  waiting for 57-byte frames");
                imuLine.setTextColor(0xFF8B93A0);
            }
        } else {
            imuLine.setText("8-ch EXG — tap board for IMU");
            imuLine.setTextColor(0xFF8B93A0);
        }
    }

    private void applyUiScale() {
        float f = ExgNative.uiScale() / 10f;
        View root = findViewById(android.R.id.content);
        if (root != null) {
            scaleTree(root, f);
        }
        traces.setLabelScale(f);
        cube.setLabelScale(f);
    }

    private void scaleTree(View v, float f) {
        if (v instanceof android.view.ViewGroup) {
            android.view.ViewGroup vg = (android.view.ViewGroup) v;
            for (int i = 0; i < vg.getChildCount(); i++) {
                scaleTree(vg.getChildAt(i), f);
            }
        }
        if (!(v instanceof TextView)) {
            return;
        }
        TextView tv = (TextView) v;
        Float base = (Float) tv.getTag(R.id.base_sp);
        if (base == null) {
            base = tv.getTextSize() / getResources().getDisplayMetrics().scaledDensity;
            tv.setTag(R.id.base_sp, base);
        }
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, base * f);
        if (tv instanceof Button) {
            Integer mh = (Integer) tv.getTag(R.id.base_min_h);
            if (mh == null) {
                int h = tv.getMinHeight();
                if (h < 8) {
                    h = (int) (48f * getResources().getDisplayMetrics().density);
                }
                mh = h;
                tv.setTag(R.id.base_min_h, mh);
            }
            int nh = Math.max(8, (int) (mh * f));
            tv.setMinHeight(nh);
            tv.setMinimumHeight(nh);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri uri = data.getData();
        File tmp = new File(getCacheDir(), requestCode == REQ_EXPORT ? "export.ini" : "import.ini");
        try {
            if (requestCode == REQ_EXPORT) {
                if (ExgNative.profExport(tmp.getAbsolutePath()) != 0) {
                    status.setText("export failed");
                    return;
                }
                copyFileToUri(tmp, uri);
                status.setText("exported profile");
            } else if (requestCode == REQ_IMPORT) {
                copyUriToFile(uri, tmp);
                if (ExgNative.profImport(tmp.getAbsolutePath()) != 0) {
                    status.setText("import failed");
                    return;
                }
                profName.setText(ExgNative.getProfile());
                refreshChannels();
                refreshProfiles();
                refreshChrome();
            }
        } catch (Exception e) {
            status.setText("file: " + e.getMessage());
        }
    }

    private void copyFileToUri(File src, Uri uri) throws Exception {
        try (InputStream in = new FileInputStream(src);
                OutputStream out = getContentResolver().openOutputStream(uri)) {
            if (out == null) {
                throw new Exception("cannot write document");
            }
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        }
    }

    private void copyUriToFile(Uri uri, File dst) throws Exception {
        try (InputStream in = getContentResolver().openInputStream(uri);
                OutputStream out = new FileOutputStream(dst)) {
            if (in == null) {
                throw new Exception("cannot read document");
            }
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        }
    }

    private void refreshProfiles() {
        String[] ps = ExgNative.profiles();
        profChips.removeAllViews();
        if (ps == null || ps.length == 0) {
            TextView empty = new TextView(this);
            empty.setText("no local profiles yet — Export… to keep one as a file");
            empty.setTextColor(0xFF8B93A0);
            profChips.addView(empty);
            return;
        }
        for (int i = 0; i < ps.length; i++) {
            final String name = ps[i];
            Button b = new Button(this);
            b.setText(name);
            b.setOnClickListener(v -> {
                profName.setText(name);
                ExgNative.setProfile(name);
                ExgNative.profLoad();
                refreshChannels();
                refreshChrome();
            });
            profChips.addView(b);
        }
        applyUiScale();
    }

    private void buildChannels() {
        chGrid.removeAllViews();
        for (int c = 0; c < 8; c++) {
            final int ch = c;
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            Button lab = new Button(this);
            lab.setText("ch" + (c + 1));
            lab.setOnClickListener(v -> {
                ExgNative.cycleColor(ch);
                refreshChannels();
            });
            Button on = new Button(this);
            Button rld = new Button(this);
            Button gn = new Button(this);
            on.setOnClickListener(v -> {
                ExgNative.setActive(ch, !ExgNative.active(ch));
                refreshChannels();
            });
            rld.setOnClickListener(v -> {
                ExgNative.setRld(ch, !ExgNative.rld(ch));
                refreshChannels();
            });
            gn.setOnClickListener(v -> {
                ExgNative.cycleGain(ch);
                refreshChannels();
            });
            row.addView(lab);
            row.addView(on);
            row.addView(rld);
            row.addView(gn);
            row.setTag(ch);
            chGrid.addView(row);
        }
        refreshChannels();
        applyUiScale();
    }

    private void refreshChannels() {
        for (int i = 0; i < chGrid.getChildCount(); i++) {
            LinearLayout row = (LinearLayout) chGrid.getChildAt(i);
            int ch = (Integer) row.getTag();
            Button on = (Button) row.getChildAt(1);
            Button rld = (Button) row.getChildAt(2);
            Button gn = (Button) row.getChildAt(3);
            on.setText(ExgNative.active(ch) ? "ON" : "off");
            rld.setText(ExgNative.rld(ch) ? "RLD" : "rld");
            gn.setText("g" + ExgNative.gain(ch));
            Button lab = (Button) row.getChildAt(0);
            int col = ExgNative.color(ch) | 0xFF000000;
            lab.setTextColor(col);
            int r = (col >> 16) & 255, gc = (col >> 8) & 255, b = col & 255;
            lab.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    0xFF000000 | ((r / 4) << 16) | ((gc / 4) << 8) | (b / 4)));
        }
    }

    private void rebuildLearnChips() {
        learnChips.removeAllViews();
        int n = ExgNative.learnN();
        if (n < 1) {
            TextView empty = new TextView(this);
            empty.setText("no poses — Record one, manage them on Poses");
            empty.setTextColor(0xFF8B93A0);
            empty.setPadding(8, 16, 8, 8);
            learnChips.addView(empty);
            applyUiScale();
            return;
        }
        for (int i = 0; i < n; i++) {
            final int idx = i;
            Button b = new Button(this);
            b.setOnClickListener(v -> {
                ExgNative.learnSelect(idx);
                learnName.setText(ExgNative.learnName(idx));
                refreshLearnChips();
                refreshPoseList();
            });
            learnChips.addView(b);
        }
        refreshLearnChips();
        applyUiScale();
    }

    private void refreshLearnChips() {
        int n = ExgNative.learnN();
        if (n < 1 || learnChips.getChildCount() != n) {
            return;
        }
        boolean matching = ExgNative.matchOn();
        int best = ExgNative.learnBest();
        int sel = ExgNative.learnSel();
        for (int i = 0; i < n; i++) {
            android.view.View child = learnChips.getChildAt(i);
            if (!(child instanceof Button)) {
                continue;
            }
            Button b = (Button) child;
            b.setText(ExgNative.learnName(i));
            int pct = (int) (ExgNative.learnScore(i) * 100f);
            boolean hit = matching && i == best && pct >= 55;
            int bg = hit ? 0xFF2E8A58 : (i == sel ? 0xFF5A1020 : 0xFF2A3038);
            b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(bg));
        }
    }

    private void rebuildPoseList() {
        poseList.removeAllViews();
        int n = ExgNative.learnN();
        if (n < 1) {
            TextView empty = new TextView(this);
            empty.setText("none yet — Record on Main");
            empty.setTextColor(0xFF8B93A0);
            empty.setPadding(8, 16, 8, 8);
            poseList.addView(empty);
            poseHint.setText("MATCH on to see live %. Delete from this tab.");
            applyUiScale();
            return;
        }
        for (int i = 0; i < n; i++) {
            final int idx = i;
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setPadding(0, 4, 0, 4);
            TextView lab = new TextView(this);
            lab.setLayoutParams(new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            lab.setTextColor(0xFFE8EAF0);
            lab.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f);
            lab.setPadding(8, 20, 8, 20);
            lab.setOnClickListener(v -> {
                ExgNative.learnSelect(idx);
                learnName.setText(ExgNative.learnName(idx));
                refreshLearnChips();
                refreshPoseList();
            });
            Button del = new Button(this);
            del.setText("Delete");
            del.setOnClickListener(v -> {
                ExgNative.learnDel(idx);
                lastLearnN = -1;
                refreshChrome();
            });
            row.addView(lab);
            row.addView(del);
            poseList.addView(row);
        }
        refreshPoseList();
        applyUiScale();
    }

    private void refreshPoseList() {
        int n = ExgNative.learnN();
        boolean matching = ExgNative.matchOn();
        int best = ExgNative.learnBest();
        int sel = ExgNative.learnSel();
        if (n < 1) {
            poseHint.setText("MATCH on to see live %. Delete from this tab.");
            return;
        }
        if (poseList.getChildCount() != n) {
            return;
        }
        poseHint.setText(matching ? "live scores — green is a hit (≥ 55%)" : "MATCH off — scores frozen");
        for (int i = 0; i < n; i++) {
            android.view.View child = poseList.getChildAt(i);
            if (!(child instanceof LinearLayout)) {
                continue;
            }
            TextView lab = (TextView) ((LinearLayout) child).getChildAt(0);
            String name = ExgNative.learnName(i);
            int pct = (int) (ExgNative.learnScore(i) * 100f);
            if (matching) {
                lab.setText(name + "   " + pct + "%");
            } else {
                lab.setText(name);
            }
            boolean hit = matching && i == best && pct >= 55;
            lab.setTextColor(hit ? 0xFF3CB46E : (i == sel ? 0xFFF0A040 : 0xFFE8EAF0));
        }
    }

    private abstract static class SimpleWatch implements TextWatcher {
        @Override
        public void beforeTextChanged(CharSequence s, int a, int b, int c) {}

        @Override
        public void onTextChanged(CharSequence s, int a, int b, int c) {}
    }
}

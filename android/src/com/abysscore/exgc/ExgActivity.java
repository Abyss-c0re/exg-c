package com.abysscore.exgc;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.TypedValue;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
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
    private FftView fft;
    private View mainPane;
    private CubeView cube;
    private View cubePane;
    private View cubeMapTools;
    private LinearLayout cubeChRow;
    private Button cubeViz, cubeMap, cubeAlgo, cubeFloat;
    private TextView siteLabel;
    private View settings;
    private View learnBar;
    private TextView status;
    private TextView imuLine;
    private TextView idLine;
    private TextView profList;
    private Button record;
    private Button csv;
    private Button pause;
    private Button atom;
    private TextView atomVs;
    private Button connect;
    private Button link;
    private Button linkDest;
    private Button linkToken;
    private Button port;
    private Button tabMain, tabCube, tabPoses, tabSet;
    private View posesPane;
    private LinearLayout poseList;
    private TextView poseHint;
    private Button clean;
    private Button calibrate;
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
    private TextView apiLine;
    private Button apiOn, apiBind, apiHz, apiHttp, apiUdp, apiTcp, apiToken, apiPush;
    private final float[] imu = new float[9];
    private TextView profNow;
    private Button learnName;
    private LinearLayout chGrid;
    private LinearLayout profChips;
    private LinearLayout learnChips;
    private int lastLearnN = -1;
    private int lastAtomN = -1;
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
                fft.pull();
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
            new File(dir, "exg-c/raw").mkdirs();
            new File(dir, "exg-c/raw/atoms").mkdirs();
            new File(dir, "exg-c/raw/learn").mkdirs();
        }
        ExgNative.start(dir != null ? dir.getAbsolutePath() : getApplicationInfo().dataDir);
        setContentView(R.layout.activity_exg);
        traces = findViewById(R.id.traces);
        fft = findViewById(R.id.fft);
        mainPane = findViewById(R.id.mainPane);
        cube = findViewById(R.id.cube);
        cubePane = findViewById(R.id.cubePane);
        cubeMapTools = findViewById(R.id.cubeMapTools);
        cubeChRow = findViewById(R.id.cubeChRow);
        cubeViz = findViewById(R.id.cubeViz);
        cubeMap = findViewById(R.id.cubeMap);
        cubeAlgo = findViewById(R.id.cubeAlgo);
        cubeFloat = findViewById(R.id.cubeFloat);
        siteLabel = findViewById(R.id.siteLabel);
        settings = findViewById(R.id.settings);
        learnBar = findViewById(R.id.learnBar);
        status = findViewById(R.id.status);
        imuLine = findViewById(R.id.imuLine);
        idLine = findViewById(R.id.idLine);
        profList = findViewById(R.id.profList);
        record = findViewById(R.id.record);
        csv = findViewById(R.id.csv);
        pause = findViewById(R.id.pause);
        atom = findViewById(R.id.atom);
        atomVs = findViewById(R.id.atomVs);
        connect = findViewById(R.id.connect);
        link = findViewById(R.id.link);
        linkDest = findViewById(R.id.linkDest);
        linkToken = findViewById(R.id.linkToken);
        port = findViewById(R.id.port);
        tabMain = findViewById(R.id.tabMain);
        tabCube = findViewById(R.id.tabCube);
        tabPoses = findViewById(R.id.tabPoses);
        tabSet = findViewById(R.id.tabSet);
        posesPane = findViewById(R.id.poses);
        poseList = findViewById(R.id.poseList);
        poseHint = findViewById(R.id.poseHint);
        clean = findViewById(R.id.clean);
        calibrate = findViewById(R.id.calibrate);
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
        apiLine = findViewById(R.id.apiLine);
        apiOn = findViewById(R.id.apiOn);
        apiBind = findViewById(R.id.apiBind);
        apiHz = findViewById(R.id.apiHz);
        apiHttp = findViewById(R.id.apiHttp);
        apiUdp = findViewById(R.id.apiUdp);
        apiTcp = findViewById(R.id.apiTcp);
        apiToken = findViewById(R.id.apiToken);
        apiPush = findViewById(R.id.apiPush);
        profNow = findViewById(R.id.profNow);
        learnName = findViewById(R.id.learnName);
        chGrid = findViewById(R.id.chGrid);
        profChips = findViewById(R.id.profChips);
        learnChips = findViewById(R.id.learnChips);

        connect.setOnClickListener(v -> {
            if (ExgNative.connected()) {
                ExgNative.disconnect();
                refreshChrome();
                return;
            }
            if (ExgNative.linkApi()) {
                String d = ExgNative.linkDest();
                if (d == null || d.length() < 1) {
                    askDest();
                    return;
                }
            }
            ExgNative.connect();
            StreamService.ensure(this, ExgNative.apiOn() || ExgNative.connected());
            refreshChrome();
        });
        link.setOnClickListener(v -> {
            ExgNative.setLinkApi(!ExgNative.linkApi());
            refreshChrome();
        });
        linkDest.setOnClickListener(v -> askDest());
        linkToken.setOnClickListener(v -> askName("API client token (empty = none)",
                ExgNative.linkToken(), s -> {
                    ExgNative.setLinkToken(s);
                    refreshChrome();
                }));
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
        cubeFloat.setOnClickListener(v -> {
            ExgNative.toggleCubeFloat();
            refreshCubeChrome();
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
        findViewById(R.id.calibrate).setOnClickListener(v -> {
            ExgNative.calStart();
            refreshChrome();
        });
        clean.setOnClickListener(v -> {
            ExgNative.toggleClean();
            refreshChrome();
        });
        learnName.setOnClickListener(v -> askName("Learn / ATOM name",
                nameOrEmpty(learnName), s -> {
                    setLearnName(s);
                    ExgNative.setName(s);
                }));
        findViewById(R.id.profNew).setOnClickListener(v -> askName("Save current settings as",
                ExgNative.getProfile(), s -> {
                    if (s.length() == 0) {
                        return;
                    }
                    ExgNative.setProfile(s);
                    ExgNative.profSave();
                    refreshProfiles();
                    refreshChrome();
                }));
        record.setOnClickListener(v -> {
            ExgNative.setName(nameOrEmpty(learnName));
            ExgNative.record();
            lastLearnN = -1;
            refreshChrome();
        });
        match.setOnClickListener(v -> {
            ExgNative.toggleMatch();
            refreshChrome();
        });
        csv.setOnClickListener(v -> {
            ExgNative.toggleCsv();
            refreshChrome();
        });
        pause.setOnClickListener(v -> {
            ExgNative.togglePause();
            refreshChrome();
        });
        atom.setOnClickListener(v -> {
            if (ExgNative.atomOn()) {
                int n = ExgNative.atomStop();
                refreshChrome();
                if (n >= 1) {
                    nameTake(n);
                }
            } else {
                ExgNative.atomStart();
                refreshChrome();
            }
        });
        findViewById(R.id.profExport).setOnClickListener(v -> {
            String name = ExgNative.getProfile();
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
        notch.setOnClickListener(v -> pick("Notch",
                new String[] {"off", "50 Hz", "60 Hz", "AUTO"},
                notchIndex(), i -> {
                    int[] hz = {0, 50, 60, -1};
                    ExgNative.setNotch(hz[i]);
                    refreshChrome();
                }));
        hp.setOnClickListener(v -> pick("High-pass",
                new String[] {"off", "1 Hz", "2 Hz", "5 Hz", "20 Hz"},
                hpIndex(), i -> {
                    int[] hz = {0, 1, 2, 5, 20};
                    ExgNative.setHp(hz[i]);
                    refreshChrome();
                }));
        scale.setOnClickListener(v -> pick("Scale",
                new String[] {"±50 µV", "±100 µV", "±200 µV", "±500 µV",
                        "±1000 µV", "±2000 µV", "±5000 µV"},
                scaleIndex(), i -> {
                    int[] uv = {50, 100, 200, 500, 1000, 2000, 5000};
                    ExgNative.setScaleUv(uv[i]);
                    refreshChrome();
                }));
        win.setOnClickListener(v -> pick("Time window",
                new String[] {"1 s", "2 s", "4 s", "8 s"},
                winIndex(), i -> {
                    ExgNative.setWindowS(new int[] {1, 2, 4, 8}[i]);
                    refreshChrome();
                }));
        band.setOnClickListener(v -> pick("Band preset",
                new String[] {"raw", "line-kill", "EEG", "EMG"},
                ExgNative.band(), i -> {
                    ExgNative.setBand(i);
                    refreshChrome();
                }));
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
        lp.setOnClickListener(v -> pick("Low-pass",
                new String[] {"off", "20 Hz", "40 Hz"},
                lpIndex(), i -> {
                    ExgNative.setLp(new int[] {0, 20, 40}[i]);
                    refreshChrome();
                }));
        algo.setOnClickListener(v -> pickAlgo());
        uiScale.setOnClickListener(v -> pick("UI scale",
                new String[] {"1.0×", "1.5×", "2.0×"},
                uiIndex(), i -> {
                    ExgNative.setUiScale(new int[] {10, 15, 20}[i]);
                    applyUiScale();
                    refreshChrome();
                }));
        board.setOnClickListener(v -> pick("Board",
                new String[] {"8-ch + IMU", "8-ch EXG"},
                ExgNative.boardImu() ? 0 : 1, i -> {
                    ExgNative.setBoardImu(i == 0);
                    refreshChrome();
                }));
        apiOn.setOnClickListener(v -> {
            ExgNative.setApiOn(!ExgNative.apiOn());
            StreamService.ensure(this, ExgNative.apiOn() || ExgNative.connected());
            refreshChrome();
        });
        apiBind.setOnClickListener(v -> {
            ExgNative.setApiLan(!ExgNative.apiLan());
            refreshChrome();
        });
        apiHz.setOnClickListener(v -> askPort("Stream rate Hz", ExgNative.apiHz(), p -> {
            ExgNative.setApiHz(p < 1 ? 1 : p);
            refreshChrome();
        }));
        apiHttp.setOnClickListener(v -> askPort("HTTP port (0 = off)", ExgNative.apiHttp(), p -> {
            ExgNative.setApiHttp(p);
            refreshChrome();
        }));
        apiUdp.setOnClickListener(v -> askPort("UDP port (0 = off)", ExgNative.apiUdp(), p -> {
            ExgNative.setApiUdp(p);
            refreshChrome();
        }));
        apiTcp.setOnClickListener(v -> askPort("TCP port (0 = off)", ExgNative.apiTcp(), p -> {
            ExgNative.setApiTcp(p);
            refreshChrome();
        }));
        apiToken.setOnClickListener(v -> askName("LAN token (empty = off)",
                ExgNative.apiToken(), s -> {
                    ExgNative.setApiToken(s);
                    refreshChrome();
                }));
        apiPush.setOnClickListener(v -> askName("UDP push dest  host:port",
                ExgNative.apiPush(), s -> {
                    ExgNative.setApiPush(s);
                    refreshChrome();
                }));
        port.setOnClickListener(v -> {
            if (ExgNative.linkApi()) {
                askDest();
            } else {
                pickPort();
            }
        });
        cubeAlgo.setOnClickListener(v -> pickAlgo());
        buildChannels();
        refreshProfiles();
        showTab(0);
        lastLearnN = -1;
        refreshLearnChips();
        applyUiScale();
        h.post(tick);
        h.postDelayed(() -> {
            if (!ExgNative.connected()) {
                if (ExgNative.linkApi()) {
                    String d = ExgNative.linkDest();
                    if (d == null || d.length() < 1) {
                        return;
                    }
                }
                ExgNative.connect();
                refreshChrome();
            }
        }, 400);
    }

    @Override
    protected void onResume() {
        super.onResume();
        try {
            StreamService.ensure(this, ExgNative.apiOn() || ExgNative.connected());
        } catch (RuntimeException ignored) {
        }
    }

    @Override
    protected void onDestroy() {
        running = false;
        h.removeCallbacks(tick);
        if (!ExgNative.apiOn() && !ExgNative.connected()) {
            ExgNative.shutdown();
            UsbSerial.close();
        }
        super.onDestroy();
    }

    private void showTab(int t) {
        tab = t;
        mainPane.setVisibility(t == 0 ? View.VISIBLE : View.GONE);
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
            lastAtomN = -1;
            rebuildSavedList();
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
        boolean fl = ExgNative.cubeFloat();
        cubeFloat.setText(fl ? "float on" : "float off");
        cubeFloat.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                fl ? 0xFF2E8A58 : 0xFF2A3038));
        siteLabel.setText(ExgNative.siteFocusLabel());
        int sel = ExgNative.elecSel();
        for (int i = 0; i < cubeChRow.getChildCount(); i++) {
            Button b = (Button) cubeChRow.getChildAt(i);
            if (!ExgNative.active(i)) {
                b.setVisibility(View.GONE);
                continue;
            }
            b.setVisibility(View.VISIBLE);
            b.setText(ExgNative.elecLabel(i));
            b.setTextColor(ExgNative.color(i) | 0xFF000000);
            b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    i == sel ? 0xFF5A1020 : 0xFF2A3038));
        }
    }

    private void refreshChrome() {
        boolean on = ExgNative.connected();
        boolean apiLink = ExgNative.linkApi();
        connect.setText(on ? "Disconnect" : "Connect");
        connect.setBackgroundTintList(android.content.res.ColorStateList.valueOf(on ? 0xFF8A3038 : 0xFF2E8A58));
        link.setText(apiLink ? "API" : "USB");
        link.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                apiLink ? 0xFF2E6A8A : 0xFF2A3038));
        String ports = ExgNative.ports();
        if (apiLink) {
            String d = ExgNative.linkDest();
            port.setText(d == null || d.length() == 0 ? "type dest" : d);
        } else {
            port.setText(ports == null || ports.length() == 0 ? "(no port)" : ports.split("\n")[0]);
        }
        String st = ExgNative.status();
        float sps = ExgNative.sps();
        int fr = ExgNative.frames();
        if (on && sps > 1f) {
            st = st + "   " + (int) sps + " sps   " + fr + " frames";
        }
        if (ExgNative.apiOn()) {
            st = st + "   " + ExgNative.apiLine();
        }
        status.setText(st);
        status.setTextColor(ExgNative.statusOk() ? 0xFF3CB46E : 0xFFF0A040);
        {
            String cl = ExgNative.calLine();
            int ph = ExgNative.calPhase();
            int pg = ExgNative.calProgress();
            if (ph == 1 || ph == 3 || ph == 5) {
                calibrate.setText(cl + "  " + pg + "%");
            } else {
                calibrate.setText(cl);
            }
            calibrate.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    ph == 4 || (ExgNative.calHave() && ExgNative.calmHave()) ? 0xFF2E8A58
                            : (ph == 1 || ph == 3 || ph == 5 ? 0xFF8A6030 : 0xFF2A3038)));
        }
        if (ExgNative.cleanLive()) {
            clean.setText("CLEAN on");
        } else if (ExgNative.cleanOn()) {
            clean.setText("DC on");
        } else {
            clean.setText("DC off");
        }
        clean.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                ExgNative.cleanOn() ? 0xFF2E8A58 : 0xFF2A3038));
        boolean matching = ExgNative.matchOn();
        boolean haveTakes = ExgNative.atomCount() > 0;
        match.setText(haveTakes ? (matching ? "ID on" : "ID off")
                : (matching ? "MATCH on" : "MATCH off"));
        match.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                matching ? 0xFF2E8A58 : 0xFF2A3038));
        boolean recCsv = ExgNative.csvOn();
        csv.setText(recCsv ? "Stop CSV" : "CSV");
        csv.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                recCsv ? 0xFF8A3038 : 0xFF2A3038));
        boolean held = ExgNative.paused();
        pause.setText(held ? "PAUSED" : "Pause");
        pause.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                held ? 0xFF8A6030 : 0xFF2A3038));
        boolean folding = ExgNative.atomOn();
        int takeN = ExgNative.atomN();
        atom.setText(folding ? ("Stop  " + takeN + "s") : "Take");
        atom.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                folding ? 0xFF8A3038 : 0xFF2A3038));
        String vs = ExgNative.atomLine();
        atomVs.setText(vs);
        atomVs.setTextColor(folding ? 0xFFF0A040 : 0xFFC87880);
        String id = ExgNative.idLine();
        String now = ExgNative.matchLine();
        int rec = ExgNative.recMs();
        int ln = ExgNative.learnN();
        if (rec > 0) {
            idLine.setText("do blink or clench…  " + ((rec + 99) / 1000) + "s  " + id);
            idLine.setTextColor(0xFFF0A040);
            record.setText("…");
        } else {
            String atoms = ExgNative.atomLine();
            String extra = now.length() > 0 ? "   " + now
                    : (atoms.length() > 0 ? "   " + atoms
                            : (ln == 0 ? "   type a name, Record a pose" : ""));
            idLine.setText(id + extra);
            boolean named = now.startsWith("now ") && !now.startsWith("now —");
            idLine.setTextColor(named ? 0xFF3CB46E : 0xFF8B93A0);
            if (on && sps > 0f && sps < 80f) {
                record.setText("wait " + (int) sps + " sps");
            } else {
                record.setText("Record");
            }
        }
        int an = ExgNative.atomCount();
        if (ln != lastLearnN || an != lastAtomN) {
            lastLearnN = ln;
            lastAtomN = an;
            rebuildLearnChips();
            rebuildSavedList();
        }
        refreshLearnChips();
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
        boolean apion = ExgNative.apiOn();
        apiOn.setText(apion ? "API on" : "API off");
        apiOn.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                apion ? 0xFF2E8A58 : 0xFF2A3038));
        apiBind.setText(ExgNative.apiLan() ? "lan" : "local");
        apiHz.setText(ExgNative.apiHz() + " Hz");
        apiHttp.setText(ExgNative.apiHttp() == 0 ? "http off" : ("http " + ExgNative.apiHttp()));
        apiUdp.setText(ExgNative.apiUdp() == 0 ? "udp off" : ("udp " + ExgNative.apiUdp()));
        apiTcp.setText(ExgNative.apiTcp() == 0 ? "tcp off" : ("tcp " + ExgNative.apiTcp()));
        {
            String tok = ExgNative.apiToken();
            apiToken.setText(tok == null || tok.length() == 0 ? "token (off)" : "token set");
            String dest = ExgNative.apiPush();
            apiPush.setText(dest == null || dest.length() == 0 ? "push dest" : dest);
            String peer = ExgNative.linkDest();
            linkDest.setText(peer == null || peer.length() == 0 ? "client dest" : peer);
            String ctok = ExgNative.linkToken();
            linkToken.setText(ctok == null || ctok.length() == 0 ? "client token" : "client token set");
            apiLine.setText(ExgNative.apiLine());
        }
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
            imuLine.setVisibility(View.GONE);
        }
    }

    private void applyUiScale() {
        float f = ExgNative.uiScale() / 10f;
        View root = findViewById(android.R.id.content);
        if (root != null) {
            scaleTree(root, f);
        }
        traces.setLabelScale(f);
        fft.setLabelScale(f);
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
        String cur = ExgNative.getProfile();
        if (profNow != null) {
            profNow.setText(cur != null && cur.length() > 0 ? ("now: " + cur) : "now: (none)");
        }
        profChips.removeAllViews();
        if (ps == null || ps.length == 0) {
            TextView empty = new TextView(this);
            empty.setText("No profiles yet. Set band/filters, then Save current as…");
            empty.setTextColor(0xFF8B93A0);
            profChips.addView(empty);
            applyUiScale();
            return;
        }
        for (int i = 0; i < ps.length; i++) {
            final String name = ps[i];
            Button b = new Button(this);
            boolean on = name.equals(cur);
            b.setText(on ? (name + "   • now") : name);
            b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    on ? 0xFF2E8A58 : 0xFF2A3038));
            b.setOnClickListener(v -> {
                ExgNative.setProfile(name);
                ExgNative.profLoad();
                refreshChannels();
                refreshProfiles();
                refreshChrome();
            });
            b.setOnLongClickListener(v -> {
                new android.app.AlertDialog.Builder(this)
                        .setTitle(name)
                        .setItems(new CharSequence[] {"Rename", "Delete"}, (d, which) -> {
                            if (which == 0) {
                                askName("Rename profile", name, s -> {
                                    if (s.length() == 0) {
                                        return;
                                    }
                                    ExgNative.setProfile(name);
                                    ExgNative.profRename(s);
                                    refreshProfiles();
                                    refreshChrome();
                                });
                            } else {
                                ExgNative.setProfile(name);
                                ExgNative.profDel();
                                refreshProfiles();
                                refreshChrome();
                            }
                        })
                        .show();
                return true;
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
            lab.setText(ExgNative.elecName(c));
            lab.setOnClickListener(v -> {
                String name = ExgNative.elecName(ch);
                ColorPick.show(this, name + " color",
                        ExgNative.color(ch), rgb -> {
                            ExgNative.setColor(ch, rgb);
                            refreshChannels();
                        });
            });
            Button on = new Button(this);
            Button rld = new Button(this);
            Button gn = new Button(this);
            LinearLayout.LayoutParams lpLab = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1.4f);
            LinearLayout.LayoutParams lpBtn = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            on.setOnClickListener(v -> {
                ExgNative.setActive(ch, !ExgNative.active(ch));
                refreshChannels();
                refreshChrome();
            });
            rld.setOnClickListener(v -> {
                ExgNative.setRld(ch, !ExgNative.rld(ch));
                refreshChannels();
                refreshChrome();
            });
            gn.setOnClickListener(v -> pick(ExgNative.elecName(ch) + " gain",
                    new String[] {"1", "2", "3", "4", "6", "8", "12"},
                    gainIndex(ch), i -> {
                        ExgNative.setGain(ch, new int[] {1, 2, 3, 4, 6, 8, 12}[i]);
                        refreshChannels();
                    }));
            row.addView(lab, lpLab);
            row.addView(on, lpBtn);
            row.addView(rld, lpBtn);
            row.addView(gn, lpBtn);
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
            boolean live = ExgNative.active(ch);
            boolean bias = ExgNative.rld(ch);
            on.setText(live ? "ON" : "off");
            on.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    live ? 0xFF2E8A58 : 0xFF3A3030));
            rld.setText(bias ? "bias ON" : "bias off");
            rld.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    bias ? 0xFF2E6A8A : 0xFF3A3030));
            gn.setText("g" + ExgNative.gain(ch));
            Button lab = (Button) row.getChildAt(0);
            lab.setText(ExgNative.elecName(ch));
            int col = ExgNative.color(ch) | 0xFF000000;
            lab.setTextColor(col);
            int r = (col >> 16) & 255, gc = (col >> 8) & 255, b = col & 255;
            lab.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    0xFF000000 | ((r / 4) << 16) | ((gc / 4) << 8) | (b / 4)));
        }
        refreshCubeChrome();
    }

    private void rebuildLearnChips() {
        learnChips.removeAllViews();
        int n = ExgNative.atomCount();
        if (n < 1) {
            TextView empty = new TextView(this);
            empty.setText("Take rest, then an action. ID names only a unique winner.");
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
                ExgNative.atomPick(idx);
                lastAtomN = -1;
                refreshChrome();
            });
            b.setOnLongClickListener(v -> {
                ExgNative.atomDel(idx);
                lastAtomN = -1;
                refreshChrome();
                return true;
            });
            learnChips.addView(b);
        }
        refreshLearnChips();
        applyUiScale();
    }

    private void refreshLearnChips() {
        int n = ExgNative.atomCount();
        if (n < 1 || learnChips.getChildCount() != n) {
            return;
        }
        boolean matching = ExgNative.matchOn();
        int best = ExgNative.atomIdBest();
        for (int i = 0; i < n; i++) {
            android.view.View child = learnChips.getChildAt(i);
            if (!(child instanceof Button)) {
                continue;
            }
            Button b = (Button) child;
            int pct = (int) (ExgNative.atomIdScore(i) * 100f);
            String name = ExgNative.atomAt(i);
            boolean hit = matching && i == best && pct >= 70;
            if (hit) {
                b.setText(name + "  " + pct + "%");
            } else {
                b.setText(name);
            }
            b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    hit ? 0xFF2E8A58 : 0xFF2A3038));
        }
    }

    private TextView savedLabel(String s, int col) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextColor(col);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f);
        t.setPadding(8, 16, 8, 8);
        return t;
    }

    private void rebuildSavedList() {
        poseList.removeAllViews();
        int na = ExgNative.atomCount();
        String pair = ExgNative.atomPair();
        poseHint.setText(pair);
        poseList.addView(savedLabel("Tap rest, then tap the action. That is the compare.", 0xFFC87880));
        if (na < 1) {
            poseList.addView(savedLabel("none — Take on Main, Stop, name it", 0xFF8B93A0));
        }
        for (int i = 0; i < na; i++) {
            final int idx = i;
            String name = ExgNative.atomAt(i);
            int sec = ExgNative.atomSecs(i);
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setPadding(0, 4, 0, 4);
            Button lab = new Button(this);
            lab.setLayoutParams(new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            String sa = ExgNative.atomSlotA();
            String sb = ExgNative.atomSlotB();
            String tag = "";
            int bg = 0xFF2A3038;
            if (name.equals(sa)) {
                tag = "   A";
                bg = 0xFF8A6030;
            } else if (name.equals(sb)) {
                tag = "   B";
                bg = 0xFF2E8A58;
            }
            lab.setText(name + "   " + sec + " s" + tag);
            lab.setBackgroundTintList(android.content.res.ColorStateList.valueOf(bg));
            lab.setOnClickListener(v -> {
                ExgNative.atomPick(idx);
                lastAtomN = -1;
                refreshChrome();
            });
            Button del = new Button(this);
            del.setText("Delete");
            del.setOnClickListener(v -> {
                ExgNative.atomDel(idx);
                lastAtomN = -1;
                refreshChrome();
            });
            row.addView(lab);
            row.addView(del);
            poseList.addView(row);
        }
        int ln = ExgNative.learnN();
        if (ln > 0) {
            poseList.addView(savedLabel(
                    "Record poses — named leftover, not ID. Delete to drop.", 0xFF8B93A0));
            for (int i = 0; i < ln; i++) {
                final int idx = i;
                LinearLayout row = new LinearLayout(this);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setPadding(0, 4, 0, 4);
                Button lab = new Button(this);
                lab.setLayoutParams(new LinearLayout.LayoutParams(0,
                        LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
                lab.setText(ExgNative.learnName(idx));
                lab.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF2A3038));
                lab.setOnClickListener(v -> {
                    ExgNative.learnSelect(idx);
                    refreshChrome();
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
        }
        applyUiScale();
    }

    private static final String LEARN_HINT = "name (tap)";

    private String nameOrEmpty(Button b) {
        CharSequence t = b.getText();
        String s = t == null ? "" : t.toString().trim();
        if (s.length() == 0 || s.startsWith("name (")) {
            return "";
        }
        return s;
    }

    private void setLearnName(String s) {
        learnName.setText(s == null || s.length() == 0 ? LEARN_HINT : s);
    }

    private void nameTake(int sec) {
        askName("Name this take (" + sec + " s)", "", s -> {
            if (s.length() == 0) {
                ExgNative.atomDiscard();
                refreshChrome();
                return;
            }
            ExgNative.setName(s);
            ExgNative.atomSave();
            lastAtomN = -1;
            refreshChrome();
        }, () -> {
            ExgNative.atomDiscard();
            refreshChrome();
        });
    }

    /* Dialog typing — extract IME is a black overlay on this handset. */
    private void askName(String title, String current, java.util.function.Consumer<String> on) {
        askName(title, current, on, null);
    }

    private void askName(String title, String current, java.util.function.Consumer<String> on,
            Runnable cancel) {
        final EditText e = new EditText(this);
        e.setText(current);
        e.setSelectAllOnFocus(true);
        e.setTextColor(0xFFE8EAF0);
        e.setHintTextColor(0xFF8B93A0);
        e.setHint("letters, digits, - _");
        e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD);
        e.setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN
                | EditorInfo.IME_ACTION_DONE);
        AlertDialog d = new AlertDialog.Builder(this)
                .setTitle(title)
                .setView(e)
                .setPositiveButton("OK", (dlg, w) -> on.accept(e.getText().toString().trim()))
                .setNegativeButton("Cancel", (dlg, w) -> {
                    if (cancel != null) {
                        cancel.run();
                    }
                })
                .create();
        d.setOnCancelListener(dlg -> {
            if (cancel != null) {
                cancel.run();
            }
        });
        if (d.getWindow() != null) {
            d.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE
                    | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN);
        }
        d.show();
        e.requestFocus();
    }

    private void pick(String title, String[] items, int selected, java.util.function.IntConsumer on) {
        if (selected < 0 || selected >= items.length) {
            selected = 0;
        }
        new AlertDialog.Builder(this)
                .setTitle(title)
                .setSingleChoiceItems(items, selected, (d, which) -> {
                    on.accept(which);
                    d.dismiss();
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void pickAlgo() {
        String[] names = {"detect", "sign", "mean", "energy", "delta", "fold", "proton"};
        pick("Cube algorithm", names, ExgNative.algo(), i -> {
            ExgNative.setAlgo(i);
            refreshChrome();
            refreshCubeChrome();
        });
    }

    private void askDest() {
        askName("API dest  host:port", ExgNative.linkDest(), s -> {
            ExgNative.setLinkDest(s);
            refreshChrome();
        });
    }

    private void pickPort() {
        if (ExgNative.linkApi()) {
            askDest();
            return;
        }
        String raw = ExgNative.ports();
        String[] items = (raw == null || raw.length() == 0) ? new String[0] : raw.split("\n");
        if (items.length == 0) {
            new AlertDialog.Builder(this)
                    .setTitle("USB")
                    .setMessage("no USB serial — switch to API to type a dest")
                    .setPositiveButton("OK", null)
                    .show();
            return;
        }
        pick("Port", items, 0, i -> {
            ExgNative.setPortI(i);
            refreshChrome();
        });
    }

    private int notchIndex() {
        int n = ExgNative.notch();
        if (n < 0) {
            return 3;
        }
        if (n == 50) {
            return 1;
        }
        if (n == 60) {
            return 2;
        }
        return 0;
    }

    private int hpIndex() {
        int h = ExgNative.hp();
        if (h == 1) {
            return 1;
        }
        if (h == 2) {
            return 2;
        }
        if (h == 5) {
            return 3;
        }
        if (h == 20) {
            return 4;
        }
        return 0;
    }

    private int lpIndex() {
        int l = ExgNative.lp();
        if (l == 20) {
            return 1;
        }
        if (l == 40) {
            return 2;
        }
        return 0;
    }

    private int scaleIndex() {
        int s = ExgNative.scaleUv();
        int[] uv = {50, 100, 200, 500, 1000, 2000, 5000};
        for (int i = 0; i < uv.length; i++) {
            if (uv[i] == s) {
                return i;
            }
        }
        return 2;
    }

    private int winIndex() {
        int w = ExgNative.windowS();
        if (w <= 1) {
            return 0;
        }
        if (w <= 2) {
            return 1;
        }
        if (w <= 4) {
            return 2;
        }
        return 3;
    }

    private void askPort(String title, int current, java.util.function.IntConsumer on) {
        final EditText e = new EditText(this);
        e.setText(current == 0 ? "" : String.valueOf(current));
        e.setSelectAllOnFocus(true);
        e.setTextColor(0xFFE8EAF0);
        e.setHintTextColor(0xFF8B93A0);
        e.setHint("port 1–65535, empty = off");
        e.setInputType(InputType.TYPE_CLASS_NUMBER);
        e.setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN
                | EditorInfo.IME_ACTION_DONE);
        AlertDialog d = new AlertDialog.Builder(this)
                .setTitle(title)
                .setView(e)
                .setPositiveButton("OK", (dlg, w) -> {
                    String s = e.getText().toString().trim();
                    if (s.length() == 0) {
                        on.accept(0);
                        return;
                    }
                    try {
                        int p = Integer.parseInt(s);
                        if (p >= 0 && p <= 65535) {
                            on.accept(p);
                        }
                    } catch (NumberFormatException ignored) {
                    }
                })
                .setNegativeButton("Cancel", null)
                .create();
        if (d.getWindow() != null) {
            d.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE
                    | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN);
        }
        d.show();
        e.requestFocus();
    }

    private int uiIndex() {
        int u = ExgNative.uiScale();
        if (u == 10) {
            return 0;
        }
        if (u == 20) {
            return 2;
        }
        return 1;
    }

    private int gainIndex(int ch) {
        int g = ExgNative.gain(ch);
        int[] gs = {1, 2, 3, 4, 6, 8, 12};
        for (int i = 0; i < gs.length; i++) {
            if (gs[i] == g) {
                return i;
            }
        }
        return 6;
    }
}

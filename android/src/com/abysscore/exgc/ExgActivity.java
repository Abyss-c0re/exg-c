package com.abysscore.exgc;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Html;
import android.text.InputType;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.SeekBar;
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
    private LinearLayout cubeList;
    private Button cubeAdd, cubeDel, cubeColor, cubeFloat;
    private Button[] cubeQ = new Button[8];
    private TextView cubeRule;
    private View algosPane;
    private LinearLayout algoList;
    private EditText algoSrc;
    private Button algoAdd, algoDel, algoReset, algoRename, algoApply, algoUseCube, algoHelpBtn;
    private TextView algoErr, algoHelp;
    private View algoHelpBox;
    private boolean algoSrcLoaded;
    private boolean algoHelpOn;
    private View settings;
    private View learnBar;
    private TextView status;
    private View pairBar;
    private TextView pairWho;
    private Button pairYes, pairNo;
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
    private Button port;
    private LinearLayout followList;
    private LinearLayout allowList;
    private Button kitSend, kitTake, kitBoth;
    private Button tabMain, tabCube, tabAlgos, tabPoses, tabSet, tabHelp;
    private Button restorePairs;
    private View helpPane;
    private View posesPane;
    private LinearLayout poseList;
    private TextView poseHint;
    private Button clean;
    private Button calibrate;
    private Button match;
    private Button notch;
    private Button hp;
    private TextView scale;
    private SeekBar scaleBar;
    private TextView win;
    private SeekBar winBar;
    private Button band;
    private Button car;
    private TextView carNote;
    private Button detrend;
    private Button env;
    private Button lp;
    private Button algo;
    private TextView uiScale;
    private SeekBar uiBar;
    private TextView cubeZoomLab;
    private SeekBar cubeZoomBar;
    private Button board;
    private Button flashOpen;
    private Button debugOpen;
    private static boolean fwAsked;
    private TextView apiLine;
    private Button apiOn, apiBind, apiHz, apiHttp, apiUdp, apiTcp, apiToken, apiPush;
    private final float[] imu = new float[9];
    private TextView profNow;
    private Button learnName;
    private LinearLayout chGrid;
    private Button negRail;
    private LinearLayout profChips;
    private LinearLayout learnChips;
    private int lastLearnN = -1;
    private int lastAtomN = -1;
    private int tab;
    private boolean running = true;
    private static final int REQ_EXPORT = 71;
    private static final int REQ_IMPORT = 72;
    private static final int REQ_CSV = 73;
    private String csvPickName = "exg.csv";
    private String holdLine;
    private long holdLineUntil;
    private volatile boolean connecting;
    private boolean resumed;

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (!running) {
                return;
            }
            ExgNative.tick();
            refreshChrome();
            if (resumed && ExgNative.connected() && ExgNative.linkPath() == 0) {
                maybeFirmwarePrompt();
            }
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
        installKnightHex(dir);
        ExgNative.start(dir != null ? dir.getAbsolutePath() : getApplicationInfo().dataDir);
        ExgNative.setSelf(Build.MODEL);
        setContentView(R.layout.activity_exg);
        applyPhoneBars();
        traces = findViewById(R.id.traces);
        fft = findViewById(R.id.fft);
        mainPane = findViewById(R.id.mainPane);
        cube = findViewById(R.id.cube);
        cubePane = findViewById(R.id.cubePane);
        cubeList = findViewById(R.id.cubeList);
        cubeAdd = findViewById(R.id.cubeAdd);
        cubeDel = findViewById(R.id.cubeDel);
        cubeColor = findViewById(R.id.cubeColor);
        cubeFloat = findViewById(R.id.cubeFloat);
        cubeRule = findViewById(R.id.cubeRule);
        algosPane = findViewById(R.id.algosPane);
        algoList = findViewById(R.id.algoList);
        algoSrc = findViewById(R.id.algoSrc);
        algoAdd = findViewById(R.id.algoAdd);
        algoDel = findViewById(R.id.algoDel);
        algoReset = findViewById(R.id.algoReset);
        algoRename = findViewById(R.id.algoRename);
        algoHelpBtn = findViewById(R.id.algoHelpBtn);
        algoHelpBox = findViewById(R.id.algoHelpBox);
        algoHelp = findViewById(R.id.algoHelp);
        algoApply = findViewById(R.id.algoApply);
        algoUseCube = findViewById(R.id.algoUseCube);
        algoErr = findViewById(R.id.algoErr);
        algoHelp.setText(
                "One rule for the cube. Save checks it. Broken source is not saved.\n"
                + "\n"
                + "A statement is ON/OFF for this cell, or chN ON/OFF for a jack.\n"
                + "IF cond THEN statements ELSE statements END\n"
                + "ELSE IF works. END can drop off a one-line if.\n"
                + "\n"
                + "if ch2 < ch5 AND ch1 > 0 then\n"
                + "  ch3 ON\n"
                + "  ch1 OFF\n"
                + "else\n"
                + "  ch3 OFF\n"
                + "end\n"
                + "\n"
                + "cond:  < > <= >= == !=   AND OR NOT   && || !\n"
                + "       NOT (ch2 < ch5)   — put compares in () after NOT\n"
                + "math:  + - * / %  abs(x)  min(a, b)  max(a, b)  sqrt(x)  pow(a, b)\n"
                + "A compare is 0 or 1, so (ch1>0)+(ch2>0) > 1 means two high.\n"
                + "\n"
                + "FOR seconds     body stays on for that many seconds\n"
                + "                (not CubalC FOR i = a TO b — that is a count loop)\n"
                + "UNTIL cond      body stays on until the cond is true\n"
                + "                (one pass per tick, not a busy loop)\n"
                + "  if ch1 > 0 then\n"
                + "    FOR 2\n"
                + "      ch3 ON\n"
                + "    END\n"
                + "  end\n"
                + "  UNTIL ch4 < ch2\n"
                + "    ch3 ON\n"
                + "  END\n"
                + "\n"
                + "this cell:  ch last mean rms prev dxmean above pos signal n\n"
                + "jacks:      ch1..ch8  last1 mean1 rms1 prev1 …\n"
                + "LET name = expr     # or LET name, expr  or  LET name expr\n"
                + "OUT name            # or OUT sensor name  ON/OFF — lattice SoT\n"
                + "\n"
                + "detect  if signal == ON then ON\n"
                + "sign    if ch > 0 then ON\n"
                + "compare if ch2 < ch5 then ch3 ON");
        cubeQ[0] = findViewById(R.id.cubeQ1);
        cubeQ[1] = findViewById(R.id.cubeQ2);
        cubeQ[2] = findViewById(R.id.cubeQ3);
        cubeQ[3] = findViewById(R.id.cubeQ4);
        cubeQ[4] = findViewById(R.id.cubeQ5);
        cubeQ[5] = findViewById(R.id.cubeQ6);
        cubeQ[6] = findViewById(R.id.cubeQ7);
        cubeQ[7] = findViewById(R.id.cubeQ8);
        settings = findViewById(R.id.settings);
        learnBar = findViewById(R.id.learnBar);
        status = findViewById(R.id.status);
        pairBar = findViewById(R.id.pairBar);
        pairWho = findViewById(R.id.pairWho);
        pairYes = findViewById(R.id.pairYes);
        pairNo = findViewById(R.id.pairNo);
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
        port = findViewById(R.id.port);
        followList = findViewById(R.id.followList);
        allowList = findViewById(R.id.allowList);
        kitSend = findViewById(R.id.kitSend);
        kitTake = findViewById(R.id.kitTake);
        kitBoth = findViewById(R.id.kitBoth);
        tabMain = findViewById(R.id.tabMain);
        tabCube = findViewById(R.id.tabCube);
        tabAlgos = findViewById(R.id.tabAlgos);
        tabPoses = findViewById(R.id.tabPoses);
        tabSet = findViewById(R.id.tabSet);
        tabHelp = findViewById(R.id.tabHelp);
        helpPane = findViewById(R.id.helpPane);
        restorePairs = findViewById(R.id.restorePairs);
        ((TextView) findViewById(R.id.helpBody)).setText(helpHtml());
        posesPane = findViewById(R.id.poses);
        poseList = findViewById(R.id.poseList);
        poseHint = findViewById(R.id.poseHint);
        clean = findViewById(R.id.clean);
        calibrate = findViewById(R.id.calibrate);
        match = findViewById(R.id.match);
        notch = findViewById(R.id.notch);
        hp = findViewById(R.id.hp);
        scale = findViewById(R.id.scale);
        scaleBar = findViewById(R.id.scaleBar);
        win = findViewById(R.id.win);
        winBar = findViewById(R.id.winBar);
        band = findViewById(R.id.band);
        car = findViewById(R.id.car);
        carNote = findViewById(R.id.carNote);
        detrend = findViewById(R.id.detrend);
        env = findViewById(R.id.env);
        lp = findViewById(R.id.lp);
        algo = findViewById(R.id.algo);
        uiScale = findViewById(R.id.uiScale);
        uiBar = findViewById(R.id.uiBar);
        cubeZoomLab = findViewById(R.id.cubeZoomLab);
        cubeZoomBar = findViewById(R.id.cubeZoomBar);
        board = findViewById(R.id.board);
        flashOpen = findViewById(R.id.flashOpen);
        debugOpen = findViewById(R.id.debugOpen);
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
        negRail = findViewById(R.id.negRail);
        profChips = findViewById(R.id.profChips);
        negRail.setOnClickListener(v -> {
            ExgNative.setNegRail(!ExgNative.negRail());
            refreshChannels();
            refreshChrome();
        });
        restorePairs.setOnClickListener(v -> confirmRestore());
        learnChips = findViewById(R.id.learnChips);

        pairYes.setOnClickListener(v -> {
            ExgNative.pairAccept();
            refreshChrome();
        });
        pairNo.setOnClickListener(v -> {
            ExgNative.pairReject();
            refreshChrome();
        });
        connect.setOnClickListener(v -> {
            if (ExgNative.connected() || connecting) {
                connecting = false;
                ExgNative.disconnect();
                StreamService.ensure(this, ExgNative.apiOn() || ExgNative.connected());
                refreshChrome();
                return;
            }
            if (ExgNative.linkPath() == 1) {
                String d = ExgNative.linkDest();
                if (d == null || d.length() < 1 || d.startsWith("bt:")) {
                    askDest();
                    return;
                }
                connectLan();
                return;
            }
            ExgNative.connect();
            StreamService.ensure(this, ExgNative.apiOn() || ExgNative.connected());
            refreshChrome();
        });
        link.setOnClickListener(v -> {
            ExgNative.cycleLink();
            refreshChrome();
        });
        kitSend.setOnClickListener(v -> {
            LanShare.sendKit();
            status.setText("sent map & settings");
        });
        kitTake.setOnClickListener(v -> {
            LanShare.takeKit();
            status.setText("asked for their map & settings");
        });
        kitBoth.setOnClickListener(v -> {
            LanShare.copyBoth();
            status.setText("copying map & settings both ways");
        });

        tabMain.setOnClickListener(v -> showTab(0));
        tabCube.setOnClickListener(v -> showTab(1));
        tabAlgos.setOnClickListener(v -> showTab(2));
        tabPoses.setOnClickListener(v -> showTab(3));
        tabSet.setOnClickListener(v -> showTab(4));
        tabHelp.setOnClickListener(v -> showTab(5));
        cubeAdd.setOnClickListener(v -> {
            ExgNative.madeAdd();
            refreshCubeChrome();
            refreshChrome();
        });
        cubeDel.setOnClickListener(v -> {
            int s = ExgNative.madeSel();
            ExgNative.madeDel(s);
            refreshCubeChrome();
            refreshChrome();
        });
        cubeColor.setOnClickListener(v -> {
            int s = ExgNative.madeSel();
            if (ExgNative.madeN() < 1) {
                return;
            }
            ColorPick.show(this, "cube " + (s + 1) + " color",
                    ExgNative.madeRgb(s), rgb -> {
                        ExgNative.madeSetRgb(s, rgb);
                        refreshCubeChrome();
                    });
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
        algoAdd.setOnClickListener(v -> {
            ExgNative.alibAdd();
            algoSrcLoaded = false;
            refreshAlgos();
            refreshChrome();
        });
        algoDel.setOnClickListener(v -> {
            ExgNative.alibDel(ExgNative.alibSel());
            algoSrcLoaded = false;
            refreshAlgos();
            refreshChrome();
        });
        algoReset.setOnClickListener(v -> {
            ExgNative.alibReset(ExgNative.alibSel());
            algoSrcLoaded = false;
            refreshAlgos();
            refreshChrome();
        });
        algoRename.setOnClickListener(v -> {
            int i = ExgNative.alibSel();
            if (ExgNative.alibDef(i)) {
                algoErr.setVisibility(View.VISIBLE);
                algoErr.setText("default names stay");
                return;
            }
            askName("Algo name", ExgNative.alibName(i), s -> {
                ExgNative.alibSetName(i, s);
                refreshAlgos();
            });
        });
        algoHelpBtn.setOnClickListener(v -> {
            algoHelpOn = !algoHelpOn;
            algoHelpBox.setVisibility(algoHelpOn ? View.VISIBLE : View.GONE);
            algoHelpBtn.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    algoHelpOn ? 0xFF3A3020 : 0xFF2A3038));
        });
        algoApply.setOnClickListener(v -> {
            String src = algoSrc.getText() != null ? algoSrc.getText().toString() : "";
            String err = ExgNative.alibCheck(src);
            if (err != null && err.length() > 0) {
                algoErr.setVisibility(View.VISIBLE);
                algoErr.setTextColor(0xFFF22647);
                algoErr.setText("not saved — " + err);
                return;
            }
            err = ExgNative.alibSetSrc(ExgNative.alibSel(), src);
            if (err != null && err.length() > 0) {
                algoErr.setVisibility(View.VISIBLE);
                algoErr.setTextColor(0xFFF22647);
                algoErr.setText("not saved — " + err);
                return;
            }
            algoErr.setVisibility(View.VISIBLE);
            algoErr.setTextColor(0xFF3CB46E);
            algoErr.setText("saved — syntax ok");
            algoSrcLoaded = true;
            refreshAlgos();
            refreshChrome();
        });
        algoUseCube.setOnClickListener(v -> {
            if (ExgNative.madeN() < 1) {
                algoErr.setVisibility(View.VISIBLE);
                algoErr.setTextColor(0xFFF22647);
                algoErr.setText("add a cube first");
                return;
            }
            ExgNative.madeSetAlgoAll(ExgNative.madeSel(), ExgNative.alibSel());
            algoErr.setVisibility(View.VISIBLE);
            algoErr.setTextColor(0xFF3CB46E);
            algoErr.setText("cube uses " + ExgNative.alibName(ExgNative.alibSel()));
            refreshCubeChrome();
            refreshChrome();
        });
        for (int qi = 0; qi < 8; qi++) {
            final int q = qi;
            cubeQ[qi].setOnClickListener(v -> pickQuarterAlgo(q));
            cubeQ[qi].setOnLongClickListener(v -> {
                pickQuarterCh(q);
                return true;
            });
        }
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
            if (ExgNative.csvOn()) {
                ExgNative.toggleCsv();
                refreshChrome();
                Intent it = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                it.addCategory(Intent.CATEGORY_OPENABLE);
                it.setType("text/csv");
                it.putExtra(Intent.EXTRA_TITLE, csvPickName);
                startActivityForResult(it, REQ_CSV);
                return;
            }
            if (!ExgNative.connected()) {
                status.setText("connect before record");
                return;
            }
            csvPickName = "knight-" + new java.text.SimpleDateFormat(
                    "yyyyMMdd-HHmmss", java.util.Locale.US)
                    .format(new java.util.Date()) + ".csv";
            java.io.File local = new java.io.File(getFilesDir(), csvPickName);
            if (ExgNative.csvBegin(local.getAbsolutePath()) != 0) {
                status.setText("cannot write CSV");
                return;
            }
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
        ownDrag(scaleBar);
        scaleBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar s, int p, boolean fromUser) {
                if (!fromUser) {
                    return;
                }
                int uv = uvFromProg(p);
                ExgNative.setScaleUv(uv);
                scale.setText(uvText(uv));
            }

            @Override
            public void onStartTrackingTouch(SeekBar s) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar s) {
                refreshChrome();
            }
        });
        ownDrag(winBar);
        winBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar s, int p, boolean fromUser) {
                if (!fromUser) {
                    return;
                }
                int sec = p + 1;
                ExgNative.setWindowS(sec);
                win.setText(sec + " s");
            }

            @Override
            public void onStartTrackingTouch(SeekBar s) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar s) {
                refreshChrome();
            }
        });
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
        algo.setOnClickListener(v -> showTab(2));
        ownDrag(uiBar);
        uiBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar s, int p, boolean fromUser) {
                if (!fromUser) {
                    return;
                }
                int tenths = 8 + p;
                ExgNative.setUiScale(tenths);
                uiScale.setText(uiText(tenths));
                applyUiScale();
            }

            @Override
            public void onStartTrackingTouch(SeekBar s) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar s) {
                refreshChrome();
            }
        });
        ownDrag(cubeZoomBar);
        cubeZoomBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar s, int p, boolean fromUser) {
                if (!fromUser) {
                    return;
                }
                float z = 0.70f + p * 0.10f;
                ExgNative.setCubeZoom(z);
                cube.setZoom(z);
                cubeZoomLab.setText(zoomText(z));
            }

            @Override
            public void onStartTrackingTouch(SeekBar s) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar s) {
                refreshChrome();
            }
        });
        board.setOnClickListener(v -> pick("Board",
                new String[] {"Auto", "8-ch + IMU", "8-ch EXG"},
                boardPickIndex(), i -> {
                    ExgNative.setBoardMode(i == 0 ? 2 : (i == 1 ? 1 : 0));
                    refreshChrome();
                }));
        flashOpen.setOnClickListener(v -> startActivity(new Intent(this, FlashActivity.class)));
        debugOpen.setOnClickListener(v -> startActivity(new Intent(this, DebugActivity.class)));

        apiOn.setOnClickListener(v -> {
            boolean on = !ExgNative.apiOn();
            ExgNative.setApiOn(on);
            if (on) {
                ensureNotify();
            }
            StreamService.ensure(this, ExgNative.apiOn() || ExgNative.connected());
            refreshChrome();
        });
        apiBind.setOnClickListener(v -> {
            ExgNative.setApiLan(!ExgNative.apiLan());
            refreshChrome();
        });
        apiHz.setOnClickListener(v -> askPort("Share rate 1–500. 125, 200, 250, and 500 follow the board.", ExgNative.apiHz(), p -> {
            ExgNative.setApiHz(p < 1 ? 1 : (p > 500 ? 500 : p));
            refreshChrome();
        }));
        apiHttp.setOnClickListener(v -> askPort("Settings port (shared after Allow). 0 = off", ExgNative.apiHttp(), p -> {
            ExgNative.setApiHttp(p);
            refreshChrome();
        }));
        apiUdp.setOnClickListener(v -> askPort("EXG port — live traces. Default is settings+1. 0 = off", ExgNative.apiUdp(), p -> {
            ExgNative.setApiUdp(p);
            refreshChrome();
        }));
        apiTcp.setOnClickListener(v -> askPort("Spare port — not needed to follow. 0 = off", ExgNative.apiTcp(), p -> {
            ExgNative.setApiTcp(p);
            refreshChrome();
        }));
        apiToken.setOnClickListener(v -> askName("Lock word (empty = off)",
                ExgNative.apiToken(), s -> {
                    ExgNative.setApiToken(s);
                    refreshChrome();
                }));
        apiPush.setOnClickListener(v -> askName("Extra EXG send  name:port  (empty = off)",
                ExgNative.apiPush(), s -> {
                    ExgNative.setApiPush(s);
                    refreshChrome();
                }));
        port.setOnClickListener(v -> {
            if (ExgNative.linkPath() == 1) {
                askDest();
            } else {
                pickPort();
            }
        });
        buildChannels();
        wireHints();
        refreshCubeChrome();
        refreshProfiles();
        showTab(0);
        lastLearnN = -1;
        refreshLearnChips();
        applyUiScale();
        refreshChrome();
        h.post(tick);
        takeFollowIntent(getIntent());
        h.postDelayed(() -> {
            if (!ExgNative.connected()) {
                if (ExgNative.linkPath() != 0) {
                    return;
                }
                ExgNative.connect();
                refreshChrome();
            }
        }, 400);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        try {
            StreamService.ensure(this, ExgNative.apiOn() || ExgNative.connected());
        } catch (RuntimeException ignored) {
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        takeFollowIntent(intent);
    }

    private void takeFollowIntent(Intent it) {
        if (it == null) {
            return;
        }
        String dest = it.getStringExtra("followdest");
        if (dest != null && dest.length() > 0 && !dest.startsWith("bt:")) {
            ExgNative.setLinkDest(dest);
            ExgNative.setLinkPath(1);
            connectLan();
        }
    }

    private void connectLan() {
        if (connecting || ExgNative.connected()) {
            return;
        }
        connecting = true;
        holdLine = "waiting for Allow on the share…";
        holdLineUntil = android.os.SystemClock.uptimeMillis() + 65000;
        refreshChrome();
        StreamService.ensure(this, true);
        new Thread(() -> {
            ExgNative.connect();
            h.post(() -> {
                if (!connecting && ExgNative.connected()) {
                    ExgNative.disconnect();
                }
                connecting = false;
                holdLine = null;
                StreamService.ensure(ExgActivity.this,
                        ExgNative.apiOn() || ExgNative.connected());
                refreshChrome();
            });
        }, "exg-lan").start();
    }

    private void askDest() {
        String cur = ExgNative.linkDest();
        if (cur != null && cur.startsWith("bt:")) {
            cur = "";
        }
        askName("EXG on LAN", cur == null ? "" : cur, "host or host:8765", s -> {
            if (s.length() < 1) {
                return;
            }
            ExgNative.setLinkDest(s);
            ExgNative.setLinkPath(1);
            connectLan();
        });
    }

    @Override
    protected void onPause() {
        resumed = false;
        super.onPause();
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

    private boolean hold(String line) {
        status.setText(line);
        return true;
    }

    private void wireHints() {
        hint(port, "Knight port. On USB this is the serial device. On LAN this is host:port.");
        hint(link, "USB talks to a cable. LAN talks to another exg-c that is sharing.");
        hint(connect, "Open or close the Knight. LAN asks for a destination first.");
        hint(pairYes, "Allow this follower to receive the live EXG.");
        hint(pairNo, "Refuse this follower.");
        hint(calibrate, "Capture the desk noise plate, then a still worn plate. ID uses both.");
        hint(clean, "DC on the trace. Cycles the cleaner. The plates stay.");
        hint(learnName, "Name used by Record and by Take.");
        hint(record, "Save one second of the current pattern for blink or clench ID.");
        hint(match, "ID on compares the live second with saved patterns and names one winner.");
        hint(csv, "Start or stop a raw CSV file of the live channels.");
        hint(pause, "Hold the plot. The board keeps running.");
        hint(atom, "Start or stop a take. A take is the longer recording used on the Takes tab.");
        hint(atomVs, "Live take compared with the one you picked. Different montage refuses the score.");
        hint(band, "Preset. raw is the board. line-kill, EEG, and EMG set filters. EEG and EMG want CAR only when NEG RAIL is off.");
        hint(car, "Common average reference. Subtracts the mean of the channels. With NEG RAIL the mean of pair voltages is not a reference, so CAR stays off.");
        hint(detrend, "Pull a slow drift out of the plot. raw DC leaves the offset in view.");
        hint(env, "Envelope rectifies the wave. Useful for muscle. Wave keeps the signed trace.");
        hint(notch, "Mains notch, 50 or 60 Hz, or off. AUTO follows the noise plate.");
        hint(hp, "High-pass. Removes what is slower than this frequency.");
        hint(lp, "Low-pass. Removes what is faster than this frequency.");
        hint(algo, "Opens the Algos tab. CubalC decides which cube cells light.");
        hint(board, "Auto locks a 21-byte EEG frame or a 57-byte IMU frame. The other two force one length.");
        hint(flashOpen, "Write knight.hex and the boot mode. Electrodes off. Two taps.");
        hint(debugOpen, "Boot text and host commands. Sample bytes stay off this page.");
        hint(negRail, "NEG RAIL turns bias off on every channel. Each sample is the + electrode minus the − electrode. bias RLD restores per-channel bias and reads each channel as one site.");
        hint(restorePairs, "Put back FC3-CP3, FC1-CP1, FCz-CPz, FC2-CP2, FC4-CP4, PO3-O1, POz-Oz, PO4-O2. NEG RAIL stays as it is.");
        hint(apiOn, "Share EXG on the network so another device can follow.");
        hint(apiBind, "wifi listens on the LAN address. this device listens only here.");
        hint(apiHz, "How often shared frames go out. 125, 200, 250, and 500 follow the locked board rate. Any other value from 1 to 500 stays as a cap.");
        hint(apiHttp, "Port for settings and control, after Allow.");
        hint(apiUdp, "Port for the live traces.");
        hint(apiTcp, "Spare port. Following does not need it.");
        hint(apiToken, "Lock word. Empty leaves the share open after Allow.");
        hint(apiPush, "Also send live EXG to name:port.");
        hint(kitSend, "Send this map and these settings to the follower.");
        hint(kitTake, "Ask the follower for their map and settings.");
        hint(kitBoth, "Copy the map and settings both ways.");
        hint(cubeAdd, "Add a 2×2×2 cube. Each cell is one channel bit.");
        hint(cubeDel, "Remove the selected cube.");
        hint(cubeColor, "Color of the selected cube.");
        hint(cubeFloat, "Let the cube turn on its own. Off keeps the angle where you leave it.");
        hint(findViewById(R.id.cubeFront), "Face the cube to the front.");
        hint(findViewById(R.id.cubeZoomIn), "Zoom the cube in.");
        hint(findViewById(R.id.cubeZoomOut), "Zoom the cube out.");
        hint(tabMain, "Traces, FFT, and the record bar.");
        hint(tabCube, "Hive of channel bits, and the scalp pairs under it.");
        hint(tabAlgos, "CubalC source for the cube. Help on that tab is the syntax.");
        hint(tabPoses, "Saved takes. Tap one to compare it with the live plot.");
        hint(tabSet, "Filters, share, profiles, and the electrode map.");
        hint(tabHelp, "What each button does.");
        hint(findViewById(R.id.profNew), "Save the current filters and view as a named profile. The electrode map stays with the app.");
        hint(findViewById(R.id.profExport), "Share the profile file.");
        hint(findViewById(R.id.profImport), "Open a profile file.");
    }

    private void hint(View v, String line) {
        if (v == null) {
            return;
        }
        v.setOnLongClickListener(view -> hold(line));
    }

    private void confirmRestore() {
        boolean rail = ExgNative.negRail();
        String keep = rail
                ? "NEG RAIL stays on. Bias stays off and CAR stays off."
                : "bias RLD stays. The − sites are stored until you turn NEG RAIL on.";
        new AlertDialog.Builder(this)
                .setTitle("Restore default pairs")
                .setMessage("Puts back FC3–CP3, FC1–CP1, FCz–CPz, FC2–CP2, FC4–CP4, PO3–O1, POz–Oz, and PO4–O2.\n\n"
                        + keep)
                .setPositiveButton("Restore", (d, w) -> {
                    ExgNative.montageDefault();
                    refreshChannels();
                    refreshChrome();
                    status.setText(ExgNative.status());
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private static String helpSec(String title) {
        return "<br/><font color=\"#E7C27A\"><b>" + title + "</b></font><br/>";
    }

    private static String helpRow(String name, String body) {
        return "<b>" + name + "</b> — " + body + "<br/>";
    }

    private static CharSequence helpHtml() {
        String html = helpSec("CAR, rail, restore")
                + helpRow("CAR", "Common average reference. Subtracts the mean of the active channels. That mean is a reference while each channel is one site against bias (bias RLD). In NEG RAIL each sample is already the + electrode minus the − electrode, so the mean of those pairs is left alone. The button then reads CAR off (rail) and stays off. EEG and EMG presets turn CAR on only while NEG RAIL is off.")
                + helpRow("NEG RAIL", "Bias off on every channel. The sample is the voltage from the + electrode to the − electrode. The bias button on each row becomes the − site picker. A shared millivolt floor means the body is floating; the status says so and leaves CAR off.")
                + helpRow("bias RLD", "Per-channel bias. The sample is read as the + site. Minus sites stay stored for the next time NEG RAIL is on. Bias is a separate contact from the pair.")
                + helpRow("Restore default pairs", "Puts back FC3–CP3, FC1–CP1, FCz–CPz, FC2–CP2, FC4–CP4, PO3–O1, POz–Oz, PO4–O2. Five pairs cross motor cortex (front +, back −). Three enter visual cortex. Leaves NEG RAIL where you set it. While NEG RAIL is on, bias and CAR stay off. While bias RLD is on, bias and CAR stay as they are.")
                + helpSec("Connection")
                + helpRow("Port", "Knight serial port on USB, or host:port on LAN.")
                + helpRow("USB / LAN", "Where Connect opens.")
                + helpRow("Connect", "Open or close the board.")
                + helpRow("Allow / No", "A follower asked for the live EXG.")
                + helpSec("Main")
                + helpRow("Calibrate", "Desk noise plate, then a still worn plate. ID needs both.")
                + helpRow("DC / CLEAN", "How DC sits on the trace. Cycles the cleaner. The plates stay.")
                + helpRow("Name", "Label for Record and Take.")
                + helpRow("Record", "One second, for blink or clench.")
                + helpRow("ID / MATCH", "Names the live second when one saved pattern wins. With saved takes the button reads ID. With none it reads MATCH.")
                + helpRow("CSV", "Raw file of the live channels. Stop CSV ends it.")
                + helpRow("Pause", "Holds the plot. The board keeps running.")
                + helpRow("Take", "Longer recording. Compare it on Takes. Stop ends the take.")
                + helpRow("Compare line", "Scores the live take against the one you picked. A different montage (NEG RAIL versus bias RLD) refuses the score.")
                + helpSec("Filters")
                + helpRow("Band", "raw, line-kill, EEG, or EMG. raw is the board. line-kill, EEG, and EMG set filters. EEG and EMG want CAR only while NEG RAIL is off. band mix means the knobs left the preset.")
                + helpRow("CAR", "See the top of this page. CAR on subtracts the mean. CAR off leaves each channel as it is. CAR off (rail) is locked while NEG RAIL is on.")
                + helpRow("detrend / raw DC", "Pulls a slow drift out of the plot, or leaves the offset in view.")
                + helpRow("envelope / wave", "Rectified muscle view, or the signed trace.")
                + helpRow("notch", "Mains notch, 50 or 60 Hz, off, or AUTO from the noise plate.")
                + helpRow("hp / lp", "High-pass removes what is slower than this frequency. Low-pass removes what is faster.")
                + helpRow("Algos tab", "Opens Algos. CubalC decides which cube cells light.")
                + helpSec("Electrodes")
                + helpRow("Name button", "Pick the + site. With NEG RAIL the label shows +−.")
                + helpRow("color", "Channel color on the plot and on the cube.")
                + helpRow("ON / off", "ON acquires this channel. off drops it from the plot, CAR, and the cube.")
                + helpRow("bias ON / − site", "Bias for that channel, or the − site while NEG RAIL is on.")
                + helpRow("gN", "Amplifier gain. Higher gain makes a smaller signal fill the plot.")
                + helpSec("Cube")
                + helpRow("add cube / del / color", "The 2×2×2 hive. Each cell is one channel bit.")
                + helpRow("− / + / front / float", "Zoom, face the front, or let the cube turn.")
                + helpRow("1–8", "Which channel feeds that cell. The scalp under the hive draws each + site to its − site.")
                + helpSec("Algos")
                + helpRow("help", "CubalC syntax, on the Algos tab. This Help tab is the buttons.")
                + helpRow("save CubalC", "Keeps source that parses.")
                + helpRow("use on cube", "Applies the selected algo to the cube.")
                + helpSec("Takes")
                + helpRow("A take", "Tap one to compare with the live plot. Long-press deletes. A take saved in the other montage is marked different montage.")
                + helpSec("Share")
                + helpRow("share EXG", "Publish on the network so another device can follow.")
                + helpRow("wifi / this device", "Who can connect.")
                + helpRow("rate", "Shared frames per second, 1 to 500. 125, 200, 250, and 500 follow the locked board rate.")
                + helpRow("Flash", "Writes knight.hex and the mode byte. Electrodes off. The second tap within 8 seconds starts it.")
                + helpRow("Debug", "Boot lines and host commands. The sample stream is not printed.")
                + helpRow("settings / EXG / spare", "Ports. 0 is off.")
                + helpRow("lock", "A word required after Allow. Empty leaves the share open after Allow.")
                + helpRow("extra send", "Also push live EXG to name:port.")
                + helpRow("send mine / take theirs / both ways", "Copy the map and settings.")
                + helpRow("Profiles", "Filters and view. The electrode map stays with the app.")
                + helpSec("Tabs")
                + helpRow("Main", "Traces, FFT, and the record bar.")
                + helpRow("Cube", "Hive of channel bits, and the scalp pairs under it.")
                + helpRow("Algos", "CubalC source for the cube.")
                + helpRow("Takes", "Saved takes.")
                + helpRow("Settings", "Filters, share, profiles, and the electrode map.")
                + helpRow("Help", "This page.");
        return Html.fromHtml(html, Html.FROM_HTML_MODE_COMPACT);
    }

    private void showTab(int t) {
        tab = t;
        mainPane.setVisibility(t == 0 ? View.VISIBLE : View.GONE);
        cubePane.setVisibility(t == 1 ? View.VISIBLE : View.GONE);
        algosPane.setVisibility(t == 2 ? View.VISIBLE : View.GONE);
        posesPane.setVisibility(t == 3 ? View.VISIBLE : View.GONE);
        settings.setVisibility(t == 4 ? View.VISIBLE : View.GONE);
        helpPane.setVisibility(t == 5 ? View.VISIBLE : View.GONE);
        learnBar.setVisibility(t == 0 ? View.VISIBLE : View.GONE);
        tabMain.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 0 ? 0xFF24322C : 0xFF2A3038));
        tabCube.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 1 ? 0xFF3A1820 : 0xFF2A3038));
        tabAlgos.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 2 ? 0xFF3A3020 : 0xFF2A3038));
        tabPoses.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 3 ? 0xFF3A3020 : 0xFF2A3038));
        tabSet.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 4 ? 0xFF243044 : 0xFF2A3038));
        tabHelp.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 5 ? 0xFF243044 : 0xFF2A3038));
        if (t == 1) {
            refreshCubeChrome();
        }
        if (t == 2) {
            algoSrcLoaded = false;
            refreshAlgos();
        }
        if (t == 3) {
            lastLearnN = -1;
            lastAtomN = -1;
            rebuildSavedList();
        }
        if (t == 4) {
            refreshChannels();
            refreshProfiles();
        }
    }

    private String[] algoNames() {
        int an = ExgNative.alibN();
        String[] names = new String[Math.max(0, an)];
        for (int i = 0; i < an; i++) {
            names[i] = ExgNative.alibName(i);
        }
        return names;
    }

    private void pickCubeAlgo() {
        if (ExgNative.madeN() < 1) {
            return;
        }
        int sel = ExgNative.madeSel();
        String[] names = algoNames();
        if (names.length < 1) {
            return;
        }
        int cur = ExgNative.madeAlgoAll(sel);
        if (cur < 0) {
            cur = ExgNative.alibSel();
        }
        pick("cube algo — all 8 bits", names, cur, i -> {
            ExgNative.madeSetAlgoAll(sel, i);
            refreshCubeChrome();
            refreshChrome();
        });
    }

    private void pickQuarterAlgo(int q) {
        int n = ExgNative.madeN();
        if (n < 1) {
            return;
        }
        int sel = ExgNative.madeSel();
        ExgNative.madeSetQSel(q);
        String[] lib = algoNames();
        if (lib.length < 1) {
            return;
        }
        int all = ExgNative.madeAlgoAll(sel);
        String cubeNm = all < 0 ? "cube" : ExgNative.alibName(all);
        String[] names = new String[lib.length + 1];
        names[0] = "same as cube (" + (cubeNm != null ? cubeNm : "?") + ")";
        System.arraycopy(lib, 0, names, 1, lib.length);
        int own = ExgNative.madeAlgoOwn(sel, q);
        int cur = own < 0 ? 0 : own + 1;
        pick("channel " + (q + 1) + " algo", names, cur, i -> {
            ExgNative.madeSetAlgo(sel, q, i == 0 ? -1 : i - 1);
            refreshCubeChrome();
            refreshChrome();
        });
    }

    private void pickQuarterCh(int q) {
        int n = ExgNative.madeN();
        if (n < 1) {
            return;
        }
        int sel = ExgNative.madeSel();
        ExgNative.madeSetQSel(q);
        int cur = ExgNative.madeCh(sel, q);
        String[] names = new String[9];
        names[0] = "empty";
        for (int c = 1; c <= 8; c++) {
            names[c] = "ch" + c + "  " + ExgNative.elecName(c - 1);
        }
        pick("bit " + (q + 1) + " channel", names, cur, i -> {
            ExgNative.madeSetCh(sel, q, i);
            refreshCubeChrome();
            refreshChrome();
        });
    }

    private void refreshCubeChrome() {
        boolean fl = ExgNative.cubeFloat();
        cubeFloat.setText(fl ? "float on" : "float off");
        cubeFloat.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                fl ? 0xFF2E8A58 : 0xFF2A3038));
        int n = ExgNative.madeN();
        int sel = ExgNative.madeSel();
        int max = ExgNative.madeMax();
        cubeAdd.setEnabled(n < max);
        cubeDel.setEnabled(n > 0);
        cubeColor.setEnabled(n > 0);
        cubeList.removeAllViews();
        for (int i = 0; i < n; i++) {
            final int ix = i;
            Button b = new Button(this);
            int all = ExgNative.madeAlgoAll(i);
            String an = all < 0 ? "mixed" : ExgNative.alibName(all);
            b.setText("cube " + (i + 1) + " · " + (an != null ? an : "?"));
            int rgb = ExgNative.madeRgb(i);
            b.setTextColor(0xFF000000 | rgb);
            b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    i == sel ? 0xFF5A1020 : 0xFF2A3038));
            b.setOnClickListener(v -> {
                ExgNative.madeSetSel(ix);
                refreshCubeChrome();
                pickCubeAlgo();
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            cubeList.addView(b, lp);
        }
        refreshCubeBits();
        applyUiScale();
    }

    private void refreshAlgos() {
        if (algoList == null) {
            return;
        }
        int n = ExgNative.alibN();
        int sel = ExgNative.alibSel();
        algoList.removeAllViews();
        for (int i = 0; i < n; i++) {
            final int ix = i;
            Button b = new Button(this);
            b.setText(ExgNative.alibName(i));
            b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    i == sel ? 0xFF5A1020 : 0xFF2A3038));
            b.setOnClickListener(v -> {
                ExgNative.alibSetSel(ix);
                algoSrcLoaded = false;
                algoErr.setVisibility(View.GONE);
                refreshAlgos();
                refreshChrome();
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            algoList.addView(b, lp);
        }
        algoDel.setEnabled(n > 8 && !ExgNative.alibDef(sel));
        algoReset.setEnabled(ExgNative.alibDef(sel));
        algoRename.setEnabled(!ExgNative.alibDef(sel));
        if (!algoSrcLoaded) {
            String src = ExgNative.alibSrc(sel);
            algoSrc.setText(src != null ? src : "");
            algoSrcLoaded = true;
        }
        applyUiScale();
    }

    private void refreshCubeBits() {
        if (cubeQ[0] == null || cubeRule == null) {
            return;
        }
        int n = ExgNative.madeN();
        int sel = ExgNative.madeSel();
        int qsel = ExgNative.madeQSel();
        if (n < 1) {
            cubeRule.setText("add a cube. tap its name for the cube algo. tap a channel to override.");
        } else {
            int all = ExgNative.madeAlgoAll(sel);
            if (all < 0) {
                cubeRule.setText("mixed — tap a channel to set its algo (default = cube). hold to pick jack.");
            } else {
                String an = ExgNative.alibName(all);
                cubeRule.setText((an != null ? an : "algo")
                        + " on the cube. tap a channel to override. hold to pick jack.");
            }
        }
        for (int q = 0; q < 8; q++) {
            cubeQ[q].setEnabled(n > 0);
            if (n < 1) {
                cubeQ[q].setText((q + 1) + " —");
                cubeQ[q].setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF2A3038));
                continue;
            }
            int ch = ExgNative.madeCh(sel, q);
            int own = ExgNative.madeAlgoOwn(sel, q);
            String lab;
            if (ch < 1) {
                lab = (q + 1) + " —";
            } else if (own < 0) {
                lab = (q + 1) + " ch" + ch;
            } else {
                String an = ExgNative.alibName(own);
                lab = (q + 1) + " ch" + ch + " " + (an != null ? an : "?");
            }
            cubeQ[q].setText(lab);
            cubeQ[q].setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    q == qsel ? 0xFF3A4050 : 0xFF2A3038));
        }
    }

    private void refreshChrome() {
        boolean on = ExgNative.connected();
        int path = ExgNative.linkPath();
        connect.setText(on ? "Disconnect" : (connecting ? "…" : "Connect"));
        connect.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                on || connecting ? 0xFF8A3038 : 0xFF2E8A58));
        if (path == 1) {
            link.setText("LAN");
        } else {
            link.setText("USB");
        }
        link.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                path == 0 ? 0xFF2A3038 : 0xFF2E6A8A));
        String ports = ExgNative.ports();
        if (path == 1) {
            String d = ExgNative.linkDest();
            port.setText(d == null || d.length() == 0 || d.startsWith("bt:")
                    ? "type dest…" : "EXG on LAN");
        } else {
            port.setText(ports == null || ports.length() == 0 ? "no Knight" : ports.split("\n")[0]);
        }
        {
            int pst = ExgNative.pairState();
            if (pst == 1) {
                pairBar.setVisibility(View.VISIBLE);
                String who = ExgNative.pairName();
                if (who == null || who.length() < 1) {
                    who = "Someone";
                }
                pairWho.setText(who.replace('_', ' ') + " wants EXG");
            } else {
                pairBar.setVisibility(View.GONE);
            }
        }
        String st = ExgNative.status();
        float sps = ExgNative.sps();
        int fr = ExgNative.frames();
        if (on && sps > 1f) {
            st = st + "   " + (int) sps + " sps   " + fr + " frames";
            int drop = ExgNative.drops();
            if (drop > 0) {
                st = st + "   drop " + drop;
            }
        }
        if (ExgNative.apiOn()) {
            st = st + "   " + ExgNative.apiLine();
        }
        boolean hold = holdLine != null
                && android.os.SystemClock.uptimeMillis() < holdLineUntil
                && !on;
        if (on) {
            holdLine = null;
        }
        if (hold) {
            status.setText(holdLine);
            status.setTextColor(0xFFF0A040);
        } else {
            status.setText(st);
            status.setTextColor(ExgNative.statusOk() ? 0xFF3CB46E : 0xFFF0A040);
        }
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
            if (on && ExgNative.streamCold()) {
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
        if (nh < 0) {
            int eff = ExgNative.notchEff();
            notch.setText(eff > 0 ? ("notch AUTO " + eff) : "notch AUTO idle");
        } else {
            notch.setText(nh == 0 ? "notch off" : "notch " + nh);
        }
        hp.setText(ExgNative.hp() == 0 ? "hp off" : "hp " + ExgNative.hp() + "Hz");
        syncBars();
        int bd = ExgNative.band();
        if (!ExgNative.bandFit()) {
            band.setText("band mix");
        } else {
            band.setText(bd == 1 ? "band line-kill" : (bd == 2 ? "band EEG" : (bd == 3 ? "band EMG" : "band raw")));
        }
        if (ExgNative.negRail()) {
            car.setText("CAR off (rail)");
            car.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF4A3038));
            if (carNote != null) {
                carNote.setVisibility(View.VISIBLE);
            }
        } else if (ExgNative.car()) {
            car.setText("CAR on");
            car.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF2E8A58));
            if (carNote != null) {
                carNote.setVisibility(View.GONE);
            }
        } else {
            car.setText("CAR off");
            car.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF2A3038));
            if (carNote != null) {
                carNote.setVisibility(View.GONE);
            }
        }
        detrend.setText(ExgNative.detrend() ? "detrend" : "raw DC");
        env.setText(ExgNative.envelope() ? "envelope" : "wave");
        lp.setText(ExgNative.lp() == 0 ? "lp off" : "lp " + ExgNative.lp() + "Hz");
        algo.setText("Algos tab");
        board.setText(ExgNative.modeLabel());
        boolean apion = ExgNative.apiOn();
        apiOn.setText(apion ? "share EXG" : "share off");
        apiOn.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                apion ? 0xFF2E8A58 : 0xFF2A3038));
        apiBind.setText(ExgNative.apiLan() ? "wifi" : "this device");
        apiHz.setText(ExgNative.apiHz() + " /s");
        apiHttp.setText(ExgNative.apiHttp() == 0 ? "settings off" : ("settings " + ExgNative.apiHttp()));
        apiUdp.setText(ExgNative.apiUdp() == 0 ? "EXG off" : ("EXG " + ExgNative.apiUdp()));
        apiTcp.setText(ExgNative.apiTcp() == 0 ? "spare off" : ("spare " + ExgNative.apiTcp()));
        {
            String tok = ExgNative.apiToken();
            apiToken.setText(tok == null || tok.length() == 0 ? "lock off" : "lock on");
            String dest = ExgNative.apiPush();
            apiPush.setText(dest == null || dest.length() == 0 ? "extra send off" : dest);
            apiLine.setText(ExgNative.apiLine());
            fillPeers();
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

    /** Edge-to-edge phone window. Keep controls inside the status and navigation bars. */
    private void applyPhoneBars() {
        View content = findViewById(android.R.id.content);
        if (!(content instanceof ViewGroup) || ((ViewGroup) content).getChildCount() < 1) {
            return;
        }
        final View root = ((ViewGroup) content).getChildAt(0);
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            root.setOnApplyWindowInsetsListener((v, insets) -> {
                int types = WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout();
                android.graphics.Insets b = insets.getInsets(types);
                v.setPadding(b.left, b.top, b.right, b.bottom);
                return WindowInsets.CONSUMED;
            });
        } else {
            root.setOnApplyWindowInsetsListener((v, insets) -> {
                v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
                return insets.consumeSystemWindowInsets();
            });
        }
        root.requestApplyInsets();
    }

    private int dp(int d) {
        return Math.round(d * getResources().getDisplayMetrics().density);
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
        if (v instanceof ViewGroup) {
            ViewGroup vg = (ViewGroup) v;
            for (int i = 0; i < vg.getChildCount(); i++) {
                scaleTree(vg.getChildAt(i), f);
            }
        }
        if (!(v instanceof TextView)) {
            return;
        }
        TextView tv = (TextView) v;
        Float base = (Float) tv.getTag(R.id.base_sp);
        float den = getResources().getDisplayMetrics().density;
        if (den < 0.75f) {
            den = 1f;
        }
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
                    h = Math.round(36f * den);
                }
                mh = Math.max(32, Math.round(h / den));
                tv.setTag(R.id.base_min_h, mh);
            }
            int nh = Math.max(dp(28), Math.round(mh * den * f));
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
        if (requestCode == REQ_CSV) {
            Uri dest = data.getData();
            try {
                java.io.File local = new java.io.File(getFilesDir(), csvPickName);
                if (!local.isFile() || local.length() < 1) {
                    status.setText("CSV empty — nothing to save");
                    return;
                }
                copyFileToUri(local, dest);
                status.setText("saved " + csvPickName);
            } catch (Exception e) {
                status.setText("CSV: " + e.getMessage());
            }
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
            lab.setOnClickListener(v -> pickSite(ch));
            Button colb = new Button(this);
            colb.setText("color");
            colb.setOnClickListener(v -> {
                ColorPick.show(this, "ch" + (ch + 1) + " color",
                        ExgNative.color(ch), rgb -> {
                            ExgNative.setColor(ch, rgb);
                            refreshChannels();
                        });
            });
            colb.setOnLongClickListener(v -> hold("Color of this channel on the plot and on the cube."));
            Button on = new Button(this);
            Button rld = new Button(this);
            Button gn = new Button(this);
            LinearLayout.LayoutParams lpLab = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1.3f);
            LinearLayout.LayoutParams lpBtn = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            on.setOnClickListener(v -> {
                ExgNative.setActive(ch, !ExgNative.active(ch));
                refreshChannels();
                refreshChrome();
            });
            on.setOnLongClickListener(v -> hold("ON acquires this channel. off drops it from the plot, CAR, and the cube."));
            rld.setOnClickListener(v -> {
                if (ExgNative.negRail()) {
                    pickNegSite(ch);
                    return;
                }
                ExgNative.setRld(ch, !ExgNative.rld(ch));
                refreshChannels();
                refreshChrome();
            });
            rld.setOnLongClickListener(v -> hold(ExgNative.negRail()
                    ? "− site. With NEG RAIL this picks the back end of the pair. Bias stays off."
                    : "Bias for this channel. The bias drive is a separate contact from the pair."));
            gn.setOnClickListener(v -> pick(ExgNative.elecName(ch) + " gain",
                    new String[] {"1", "2", "3", "4", "6", "8", "12"},
                    gainIndex(ch), i -> {
                        ExgNative.setGain(ch, new int[] {1, 2, 3, 4, 6, 8, 12}[i]);
                        refreshChannels();
                    }));
            gn.setOnLongClickListener(v -> hold("Amplifier gain for this channel. Higher gain makes a smaller signal fill the plot."));
            row.addView(lab, lpLab);
            row.addView(colb, lpBtn);
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
            Button colb = (Button) row.getChildAt(1);
            Button on = (Button) row.getChildAt(2);
            Button rld = (Button) row.getChildAt(3);
            Button gn = (Button) row.getChildAt(4);
            boolean live = ExgNative.active(ch);
            boolean rail = ExgNative.negRail();
            boolean bias = !rail && ExgNative.rld(ch);
            on.setText(live ? "ON" : "off");
            on.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    live ? 0xFF2E8A58 : 0xFF3A3030));
            if (rail) {
                String nn = ExgNative.negName(ch);
                if (nn == null || nn.length() == 0) {
                    nn = "NONE";
                }
                rld.setEnabled(true);
                rld.setText("− " + nn);
                rld.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF5A2830));
            } else {
                rld.setEnabled(true);
                rld.setText(bias ? "bias ON" : "bias off");
                rld.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                        bias ? 0xFF2E6A8A : 0xFF3A3030));
            }
            gn.setText("g" + ExgNative.gain(ch));
            Button lab = (Button) row.getChildAt(0);
            String plus = ExgNative.elecName(ch);
            String shown = plus == null ? "" : plus;
            if (rail) {
                String nn = ExgNative.negName(ch);
                if (nn != null && nn.length() > 0 && !"NONE".equals(nn)) {
                    shown = shown + "-" + nn;
                }
            }
            lab.setOnLongClickListener(v -> hold("Channel name. Tap to pick the + site. With NEG RAIL the label is + site minus − site."));
            lab.setText((ch + 1) + "  " + shown);
            int col = ExgNative.color(ch) | 0xFF000000;
            colb.setTextColor(col);
            colb.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    0xFF000000 | ((((col >> 16) & 255) / 4) << 16)
                            | ((((col >> 8) & 255) / 4) << 8)
                            | ((col & 255) / 4)));
        }
        if (negRail != null) {
            boolean rail = ExgNative.negRail();
            negRail.setText(rail ? "NEG RAIL" : "bias RLD");
            negRail.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                    rail ? 0xFF8A3038 : 0xFF2A3038));
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
            empty.setPadding(dp(8), dp(8), dp(8), dp(4));
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
        t.setPadding(dp(8), dp(8), dp(8), dp(4));
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
            row.setPadding(0, dp(2), 0, dp(2));
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
                    "Record poses — named pose, not ID. Delete to drop.", 0xFF8B93A0));
            for (int i = 0; i < ln; i++) {
                final int idx = i;
                LinearLayout row = new LinearLayout(this);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setPadding(0, dp(2), 0, dp(2));
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
        askName(title, current, "letters, digits, - _", on, null);
    }

    private void askName(String title, String current, String hint,
            java.util.function.Consumer<String> on) {
        askName(title, current, hint, on, null);
    }

    private void askName(String title, String current, java.util.function.Consumer<String> on,
            Runnable cancel) {
        askName(title, current, "letters, digits, - _", on, cancel);
    }

    private void askName(String title, String current, String hint,
            java.util.function.Consumer<String> on, Runnable cancel) {
        final EditText e = new EditText(this);
        e.setText(current);
        e.setSelectAllOnFocus(true);
        e.setTextColor(0xFFE8EAF0);
        e.setHintTextColor(0xFF8B93A0);
        e.setHint(hint);
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

    private void pickNegSite(int ch) {
        int n = ExgNative.siteN();
        if (n < 1) {
            return;
        }
        String[] names = new String[n + 1];
        names[0] = "NONE";
        int cur = 0;
        int have = ExgNative.negSite(ch);
        for (int i = 0; i < n; i++) {
            names[i + 1] = ExgNative.siteName(i);
            if (have == i) {
                cur = i + 1;
            }
        }
        pick("ch" + (ch + 1) + " − site", names, cur, i -> {
            ExgNative.setNegSite(ch, i == 0 ? -1 : i - 1);
            refreshChannels();
            refreshChrome();
        });
    }

    private void pickSite(int ch) {
        int n = ExgNative.siteN();
        if (n < 1) {
            return;
        }
        String[] names = new String[n + 1];
        names[0] = "NONE";
        int cur = 0;
        String have = ExgNative.elecName(ch);
        for (int i = 0; i < n; i++) {
            names[i + 1] = ExgNative.siteName(i);
            if (have != null && have.equals(names[i + 1])) {
                cur = i + 1;
            }
        }
        pick("ch" + (ch + 1) + " + site", names, cur, i -> {
            ExgNative.setNegPick(false);
            ExgNative.setElecSel(ch);
            ExgNative.assignSite(i == 0 ? -1 : i - 1);
            refreshChannels();
            refreshChrome();
        });
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

    private void ensureNotify() {
        if (Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                        != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 71);
        }
    }

    private void fillPeers() {
        if (followList == null || allowList == null) {
            return;
        }
        followList.removeAllViews();
        int nf = ExgNative.followN();
        for (int i = 0; i < nf; i++) {
            final int ix = i;
            String nm = ExgNative.followName(i);
            String dest = ExgNative.followDest(i);
            if (nm == null || nm.length() < 1) {
                continue;
            }
            if (dest == null || dest.length() < 1 || dest.startsWith("bt:")) {
                continue;
            }
            Button b = new Button(this);
            b.setText(nm.replace('_', ' '));
            b.setOnClickListener(v -> {
                ExgNative.followUse(ix);
                ExgNative.setLinkPath(1);
                connectLan();
            });
            b.setOnLongClickListener(v -> {
                ExgNative.followDel(ix);
                refreshChrome();
                return true;
            });
            followList.addView(b);
        }
        allowList.removeAllViews();
        int na = ExgNative.allowN();
        for (int i = 0; i < na; i++) {
            final int ix = i;
            String nm = ExgNative.allowName(i);
            if (nm == null || nm.length() < 1) {
                continue;
            }
            Button b = new Button(this);
            b.setText("revoke  " + nm.replace('_', ' '));
            b.setOnClickListener(v -> {
                ExgNative.allowDel(ix);
                refreshChrome();
            });
            allowList.addView(b);
        }
    }

    private void pickPort() {
        if (ExgNative.linkPath() != 0) {
            askDest();
            return;
        }
        String raw = ExgNative.ports();
        String[] items = (raw == null || raw.length() == 0) ? new String[0] : raw.split("\n");
        if (items.length == 0) {
            new AlertDialog.Builder(this)
                    .setTitle("This board")
                    .setMessage("No Knight on USB. Switch to LAN and type the share address.")
                    .setPositiveButton("OK", null)
                    .show();
            return;
        }
        pick("Knight USB", items, 0, i -> {
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

    private void ownDrag(View v) {
        v.setOnTouchListener((view, ev) -> {
            view.getParent().requestDisallowInterceptTouchEvent(true);
            return false;
        });
    }

    private static int uvFromProg(int p) {
        if (p < 0) {
            p = 0;
        }
        if (p > 100) {
            p = 100;
        }
        double t = p / 100.0;
        int uv = (int) Math.round(20.0 * Math.pow(8000.0 / 20.0, t));
        if (uv < 20) {
            uv = 20;
        }
        if (uv > 8000) {
            uv = 8000;
        }
        return uv;
    }

    private static int uvToProg(int uv) {
        if (uv < 20) {
            uv = 20;
        }
        if (uv > 8000) {
            uv = 8000;
        }
        double t = Math.log(uv / 20.0) / Math.log(8000.0 / 20.0);
        int p = (int) Math.round(t * 100.0);
        if (p < 0) {
            p = 0;
        }
        if (p > 100) {
            p = 100;
        }
        return p;
    }

    private static String uvText(int uv) {
        if (uv >= 1000 && uv % 100 == 0) {
            if (uv % 1000 == 0) {
                return "±" + (uv / 1000) + " mV";
            }
            return String.format(java.util.Locale.US, "±%.1f mV", uv / 1000.0);
        }
        return "±" + uv + " µV";
    }

    private static String uiText(int tenths) {
        return String.format(java.util.Locale.US, "UI %.1f×", tenths / 10.0);
    }

    private static String zoomText(float z) {
        return String.format(java.util.Locale.US, "zoom %.1f×", z);
    }

    private void syncBars() {
        int uv = ExgNative.scaleUv();
        if (uv < 20) {
            uv = 20;
        }
        if (uv > 8000) {
            uv = 8000;
        }
        scale.setText(uvText(uv));
        if (!scaleBar.isPressed()) {
            scaleBar.setProgress(uvToProg(uv));
        }
        int sec = ExgNative.windowS();
        if (sec < 1) {
            sec = 1;
        }
        if (sec > 8) {
            sec = 8;
        }
        win.setText(sec + " s");
        if (!winBar.isPressed()) {
            winBar.setProgress(sec - 1);
        }
        int tenths = ExgNative.uiScale();
        if (tenths < 8) {
            tenths = 8;
        }
        if (tenths > 22) {
            tenths = 22;
        }
        uiScale.setText(uiText(tenths));
        if (!uiBar.isPressed()) {
            uiBar.setProgress(tenths - 8);
        }
        float z = ExgNative.cubeZoomF();
        if (z < 0.70f) {
            z = 0.70f;
        }
        if (z > 2.80f) {
            z = 2.80f;
        }
        cubeZoomLab.setText(zoomText(z));
        cube.setZoom(z);
        if (!cubeZoomBar.isPressed()) {
            int p = Math.round((z - 0.70f) / 0.10f);
            if (p < 0) {
                p = 0;
            }
            if (p > 21) {
                p = 21;
            }
            cubeZoomBar.setProgress(p);
        }
    }

    private static int boardPickIndex() {
        int mode = ExgNative.boardMode();
        if (mode == 2) {
            return 0;
        }
        if (mode == 1) {
            return 1;
        }
        return 2;
    }

    private void installKnightHex(File files) {
        if (files == null) {
            return;
        }
        InputStream in;
        try {
            in = getAssets().open("knight.hex");
        } catch (java.io.IOException e) {
            return;
        }
        File dir = new File(files, "exg-c/firmware");
        if (!dir.isDirectory() && !dir.mkdirs()) {
            try {
                in.close();
            } catch (java.io.IOException ignored) {
            }
            return;
        }
        File out = new File(dir, "knight.hex");
        File tmp = new File(dir, "knight.hex.tmp");
        try {
            OutputStream o = new FileOutputStream(tmp);
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                o.write(buf, 0, n);
            }
            o.close();
            in.close();
            if (out.exists() && !out.delete()) {
                tmp.delete();
                return;
            }
            if (!tmp.renameTo(out)) {
                tmp.delete();
            }
        } catch (java.io.IOException e) {
            try {
                in.close();
            } catch (java.io.IOException ignored) {
            }
            tmp.delete();
        }
    }

    private void maybeFirmwarePrompt() {
        if (fwAsked || !ExgNative.fwBehind()) {
            return;
        }
        fwAsked = true;
        new AlertDialog.Builder(this)
                .setTitle("Knight firmware")
                .setMessage("This connected Knight is not on firmware " + ExgNative.fwNeed()
                        + ". Electrodes off, then Upload.")
                .setPositiveButton("Open flasher", (d, w) ->
                        startActivity(new Intent(this, FlashActivity.class)))
                .setNegativeButton("Later", null)
                .show();
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

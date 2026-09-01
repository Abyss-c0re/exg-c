package com.abysscore.exgc;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
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
    private Button cubeViz, cubeMap;
    private TextView siteLabel;
    private View settings;
    private View learnBar;
    private TextView status;
    private TextView idLine;
    private TextView profList;
    private Button record;
    private Button connect;
    private Button port;
    private Button tabMain, tabCube, tabSet;
    private Button clean;
    private Button match;
    private Button notch;
    private Button hp;
    private Button scale;
    private Button win;
    private EditText profName;
    private EditText learnName;
    private LinearLayout chGrid;
    private LinearLayout profChips;
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
        siteLabel = findViewById(R.id.siteLabel);
        settings = findViewById(R.id.settings);
        learnBar = findViewById(R.id.learnBar);
        status = findViewById(R.id.status);
        idLine = findViewById(R.id.idLine);
        profList = findViewById(R.id.profList);
        record = findViewById(R.id.record);
        connect = findViewById(R.id.connect);
        port = findViewById(R.id.port);
        tabMain = findViewById(R.id.tabMain);
        tabCube = findViewById(R.id.tabCube);
        tabSet = findViewById(R.id.tabSet);
        clean = findViewById(R.id.clean);
        match = findViewById(R.id.match);
        notch = findViewById(R.id.notch);
        hp = findViewById(R.id.hp);
        scale = findViewById(R.id.scale);
        win = findViewById(R.id.win);
        profName = findViewById(R.id.profName);
        learnName = findViewById(R.id.learnName);
        chGrid = findViewById(R.id.chGrid);
        profChips = findViewById(R.id.profChips);

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
        tabSet.setOnClickListener(v -> showTab(2));
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
        buildChannels();
        profName.setText(ExgNative.getProfile());
        refreshProfiles();
        showTab(0);
        h.post(tick);
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
        settings.setVisibility(t == 2 ? View.VISIBLE : View.GONE);
        learnBar.setVisibility(t == 0 ? View.VISIBLE : View.GONE);
        tabMain.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 0 ? 0xFF24322C : 0xFF2A3038));
        tabCube.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 1 ? 0xFF3A1820 : 0xFF2A3038));
        tabSet.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 2 ? 0xFF243044 : 0xFF2A3038));
        if (t == 1) {
            refreshCubeChrome();
        }
        if (t == 2) {
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
    }

    private void refreshCubeChrome() {
        int map = ExgNative.cubeView();
        cubeViz.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                map == 0 ? 0xFF5A1020 : 0xFF2A3038));
        cubeMap.setBackgroundTintList(android.content.res.ColorStateList.valueOf(
                map == 1 ? 0xFF5A2810 : 0xFF2A3038));
        cubeMapTools.setVisibility(map == 1 ? View.VISIBLE : View.GONE);
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
        match.setText(ExgNative.matchOn() ? "MATCH" : "match");
        String id = ExgNative.idLine();
        String now = ExgNative.matchLine();
        int rec = ExgNative.recMs();
        if (rec > 0) {
            idLine.setText("do blink or clench…  " + ((rec + 99) / 1000) + "s  " + id);
            idLine.setTextColor(0xFFF0A040);
            record.setText("…");
        } else {
            idLine.setText(now.length() > 0 ? id + "   " + now : id);
            idLine.setTextColor(id.contains("clench") || id.contains("blink")
                    ? 0xFF3CB46E : 0xFF8B93A0);
            record.setText("Record");
        }
        int nh = ExgNative.notch();
        notch.setText(nh < 0 ? "notch AUTO" : (nh == 0 ? "notch off" : "notch " + nh));
        hp.setText(ExgNative.hp() == 0 ? "hp off" : "hp " + ExgNative.hp() + "Hz");
        scale.setText("±" + ExgNative.scaleUv() + " µV");
        win.setText("win " + ExgNative.windowS() + "s");
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
    }

    private void buildChannels() {
        chGrid.removeAllViews();
        for (int c = 0; c < 8; c++) {
            final int ch = c;
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            TextView lab = new TextView(this);
            lab.setText("ch" + (c + 1));
            lab.setTextColor(ExgNative.color(c));
            lab.setPadding(8, 16, 16, 16);
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
            ((TextView) row.getChildAt(0)).setTextColor(ExgNative.color(ch) | 0xFF000000);
        }
    }

    private abstract static class SimpleWatch implements TextWatcher {
        @Override
        public void beforeTextChanged(CharSequence s, int a, int b, int c) {}

        @Override
        public void onTextChanged(CharSequence s, int a, int b, int c) {}
    }
}

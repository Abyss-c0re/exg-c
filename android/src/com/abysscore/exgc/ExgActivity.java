package com.abysscore.exgc;

import android.app.Activity;
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

public class ExgActivity extends Activity {
    private final Handler h = new Handler(Looper.getMainLooper());
    private TraceView traces;
    private CubeView cube;
    private View settings;
    private View learnBar;
    private TextView status;
    private TextView profList;
    private Button connect;
    private Button port;
    private Button tabMain, tabCube, tabSet;
    private Button clean;
    private Button match;
    private Button notch;
    private Button hp;
    private Button scale;
    private EditText profName;
    private EditText learnName;
    private LinearLayout chGrid;
    private int tab;
    private int profI;
    private boolean running = true;

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
        settings = findViewById(R.id.settings);
        learnBar = findViewById(R.id.learnBar);
        status = findViewById(R.id.status);
        profList = findViewById(R.id.profList);
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
        profName = findViewById(R.id.profName);
        learnName = findViewById(R.id.learnName);
        chGrid = findViewById(R.id.chGrid);

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
        findViewById(R.id.noise).setOnClickListener(v -> ExgNative.noiseArm());
        findViewById(R.id.noiseOk).setOnClickListener(v -> ExgNative.noiseOk());
        findViewById(R.id.calm).setOnClickListener(v -> ExgNative.calm());
        clean.setOnClickListener(v -> {
            ExgNative.toggleClean();
            refreshChrome();
        });
        findViewById(R.id.record).setOnClickListener(v -> {
            ExgNative.setName(learnName.getText().toString().trim());
            ExgNative.record();
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
        findViewById(R.id.profNext).setOnClickListener(v -> {
            String[] ps = ExgNative.profiles();
            if (ps == null || ps.length == 0) {
                return;
            }
            profI = (profI + 1) % ps.length;
            profName.setText(ps[profI]);
            ExgNative.setProfile(ps[profI]);
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
        cube.setVisibility(t == 1 ? View.VISIBLE : View.GONE);
        settings.setVisibility(t == 2 ? View.VISIBLE : View.GONE);
        learnBar.setVisibility(t == 0 ? View.VISIBLE : View.GONE);
        tabMain.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 0 ? 0xFF24322C : 0xFF2A3038));
        tabCube.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 1 ? 0xFF3A1820 : 0xFF2A3038));
        tabSet.setBackgroundTintList(android.content.res.ColorStateList.valueOf(t == 2 ? 0xFF243044 : 0xFF2A3038));
        if (t == 2) {
            refreshChannels();
            refreshProfiles();
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
        int nh = ExgNative.notch();
        notch.setText(nh < 0 ? "notch AUTO" : (nh == 0 ? "notch off" : "notch " + nh));
        hp.setText(ExgNative.hp() == 0 ? "hp off" : "hp " + ExgNative.hp() + "Hz");
        scale.setText("±" + ExgNative.scaleUv() + " µV");
    }

    private void refreshProfiles() {
        String[] ps = ExgNative.profiles();
        StringBuilder sb = new StringBuilder("saved: ");
        if (ps == null || ps.length == 0) {
            sb.append("(none yet)");
        } else {
            for (int i = 0; i < ps.length; i++) {
                if (i > 0) {
                    sb.append("  ·  ");
                }
                sb.append(ps[i]);
            }
        }
        profList.setText(sb.toString());
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

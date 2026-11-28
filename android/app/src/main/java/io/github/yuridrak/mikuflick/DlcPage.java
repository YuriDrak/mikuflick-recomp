package io.github.yuridrak.mikuflick;

import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

/**
 * The DLC page (title SHOP slot): show/hide DLC songs and the import of an IPA with DLC. Mirrors
 * ios/App/DlcViewController.m. The native code (port/src/main.c android_dlc_page) sends its state as
 * one line "S show packs songs" (tab-separated) and gets actions back ("later", "import", "show 0|1").
 */
final class DlcPage extends NativePage {
    /* state from native code */
    private int installedPacks, installedSongs;
    private boolean showDlc;

    /* views */
    private Switch showSwitch;
    private TextView showLabel, songsFooter;
    private boolean updatingSwitch;

    DlcPage(MikuFlickActivity act) {
        super(act);
    }

    // ---------------------------------------------------------------- views

    @Override
    View build() {
        /* a top bar with Done, and the page scrolling below it (not under it) */
        LinearLayout root = new LinearLayout(ctx);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BACKGROUND);
        FrameLayout bar = new FrameLayout(ctx);
        bar.setBackgroundColor(BACKGROUND);
        root.addView(bar, lp(MATCH, dp(52)));
        View hairline = new View(ctx);   /* shown once the page scrolls under the bar */
        hairline.setBackgroundColor(SEPARATOR);
        hairline.setVisibility(View.INVISIBLE);
        root.addView(hairline, lp(MATCH, Math.max(1, dp(0.5f))));
        ScrollView scroll = new ScrollView(ctx);
        scroll.setFillViewport(true);
        scroll.setOnScrollChangeListener((v, x, y, ox, oy) -> hairline.setVisibility(y > 0 ? View.VISIBLE : View.INVISIBLE));
        LinearLayout.LayoutParams slp = lp(MATCH, 0);
        slp.weight = 1;
        root.addView(scroll, slp);

        LinearLayout col = new LinearLayout(ctx);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(16), 0, dp(16), dp(32));
        scroll.addView(col, new FrameLayout.LayoutParams(MATCH, WRAP));

        TextView title = text(34, LABEL, false);
        title.setTypeface(Typeface.create("sans-serif", Typeface.BOLD));
        title.setText("DLC Packs");
        title.setPadding(dp(4), 0, 0, dp(6));
        col.addView(title, lp(MATCH, WRAP));

        TextView done = text(17, TEAL, true);
        done.setText("Done");
        done.setGravity(Gravity.CENTER);
        done.setPadding(dp(16), 0, dp(16), 0);
        done.setBackground(pressable(null, dp(22)));
        done.setClickable(true);
        done.setOnClickListener(v -> act.pageAction("later"));
        FrameLayout.LayoutParams dlp = new FrameLayout.LayoutParams(WRAP, dp(44), Gravity.CENTER_VERTICAL | Gravity.END);
        dlp.setMarginEnd(dp(8));
        bar.addView(done, dlp);

        /* songs */
        LinearLayout songsCard = card();
        LinearLayout showRow = row(R.drawable.ic_page_music);
        showLabel = rowTitle(showRow, "Show DLC Songs");
        showSwitch = new Switch(ctx);
        /* disabled (no DLC songs yet) must look it: grey before checked */
        int[][] st = {{-android.R.attr.state_enabled}, {android.R.attr.state_checked}, {}};
        showSwitch.setThumbTintList(new ColorStateList(st, new int[] {0xFF636366, LABEL, 0xFFDDDDDD}));
        showSwitch.setTrackTintList(new ColorStateList(st, new int[] {0x29787880, TEAL, 0x52787880}));
        showSwitch.setClickable(false);   /* the row takes the taps, also while the switch is disabled */
        showSwitch.setFocusable(false);
        showSwitch.setOnCheckedChangeListener((b, on) -> {
            if (!updatingSwitch) act.pageAction(on ? "show 1" : "show 0");
        });
        showRow.addView(showSwitch, lp(WRAP, WRAP));
        showRow.setOnClickListener(v -> {
            if (showSwitch.isEnabled()) showSwitch.toggle();
            else Toast.makeText(ctx, "No DLC songs installed yet: they come from an IPA with the DLC packs.", Toast.LENGTH_SHORT).show();
        });
        songsCard.addView(showRow, lp(MATCH, WRAP));
        col.addView(songsCard, lp(MATCH, WRAP));
        songsFooter = footer(col);

        /* import */
        LinearLayout importCard = card();
        LinearLayout importRow = row(R.drawable.ic_page_import);
        rowTitle(importRow, "Import IPA with DLC…").setTextColor(TEAL);
        importRow.setOnClickListener(v -> act.pageAction("import"));
        importCard.addView(importRow, lp(MATCH, WRAP));
        LinearLayout.LayoutParams ilp = lp(MATCH, WRAP);
        ilp.topMargin = dp(26);
        col.addView(importCard, ilp);
        apply();
        return root;
    }

    private LinearLayout card() {
        LinearLayout c = new LinearLayout(ctx);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(rounded(CARD, dp(12)));
        c.setClipToOutline(true);   /* the rows' ripples stay inside the rounded corners */
        return c;
    }

    private LinearLayout row(int iconRes) {
        LinearLayout r = new LinearLayout(ctx);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(dp(16), dp(10), dp(16), dp(10));
        r.setMinimumHeight(dp(52));
        r.setBackground(pressable(null, 0));
        r.setClickable(true);
        r.addView(icon(iconRes, TEAL, 24));
        return r;
    }

    private TextView rowTitle(LinearLayout row, String s) {
        TextView t = text(17, LABEL, false);
        t.setText(s);
        LinearLayout.LayoutParams l = lp(0, WRAP);
        l.weight = 1;
        l.setMarginStart(dp(14));
        row.addView(t, l);
        return t;
    }

    private TextView footer(LinearLayout col) {
        TextView f = text(13, SECONDARY, false);
        f.setPadding(dp(16), dp(7), dp(16), 0);
        col.addView(f, lp(MATCH, WRAP));
        return f;
    }

    // ---------------------------------------------------------------- state

    /** From native code (MikuFlickActivity.dlcState), on the UI thread. */
    void setState(String s) {
        for (String line : s.split("\n")) {
            String[] f = line.split("\t", -1);
            if (f[0].equals("S") && f.length >= 4) {
                showDlc = num(f[1]) != 0;
                installedPacks = num(f[2]);
                installedSongs = num(f[3]);
            }
        }
        apply();
    }

    private static int num(String s) {
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private void apply() {
        if (showSwitch == null) return;
        boolean have = installedSongs > 0;
        updatingSwitch = true;
        if (showSwitch.isChecked() != showDlc) showSwitch.setChecked(showDlc);
        updatingSwitch = false;
        showSwitch.setEnabled(have);
        showLabel.setTextColor(have ? LABEL : SECONDARY);
        String foot = !have ? "No DLC songs installed yet."
                : installedPacks + (installedPacks == 1 ? " pack, " : " packs, ") + installedSongs + " songs installed.";
        if (!TextUtils.equals(songsFooter.getText(), foot)) songsFooter.setText(foot);
    }

    @Override
    void onBack() {
        act.pageAction("later");
    }
}

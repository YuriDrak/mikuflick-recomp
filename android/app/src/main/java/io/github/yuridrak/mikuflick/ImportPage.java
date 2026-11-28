package io.github.yuridrak.mikuflick;

import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.content.res.ColorStateList;

/**
 * Game data import (port/src/main.c import_screen): the first start, an update of data imported
 * by an older version, or an IPA with DLC from the DLC page. Mirrors ios/App/ImportViewController.m.
 * The file is picked here; the import itself runs in native code, which sends its progress.
 */
final class ImportPage extends NativePage {
    static final int MODE_NEEDED = 0, MODE_UPDATE = 1, MODE_DLC = 2;
    static final int PHASE_PROMPT = 0, PHASE_IMPORTING = 1, PHASE_FAILED = 2;   /* android.c */

    private final int mode;
    private final String filesDir;
    private int phase = -1;

    private ImageView statusIcon;
    private TextView cardTitle, body, percent, file, footer, action, later;
    private View pill, progressBox;
    private ProgressBar bar;

    ImportPage(MikuFlickActivity act, int mode, String filesDir) {
        super(act);
        this.mode = mode;
        this.filesDir = filesDir;
    }

    @Override
    View build() {
        FrameLayout root = new FrameLayout(ctx);
        root.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                                                new int[] {0xFF0B2B2A, BACKGROUND, BACKGROUND}));
        ScrollView scroll = new ScrollView(ctx);
        scroll.setFillViewport(true);
        scroll.setOverScrollMode(View.OVER_SCROLL_NEVER);
        root.addView(scroll, new FrameLayout.LayoutParams(MATCH, MATCH));

        LinearLayout col = new LinearLayout(ctx);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setGravity(Gravity.CENTER_VERTICAL);
        col.setPadding(dp(24), dp(20), dp(24), dp(20));
        scroll.addView(col, new FrameLayout.LayoutParams(MATCH, MATCH));

        TextView appTitle = text(34, LABEL, true);
        appTitle.setText("Miku Flick/02");
        appTitle.setTypeface(Typeface.create("sans-serif", Typeface.BOLD));
        appTitle.setGravity(Gravity.CENTER);
        col.addView(appTitle, lp(MATCH, WRAP));

        /* the card: header, body, storage note, progress */
        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable cardBg = rounded(CARD, dp(28));
        cardBg.setStroke(Math.max(1, dp(0.5f)), 0x1FFFFFFF);
        card.setBackground(cardBg);
        card.setPadding(dp(22), dp(24), dp(22), dp(24));
        LinearLayout.LayoutParams cardLp = lp(MATCH, WRAP);
        cardLp.topMargin = dp(26);
        col.addView(card, cardLp);

        LinearLayout header = new LinearLayout(ctx);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        statusIcon = icon(R.drawable.ic_page_ipa, TEAL, 24);
        header.addView(statusIcon);
        cardTitle = text(20, LABEL, true);
        LinearLayout.LayoutParams tlp = lp(0, WRAP);
        tlp.weight = 1;
        tlp.setMarginStart(dp(10));
        header.addView(cardTitle, tlp);
        card.addView(header, lp(MATCH, WRAP));

        body = text(15, 0xD6FFFFFF, false);
        body.setLineSpacing(dp(2), 1f);
        card.addView(body, spaced(14));

        LinearLayout pillRow = new LinearLayout(ctx);
        pillRow.setOrientation(LinearLayout.HORIZONTAL);
        pillRow.setGravity(Gravity.CENTER_VERTICAL);
        pillRow.setBackground(rounded(0x14FFFFFF, dp(12)));
        pillRow.setPadding(dp(12), dp(10), dp(12), dp(10));
        pillRow.addView(icon(R.drawable.ic_page_storage, TEAL, 16));
        TextView pillText = text(12.5f, 0xBFFFFFFF, true);
        pillText.setText(mode == MODE_DLC ? "About 16 GB free space needed · The IPA can be deleted afterwards"
                                          : "About 1 GB free space needed · The IPA can be deleted afterwards");
        LinearLayout.LayoutParams plp = lp(0, WRAP);
        plp.weight = 1;
        plp.setMarginStart(dp(8));
        pillRow.addView(pillText, plp);
        pill = pillRow;
        card.addView(pill, spaced(14));

        LinearLayout prog = new LinearLayout(ctx);
        prog.setOrientation(LinearLayout.VERTICAL);
        bar = new ProgressBar(ctx, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        bar.setIndeterminate(false);
        bar.setProgressTintList(ColorStateList.valueOf(TEAL));
        bar.setProgressBackgroundTintList(ColorStateList.valueOf(0x29FFFFFF));
        prog.addView(bar, lp(MATCH, dp(8)));
        LinearLayout meta = new LinearLayout(ctx);
        meta.setOrientation(LinearLayout.HORIZONTAL);
        meta.setGravity(Gravity.CENTER_VERTICAL);
        file = text(12.5f, 0xB8FFFFFF, false);
        file.setTypeface(Typeface.MONOSPACE);
        file.setSingleLine(true);
        file.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        LinearLayout.LayoutParams flp = lp(0, WRAP);
        flp.weight = 1;
        meta.addView(file, flp);
        percent = text(15, TEAL, true);
        percent.setGravity(Gravity.END);
        percent.setFontFeatureSettings("tnum");   /* digits of one width: the label doesn't wobble */
        LinearLayout.LayoutParams pclp = lp(WRAP, WRAP);
        pclp.setMarginStart(dp(8));
        meta.addView(percent, pclp);
        prog.addView(meta, spaced(10));
        TextView keepOpen = text(13, 0xADFFFFFF, false);
        keepOpen.setText("Keep the app open while files are verified and extracted.");
        prog.addView(keepOpen, spaced(10));
        progressBox = prog;
        card.addView(progressBox, spaced(14));

        action = button("Import IPA", true);
        action.setOnClickListener(v -> {
            haptic(v);
            if (phase != PHASE_IMPORTING) act.pickIpa(this);
        });
        LinearLayout.LayoutParams alp = lp(MATCH, dp(56));
        alp.topMargin = dp(22);
        col.addView(action, alp);

        later = button(mode == MODE_DLC ? "Cancel" : "Not Now", false);
        later.setOnClickListener(v -> onBack());
        LinearLayout.LayoutParams llp = lp(MATCH, dp(44));
        llp.topMargin = dp(10);
        col.addView(later, llp);

        footer = text(12, TERTIARY, false);
        footer.setGravity(Gravity.CENTER);
        footer.setText("Or copy MikuFlick2.app and gamedata.txt to:\n" + filesDir);
        footer.setTextIsSelectable(false);
        col.addView(footer, spaced(18));

        setState(PHASE_PROMPT, 0, "", "");
        return root;
    }

    private LinearLayout.LayoutParams spaced(int topDp) {
        LinearLayout.LayoutParams l = lp(MATCH, WRAP);
        l.topMargin = dp(topDp);
        return l;
    }

    @Override
    void onBack() {
        if (phase != PHASE_IMPORTING) act.pageAction("later");   /* on the first start: quits */
    }

    /** From native code (android.c android_import_state), on the UI thread. */
    void setState(int ph, int permille, String fileName, String err) {
        if (ph == PHASE_IMPORTING && phase == PHASE_IMPORTING) {   /* progress only: no layout change */
            bar.setProgress(permille, true);
            percent.setText((permille + 5) / 10 + "%");
            if (!fileName.isEmpty()) file.setText(fileName);
            return;
        }
        phase = ph;
        boolean declinable = mode != MODE_NEEDED;
        if (ph == PHASE_PROMPT) {
            statusIcon.setImageDrawable(tinted(R.drawable.ic_page_ipa, TEAL));
            cardTitle.setText(mode == MODE_DLC ? "Add DLC Songs" : mode == MODE_UPDATE ? "Game Data Update" : "Game Data Needed");
            body.setText(mode == MODE_DLC
                    ? "Pick an IPA that includes the DLC packs, like the “All DLC Included” one."
                    : mode == MODE_UPDATE
                    ? "This version also reads the help text and the ending credits from your IPA. Import it once more to add them; your scores and options are kept."
                    : "This app contains no SEGA or Crypton game data. Pick your own decrypted Miku Flick/02 v1.1.5 IPA on this device to import it.");
            body.setVisibility(View.VISIBLE);
            pill.setVisibility(View.VISIBLE);
            progressBox.setVisibility(View.GONE);
            action.setText("Import IPA");
            setButtonIcon(action, R.drawable.ic_page_folder);
            action.setVisibility(View.VISIBLE);
            later.setVisibility(declinable ? View.VISIBLE : View.GONE);
            footer.setVisibility(mode == MODE_DLC ? View.GONE : View.VISIBLE);
        } else if (ph == PHASE_IMPORTING) {
            statusIcon.setImageDrawable(tinted(R.drawable.ic_page_download, TEAL));
            cardTitle.setText("Importing Game Data…");
            body.setVisibility(View.GONE);
            pill.setVisibility(View.GONE);
            progressBox.setVisibility(View.VISIBLE);
            bar.setProgress(permille);
            percent.setText((permille + 5) / 10 + "%");
            file.setText(fileName.isEmpty() ? "Verifying archive…" : fileName);
            action.setVisibility(View.GONE);
            later.setVisibility(View.GONE);
            footer.setVisibility(View.GONE);
        } else {
            statusIcon.setImageDrawable(tinted(R.drawable.ic_page_warning, ORANGE));
            cardTitle.setText("Import Failed");
            body.setText(err.isEmpty() ? "Could not import the selected file." : err);
            body.setVisibility(View.VISIBLE);
            pill.setVisibility(View.GONE);
            progressBox.setVisibility(View.GONE);
            action.setText("Try Again");
            setButtonIcon(action, R.drawable.ic_page_retry);
            action.setVisibility(View.VISIBLE);
            later.setVisibility(declinable ? View.VISIBLE : View.GONE);
            footer.setVisibility(View.GONE);
        }
    }

    /** The picker could not be opened. */
    void pickerFailed() {
        setState(PHASE_FAILED, 0, "", "Could not open the file picker.");
    }
}

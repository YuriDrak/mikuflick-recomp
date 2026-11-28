package io.github.yuridrak.mikuflick;

import android.app.AlertDialog;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.text.InputType;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextUtils;
import android.text.style.ForegroundColorSpan;
import android.view.Gravity;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The DLC page (title SHOP slot): show/hide DLC songs, the download link (an editable field, with Restore
 * Default Link when it differs from the built-in one), per-pack downloads and the
 * import of a whole IPA with DLC. Mirrors ios/App/DlcViewController.m. The native code
 * (port/src/main.c android_dlc_page) sends its state as text (see there) and gets actions back.
 * Rows are built once and updated in place, so a running download only changes a few labels.
 */
final class DlcPage extends NativePage {
    private static final int IDLE = 0, LISTING = 1, READY = 2, INSTALLING = 3, FAILED = 4;   /* dlcdl.h DLD_* */

    /** One pack of the state. */
    private static final class Pack {
        int pack, nsongs, megabytes;
        boolean available, installed;
        String name = "";
        final List<String[]> songs = new ArrayList<>();   /* {title, latin} */
    }

    /* state from native code */
    private int state = IDLE, permille, busyPack = -1, installedPacks, installedSongs;
    private boolean error, showDlc, haveState;
    private String msg = "", url = "", defaultUrl = "";
    private final List<Pack> packs = new ArrayList<>();

    /* views */
    private Switch showSwitch;
    private TextView showLabel, songsFooter, importLabel, statusText;
    private EditText linkField;
    private ImageView importIcon;
    private View restoreRow, restoreSep;
    private ProgressBar statusSpinner, statusBar;
    private View statusRow, packsHeader, packsFooter;
    private LinearLayout packsCard;
    private final List<PackRow> packRows = new ArrayList<>();
    private boolean updatingSwitch;

    private final class PackRow {
        final View row;
        final ImageView icon;
        final TextView name, songs, right;
        String songsKey = "";

        PackRow() {
            LinearLayout r = new LinearLayout(ctx);
            r.setOrientation(LinearLayout.HORIZONTAL);
            r.setGravity(Gravity.CENTER_VERTICAL);
            r.setPadding(dp(16), dp(11), dp(16), dp(11));
            r.setMinimumHeight(dp(52));
            icon = icon(R.drawable.ic_page_download, TEAL, 24);
            r.addView(icon);
            LinearLayout texts = new LinearLayout(ctx);
            texts.setOrientation(LinearLayout.VERTICAL);
            name = text(17, LABEL, false);
            texts.addView(name, lp(MATCH, WRAP));
            songs = text(15, SECONDARY, false);
            songs.setLineSpacing(dp(1.5f), 1f);
            LinearLayout.LayoutParams slp = lp(MATCH, WRAP);
            slp.topMargin = dp(2);
            texts.addView(songs, slp);
            LinearLayout.LayoutParams tlp = lp(0, WRAP);
            tlp.weight = 1;
            tlp.setMarginStart(dp(14));
            tlp.setMarginEnd(dp(8));
            r.addView(texts, tlp);
            right = text(15, SECONDARY, true);
            right.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
            right.setFontFeatureSettings("tnum");
            right.setSingleLine(true);
            /* one width for every state (the widest text), so a changing percentage never re-wraps the songs */
            int w = (int) Math.ceil(right.getPaint().measureText("Installed")) + dp(2);
            r.addView(right, lp(w, WRAP));
            row = r;
        }
    }

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
        LinearLayout showRow = row(R.drawable.ic_page_music, TEAL);
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
            else Toast.makeText(ctx, "No DLC songs installed yet: download a pack below, or import an IPA with the DLC packs.", Toast.LENGTH_SHORT).show();
        });
        songsCard.addView(showRow, lp(MATCH, WRAP));
        col.addView(songsCard, lp(MATCH, WRAP));
        songsFooter = footer(col);

        /* download */
        header(col, "Download");
        LinearLayout dl = card();
        LinearLayout linkRow = row(R.drawable.ic_page_link, TEAL);
        LinearLayout linkTexts = new LinearLayout(ctx);
        linkTexts.setOrientation(LinearLayout.VERTICAL);
        TextView linkTitle = text(13, SECONDARY, false);
        linkTitle.setText("Download Link");
        linkTexts.addView(linkTitle);
        /* the link itself is editable; Go, Done or leaving the field takes it (commitLink) */
        linkField = new EditText(ctx);
        linkField.setTextSize(15);
        linkField.setTextColor(LABEL);
        linkField.setHintTextColor(TERTIARY);
        linkField.setHint("https://…/file.ipa");
        linkField.setSingleLine(true);
        linkField.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        linkField.setImeOptions(EditorInfo.IME_ACTION_GO | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        linkField.setBackground(null);
        linkField.setPadding(0, dp(2), 0, dp(2));
        linkField.setOnEditorActionListener((v, id, ev) -> {
            linkField.clearFocus();   /* commits through the focus listener */
            return true;
        });
        linkField.setOnFocusChangeListener((v, has) -> {
            if (has) {
                if (refuseWhileBusy()) linkField.clearFocus();
                return;
            }
            hideKeyboard();
            commitLink();
        });
        linkTexts.addView(linkField, lp(MATCH, WRAP));
        linkRow.addView(linkTexts, weighted());
        linkRow.setBackground(null);   /* the field takes the taps */
        linkRow.setClickable(false);
        dl.addView(linkRow, lp(MATCH, WRAP));
        separator(dl);

        /* only while the link differs from the built-in one: brings that back */
        LinearLayout restore = row(R.drawable.ic_page_retry, TEAL);
        rowTitle(restore, "Restore Default Link").setTextColor(TEAL);
        restore.setOnClickListener(v -> {
            if (refuseWhileBusy() || defaultUrl.isEmpty()) return;
            linkField.setText(defaultUrl);
            act.pageAction("url " + defaultUrl);
        });
        restoreRow = restore;
        dl.addView(restore, lp(MATCH, WRAP));
        separator(dl);
        restoreSep = dl.getChildAt(dl.getChildCount() - 1);

        LinearLayout importRow = row(R.drawable.ic_page_import, TEAL);
        importIcon = (ImageView) importRow.getChildAt(0);
        importLabel = rowTitle(importRow, "Import IPA with DLC…");
        importRow.setOnClickListener(v -> {
            if (!refuseWhileBusy()) act.pageAction("import");   /* the importer replaces the folder packs are written to */
        });
        dl.addView(importRow, lp(MATCH, WRAP));
        separator(dl);

        LinearLayout status = new LinearLayout(ctx);
        status.setOrientation(LinearLayout.HORIZONTAL);
        status.setGravity(Gravity.CENTER_VERTICAL);
        status.setPadding(dp(16), dp(12), dp(16), dp(12));
        status.setMinimumHeight(dp(48));
        statusText = text(15, LABEL, false);
        status.addView(statusText, weighted());
        statusSpinner = new ProgressBar(ctx, null, android.R.attr.progressBarStyleSmall);
        statusSpinner.setIndeterminateTintList(ColorStateList.valueOf(SECONDARY));
        LinearLayout.LayoutParams sp = lp(dp(20), dp(20));
        sp.setMarginStart(dp(12));
        status.addView(statusSpinner, sp);
        statusBar = new ProgressBar(ctx, null, android.R.attr.progressBarStyleHorizontal);
        statusBar.setMax(1000);
        statusBar.setProgressTintList(ColorStateList.valueOf(TEAL));
        statusBar.setProgressBackgroundTintList(ColorStateList.valueOf(0x29FFFFFF));
        LinearLayout.LayoutParams bp = lp(dp(90), dp(4));
        bp.setMarginStart(dp(12));
        status.addView(statusBar, bp);
        status.setOnClickListener(v -> {
            if (state == FAILED && !url.isEmpty()) act.pageAction("list");   /* try again */
        });
        statusRow = status;
        dl.addView(status, lp(MATCH, WRAP));
        col.addView(dl, lp(MATCH, WRAP));
        footer(col).setText("Packs download from this link. It starts as the archive.org copy of the \u201cAll DLC Included\u201d IPA; enter the link of another IPA with the DLC packs to use that one.");

        /* packs */
        packsHeader = header(col, "Packs");
        packsCard = card();
        col.addView(packsCard, lp(MATCH, WRAP));
        packsFooter = footer(col);
        ((TextView) packsFooter).setText("Tap a pack to download it, or tap it again to cancel.");

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

    private LinearLayout row(int iconRes, int iconColor) {
        LinearLayout r = new LinearLayout(ctx);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(dp(16), dp(10), dp(16), dp(10));
        r.setMinimumHeight(dp(52));
        r.setBackground(pressable(null, 0));
        r.setClickable(true);
        r.addView(icon(iconRes, iconColor, 24));
        return r;
    }

    private TextView rowTitle(LinearLayout row, String s) {
        TextView t = text(17, LABEL, false);
        t.setText(s);
        row.addView(t, weighted());
        return t;
    }

    private LinearLayout.LayoutParams weighted() {
        LinearLayout.LayoutParams l = lp(0, WRAP);
        l.weight = 1;
        l.setMarginStart(dp(14));
        return l;
    }

    private void separator(LinearLayout card) {
        View s = new View(ctx);
        s.setBackgroundColor(SEPARATOR);
        LinearLayout.LayoutParams l = lp(MATCH, Math.max(1, dp(0.5f)));
        l.setMarginStart(dp(54));
        card.addView(s, l);
    }

    private TextView header(LinearLayout col, String s) {
        TextView h = text(13, SECONDARY, false);
        h.setText(s.toUpperCase());
        h.setPadding(dp(16), dp(26), dp(16), dp(7));
        col.addView(h, lp(MATCH, WRAP));
        return h;
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
        String[] lines = s.split("\n");
        List<Pack> np = new ArrayList<>();
        for (String line : lines) {
            String[] f = line.split("\t", -1);
            if (f[0].equals("S") && f.length >= 11) {   /* S state permille busy error msg show packs songs url default */
                state = num(f[1]);
                permille = num(f[2]);
                busyPack = num(f[3]);
                error = num(f[4]) != 0;
                msg = f[5];
                showDlc = num(f[6]) != 0;
                installedPacks = num(f[7]);
                installedSongs = num(f[8]);
                url = f[9];
                defaultUrl = f[10];
                haveState = true;
            } else if (f[0].equals("P") && f.length >= 8) {
                Pack p = new Pack();
                p.pack = num(f[1]);
                p.name = f[2];
                p.nsongs = num(f[3]);
                p.megabytes = num(f[4]);
                p.available = num(f[5]) != 0;
                p.installed = num(f[6]) != 0;
                for (int i = 7; i + 1 < f.length; i += 2) p.songs.add(new String[] {f[i], f[i + 1]});
                np.add(p);
            }
        }
        packs.clear();
        packs.addAll(np);
        apply();
    }

    private static int num(String s) {
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private boolean busy() {
        return state == LISTING || state == INSTALLING;
    }

    private String packName(int pack) {
        for (Pack p : packs) if (p.pack == pack) return p.name;
        return "pack";
    }

    private static void setText(TextView t, CharSequence s) {   /* unchanged text: no relayout */
        if (!TextUtils.equals(t.getText(), s)) t.setText(s);
    }

    private static void setVisible(View v, boolean on) {
        int vis = on ? View.VISIBLE : View.GONE;
        if (v.getVisibility() != vis) v.setVisibility(vis);
    }

    /** Shows the current state; only views whose content changed are touched. */
    private void apply() {
        if (showSwitch == null) return;
        boolean have = installedSongs > 0;
        updatingSwitch = true;
        if (showSwitch.isChecked() != showDlc) showSwitch.setChecked(showDlc);
        updatingSwitch = false;
        showSwitch.setEnabled(have);
        showLabel.setTextColor(have ? LABEL : SECONDARY);
        setText(songsFooter, !have ? "No DLC songs installed yet."
                : installedPacks + (installedPacks == 1 ? " pack, " : " packs, ") + installedSongs + " songs installed.");

        if (!linkField.hasFocus()) setText(linkField, url);   /* never under the player's typing */
        boolean custom = !defaultUrl.isEmpty() && !url.equals(defaultUrl);
        setVisible(restoreRow, custom);
        setVisible(restoreSep, custom);
        int actionColor = busy() ? SECONDARY : TEAL;
        importLabel.setTextColor(actionColor);
        importIcon.setImageTintList(ColorStateList.valueOf(actionColor));

        /* status (the row is always there, so the page never changes shape under the finger) */
        String st;
        int color = LABEL;
        switch (state) {
        case LISTING: st = "Reading the pack list…"; break;
        case INSTALLING: st = "Downloading " + packName(busyPack) + " — " + permille / 10 + "%"; break;
        case FAILED: st = msg + " Tap to try again."; color = ORANGE; break;
        case READY: {
            color = error ? ORANGE : TEAL;
            if (!msg.isEmpty()) st = msg;
            else {
                int n = 0;
                for (Pack p : packs) if (p.available) n++;
                st = "The link has " + n + " of the " + packs.size() + " packs.";
            }
            break;
        }
        default: st = haveState ? "Enter a download link." : ""; break;
        }
        setText(statusText, st);
        statusText.setTextColor(color);
        setVisible(statusSpinner, state == LISTING);
        setVisible(statusBar, state == INSTALLING);
        if (state == INSTALLING) statusBar.setProgress(permille, true);
        boolean retry = state == FAILED;
        if (statusRow.isClickable() != retry) {
            statusRow.setBackground(retry ? pressable(null, 0) : null);
            statusRow.setClickable(retry);
        }

        /* packs: rows are made when the list changes, otherwise updated */
        boolean any = !packs.isEmpty();
        setVisible(packsHeader, any);
        setVisible(packsCard, any);
        setVisible(packsFooter, any);
        if (packRows.size() != packs.size()) {
            packsCard.removeAllViews();
            packRows.clear();
            for (int i = 0; i < packs.size(); i++) {
                if (i > 0) separator(packsCard);
                PackRow r = new PackRow();
                final int index = i;
                r.row.setBackground(pressable(null, 0));
                r.row.setClickable(true);
                r.row.setOnClickListener(v -> packTapped(index, v));
                packsCard.addView(r.row, lp(MATCH, WRAP));
                packRows.add(r);
            }
        }
        for (int i = 0; i < packs.size(); i++) updatePack(packRows.get(i), packs.get(i));
    }

    private void updatePack(PackRow r, Pack p) {
        boolean grey = !p.available && !p.installed;   /* not in the linked IPA (or no link yet) */
        setText(r.name, p.name);
        r.name.setTextColor(grey ? SECONDARY : LABEL);
        String key = (grey ? "g" : "n") + Arrays.deepToString(p.songs.toArray());
        if (!key.equals(r.songsKey)) {   /* a line per song: English (or romanized), then the Japanese title dimmer */
            r.songsKey = key;
            r.songs.setText(songLines(p, grey));
        }
        int icon, iconColor, rightColor = SECONDARY;
        String right;
        if (state == INSTALLING && busyPack == p.pack) {
            right = permille / 10 + "%";
            rightColor = TEAL;
            icon = R.drawable.ic_page_stop;   /* tap again: cancel */
            iconColor = TEAL;
        } else if (p.installed) {
            right = "Installed";
            rightColor = TEAL;
            icon = R.drawable.ic_page_check;
            iconColor = TEAL;
        } else if (grey) {
            right = "";
            icon = R.drawable.ic_page_download;
            iconColor = TERTIARY;
        } else {
            right = p.megabytes + " MB";
            icon = R.drawable.ic_page_download;
            iconColor = TEAL;
        }
        setText(r.right, right);
        r.right.setTextColor(rightColor);
        Object tag = r.icon.getTag();
        int iconKey = icon * 31 + iconColor;
        if (!(tag instanceof Integer) || (Integer) tag != iconKey) {
            r.icon.setTag(iconKey);
            r.icon.setImageDrawable(tinted(icon, iconColor));
        }
        r.row.setClickable(!grey || (state == INSTALLING && busyPack == p.pack));
    }

    /** The title as one unbreakable word: Japanese text may otherwise wrap between any two characters. */
    private static String noBreak(String s) {
        StringBuilder o = new StringBuilder();
        for (int i = 0; i < s.length(); ) {
            int cp = s.codePointAt(i);
            if (o.length() > 0) o.append('⁠');
            if (cp == ' ') o.append(' ');
            else o.appendCodePoint(cp);
            i += Character.charCount(cp);
        }
        return o.toString();
    }

    private CharSequence songLines(Pack p, boolean grey) {
        int c1 = grey ? TERTIARY : SECONDARY, c2 = grey ? QUATERNARY : TERTIARY;
        SpannableStringBuilder s = new SpannableStringBuilder();
        for (int k = 0; k < p.songs.size(); k++) {
            String title = p.songs.get(k)[0], latin = p.songs.get(k)[1];
            if (k > 0) s.append('\n');
            int a = s.length();
            s.append(latin.isEmpty() ? title : latin);
            s.setSpan(new ForegroundColorSpan(c1), a, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            if (!latin.isEmpty()) {
                int b = s.length();
                s.append("  ").append(noBreak(title));
                s.setSpan(new ForegroundColorSpan(c2), b, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            }
        }
        return s;
    }

    // ---------------------------------------------------------------- actions

    private AlertDialog.Builder dialog() {
        return new AlertDialog.Builder(act, android.R.style.Theme_DeviceDefault_Dialog_Alert);
    }

    private void alert(String title, String message) {
        dialog().setTitle(title).setMessage(message).setPositiveButton("OK", null).show();
    }

    private boolean refuseWhileBusy() {
        if (!busy()) return false;
        alert("Download in Progress", "Wait for the current download to finish, or cancel it first.");
        return true;
    }

    private void hideKeyboard() {
        InputMethodManager imm = (InputMethodManager) act.getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) imm.hideSoftInputFromWindow(linkField.getWindowToken(), 0);
    }

    /** The edited link: an empty field means the built-in link; anything else must be an http(s) link. */
    private void commitLink() {
        String s = linkField.getText().toString().trim();
        if (s.isEmpty()) s = defaultUrl;
        if (s.equals(url)) {
            linkField.setText(url);
            return;
        }
        if (busy()) {   /* a download started meanwhile: keep the link it uses */
            linkField.setText(url);
            return;
        }
        if (!s.matches("(?i)https?://\\S+") || s.length() >= 512 || !s.matches("\\p{ASCII}+")) {
            alert("Not a Download Link",
                  "Enter the direct http(s) download link of an IPA that includes the DLC packs. The link was not changed.");
            linkField.setText(url);
            return;
        }
        linkField.setText(s);
        act.pageAction("url " + s);
    }

    private void packTapped(int index, View v) {
        if (index < 0 || index >= packs.size()) return;
        Pack p = packs.get(index);
        if (state == INSTALLING && busyPack == p.pack) {
            dialog().setTitle("Cancel " + p.name + "?")
                    .setMessage("The files downloaded so far are removed.")
                    .setPositiveButton("Cancel Download", (d, w) -> act.pageAction("cancel"))
                    .setNegativeButton("Keep Downloading", null)
                    .show();
            return;
        }
        if (!p.available) return;   /* greyed: not in the linked IPA */
        if (refuseWhileBusy() || state != READY) return;
        haptic(v);
        act.pageAction("install " + p.pack);
    }

    @Override
    void onBack() {
        act.pageAction("later");
    }
}

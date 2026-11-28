package io.github.yuridrak.mikuflick;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.util.TypedValue;
import android.view.DisplayCutout;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.view.ContextThemeWrapper;

/**
 * A screen of plain Android views shown over the game (port/src/android.c), the Android
 * counterpart of the UIKit screens on iOS. Pages only show what the native code sends and
 * pass taps back as actions (MikuFlickActivity.pageAction); they run on the UI thread.
 * Only framework widgets and the device's own theme are used, so pages look native on
 * every phone the app supports (Android 7+).
 */
abstract class NativePage {
    static final int TEAL = 0xFF39C5BB;          /* Miku #39C5BB */
    static final int DEEP_TEAL = 0xFF1BB5B0;
    static final int BACKGROUND = 0xFF000000;
    static final int CARD = 0xFF1C1C1E;
    static final int SEPARATOR = 0x33FFFFFF;
    static final int LABEL = 0xFFFFFFFF;
    static final int SECONDARY = 0x99EBEBF5;
    static final int TERTIARY = 0x4DEBEBF5;
    static final int QUATERNARY = 0x2EEBEBF5;
    static final int ORANGE = 0xFFFF9F0A;

    final MikuFlickActivity act;
    final Context ctx;
    private View root;

    NativePage(MikuFlickActivity act) {
        this.act = act;
        this.ctx = new ContextThemeWrapper(act, android.R.style.Theme_DeviceDefault_NoActionBar);
    }

    /** The page's views; root of the page, full screen. */
    abstract View build();

    /** Back key: by default the page's "later" action (Not Now / Cancel / Done). */
    void onBack() {
        act.pageAction("later");
    }

    /** The system file picker's result (MikuFlickActivity.pickIpa); null if cancelled. */
    void onPicked(String action) {
        if (action != null) act.pageAction(action);
    }

    final void attach() {
        root = build();
        root.setClickable(true);   /* no touches reach the game underneath */
        root.setOnApplyWindowInsetsListener((v, in) -> {
            int l = in.getSystemWindowInsetLeft(), t = in.getSystemWindowInsetTop();
            int r = in.getSystemWindowInsetRight(), b = in.getSystemWindowInsetBottom();
            if (Build.VERSION.SDK_INT >= 28) {
                DisplayCutout c = in.getDisplayCutout();
                if (c != null) {
                    l = Math.max(l, c.getSafeInsetLeft());
                    t = Math.max(t, c.getSafeInsetTop());
                    r = Math.max(r, c.getSafeInsetRight());
                    b = Math.max(b, c.getSafeInsetBottom());
                }
            }
            v.setPadding(l, t, r, b);
            return in;
        });
        root.setAlpha(0f);
        act.addContentView(root, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                            ViewGroup.LayoutParams.MATCH_PARENT));
        root.requestApplyInsets();
        root.animate().alpha(1f).setDuration(180).start();
    }

    /** Takes the page away; animated: fades out while the game shows again underneath. */
    final void detach(boolean animated) {
        View v = root;
        root = null;
        if (v == null) return;
        v.setClickable(false);
        v.animate().cancel();
        Runnable gone = () -> {
            ViewParent p = v.getParent();
            if (p instanceof ViewGroup) ((ViewGroup) p).removeView(v);
        };
        if (animated) v.animate().alpha(0f).setDuration(160).withEndAction(gone).start();
        else gone.run();
    }

    // ---------------------------------------------------------------- building blocks

    final int dp(float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, ctx.getResources().getDisplayMetrics()));
    }

    final TextView text(float sp, int color, boolean medium) {
        TextView t = new TextView(ctx);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        t.setTypeface(Typeface.create(medium ? "sans-serif-medium" : "sans-serif", Typeface.NORMAL));
        t.setIncludeFontPadding(true);
        return t;
    }

    final ImageView icon(int res, int color, int sizeDp) {
        ImageView i = new ImageView(ctx);
        i.setImageDrawable(tinted(res, color));
        i.setLayoutParams(new LinearLayout.LayoutParams(dp(sizeDp), dp(sizeDp)));
        return i;
    }

    final Drawable tinted(int res, int color) {
        Drawable d = ctx.getDrawable(res).mutate();
        d.setTint(color);
        return d;
    }

    static GradientDrawable rounded(int fill, float radiusPx) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(radiusPx);
        return g;
    }

    /** Touch feedback over a background (null: transparent, ripple clipped to radiusPx). */
    static Drawable pressable(Drawable bg, float radiusPx) {
        return new RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), bg, rounded(0xFFFFFFFF, radiusPx));
    }

    /** A capsule button: filled (primary) or plain text. */
    final TextView button(String label, boolean primary) {
        TextView b = text(17, primary ? LABEL : 0xCCFFFFFF, true);
        b.setText(label);
        b.setGravity(Gravity.CENTER);
        b.setSingleLine(true);
        float r = dp(28);
        b.setBackground(pressable(primary ? rounded(DEEP_TEAL, r) : null, r));
        b.setClickable(true);
        b.setFocusable(true);
        b.setPadding(dp(20), 0, dp(20), 0);
        return b;
    }

    final void setButtonIcon(TextView b, int res) {
        Drawable d = res != 0 ? tinted(res, LABEL) : null;
        if (d != null) d.setBounds(0, 0, dp(20), dp(20));
        b.setCompoundDrawablesRelative(d, null, null, null);
        b.setCompoundDrawablePadding(dp(10));
    }

    static void haptic(View v) {
        v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }

    static LinearLayout.LayoutParams lp(int w, int h) {
        return new LinearLayout.LayoutParams(w, h);
    }

    static final int MATCH = ViewGroup.LayoutParams.MATCH_PARENT;
    static final int WRAP = ViewGroup.LayoutParams.WRAP_CONTENT;
}

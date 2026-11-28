package io.github.yuridrak.mikuflick;

import android.content.Intent;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.view.KeyEvent;

import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity running the C port (libmain.so).
 *
 * Native pages (NativePage): screens of plain Android views over the game, driven by the native
 * code (port/src/android.c): showPage() / hidePage() / importState() / dlcState() from the SDL thread, and
 * takeAction(), which the native code polls for the page's taps. While a page is up the game
 * draws nothing, so scrolling and animations run at the display's rate without competing with it.
 *
 * Game data import (ImportPage): the page opens the system file picker and passes the chosen
 * file's descriptor to the native code ("fd N"), which reads the zip from it (port/src/ipa.c).
 *
 * DLC page (DlcPage): the title SHOP slot; shows the pack list and download state of the native
 * downloader (port/src/dlcdl.c) and sends the player's choices back.
 *
 * DLC page downloads: the native code (port/src/net.c) reads single DLC packs out of a
 * large IPA with HTTP range requests through httpRange() / httpError(), on its worker thread.
 *
 * Command-line flags of the Linux build can be passed for testing:
 *   adb shell am start -n io.github.yuridrak.mikuflick/.MikuFlickActivity --es args "--song 6 --diff 3 --autoplay"
 */
public class MikuFlickActivity extends SDLActivity {
    private static final int REQUEST_IPA = 0x1B4;
    private static final String TAG = "mikuflick";
    static final int PAGE_IMPORT = 1, PAGE_DLC = 2;   /* android.c ANDROID_PAGE_* */

    private final ConcurrentLinkedQueue<String> pageActions = new ConcurrentLinkedQueue<>();
    private NativePage page;     /* UI thread only */
    private NativePage picking;  /* the page that opened the file picker */

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "SDL2_image", "SDL2_ttf", "main"};
    }

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        String args = intent != null ? intent.getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty()) return new String[0];
        return args.trim().split("\\s+");
    }

    // ---------------------------------------------------------------- native pages

    /** Called from native code (SDL thread): shows a page over the game. */
    public void showPage(int kind, int mode, byte[] filesDir) {
        pageActions.clear();
        runOnUiThread(() -> {
            if (page != null) page.detach(false);
            page = kind == PAGE_IMPORT ? new ImportPage(this, mode) : kind == PAGE_DLC ? new DlcPage(this) : null;
            if (page != null) page.attach();
        });
    }

    /** Called from native code (SDL thread). */
    public void hidePage() {
        runOnUiThread(() -> {
            if (page != null) page.detach(true);
            page = null;
        });
    }

    /** Called from native code (SDL thread): the page's next action, or null. */
    public String takeAction() {
        return pageActions.poll();
    }

    void pageAction(String a) {
        pageActions.add(a);
    }

    /** Called from native code (SDL thread): import screen state (android.c android_import_state). */
    public void importState(int phase, int permille, byte[] file, byte[] err) {
        String f = new String(file, StandardCharsets.UTF_8), e = new String(err, StandardCharsets.UTF_8);
        runOnUiThread(() -> {
            if (page instanceof ImportPage) ((ImportPage) page).setState(phase, permille, f, e);
        });
    }

    /** Called from native code (SDL thread): DLC page state (main.c android_dlc_page). */
    public void dlcState(byte[] state) {
        String s = new String(state, StandardCharsets.UTF_8);
        runOnUiThread(() -> {
            if (page instanceof DlcPage) ((DlcPage) page).setState(s);
        });
    }

    /** Back closes or declines the page instead of reaching the game underneath. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent ev) {
        if (page != null && ev.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (ev.getAction() == KeyEvent.ACTION_UP && !ev.isCanceled()) page.onBack();
            return true;
        }
        return super.dispatchKeyEvent(ev);
    }

    /** UI thread: the system file picker for an IPA; the result goes to the page as "fd N". */
    void pickIpa(ImportPage from) {
        if (picking != null) return;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");   /* IPAs have no reliable MIME type; the importer checks the content */
        try {
            picking = from;
            startActivityForResult(i, REQUEST_IPA);
        } catch (Exception e) {
            Log.w(TAG, "no file picker", e);
            picking = null;
            from.pickerFailed();
        }
    }

    private static final Pattern CONTENT_RANGE = Pattern.compile("bytes\\s+(\\d+)-(\\d+)/(\\d+)");
    private final Object httpLock = new Object();
    private String httpUrl, httpFinalUrl;   /* last link and where its redirects ended */
    private volatile String httpErr = "";

    /** Last httpRange() failure; a leading "!" means retrying will not help. */
    public String httpError() {
        return httpErr;
    }

    /**
     * Called from native code (worker thread): bytes [off, off + len) of url with a range request.
     * Returns 8 bytes of total file size (big-endian) followed by the data, or null (see httpError).
     * Redirects are followed by hand (http/https only); the final URL is reused for the same link.
     */
    public byte[] httpRange(String url, long off, int len) {
        String start;
        synchronized (httpLock) {
            if (!url.equals(httpUrl)) { httpUrl = url; httpFinalUrl = null; }
            start = httpFinalUrl != null ? httpFinalUrl : url;
        }
        try {
            byte[] r = fetchRange(url, start, off, len);
            if (r == null && !start.equals(url) && !httpErr.startsWith("!")) {   /* data server link expired? */
                synchronized (httpLock) { httpFinalUrl = null; }
                r = fetchRange(url, url, off, len);
            }
            return r;
        } catch (OutOfMemoryError e) {
            httpErr = "Out of memory.";
            return null;
        }
    }

    private byte[] fetchRange(String link, String url, long off, int len) {
        HttpURLConnection c = null;
        try {
            for (int hop = 0; hop < 8; hop++) {
                URL u = new URL(url);
                String proto = u.getProtocol().toLowerCase(Locale.ROOT);
                if (!proto.equals("http") && !proto.equals("https")) { httpErr = "!Unsupported link."; return null; }
                c = (HttpURLConnection) u.openConnection();
                c.setInstanceFollowRedirects(false);
                c.setConnectTimeout(20000);
                c.setReadTimeout(30000);
                c.setRequestProperty("Range", "bytes=" + off + "-" + (off + len - 1));
                c.setRequestProperty("Accept-Encoding", "identity");
                int status = c.getResponseCode();
                if (status >= 300 && status < 400) {
                    String loc = c.getHeaderField("Location");
                    c.disconnect();
                    c = null;
                    if (loc == null) { httpErr = "Bad redirect."; return null; }
                    url = new URL(u, loc).toString();
                    continue;
                }
                if (status == 200) {
                    httpErr = "!This link does not lead to an IPA file the app can read in parts (the server sent the whole file).";
                    return null;
                }
                if (status != 206) {
                    httpErr = (status >= 400 && status < 500 ? "!" : "") + "The server answered HTTP " + status + ".";
                    return null;
                }
                Matcher m = CONTENT_RANGE.matcher(String.valueOf(c.getHeaderField("Content-Range")));
                if (!m.find() || Long.parseLong(m.group(1)) != off || Long.parseLong(m.group(2)) != off + len - 1) {
                    httpErr = "!The server sent the wrong part of the file.";
                    return null;
                }
                long total = Long.parseLong(m.group(3));
                byte[] out = new byte[8 + len];
                for (int i = 0; i < 8; i++) out[i] = (byte) (total >>> (56 - 8 * i));
                int got = 0;
                try (InputStream in = c.getInputStream()) {
                    while (got < len) {
                        int k = in.read(out, 8 + got, len - got);
                        if (k < 0) break;
                        got += k;
                    }
                }
                if (got != len) { httpErr = "Download interrupted."; return null; }
                synchronized (httpLock) { if (link.equals(httpUrl)) httpFinalUrl = url; }
                return out;
            }
            httpErr = "Too many redirects.";
            return null;
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "download " + off + "+" + len, e);
            httpErr = "Network error: " + e.getMessage();
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_IPA) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        NativePage from = picking;
        picking = null;
        Uri uri = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        String action = null;   /* cancelled: the page stays as it was */
        if (uri != null) {
            try {
                ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
                action = pfd != null ? "fd " + pfd.detachFd() : "fdfail";   /* native code closes it */
            } catch (Exception e) {
                Log.w(TAG, "cannot open " + uri, e);
                action = "fdfail";
            }
        }
        if (from != null && from == page) from.onPicked(action);
        else if (action != null && action.startsWith("fd ")) {   /* the page is gone: don't leak the file */
            try {
                ParcelFileDescriptor.adoptFd(Integer.parseInt(action.substring(3))).close();
            } catch (Exception ignored) {
            }
        }
    }
}

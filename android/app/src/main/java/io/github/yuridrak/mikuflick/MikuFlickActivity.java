package io.github.yuridrak.mikuflick;

import android.content.Intent;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.view.KeyEvent;

import java.nio.charset.StandardCharsets;
import java.util.concurrent.ConcurrentLinkedQueue;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity running the C port (libmain.so).
 *
 * Native pages (NativePage): screens of plain Android views over the game, driven by the native
 * code (port/src/android.c): showPage() / hidePage() / importState() from the SDL thread, and
 * takeAction(), which the native code polls for the page's taps. While a page is up the game
 * draws nothing, so scrolling and animations run at the display's rate without competing with it.
 *
 * Game data import (ImportPage): the page opens the system file picker and passes the chosen
 * file's descriptor to the native code ("fd N"), which reads the zip from it (port/src/ipa.c).
 *
 * Command-line flags of the Linux build can be passed for testing:
 *   adb shell am start -n io.github.yuridrak.mikuflick/.MikuFlickActivity --es args "--song 6 --diff 3 --autoplay"
 */
public class MikuFlickActivity extends SDLActivity {
    private static final int REQUEST_IPA = 0x1B4;
    private static final String TAG = "mikuflick";
    static final int PAGE_IMPORT = 1;   /* android.c ANDROID_PAGE_* */

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
        String dir = new String(filesDir, StandardCharsets.UTF_8);
        pageActions.clear();
        runOnUiThread(() -> {
            if (page != null) page.detach(false);
            page = kind == PAGE_IMPORT ? new ImportPage(this, mode, dir) : null;
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

package io.github.yuridrak.mikuflick;

import android.content.Intent;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity running the C port (libmain.so).
 *
 * Game data import: the native import screen calls pickIpa() to open the system
 * file picker and polls takePickedFd() until the player has chosen their IPA; the
 * native code then reads the zip from that file descriptor (port/src/ipa.c).
 *
 * Command-line flags of the Linux build can be passed for testing:
 *   adb shell am start -n io.github.yuridrak.mikuflick/.MikuFlickActivity --es args "--song 6 --diff 3 --autoplay"
 */
public class MikuFlickActivity extends SDLActivity {
    private static final int REQUEST_IPA = 0x1B4;
    private static final String TAG = "mikuflick";

    /** -1 = picker open / nothing yet, -2 = cancelled or failed, >= 0 = fd of the chosen file */
    private volatile int pickedFd = -1;

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

    /** Called from native code (SDL thread). */
    public void pickIpa() {
        pickedFd = -1;
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");   /* IPAs have no reliable MIME type; the importer checks the content */
            try {
                startActivityForResult(i, REQUEST_IPA);
            } catch (Exception e) {
                Log.w(TAG, "no file picker", e);
                pickedFd = -2;
            }
        });
    }

    /** Called from native code: returns the picker result once, then resets. */
    public int takePickedFd() {
        int fd = pickedFd;
        if (fd != -1) pickedFd = -1;
        return fd;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_IPA) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        Uri uri = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (uri == null) {
            pickedFd = -2;
            return;
        }
        try {
            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
            pickedFd = pfd != null ? pfd.detachFd() : -2;   /* native code closes it */
        } catch (Exception e) {
            Log.w(TAG, "cannot open " + uri, e);
            pickedFd = -2;
        }
    }
}

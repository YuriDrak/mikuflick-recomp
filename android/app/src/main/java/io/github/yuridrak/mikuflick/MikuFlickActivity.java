package io.github.yuridrak.mikuflick;

import android.content.Intent;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity running the C port (libmain.so). Command-line flags of the
 * Linux build can be passed for testing:
 *   adb shell am start -n io.github.yuridrak.mikuflick/.MikuFlickActivity --es args "--song 6 --diff 3 --autoplay"
 */
public class MikuFlickActivity extends SDLActivity {
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
}

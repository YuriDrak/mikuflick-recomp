/* Loads textures/atlases and sound effects straight from the user's extracted .app. */
#include "mf.h"

#include <stdio.h>
#include <string.h>

static bool must(bool ok, const char *what) {
    if (!ok) log_msg("missing asset: %s", what);
    return ok;
}

bool assets_load(Assets *as, SDL_Renderer *ren, const GameData *gd, const char *appdir) {
    memset(as, 0, sizeof *as);
    as->appdir = appdir;
    bool ok = true;
    ok &= must(atlas_load(&as->game1, ren, appdir, "game_tex_01"), "game_tex_01");
    ok &= must(atlas_load(&as->game2, ren, appdir, "game_tex_02"), "game_tex_02");
    ok &= must(atlas_load(&as->effect1, ren, appdir, "game_effect_01"), "game_effect_01");
    ok &= must(atlas_load(&as->ui1, ren, appdir, "UI_tex_01"), "UI_tex_01");
    ok &= must(atlas_load(&as->ui2, ren, appdir, "UI_tex_02"), "UI_tex_02");
    ok &= must(atlas_load(&as->ui5, ren, appdir, "UI_tex_05"), "UI_tex_05");
    ok &= must(atlas_load(&as->bg_title, ren, appdir, "bg_tex_01"), "bg_tex_01");
    ok &= must(atlas_load(&as->bg_select, ren, appdir, "bg_tex_02"), "bg_tex_02");
    ok &= must(atlas_load(&as->bg_result, ren, appdir, "bg_tex_03"), "bg_tex_03");
    static const char *kana[4] = {"mf_hiragana_blank", "mf_hiragana_telop", "mf_katakana_blank", "mf_katakana_telop"};
    for (int i = 0; i < 4; i++) ok &= must(atlas_load(&as->kana_tex[i], ren, appdir, kana[i]), kana[i]);
    static const char *logos[3] = {"logo_sega", "logo_crypton", "logo_cri"};
    for (int i = 0; i < 3; i++) atlas_load(&as->logos[i], ren, appdir, logos[i]); /* optional */
    atlas_load(&as->launch, ren, appdir, "LaunchImage@2x");

    for (int i = 0; i < NUM_SOUNDS; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.caf", appdir, gd->sound[i]);
        if (media_decode_audio(path, &as->sounds[i], 1) < 1) log_msg("sound %d (%s) not loaded", i, gd->sound[i]);
    }
    return ok;
}

void assets_free(Assets *as) {
    Atlas *all[] = {&as->game1, &as->game2, &as->effect1, &as->ui1, &as->ui2, &as->ui5, &as->bg_title,
                    &as->bg_select, &as->bg_result, &as->kana_tex[0], &as->kana_tex[1], &as->kana_tex[2],
                    &as->kana_tex[3], &as->logos[0], &as->logos[1], &as->logos[2], &as->launch};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) atlas_free(all[i]);
    for (int i = 0; i < NUM_SOUNDS; i++) pcm_free(&as->sounds[i]);
}

static float g_se_volume = 1.0f;

void play_sound(Assets *as, int id) {
    if (id < 0 || id >= NUM_SOUNDS) return;
    mixer_sfx_play(&as->sounds[id], g_se_volume, false);
}

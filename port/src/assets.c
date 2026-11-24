/* Loads textures/atlases and sound effects straight from the user's extracted .app. */
#include "mf.h"

#include <stdio.h>
#include <stdlib.h>
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
    /* options screen (WindowOption loadTexture: bg_tex_04, UI_tex_03/04) */
    atlas_load(&as->ui3, ren, appdir, "UI_tex_03");
    atlas_load(&as->ui4, ren, appdir, "UI_tex_04");
    atlas_load(&as->bg_options, ren, appdir, "bg_tex_04");
    atlas_load(&as->help[0], ren, appdir, "help_01");
    atlas_load(&as->help[1], ren, appdir, "help_02");
    atlas_load(&as->credits, ren, appdir, "credits_en");
    atlas_load(&as->saisei, ren, appdir, "saisei");
    /* KEYBOARD variants (SystemTexture LoadBoardTex): same layout as game_tex_02, no plist of their own */
    static const char *kb[2] = {"game_tex_02_fullroman", "game_tex_02_romanhira"};
    for (int i = 0; i < 2; i++)
        if (atlas_load(&as->game2_kb[i], ren, appdir, kb[i]) && as->game2.nframes) {
            as->game2_kb[i].frames = malloc(sizeof(Frame) * (size_t)as->game2.nframes);
            if (as->game2_kb[i].frames) {
                memcpy(as->game2_kb[i].frames, as->game2.frames, sizeof(Frame) * (size_t)as->game2.nframes);
                as->game2_kb[i].nframes = as->game2.nframes;
            }
        }

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
                    &as->kana_tex[3], &as->logos[0], &as->logos[1], &as->logos[2], &as->launch, &as->ui3,
                    &as->ui4, &as->bg_options, &as->help[0], &as->help[1], &as->credits, &as->game2_kb[0],
                    &as->game2_kb[1], &as->saisei};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) atlas_free(all[i]);
    for (int i = 0; i < NUM_SOUNDS; i++) pcm_free(&as->sounds[i]);
}

static float g_se_volume = 1.0f;

void play_sound(Assets *as, int id) {
    if (id < 0 || id >= NUM_SOUNDS) return;
    mixer_sfx_play(&as->sounds[id], g_se_volume, false);
}

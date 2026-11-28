/*
 * dlcdl - background DLC pack downloads for the DLC page (title SHOP slot).
 *
 * One worker thread at a time: "list" opens the link (net.c) and reads the pack list of the IPA
 * behind it (ipa.c dlc_catalog_open: zip directory + the original save); "install" fetches one
 * pack into the game data folder and rewrites gamedata.txt. The catalog stays open between the
 * two, so picking several packs in a row reads the directory only once. The UI polls
 * dlcdl_status; dlcdl_take_changed tells the main loop to reload the song list.
 */
#include "mf.h"
#include "dlc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    SDL_mutex *mu;
    SDL_Thread *th;
    volatile int cancel;
    char app[1200], gamedata[1200];
    char url[1024];
    int job_pack;            /* -1 = list */
    ZipSrc *src;             /* owned by the worker while it runs */
    DlcCatalog *cat;
    DlcDlStatus st;
    bool changed;
    bool detached;           /* left running at quit: its data is not freed */
} D;

/* the packs the game offered (dlc.c) that the linked IPA does not have: listed, not available */
static void add_known(DlcDlStatus *st) {
    int n;
    const DlcKnownSong *k = dlc_known_songs(&n);
    for (int i = 0; i < n; i++) {
        int p = 0;
        while (p < st->npacks && st->packs[p].pack != k[i].pack) p++;
        if (p == st->npacks) {
            if (p >= DLC_MAX_PACKS) continue;
            memset(&st->packs[p], 0, sizeof st->packs[p]);
            st->packs[p].pack = k[i].pack;
            snprintf(st->packs[p].name, sizeof st->packs[p].name, "%s", k[i].pack_name);
            st->npacks++;
        } else if (st->packs[p].available) {
            continue;
        }
        DlcPackInfo *pi = &st->packs[p];
        if (pi->nsongs < DLC_PACK_SONGS) {
            DlcPackSong *s = &pi->songs[pi->nsongs];
            snprintf(s->title, sizeof s->title, "%s", k[i].title);
            dlc_song_latin(k[i].movie, k[i].title, s->latin, sizeof s->latin);
        }
        pi->nsongs++;
    }
    for (int a = 1; a < st->npacks; a++)   /* by pack number */
        for (int b = a; b > 0 && st->packs[b - 1].pack > st->packs[b].pack; b--) {
            DlcPackInfo t = st->packs[b];
            st->packs[b] = st->packs[b - 1];
            st->packs[b - 1] = t;
        }
}

static void lock(void) { if (D.mu) SDL_LockMutex(D.mu); }
static void unlock(void) { if (D.mu) SDL_UnlockMutex(D.mu); }

void dlcdl_init(const char *app_dir, const char *gamedata) {
    if (!D.mu) D.mu = SDL_CreateMutex();
    snprintf(D.app, sizeof D.app, "%s", app_dir);
    snprintf(D.gamedata, sizeof D.gamedata, "%s", gamedata);
    D.st.busy_pack = -1;
    lock();
    D.st.npacks = 0;
    add_known(&D.st);
    unlock();
}

static void progress(void *ud, double frac, const char *file) {
    (void)ud;
    (void)file;
    lock();
    D.st.frac = frac;
    unlock();
}

/* a read error from the network says more than "could not extract x: archive truncated" */
static void explain(char *err, size_t cap) {
    const char *ne = net_error(D.src);
    if (D.cancel) snprintf(err, cap, "Cancelled.");
    else if (ne && strcmp(ne, "Cancelled.")) snprintf(err, cap, "Download failed: %s", ne);
}

static int SDLCALL worker(void *ud) {
    (void)ud;
    char err[240] = "";
    if (D.job_pack < 0) {
        ZipSrc *src = net_open(D.url, &D.cancel, err, sizeof err);
        DlcCatalog *cat = NULL;
        if (src) {
            D.src = src;
            cat = dlc_catalog_open(src, err, sizeof err);
            if (!cat) {
                explain(err, sizeof err);
                net_close(src);
                D.src = NULL;
            }
        }
        lock();
        D.cat = cat;
        int ncat = cat ? dlc_catalog_packs(cat, D.st.packs, DLC_MAX_PACKS) : 0;
        for (int i = 0; i < ncat; i++) D.st.packs[i].available = true;
        D.st.npacks = ncat;
        add_known(&D.st);
        D.st.state = cat ? DLD_READY : DLD_FAILED;
        D.st.error = !cat || !ncat;
        if (cat && !ncat) snprintf(D.st.msg, sizeof D.st.msg, "No complete DLC packs in this IPA.");
        else snprintf(D.st.msg, sizeof D.st.msg, "%s", cat ? "" : err);
        unlock();
        if (cat) log_msg("dlc: %d packs available at %s", ncat, D.url);
        else log_msg("dlc: %s", err);
    } else {
        bool ok = dlc_catalog_install(D.cat, D.job_pack, D.app, D.gamedata, progress, NULL, &D.cancel, err, sizeof err);
        if (!ok) explain(err, sizeof err);
        char name[48] = "";
        for (int i = 0; i < D.st.npacks; i++)
            if (D.st.packs[i].pack == D.job_pack) snprintf(name, sizeof name, "%s", D.st.packs[i].name);
        lock();
        D.st.state = DLD_READY;
        D.st.busy_pack = -1;
        D.st.error = !ok;
        if (ok) snprintf(D.st.msg, sizeof D.st.msg, "Installed %s.", name);
        else snprintf(D.st.msg, sizeof D.st.msg, "%s", err);
        if (ok) D.changed = true;
        unlock();
        if (!ok) log_msg("dlc: pack %d: %s", D.job_pack, err);
    }
    return 0;
}

static bool busy(void) {
    lock();
    bool b = D.st.state == DLD_LISTING || D.st.state == DLD_INSTALLING;
    unlock();
    return b;
}

static bool start(int pack) {
    if (D.th) { SDL_WaitThread(D.th, NULL); D.th = NULL; }   /* finished: its state is final */
    D.cancel = 0;
    D.job_pack = pack;
    D.th = SDL_CreateThread(worker, "dlc-download", NULL);
    if (!D.th) {
        lock();
        D.st.state = pack < 0 ? DLD_FAILED : DLD_READY;
        D.st.busy_pack = -1;
        D.st.error = true;
        snprintf(D.st.msg, sizeof D.st.msg, "Could not start the download.");
        unlock();
        return false;
    }
    return true;
}

bool dlcdl_list(const char *url) {
    if (!D.mu || busy()) return false;
    if (D.th) { SDL_WaitThread(D.th, NULL); D.th = NULL; }
    dlc_catalog_free(D.cat);   /* no worker now: safe to drop the previous link */
    D.cat = NULL;
    net_close(D.src);
    D.src = NULL;
    snprintf(D.url, sizeof D.url, "%s", url);
    lock();
    D.st.state = DLD_LISTING;
    D.st.npacks = 0;
    add_known(&D.st);
    D.st.busy_pack = -1;
    D.st.error = false;
    D.st.msg[0] = 0;
    unlock();
    return start(-1);
}

bool dlcdl_install(int pack) {
    if (!D.mu || busy() || !D.cat) return false;
    lock();
    D.st.state = DLD_INSTALLING;
    D.st.busy_pack = pack;
    D.st.frac = 0;
    D.st.error = false;
    D.st.msg[0] = 0;
    unlock();
    return start(pack);
}

void dlcdl_cancel(void) { D.cancel = 1; }

void dlcdl_status(DlcDlStatus *out) {
    if (!D.mu) { memset(out, 0, sizeof *out); out->busy_pack = -1; return; }
    lock();
    *out = D.st;
    unlock();
}

bool dlcdl_take_changed(void) {
    lock();
    bool c = D.changed;
    D.changed = false;
    unlock();
    return c;
}

void dlcdl_quit(void) {
    if (!D.mu) return;
    if (busy()) {
        D.cancel = 1;
#if defined(__ANDROID__) || defined(MF_IOS)
        /* a blocking platform request may take a while to time out; the process is ending anyway */
        if (D.th) SDL_DetachThread(D.th);
        D.th = NULL;
        D.detached = true;
        return;
#endif
    }
    if (D.th) SDL_WaitThread(D.th, NULL);   /* desktop: curl is stopped within a fraction of a second */
    D.th = NULL;
    dlc_catalog_free(D.cat);
    D.cat = NULL;
    net_close(D.src);
    D.src = NULL;
}

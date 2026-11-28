/*
 * net - an http(s) URL as a random-access ZipSrc, for installing single DLC packs out of a
 * multi-GB IPA without downloading all of it (ipa.c dlc_catalog_*).
 *
 * Every read is an HTTP range request ("Range: bytes=a-b", the server must answer 206 with a
 * matching Content-Range). Requests start small (256 KiB, for the zip directory and local
 * headers) and double while the reads stay sequential, up to 8 MiB per request; two ranges
 * are cached. A failed request is retried a few times. The final URL after redirects is reused
 * (archive.org sends every request on to one of its data servers).
 *
 * Transport: desktop runs the curl tool (posix_spawnp, no shell, http/https only), Android calls
 * MikuFlickActivity.httpRange (Java HttpURLConnection), iOS ios_http_range (NSURLSession).
 * Nothing is written to disk here.
 */
#define _FILE_OFFSET_BITS 64
#include "mf.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MIN_REQ (256u << 10)
#define MAX_REQ (8u << 20)
#define SLOTS 2
#define URL_MAX 1024

enum { HTTP_OK = 1, HTTP_RETRY = 0, HTTP_FATAL = -1 };

typedef struct {
    ZipSrc z;
    char url[URL_MAX];
    char final_url[URL_MAX];   /* after redirects; "" = not known */
    const volatile int *cancel;
    uint8_t *buf[SLOTS];
    uint64_t base[SLOTS];
    size_t len[SLOTS];
    unsigned used[SLOTS], clock;
    uint64_t next_seq;         /* end of the last request */
    size_t last_req;
    bool have_size;
    char err[240];
} Net;

bool net_url_ok(const char *url) {
    if (strncasecmp(url, "http://", 7) && strncasecmp(url, "https://", 8)) return false;
    size_t n = strlen(url);
    if (n < 12 || n >= URL_MAX) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p <= 0x20 || *p >= 0x7f || *p == '"' || *p == '\\') return false;   /* encoded URLs only */
    return true;
}

static bool cancelled(const Net *n) { return n->cancel && *n->cancel; }

#if defined(__ANDROID__)
/* ---------------- Android: MikuFlickActivity.httpRange ---------------- */
#include <jni.h>

static int http_range(Net *n, const char *url, uint64_t off, size_t len, uint8_t *dst, uint64_t *total, char *redirect,
                      size_t redirect_cap) {
    (void)redirect_cap;
    redirect[0] = 0;   /* the Java side follows and remembers redirects itself */
    JNIEnv *env = SDL_AndroidGetJNIEnv();   /* attaches this worker thread if needed */
    jobject act = SDL_AndroidGetActivity();
    if (!env || !act) { snprintf(n->err, sizeof n->err, "No Java environment."); return HTTP_FATAL; }
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, cls, "httpRange", "(Ljava/lang/String;JI)[B");
    jmethodID me = (*env)->GetMethodID(env, cls, "httpError", "()Ljava/lang/String;");
    int rc = HTTP_FATAL;
    if (!m || !me) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        snprintf(n->err, sizeof n->err, "Downloads are not supported by this app build.");
        goto out;
    }
    jstring ju = (*env)->NewStringUTF(env, url);
    jbyteArray arr = ju ? (jbyteArray)(*env)->CallObjectMethod(env, act, m, ju, (jlong)off, (jint)len) : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); arr = NULL; }
    if (ju) (*env)->DeleteLocalRef(env, ju);
    if (!arr) {
        jstring je = (jstring)(*env)->CallObjectMethod(env, act, me);
        if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); je = NULL; }
        const char *s = je ? (*env)->GetStringUTFChars(env, je, NULL) : NULL;
        /* "!" prefix: retrying will not help (no range support, HTTP 4xx...) */
        rc = s && s[0] == '!' ? HTTP_FATAL : HTTP_RETRY;
        snprintf(n->err, sizeof n->err, "%s", s ? s + (s[0] == '!') : "Download failed.");
        if (s) (*env)->ReleaseStringUTFChars(env, je, s);
        if (je) (*env)->DeleteLocalRef(env, je);
        goto out;
    }
    jsize got = (*env)->GetArrayLength(env, arr);
    if ((size_t)got != len + 8) {
        snprintf(n->err, sizeof n->err, "Incomplete download.");
        rc = HTTP_RETRY;
    } else {
        jbyte head[8];
        (*env)->GetByteArrayRegion(env, arr, 0, 8, head);
        uint64_t t = 0;
        for (int i = 0; i < 8; i++) t = t << 8 | (uint8_t)head[i];
        *total = t;
        (*env)->GetByteArrayRegion(env, arr, 8, (jsize)len, (jbyte *)dst);
        rc = HTTP_OK;
    }
    (*env)->DeleteLocalRef(env, arr);
out:
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return rc;
}

#elif defined(MF_IOS)
/* ---------------- iOS: NSURLSession (ios/App/Platform.m) ---------------- */
int ios_http_range(const char *url, uint64_t off, size_t len, uint8_t *dst, uint64_t *total, char *final_url,
                   size_t final_cap, char *err, size_t errlen);

static int http_range(Net *n, const char *url, uint64_t off, size_t len, uint8_t *dst, uint64_t *total, char *redirect,
                      size_t redirect_cap) {
    return ios_http_range(url, off, len, dst, total, redirect, redirect_cap, n->err, sizeof n->err);
}

#else
/* "bytes a-b/total" */
static bool parse_content_range(const char *v, uint64_t *a, uint64_t *b, uint64_t *total) {
    unsigned long long x, y, t;
    while (*v == ' ') v++;
    if (strncasecmp(v, "bytes", 5)) return false;
    if (sscanf(v + 5, " %llu-%llu/%llu", &x, &y, &t) != 3 || y < x || y >= t) return false;
    *a = x;
    *b = y;
    *total = t;
    return true;
}

/* ---------------- desktop: the curl tool ---------------- */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* ends of the header block starting at p (or NULL) */
static const char *block_end(const char *p, const char *end) {
    for (; p + 3 < end; p++)
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') return p + 4;
    return NULL;
}

/* value of header `name` inside one block, copied to out */
static bool header(const char *blk, const char *blk_end, const char *name, char *out, size_t cap) {
    size_t nl = strlen(name);
    for (const char *l = blk; l < blk_end;) {
        const char *e = l;
        while (e < blk_end && *e != '\r' && *e != '\n') e++;
        if ((size_t)(e - l) > nl && !strncasecmp(l, name, nl) && l[nl] == ':') {
            const char *v = l + nl + 1;
            while (v < e && *v == ' ') v++;
            snprintf(out, cap, "%.*s", (int)(e - v), v);
            return true;
        }
        l = e;
        while (l < blk_end && (*l == '\r' || *l == '\n')) l++;
    }
    return false;
}

/* read some output; false at EOF/error or when cancelled (then curl is stopped) */
static ssize_t pipe_read(Net *n, int fd, pid_t pid, void *dst, size_t cap) {
    for (;;) {
        if (cancelled(n)) { kill(pid, SIGTERM); errno = ECANCELED; return -1; }
        struct pollfd pf = {fd, POLLIN, 0};
        int r = poll(&pf, 1, 250);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) return -1;
        if (r == 0) continue;
        ssize_t k = read(fd, dst, cap);
        if (k < 0 && errno == EINTR) continue;
        return k;
    }
}

static int http_range(Net *n, const char *url, uint64_t off, size_t len, uint8_t *dst, uint64_t *total, char *redirect,
                      size_t redirect_cap) {
    char range[64];
    snprintf(range, sizeof range, "%llu-%llu", (unsigned long long)off, (unsigned long long)(off + len - 1));
    char *argv[] = {"curl", "-sS", "-L", "--max-redirs", "8", "--proto", "=http,https", "--proto-redir", "=http,https",
                    "--connect-timeout", "20", "--speed-limit", "1024", "--speed-time", "60", "-r", range, "-D", "-",
                    "--suppress-connect-headers", (char *)url, NULL};   /* no proxy CONNECT reply in -D */
    int pfd[2];
    if (pipe(pfd) != 0) { snprintf(n->err, sizeof n->err, "pipe: %s", strerror(errno)); return HTTP_FATAL; }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    posix_spawn_file_actions_addclose(&fa, pfd[0]);
    posix_spawn_file_actions_addclose(&fa, pfd[1]);
    /* the worker is an SDL thread, which blocks SIGTERM and friends; curl must not inherit that, or the
       SIGTERM of a cancel does nothing */
    posix_spawnattr_t sa;
    posix_spawnattr_init(&sa);
    sigset_t none, dfl;
    sigemptyset(&none);
    sigemptyset(&dfl);
    sigaddset(&dfl, SIGTERM);
    sigaddset(&dfl, SIGINT);
    sigaddset(&dfl, SIGPIPE);
    posix_spawnattr_setsigmask(&sa, &none);
    posix_spawnattr_setsigdefault(&sa, &dfl);
    posix_spawnattr_setflags(&sa, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    pid_t pid;
    int se = posix_spawnp(&pid, "curl", &fa, &sa, argv, environ);
    posix_spawnattr_destroy(&sa);
    posix_spawn_file_actions_destroy(&fa);
    close(pfd[1]);
    if (se != 0) {
        close(pfd[0]);
        snprintf(n->err, sizeof n->err, "Downloads need the curl tool (%s).", strerror(se));
        return HTTP_FATAL;
    }
    int rc = HTTP_RETRY;
    char hdr[16384];
    size_t hn = 0, pos = 0, body = 0;
    redirect[0] = 0;
    /* header blocks: 1xx/3xx ones (-L follows redirects) until the final response */
    for (;;) {
        const char *e = block_end(hdr + pos, hdr + hn);
        if (!e) {
            if (hn == sizeof hdr) { snprintf(n->err, sizeof n->err, "Bad server response."); goto stop; }
            ssize_t k = pipe_read(n, pfd[0], pid, hdr + hn, sizeof hdr - hn);
            if (k <= 0) {
                snprintf(n->err, sizeof n->err, k < 0 && errno == ECANCELED ? "Cancelled." : "No response from the server.");
                goto stop;
            }
            hn += (size_t)k;
            continue;
        }
        const char *blk = hdr + pos;
        int status = 0;
        if (strncmp(blk, "HTTP/", 5) || sscanf(blk, "HTTP/%*s %d", &status) != 1) {
            snprintf(n->err, sizeof n->err, "Bad server response.");
            goto stop;
        }
        pos = (size_t)(e - hdr);
        if (status < 200 || (status >= 300 && status < 400)) {
            char loc[URL_MAX];
            if (status >= 300 && header(blk, e, "location", loc, sizeof loc) && net_url_ok(loc))
                snprintf(redirect, redirect_cap, "%s", loc);
            continue;
        }
        if (status == 200) {
            snprintf(n->err, sizeof n->err, "!This link does not lead to an IPA file the app can read in parts "
                                            "(the server sent the whole file).");
            rc = HTTP_FATAL;
            goto stop;
        }
        if (status != 206) {
            snprintf(n->err, sizeof n->err, "%sThe server answered HTTP %d.", status >= 400 && status < 500 ? "!" : "", status);
            rc = status >= 400 && status < 500 ? HTTP_FATAL : HTTP_RETRY;
            goto stop;
        }
        char cr[128];
        uint64_t a, b;
        if (!header(blk, e, "content-range", cr, sizeof cr) || !parse_content_range(cr, &a, &b, total) || a != off ||
            b != off + len - 1) {
            snprintf(n->err, sizeof n->err, "!The server sent the wrong part of the file.");
            rc = HTTP_FATAL;
            goto stop;
        }
        break;
    }
    body = hn - pos;
    if (body > len) { snprintf(n->err, sizeof n->err, "The server sent too much data."); goto stop; }
    memcpy(dst, hdr + pos, body);
    while (body < len) {
        ssize_t k = pipe_read(n, pfd[0], pid, dst + body, len - body);
        if (k <= 0) {
            snprintf(n->err, sizeof n->err, k < 0 && errno == ECANCELED ? "Cancelled." : "Download interrupted.");
            goto stop;
        }
        body += (size_t)k;
    }
    char extra;
    ssize_t x = pipe_read(n, pfd[0], pid, &extra, 1);   /* expect EOF */
    if (x != 0) {
        snprintf(n->err, sizeof n->err, x > 0 ? "The server sent too much data." : errno == ECANCELED ? "Cancelled." : "Download interrupted.");
        goto stop;
    }
    rc = HTTP_OK;
stop:
    if (rc != HTTP_OK) kill(pid, SIGTERM);
    close(pfd[0]);
    int ws = 0;
    while (waitpid(pid, &ws, 0) < 0 && errno == EINTR) {}
    if (rc == HTTP_OK && !(WIFEXITED(ws) && WEXITSTATUS(ws) == 0)) {
        snprintf(n->err, sizeof n->err, "Download failed (curl exit %d).", WIFEXITED(ws) ? WEXITSTATUS(ws) : -1);
        rc = HTTP_RETRY;
    }
    if (n->err[0] == '!') memmove(n->err, n->err + 1, strlen(n->err));
    return rc;
}
#endif

/* one range request, with retries; on success the total size of the file is in *total */
static bool fetch(Net *n, uint64_t off, size_t len, uint8_t *dst, uint64_t *total) {
    static const int wait_ms[] = {0, 1000, 3000, 8000};
    for (int attempt = 0; attempt < 4; attempt++) {
        for (int t = 0; t < wait_ms[attempt]; t += 100) {
            if (cancelled(n)) break;
            SDL_Delay(100);
        }
        if (cancelled(n)) { snprintf(n->err, sizeof n->err, "Cancelled."); return false; }
        n->err[0] = 0;
        char redirect[URL_MAX] = "";
        int rc = http_range(n, n->final_url[0] ? n->final_url : n->url, off, len, dst, total, redirect, sizeof redirect);
        if (rc == HTTP_OK) {
            if (redirect[0] && !n->final_url[0]) snprintf(n->final_url, sizeof n->final_url, "%s", redirect);
            return true;
        }
        if (cancelled(n)) { snprintf(n->err, sizeof n->err, "Cancelled."); return false; }
        log_msg("net: bytes %llu+%zu: %s", (unsigned long long)off, len, n->err);
        if (n->final_url[0]) { n->final_url[0] = 0; continue; }   /* data server link may have expired */
        if (rc == HTTP_FATAL) return false;
    }
    return false;
}

static bool net_read(void *ud, uint64_t off, void *dst, size_t len) {
    Net *n = ud;
    uint8_t *d = dst;
    if (off > n->z.size || len > n->z.size - off) return false;
    while (len) {
        int s = -1;
        for (int i = 0; i < SLOTS && s < 0; i++)
            if (n->len[i] && off >= n->base[i] && off - n->base[i] < n->len[i]) s = i;
        if (s < 0) {
            s = n->used[0] <= n->used[1] ? 0 : 1;
            size_t want = off == n->next_seq && n->last_req ? n->last_req * 2 : MIN_REQ;
            if (len > want) want = len;
            if (want > MAX_REQ) want = MAX_REQ;
            if (want > n->z.size - off) want = (size_t)(n->z.size - off);
            uint64_t total = 0;
            n->len[s] = 0;
            if (!fetch(n, off, want, n->buf[s], &total)) return false;
            if (total != n->z.size) { snprintf(n->err, sizeof n->err, "The file on the server has changed."); return false; }
            n->base[s] = off;
            n->len[s] = want;
            n->next_seq = off + want;
            n->last_req = want;
        }
        n->used[s] = ++n->clock;
        size_t in = (size_t)(off - n->base[s]), k = n->len[s] - in;
        if (k > len) k = len;
        memcpy(d, n->buf[s] + in, k);
        d += k;
        off += k;
        len -= k;
    }
    return true;
}

ZipSrc *net_open(const char *url, const volatile int *cancel, char *err, size_t errlen) {
    if (!net_url_ok(url)) {
        snprintf(err, errlen, "Not a usable link. Paste the direct http(s) download link of the .ipa file.");
        return NULL;
    }
    Net *n = calloc(1, sizeof *n);
    if (!n) { snprintf(err, errlen, "Out of memory."); return NULL; }
    for (int i = 0; i < SLOTS; i++)
        if (!(n->buf[i] = malloc(MAX_REQ))) { net_close(&n->z); snprintf(err, errlen, "Out of memory."); return NULL; }
    snprintf(n->url, sizeof n->url, "%s", url);
    n->cancel = cancel;
    n->z.read = net_read;
    n->z.ud = n;
    uint8_t first = 0;
    uint64_t total = 0;
    if (!fetch(n, 0, 1, &first, &total)) {
        snprintf(err, errlen, "%s", n->err[0] ? n->err : "Download failed.");
        net_close(&n->z);
        return NULL;
    }
    if (first != 'P' || total < 22) {
        snprintf(err, errlen, "This link does not lead to a zip/IPA file.");
        net_close(&n->z);
        return NULL;
    }
    n->z.size = total;
    log_msg("net: %s: %llu MB%s%s", url, (unsigned long long)(total >> 20), n->final_url[0] ? " via " : "", n->final_url);
    return &n->z;
}

void net_close(ZipSrc *z) {
    if (!z) return;
    Net *n = z->ud;
    for (int i = 0; i < SLOTS; i++) free(n->buf[i]);
    free(n);
}

/* last error of a read (for messages after dlc_catalog_* failed on a network read) */
const char *net_error(const ZipSrc *z) {
    const Net *n = z ? z->ud : NULL;
    return n && n->err[0] ? n->err : NULL;
}

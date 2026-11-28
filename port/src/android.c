/*
 * Native Android screens (android/.../MikuFlickActivity.java, ImportPage.java): the IPA import
 * screen is a page of plain Android views over the game instead of one drawn with SDL, like the
 * UIKit screens on iOS. The page only shows state and passes taps back; the logic stays here and in
 * main.c, on the SDL thread. While a page is up the game draws nothing, so the UI thread and the
 * GPU are left to the page.
 *
 *   C -> Java: showPage / hidePage / importState (byte[] for text: JNI's NewStringUTF wants
 *              modified UTF-8, which file names need not be)
 *   Java -> C: takeAction() returns the page's next action ("later", "fd 42", "fdfail"), or null
 */
#include "mf.h"

#ifdef __ANDROID__
#include <jni.h>
#include <stdarg.h>
#include <string.h>

/* calls a method of the activity; sig's return type must be V (void) or Ljava/lang/String; */
static bool call(const char *name, const char *sig, jobject *ret, ...) {
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject act = SDL_AndroidGetActivity();
    if (!env || !act) return false;
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, cls, name, sig);
    bool ok = m != NULL;
    if (ok) {
        va_list ap;
        va_start(ap, ret);
        if (ret) *ret = (*env)->CallObjectMethodV(env, act, m, ap);
        else (*env)->CallVoidMethodV(env, act, m, ap);
        va_end(ap);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        ok = false;
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return ok;
}

static jbyteArray bytes(JNIEnv *env, const char *s, size_t n) {
    jbyteArray a = (*env)->NewByteArray(env, (jsize)n);
    if (a && n) (*env)->SetByteArrayRegion(env, a, 0, (jsize)n, (const jbyte *)s);
    return a;
}

bool android_page_show(int kind, int mode, const char *files_dir) {
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    if (!env) return false;
    const char *d = files_dir ? files_dir : "";
    jbyteArray dir = bytes(env, d, strlen(d));
    bool ok = dir && call("showPage", "(II[B)V", NULL, (jint)kind, (jint)mode, dir);
    if (dir) (*env)->DeleteLocalRef(env, dir);
    return ok;
}

void android_page_hide(void) { call("hidePage", "()V", NULL); }

bool android_page_action(char *buf, size_t cap) {
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject s = NULL;
    if (!env || !call("takeAction", "()Ljava/lang/String;", &s) || !s) return false;
    const char *c = (*env)->GetStringUTFChars(env, s, NULL);
    if (c) {
        snprintf(buf, cap, "%s", c);
        (*env)->ReleaseStringUTFChars(env, s, c);
    }
    (*env)->DeleteLocalRef(env, s);
    return c != NULL;
}

void android_import_state(int phase, double frac, const char *file, const char *err) {
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    if (!env) return;
    if (!file) file = "";
    if (!err) err = "";
    jbyteArray f = bytes(env, file, strlen(file)), e = bytes(env, err, strlen(err));
    int permille = (int)(frac * 1000.0 + 0.5);
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    if (f && e) call("importState", "(II[B[B)V", NULL, (jint)phase, (jint)permille, f, e);
    if (f) (*env)->DeleteLocalRef(env, f);
    if (e) (*env)->DeleteLocalRef(env, e);
}
#else
typedef int android_c_unused;   /* ISO C: no empty translation units */
#endif

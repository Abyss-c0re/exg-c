#ifdef __ANDROID__
#include "np_serial.h"

#ifndef NP_ANDROID_UI
#include "SDL.h"
#include "SDL_system.h"
#endif

#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define TAG "exg-c"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define AERR(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static pthread_mutex_t usb_mu = PTHREAD_MUTEX_INITIALIZER;
static JavaVM *jvm;
static jclass cls;
static jmethodID m_list, m_open, m_close, m_read, m_read_for, m_write, m_dtr, m_flush, m_baud;
static int bound;
static int open_ok;

/* Remembers the JavaVM. A null vm is ignored. */
void np_serial_set_vm(JavaVM *vm)
{
    if (vm) {
        jvm = vm;
    }
}

/* JNIEnv for this thread. Attaches if the VM is set. Otherwise asks SDL when that build has it. */
static JNIEnv *env_now(void)
{
    JNIEnv *env = NULL;
    if (jvm) {
        if ((*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_6) == JNI_OK && env) {
            return env;
        }
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) == 0 && env) {
            return env;
        }
    }
#ifndef NP_ANDROID_UI
    env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (env && !jvm) {
        (*env)->GetJavaVM(env, &jvm);
    }
#endif
    return env;
}

/* Resolves UsbSerial once and keeps a global ref. 0 when the methods are there. */
static int bind_locked(JNIEnv *env)
{
    jclass local;

    if (bound) {
        return 0;
    }
    local = (*env)->FindClass(env, "com/abysscore/exgc/UsbSerial");
    if (!local) {
        AERR("UsbSerial class missing");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        return -1;
    }
    cls = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    if (!jvm) {
        (*env)->GetJavaVM(env, &jvm);
    }
    m_list = (*env)->GetStaticMethodID(env, cls, "listPorts", "()[Ljava/lang/String;");
    m_open = (*env)->GetStaticMethodID(env, cls, "open", "(Ljava/lang/String;)I");
    m_close = (*env)->GetStaticMethodID(env, cls, "close", "()V");
    m_read = (*env)->GetStaticMethodID(env, cls, "read", "([BI)I");
    m_read_for = (*env)->GetStaticMethodID(env, cls, "readFor", "([BII)I");
    m_write = (*env)->GetStaticMethodID(env, cls, "write", "([BI)I");
    m_dtr = (*env)->GetStaticMethodID(env, cls, "pulseDtr", "()V");
    m_flush = (*env)->GetStaticMethodID(env, cls, "flush", "()V");
    m_baud = (*env)->GetStaticMethodID(env, cls, "setBaud", "(I)I");
    if (!m_list || !m_open || !m_close || !m_read || !m_read_for || !m_write || !m_dtr ||
        !m_flush || !m_baud) {
        AERR("UsbSerial method missing");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        return -1;
    }
    bound = 1;
    return 0;
}

/* UsbSerial.open. Copies the path. Returns 100, never 0 or 1. -1 if Java fails. */
int np_serial_open(const char *path)
{
    JNIEnv *env;
    jstring jpath;
    jint rc;

    env = env_now();
    if (!env) {
        return -1;
    }
    pthread_mutex_lock(&usb_mu);
    if (bind_locked(env) != 0) {
        pthread_mutex_unlock(&usb_mu);
        return -1;
    }
    jpath = (*env)->NewStringUTF(env, path ? path : "");
    rc = (*env)->CallStaticIntMethod(env, cls, m_open, jpath);
    (*env)->DeleteLocalRef(env, jpath);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        rc = -1;
    }
    open_ok = rc == 0;
    pthread_mutex_unlock(&usb_mu);
    /* Never return 0/1 — those are stdin/stdout. Reader must not poll this. */
    return open_ok ? 100 : -1;
}

/* UsbSerial.pulseDtr. Ignores fd. Clears a Java exception. */
void np_serial_pulse_dtr(int fd)
{
    JNIEnv *env = env_now();
    (void)fd;
    if (!env) {
        return;
    }
    pthread_mutex_lock(&usb_mu);
    if (bind_locked(env) == 0) {
        (*env)->CallStaticVoidMethod(env, cls, m_dtr);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
    }
    pthread_mutex_unlock(&usb_mu);
}

/* UsbSerial.setBaud. Ignores fd. Does not clamp baud. -1 if Java throws or returns non-zero. */
int np_serial_set_baud(int fd, int baud)
{
    JNIEnv *env;
    jint rc;

    (void)fd;
    env = env_now();
    if (!env) {
        return -1;
    }
    pthread_mutex_lock(&usb_mu);
    if (bind_locked(env) != 0) {
        pthread_mutex_unlock(&usb_mu);
        return -1;
    }
    rc = (*env)->CallStaticIntMethod(env, cls, m_baud, (jint)baud);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        rc = -1;
    }
    pthread_mutex_unlock(&usb_mu);
    return rc == 0 ? 0 : -1;
}

/* UsbSerial.close. Ignores fd. Clears a Java exception. */
void np_serial_close(int fd)
{
    JNIEnv *env = env_now();
    (void)fd;
    if (!env) {
        return;
    }
    pthread_mutex_lock(&usb_mu);
    if (bind_locked(env) == 0) {
        (*env)->CallStaticVoidMethod(env, cls, m_close);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
    }
    open_ok = 0;
    pthread_mutex_unlock(&usb_mu);
}

/* Copies n bytes into a Java array and calls UsbSerial.write. n <= 0 returns 0. -1 if the array or the bind fails. */
int np_serial_write(int fd, const void *buf, int n)
{
    JNIEnv *env;
    jbyteArray arr;
    jint rc;

    (void)fd;
    if (n <= 0) {
        return 0;
    }
    env = env_now();
    if (!env) {
        return -1;
    }
    pthread_mutex_lock(&usb_mu);
    if (bind_locked(env) != 0) {
        pthread_mutex_unlock(&usb_mu);
        return -1;
    }
    arr = (*env)->NewByteArray(env, n);
    if (!arr) {
        pthread_mutex_unlock(&usb_mu);
        return -1;
    }
    (*env)->SetByteArrayRegion(env, arr, 0, n, (const jbyte *)buf);
    rc = (*env)->CallStaticIntMethod(env, cls, m_write, arr, n);
    (*env)->DeleteLocalRef(env, arr);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        rc = -1;
    }
    pthread_mutex_unlock(&usb_mu);
    return (int)rc;
}

/* UsbSerial.read into buf. n <= 0 returns 0. A Java count longer than n is clamped. */
int np_serial_read(int fd, void *buf, int n)
{
    JNIEnv *env;
    jbyteArray arr;
    jint rc;

    (void)fd;
    if (n <= 0) {
        return 0;
    }
    env = env_now();
    if (!env) {
        return -1;
    }
    pthread_mutex_lock(&usb_mu);
    if (bind_locked(env) != 0) {
        pthread_mutex_unlock(&usb_mu);
        return -1;
    }
    arr = (*env)->NewByteArray(env, n);
    if (!arr) {
        pthread_mutex_unlock(&usb_mu);
        return -1;
    }
    rc = (*env)->CallStaticIntMethod(env, cls, m_read, arr, n);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        rc = -1;
    } else if (rc > 0) {
        static int logged;
        if (rc > n) {
            rc = n;
        }
        (*env)->GetByteArrayRegion(env, arr, 0, rc, (jbyte *)buf);
        if (!logged) {
            ALOG("usb first read %d bytes", (int)rc);
            logged = 1;
        }
    }
    (*env)->DeleteLocalRef(env, arr);
    pthread_mutex_unlock(&usb_mu);
    if (rc < 0) {
        return -1;
    }
    return (int)rc;
}

/* UsbSerial.readFor. Clamps the wait to 5..80 ms. n <= 0 returns 0. A count longer than n is clamped. */
int np_serial_read_wait(int fd, void *buf, int n, int timeout_ms)
{
    JNIEnv *env;
    jbyteArray arr;
    jint rc;

    (void)fd;
    if (n <= 0) {
        return 0;
    }
    if (timeout_ms < 5) {
        timeout_ms = 5;
    }
    if (timeout_ms > 80) {
        timeout_ms = 80;
    }
    env = env_now();
    if (!env) {
        return -1;
    }
    pthread_mutex_lock(&usb_mu);
    if (bind_locked(env) != 0) {
        pthread_mutex_unlock(&usb_mu);
        return -1;
    }
    arr = (*env)->NewByteArray(env, n);
    if (!arr) {
        pthread_mutex_unlock(&usb_mu);
        return -1;
    }
    rc = (*env)->CallStaticIntMethod(env, cls, m_read_for, arr, n, timeout_ms);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        rc = -1;
    } else if (rc > 0) {
        if (rc > n) {
            rc = n;
        }
        (*env)->GetByteArrayRegion(env, arr, 0, rc, (jbyte *)buf);
    }
    (*env)->DeleteLocalRef(env, arr);
    pthread_mutex_unlock(&usb_mu);
    if (rc < 0) {
        return -1;
    }
    return (int)rc;
}

/* One byte through np_serial_read. */
int np_serial_read_byte(int fd, unsigned char *b)
{
    return np_serial_read(fd, b, 1);
}

/* UsbSerial.flush. Ignores fd. Clears a Java exception. */
void np_serial_flush(int fd)
{
    JNIEnv *env = env_now();
    (void)fd;
    if (!env) {
        return;
    }
    pthread_mutex_lock(&usb_mu);
    if (bind_locked(env) == 0) {
        (*env)->CallStaticVoidMethod(env, cls, m_flush);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
    }
    pthread_mutex_unlock(&usb_mu);
}

/* UsbSerial.listPorts. Copies up to max names and skips a null entry. max <= 0 returns 0. */
int np_list_ports(char out[][NP_MAX_PATH], int max)
{
    JNIEnv *env;
    jobjectArray arr;
    int n = 0, i, len;

    if (max <= 0) {
        return 0;
    }
    env = env_now();
    if (!env) {
        return 0;
    }
    pthread_mutex_lock(&usb_mu);
    if (bind_locked(env) != 0) {
        pthread_mutex_unlock(&usb_mu);
        return 0;
    }
    arr = (jobjectArray)(*env)->CallStaticObjectMethod(env, cls, m_list);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        arr = NULL;
    }
    if (arr) {
        len = (*env)->GetArrayLength(env, arr);
        for (i = 0; i < len && n < max; i++) {
            jstring js = (jstring)(*env)->GetObjectArrayElement(env, arr, i);
            const char *s;
            if (!js) {
                continue;
            }
            s = (*env)->GetStringUTFChars(env, js, NULL);
            if (s) {
                snprintf(out[n], NP_MAX_PATH, "%s", s);
                n++;
                (*env)->ReleaseStringUTFChars(env, js, s);
            }
            (*env)->DeleteLocalRef(env, js);
        }
        (*env)->DeleteLocalRef(env, arr);
    }
    pthread_mutex_unlock(&usb_mu);
    return n;
}

#endif /* __ANDROID__ */

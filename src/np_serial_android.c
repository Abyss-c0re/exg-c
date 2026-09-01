#ifdef __ANDROID__
#include "np_serial.h"

#include "SDL.h"
#include "SDL_system.h"

#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <string.h>

#define TAG "exg-c"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define AERR(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static pthread_mutex_t usb_mu = PTHREAD_MUTEX_INITIALIZER;
static JavaVM *jvm;
static jclass cls;
static jmethodID m_list, m_open, m_close, m_read, m_write, m_dtr, m_flush;
static int bound;
static int open_ok;

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
    env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (env && !jvm) {
        (*env)->GetJavaVM(env, &jvm);
    }
    return env;
}

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
    m_write = (*env)->GetStaticMethodID(env, cls, "write", "([BI)I");
    m_dtr = (*env)->GetStaticMethodID(env, cls, "pulseDtr", "()V");
    m_flush = (*env)->GetStaticMethodID(env, cls, "flush", "()V");
    if (!m_list || !m_open || !m_close || !m_read || !m_write || !m_dtr || !m_flush) {
        AERR("UsbSerial method missing");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        return -1;
    }
    bound = 1;
    return 0;
}

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

int np_serial_read_byte(int fd, unsigned char *b)
{
    return np_serial_read(fd, b, 1);
}

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

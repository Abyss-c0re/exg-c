#ifdef NP_ANDROID_UI
#include "np_host.h"

#include <jni.h>
#include <stdio.h>
#include <string.h>

extern void np_serial_set_vm(JavaVM *vm);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    np_serial_set_vm(vm);
    return JNI_VERSION_1_6;
}

static void jstr_to(JNIEnv *env, jstring js, char *out, int n)
{
    const char *s;
    out[0] = 0;
    if (!js) {
        return;
    }
    s = (*env)->GetStringUTFChars(env, js, NULL);
    if (s) {
        snprintf(out, (size_t)n, "%s", s);
        (*env)->ReleaseStringUTFChars(env, js, s);
    }
}

static jstring jstr_from(JNIEnv *env, const char *s)
{
    return (*env)->NewStringUTF(env, s ? s : "");
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_start(JNIEnv *env, jclass cls, jstring dir)
{
    char path[256];
    (void)cls;
    jstr_to(env, dir, path, sizeof(path));
    return np_host_start(path);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_shutdown(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_shutdown();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_tick(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_tick();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_connect(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_connect();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_disconnect(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_disconnect();
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_connected(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_connected() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_status(JNIEnv *env, jclass cls)
{
    char buf[240];
    (void)cls;
    np_host_status(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_statusOk(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_status_ok() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_sps(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_sps();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_frames(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)np_host_frames();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_copyWave(JNIEnv *env, jclass cls, jint ch, jfloatArray dst)
{
    jfloat *p;
    int n, got;
    (void)cls;
    if (!dst) {
        return 0;
    }
    n = (*env)->GetArrayLength(env, dst);
    p = (*env)->GetFloatArrayElements(env, dst, NULL);
    if (!p) {
        return 0;
    }
    got = np_host_copy_wave(ch, p, n);
    (*env)->ReleaseFloatArrayElements(env, dst, p, 0);
    return got;
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_scaleUv(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_scale_uv();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleScale(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_scale();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setActive(JNIEnv *env, jclass cls, jint ch, jboolean on)
{
    (void)env;
    (void)cls;
    np_host_set_active(ch, on ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setRld(JNIEnv *env, jclass cls, jint ch, jboolean on)
{
    (void)env;
    (void)cls;
    np_host_set_rld(ch, on ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleGain(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    np_host_cycle_gain(ch);
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_active(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_active(ch) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_rld(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_rld(ch) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_gain(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_gain(ch);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_color(JNIEnv *env, jclass cls, jint ch)
{
    int r = 200, g = 200, b = 200;
    (void)env;
    (void)cls;
    np_host_color(ch, &r, &g, &b);
    return (jint)((0xFF << 24) | ((r & 255) << 16) | ((g & 255) << 8) | (b & 255));
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_noiseArm(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_noise_arm();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_noiseOk(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_noise_ok();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_calm(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_calm();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleClean(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_clean();
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_calHave(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_cal_have() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_calmHave(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_calm_have() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_cleanOn(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_clean() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setName(JNIEnv *env, jclass cls, jstring s)
{
    char buf[24];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    np_host_set_name(buf);
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_getName(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_get_name(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_record(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_record();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleMatch(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_match();
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_matchOn(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_match() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setProfile(JNIEnv *env, jclass cls, jstring s)
{
    char buf[24];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    np_host_set_profile(buf);
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_getProfile(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_get_profile(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profSave(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_prof_save();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profLoad(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_prof_load();
}

JNIEXPORT jobjectArray JNICALL
Java_com_abysscore_exgc_ExgNative_profiles(JNIEnv *env, jclass cls)
{
    int n, i;
    jobjectArray arr;
    (void)cls;
    n = np_host_prof_count();
    arr = (*env)->NewObjectArray(env, n, (*env)->FindClass(env, "java/lang/String"),
                                 jstr_from(env, ""));
    for (i = 0; i < n; i++) {
        char buf[24];
        np_host_prof_at(i, buf, sizeof(buf));
        (*env)->SetObjectArrayElement(env, arr, i, jstr_from(env, buf));
    }
    return arr;
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_ports(JNIEnv *env, jclass cls)
{
    char buf[1024];
    (void)cls;
    np_host_ports(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cyclePort(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_port();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_copyCube(JNIEnv *env, jclass cls, jbyteArray dst)
{
    unsigned char cube[512];
    (void)cls;
    if (!dst || (*env)->GetArrayLength(env, dst) < 512) {
        return;
    }
    np_host_copy_cube(cube);
    (*env)->SetByteArrayRegion(env, dst, 0, 512, (jbyte *)cube);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleNotch(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_notch();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleHp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_hp();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_notch(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_notch();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_hp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_hp();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_togglePause(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_pause();
}
#endif

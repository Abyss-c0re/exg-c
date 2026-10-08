#ifdef NP_ANDROID_UI
#include "np_algo.h"
#include "np_host.h"
#include "np_api.h"

#include <jni.h>
#include <stdio.h>
#include <string.h>

/* Java boundary for ExgNative.
 * Behavior lives in np_host_*.
 * Do not put a new feature only here. */


extern void np_serial_set_vm(JavaVM *vm);

/* Remembers the JavaVM for UsbSerial. Returns JNI 1.6. reserved is ignored. */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    np_serial_set_vm(vm);
    return JNI_VERSION_1_6;
}

/* Copies a Java string into out. A null jstring, or a failed pin, stores an empty string. */
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

/* New Java string. A null C string becomes empty. */
static jstring jstr_from(JNIEnv *env, const char *s)
{
    return (*env)->NewStringUTF(env, s ? s : "");
}

/* ExgNative.start. Copies the Java string and calls np_host_start. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_start(JNIEnv *env, jclass cls, jstring dir)
{
    char path[256];
    (void)cls;
    jstr_to(env, dir, path, sizeof(path));
    return np_host_start(path);
}

/* ExgNative.setTempDir. Copies the Java string and calls np_host_set_temp_dir. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setTempDir(JNIEnv *env, jclass cls, jstring dir)
{
    char path[256];
    (void)cls;
    jstr_to(env, dir, path, sizeof(path));
    np_host_set_temp_dir(path);
}

/* ExgNative.shutdown. Calls np_host_shutdown. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_shutdown(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_shutdown();
}

/* ExgNative.tick on the UI thread. Calls np_host_tick. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_tick(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_tick();
}

/* ExgNative.connect. Calls np_host_connect. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_connect(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_connect();
}

/* ExgNative.disconnect. Calls np_host_disconnect. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_disconnect(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_disconnect();
}

/* ExgNative.connected. Calls np_host_connected and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_connected(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_connected() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.status. Calls np_host_status and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_status(JNIEnv *env, jclass cls)
{
    char buf[240];
    (void)cls;
    np_host_status(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.statusOk. Calls np_host_status_ok and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_statusOk(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_status_ok() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.sps. Calls np_host_sps. */
JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_sps(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_sps();
}

/* ExgNative.frames. Calls np_host_frames. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_frames(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)np_host_frames();
}

/* ExgNative.drops. Calls np_host_drops. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_drops(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)np_host_drops();
}

/* ExgNative.copyWave. Pins the float array. A null array or a failed pin returns 0. */
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

/* ExgNative.scaleUv. Calls np_host_scale_uv. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_scaleUv(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_scale_uv();
}

/* ExgNative.cycleScale. Calls np_host_cycle_scale. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleScale(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_scale();
}

/* ExgNative.windowS. Calls np_host_window_s. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_windowS(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_window_s();
}

/* ExgNative.cycleWindow. Calls np_host_cycle_window. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleWindow(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_window();
}

/* ExgNative.setActive. Passes the boolean as 0 or 1 to np_host_set_active. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setActive(JNIEnv *env, jclass cls, jint ch, jboolean on)
{
    (void)env;
    (void)cls;
    np_host_set_active(ch, on ? 1 : 0);
}

/* ExgNative.setRld. Passes the boolean as 0 or 1 to np_host_set_rld. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setRld(JNIEnv *env, jclass cls, jint ch, jboolean on)
{
    (void)env;
    (void)cls;
    np_host_set_rld(ch, on ? 1 : 0);
}

/* ExgNative.cycleGain. Calls np_host_cycle_gain. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleGain(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    np_host_cycle_gain(ch);
}

/* ExgNative.active. Calls np_host_active and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_active(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_active(ch) ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.rld. Calls np_host_rld and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_rld(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_rld(ch) ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.negRail. Calls np_host_neg_rail and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_negRail(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_neg_rail() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.setNegRail. Passes the boolean as 0 or 1 to np_host_set_neg_rail. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setNegRail(JNIEnv *env, jclass cls, jboolean on)
{
    (void)env;
    (void)cls;
    np_host_set_neg_rail(on ? 1 : 0);
}

/* ExgNative.montageDefault. Calls np_host_montage_default. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_montageDefault(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_montage_default();
}

/* ExgNative.negSite. Calls np_host_neg_site. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_negSite(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_neg_site(ch);
}

/* ExgNative.setNegSite. Calls np_host_set_neg_site. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setNegSite(JNIEnv *env, jclass cls, jint ch, jint site)
{
    (void)env;
    (void)cls;
    np_host_set_neg_site(ch, site);
}

/* ExgNative.negName. Calls np_host_neg_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_negName(JNIEnv *env, jclass cls, jint ch)
{
    char buf[16];
    (void)cls;
    np_host_neg_name(ch, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.negPick. Calls np_host_neg_pick and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_negPick(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_neg_pick() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.setNegPick. Passes the boolean as 0 or 1 to np_host_set_neg_pick. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setNegPick(JNIEnv *env, jclass cls, jboolean on)
{
    (void)env;
    (void)cls;
    np_host_set_neg_pick(on ? 1 : 0);
}

/* ExgNative.gain. Calls np_host_gain. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_gain(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_gain(ch);
}

/* ExgNative.color. Packs np_host_color as 0xAARRGGBB. A missing color stays 200, 200, 200. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_color(JNIEnv *env, jclass cls, jint ch)
{
    int r = 200, g = 200, b = 200;
    (void)env;
    (void)cls;
    np_host_color(ch, &r, &g, &b);
    return (jint)((0xFF << 24) | ((r & 255) << 16) | ((g & 255) << 8) | (b & 255));
}

/* ExgNative.cycleColor. Calls np_host_cycle_color. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleColor(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    np_host_cycle_color(ch);
}

/* ExgNative.setColor. Splits a packed color into r, g, and b for np_host_set_color. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setColor(JNIEnv *env, jclass cls, jint ch, jint rgb)
{
    (void)env;
    (void)cls;
    np_host_set_color(ch, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

/* ExgNative.setScaleUv. Calls np_host_set_scale_uv. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setScaleUv(JNIEnv *env, jclass cls, jint uv)
{
    (void)env;
    (void)cls;
    np_host_set_scale_uv(uv);
}

/* ExgNative.setWindowS. Calls np_host_set_window_s. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setWindowS(JNIEnv *env, jclass cls, jint s)
{
    (void)env;
    (void)cls;
    np_host_set_window_s(s);
}

/* ExgNative.setNotch. Calls np_host_set_notch. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setNotch(JNIEnv *env, jclass cls, jint hz)
{
    (void)env;
    (void)cls;
    np_host_set_notch(hz);
}

/* ExgNative.setHp. Calls np_host_set_hp. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setHp(JNIEnv *env, jclass cls, jint hz)
{
    (void)env;
    (void)cls;
    np_host_set_hp(hz);
}

/* ExgNative.setLp. Calls np_host_set_lp. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setLp(JNIEnv *env, jclass cls, jint hz)
{
    (void)env;
    (void)cls;
    np_host_set_lp(hz);
}

/* ExgNative.setBand. Calls np_host_set_band. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setBand(JNIEnv *env, jclass cls, jint band)
{
    (void)env;
    (void)cls;
    np_host_set_band(band);
}

/* ExgNative.setAlgo. Calls np_host_set_algo. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setAlgo(JNIEnv *env, jclass cls, jint id)
{
    (void)env;
    (void)cls;
    np_host_set_algo(id);
}

/* ExgNative.setUiScale. Calls np_host_set_ui_scale. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setUiScale(JNIEnv *env, jclass cls, jint tenths)
{
    (void)env;
    (void)cls;
    np_host_set_ui_scale(tenths);
}

/* ExgNative.setBoardImu. Passes the boolean as 0 or 1 to np_host_set_board_imu. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setBoardImu(JNIEnv *env, jclass cls, jboolean imu)
{
    (void)env;
    (void)cls;
    np_host_set_board_imu(imu ? 1 : 0);
}

/* ExgNative.setGain. Calls np_host_set_gain. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setGain(JNIEnv *env, jclass cls, jint ch, jint gain)
{
    (void)env;
    (void)cls;
    np_host_set_gain(ch, gain);
}

/* ExgNative.calStart. Calls np_host_cal_start. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_calStart(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cal_start();
}

/* ExgNative.calPhase. Calls np_host_cal_phase. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_calPhase(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_cal_phase();
}

/* ExgNative.calProgress. Calls np_host_cal_progress. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_calProgress(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_cal_progress();
}

/* ExgNative.calLine. Calls np_host_cal_line and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_calLine(JNIEnv *env, jclass cls)
{
    char buf[48];
    (void)cls;
    np_host_cal_line(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.noiseArm. Calls np_host_noise_arm. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_noiseArm(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_noise_arm();
}

/* ExgNative.noiseOk. Calls np_host_noise_ok. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_noiseOk(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_noise_ok();
}

/* ExgNative.calm. Calls np_host_calm. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_calm(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_calm();
}

/* ExgNative.toggleClean. Calls np_host_toggle_clean. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleClean(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_clean();
}

/* ExgNative.calHave. Calls np_host_cal_have and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_calHave(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_cal_have() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.calmHave. Calls np_host_calm_have and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_calmHave(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_calm_have() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.cleanOn. Calls np_host_clean and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_cleanOn(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_clean() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.cleanLive. Calls np_host_clean_live and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_cleanLive(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_clean_live() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.setName. Copies the Java string and calls np_host_set_name. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setName(JNIEnv *env, jclass cls, jstring s)
{
    char buf[24];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    np_host_set_name(buf);
}

/* ExgNative.getName. Calls np_host_get_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_getName(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_get_name(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.record. Calls np_host_record. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_record(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_record();
}

/* ExgNative.toggleMatch. Calls np_host_toggle_match. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleMatch(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_match();
}

/* ExgNative.matchOn. Calls np_host_match and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_matchOn(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_match() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.setProfile. Copies the Java string and calls np_host_set_profile. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setProfile(JNIEnv *env, jclass cls, jstring s)
{
    char buf[24];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    np_host_set_profile(buf);
}

/* ExgNative.getProfile. Calls np_host_get_profile and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_getProfile(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_get_profile(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.profSave. Calls np_host_prof_save. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profSave(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_prof_save();
}

/* ExgNative.profLoad. Calls np_host_prof_load. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profLoad(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_prof_load();
}

/* ExgNative.profDel. Calls np_host_prof_del. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profDel(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_prof_del();
}

/* ExgNative.profRename. Copies the Java string and calls np_host_prof_rename. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profRename(JNIEnv *env, jclass cls, jstring s)
{
    char buf[24];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    return np_host_prof_rename(buf);
}

/* ExgNative.profiles. Builds a Java string array from the saved profile names. */
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

/* ExgNative.ports. Calls np_host_ports and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_ports(JNIEnv *env, jclass cls)
{
    char buf[1024];
    (void)cls;
    np_host_ports(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.cyclePort. Calls np_host_cycle_port. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cyclePort(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_port();
}

/* ExgNative.setPortI. Calls np_host_set_port_i. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setPortI(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_set_port_i(i);
}

/* ExgNative.copyCube. Writes 512 bytes. Returns if the array is null or shorter. */
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

/* ExgNative.cookUv. Writes 8 floats. Returns if the array is null or shorter. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cookUv(JNIEnv *env, jclass cls, jfloatArray dst)
{
    float uv[8];
    (void)cls;
    if (!dst || (*env)->GetArrayLength(env, dst) < 8) {
        return;
    }
    np_host_cook_uv(uv);
    (*env)->SetFloatArrayRegion(env, dst, 0, 8, uv);
}

/* ExgNative.pairN. Calls np_host_pair_n. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_pairN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_pair_n();
}

/* ExgNative.pairLabel. Calls np_host_pair_label and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_pairLabel(JNIEnv *env, jclass cls, jint i)
{
    char buf[24];
    (void)cls;
    np_host_pair_label(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.pairChs. Writes two channel indexes. Returns if the array is null or shorter than 2. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_pairChs(JNIEnv *env, jclass cls, jint i, jintArray dst)
{
    int a = -1, b = -1;
    jint ab[2];
    (void)cls;
    if (!dst || (*env)->GetArrayLength(env, dst) < 2) {
        return;
    }
    np_host_pair_chs(i, &a, &b);
    ab[0] = a;
    ab[1] = b;
    (*env)->SetIntArrayRegion(env, dst, 0, 2, ab);
}

/* ExgNative.pairUv. Writes 4 floats. Returns if the array is null or shorter. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_pairUv(JNIEnv *env, jclass cls, jfloatArray dst)
{
    float uv[4];
    (void)cls;
    if (!dst || (*env)->GetArrayLength(env, dst) < 4) {
        return;
    }
    np_host_pair_uv(uv);
    (*env)->SetFloatArrayRegion(env, dst, 0, 4, uv);
}

/* ExgNative.pairMode. Calls np_host_pair_mode and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_pairMode(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_pair_mode() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.setPairMode. Passes the boolean as 0 or 1 to np_host_set_pair_mode. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setPairMode(JNIEnv *env, jclass cls, jboolean on)
{
    (void)env;
    (void)cls;
    np_host_set_pair_mode(on ? 1 : 0);
}

/* ExgNative.copyPair. Pins the float array. A null array or a failed pin returns 0. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_copyPair(JNIEnv *env, jclass cls, jint p, jfloatArray dst)
{
    jfloat *buf;
    int n, got;
    (void)cls;
    if (!dst) {
        return 0;
    }
    n = (*env)->GetArrayLength(env, dst);
    buf = (*env)->GetFloatArrayElements(env, dst, NULL);
    if (!buf) {
        return 0;
    }
    got = np_host_copy_pair(p, buf, n);
    (*env)->ReleaseFloatArrayElements(env, dst, buf, 0);
    return got;
}

/* ExgNative.pairClipped. Calls np_host_pair_clip and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_pairClipped(JNIEnv *env, jclass cls, jint p)
{
    (void)env;
    (void)cls;
    return np_host_pair_clip(p) ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.cycleNotch. Calls np_host_cycle_notch. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleNotch(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_notch();
}

/* ExgNative.cycleHp. Calls np_host_cycle_hp. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleHp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_hp();
}

/* ExgNative.notch. Calls np_host_notch. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_notch(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_notch();
}

/* ExgNative.notchEff. Calls np_host_notch_eff. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_notchEff(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_notch_eff();
}

/* ExgNative.hp. Calls np_host_hp. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_hp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_hp();
}

/* ExgNative.lp. Calls np_host_lp. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_lp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_lp();
}

/* ExgNative.cycleLp. Calls np_host_cycle_lp. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleLp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_lp();
}

/* ExgNative.car. Calls np_host_car and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_car(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_car() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.toggleCar. Calls np_host_toggle_car. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleCar(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_car();
}

/* ExgNative.detrend. Calls np_host_detrend and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_detrend(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_detrend() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.toggleDetrend. Calls np_host_toggle_detrend. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleDetrend(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_detrend();
}

/* ExgNative.envelope. Calls np_host_envelope and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_envelope(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_envelope() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.toggleEnvelope. Calls np_host_toggle_envelope. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleEnvelope(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_envelope();
}

/* ExgNative.band. Calls np_host_band. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_band(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_band();
}

/* ExgNative.bandFit. Calls np_host_band_fit and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_bandFit(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_band_fit() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.cycleBand. Calls np_host_cycle_band. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleBand(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_band();
}

/* ExgNative.clipped. Calls np_host_ch_clip and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_clipped(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_ch_clip(ch) ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.algo. Calls np_host_algo. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_algo(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_algo();
}

/* ExgNative.cycleAlgo. Calls np_host_cycle_algo. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleAlgo(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_algo();
}

/* ExgNative.algoName. Calls np_host_algo_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_algoName(JNIEnv *env, jclass cls)
{
    char buf[16];
    (void)cls;
    np_host_algo_name(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.algoRule. Calls np_host_algo_rule and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_algoRule(JNIEnv *env, jclass cls)
{
    char buf[96];
    (void)cls;
    np_host_algo_rule(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.algoSrc. Calls np_host_algo_src and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_algoSrc(JNIEnv *env, jclass cls)
{
    char buf[NP_ALGO_SRC];
    (void)cls;
    np_host_algo_src(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.setAlgoSrc. Copies the source. Returns the error text, or empty when np_host_set_algo_src succeeds. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_setAlgoSrc(JNIEnv *env, jclass cls, jstring src)
{
    char buf[NP_ALGO_SRC], err[80];
    (void)cls;
    jstr_to(env, src, buf, sizeof(buf));
    if (np_host_set_algo_src(buf, err, (int)sizeof(err)) != 0) {
        return jstr_from(env, err);
    }
    return jstr_from(env, "");
}

/* ExgNative.algoFold. Calls np_host_algo_fold. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_algoFold(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)np_host_algo_fold();
}

/* ExgNative.alibN. Calls np_host_alib_n. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_alibN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_alib_n();
}

/* ExgNative.alibSel. Calls np_host_alib_sel. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_alibSel(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_alib_sel();
}

/* ExgNative.alibSetSel. Calls np_host_alib_set_sel. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_alibSetSel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_alib_set_sel(i);
}

/* ExgNative.alibName. Calls np_host_alib_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_alibName(JNIEnv *env, jclass cls, jint i)
{
    char buf[NP_ALIB_NAME];
    (void)cls;
    np_host_alib_name(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.alibSrc. Calls np_host_alib_src and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_alibSrc(JNIEnv *env, jclass cls, jint i)
{
    char buf[NP_ALGO_SRC];
    (void)cls;
    np_host_alib_src(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.alibDef. Calls np_host_alib_def and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_alibDef(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_alib_def(i) ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.alibCheck. Copies the source. Returns the error text, or empty when the check succeeds. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_alibCheck(JNIEnv *env, jclass cls, jstring src)
{
    char buf[NP_ALGO_SRC], err[80];
    (void)cls;
    jstr_to(env, src, buf, sizeof(buf));
    if (np_host_alib_check(buf, err, (int)sizeof(err)) != 0) {
        return jstr_from(env, err);
    }
    return jstr_from(env, "");
}

/* ExgNative.alibSetSrc. Copies the source. Returns the error text, or empty when the save succeeds. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_alibSetSrc(JNIEnv *env, jclass cls, jint i,
                                            jstring src)
{
    char buf[NP_ALGO_SRC], err[80];
    (void)cls;
    jstr_to(env, src, buf, sizeof(buf));
    if (np_host_alib_set_src(i, buf, err, (int)sizeof(err)) != 0) {
        return jstr_from(env, err);
    }
    return jstr_from(env, "");
}

/* ExgNative.alibSetName. Copies the Java string and calls np_host_alib_set_name. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_alibSetName(JNIEnv *env, jclass cls, jint i,
                                             jstring name)
{
    char buf[NP_ALIB_NAME];
    (void)cls;
    jstr_to(env, name, buf, sizeof(buf));
    return np_host_alib_set_name(i, buf);
}

/* ExgNative.alibAdd. Calls np_host_alib_add. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_alibAdd(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_alib_add();
}

/* ExgNative.alibDel. Calls np_host_alib_del. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_alibDel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_alib_del(i);
}

/* ExgNative.alibReset. Calls np_host_alib_reset. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_alibReset(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_alib_reset(i);
}

/* ExgNative.madeAlgo. Calls np_host_made_algo. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeAlgo(JNIEnv *env, jclass cls, jint cube,
                                          jint q)
{
    (void)env;
    (void)cls;
    return np_host_made_algo(cube, q);
}

/* ExgNative.madeAlgoOwn. Calls np_host_made_algo_own. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeAlgoOwn(JNIEnv *env, jclass cls, jint cube,
                                             jint q)
{
    (void)env;
    (void)cls;
    return np_host_made_algo_own(cube, q);
}

/* ExgNative.madeSetAlgo. Calls np_host_made_set_algo. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeSetAlgo(JNIEnv *env, jclass cls, jint cube,
                                             jint q, jint id)
{
    (void)env;
    (void)cls;
    return np_host_made_set_algo(cube, q, id);
}

/* ExgNative.madeAlgoAll. Calls np_host_made_algo_all. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeAlgoAll(JNIEnv *env, jclass cls, jint cube)
{
    (void)env;
    (void)cls;
    return np_host_made_algo_all(cube);
}

/* ExgNative.madeSetAlgoAll. Calls np_host_made_set_algo_all. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeSetAlgoAll(JNIEnv *env, jclass cls,
                                                jint cube, jint id)
{
    (void)env;
    (void)cls;
    return np_host_made_set_algo_all(cube, id);
}

/* ExgNative.togglePause. Calls np_host_toggle_pause. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_togglePause(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_pause();
}

/* ExgNative.paused. Calls np_host_paused and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_paused(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_paused() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.csvOn. Calls np_host_csv and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_csvOn(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_csv() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.toggleCsv. Calls np_host_toggle_csv. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleCsv(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_csv();
}

/* ExgNative.csvBegin. Copies the Java string and calls np_host_csv_begin. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_csvBegin(JNIEnv *env, jclass cls, jstring path)
{
    char buf[NP_MAX_PATH];
    (void)cls;
    jstr_to(env, path, buf, sizeof(buf));
    return np_host_csv_begin(buf);
}

/* ExgNative.csvBeginFd. Copies the Java string and calls np_host_csv_begin_fd. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_csvBeginFd(JNIEnv *env, jclass cls, jint fd, jstring name)
{
    char buf[80];
    (void)cls;
    jstr_to(env, name, buf, sizeof(buf));
    return np_host_csv_begin_fd((int)fd, buf);
}

/* ExgNative.copyFft. Copies at most 64 bins. Returns the peak Hz, or 0 if the array is null or shorter than 1. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_copyFft(JNIEnv *env, jclass cls, jfloatArray dst)
{
    float tmp[64];
    int hz = 0, n, want;
    (void)cls;
    if (!dst) {
        return 0;
    }
    want = (*env)->GetArrayLength(env, dst);
    if (want < 1) {
        return 0;
    }
    n = np_host_fft(tmp, want < 64 ? want : 64, &hz);
    if (n > want) {
        n = want;
    }
    if (n > 0) {
        (*env)->SetFloatArrayRegion(env, dst, 0, n, tmp);
    }
    return hz;
}

/* ExgNative.cubeView. Calls np_host_cube_view. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_cubeView(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_cube_view();
}

/* ExgNative.setCubeView. Calls np_host_set_cube_view. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setCubeView(JNIEnv *env, jclass cls, jint map)
{
    (void)env;
    (void)cls;
    np_host_set_cube_view(map);
}

/* ExgNative.cubeSpin. Calls np_host_cube_spin. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cubeSpin(JNIEnv *env, jclass cls, jfloat yaw, jfloat pitch)
{
    (void)env;
    (void)cls;
    np_host_cube_spin(yaw, pitch);
}

/* ExgNative.cubeZoom. Calls np_host_cube_zoom. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cubeZoom(JNIEnv *env, jclass cls, jint dir)
{
    (void)env;
    (void)cls;
    np_host_cube_zoom(dir);
}

/* ExgNative.cubeZoomF. Calls np_host_cube_zoom_get. */
JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_cubeZoomF(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_cube_zoom_get();
}

/* ExgNative.setCubeZoom. Calls np_host_set_cube_zoom. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setCubeZoom(JNIEnv *env, jclass cls, jfloat z)
{
    (void)env;
    (void)cls;
    np_host_set_cube_zoom(z);
}

/* ExgNative.cubeFront. Calls np_host_cube_front. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cubeFront(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cube_front();
}

/* ExgNative.cubeFloat. Calls np_host_cube_float and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_cubeFloat(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_cube_float() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.toggleCubeFloat. Calls np_host_toggle_cube_float. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleCubeFloat(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_cube_float();
}

/* ExgNative.madeMax. Calls np_host_made_max. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeMax(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_made_max();
}

/* ExgNative.madeN. Calls np_host_made_n. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_made_n();
}

/* ExgNative.madeSel. Calls np_host_made_sel. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeSel(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_made_sel();
}

/* ExgNative.madeSetSel. Calls np_host_made_set_sel. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_madeSetSel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_made_set_sel(i);
}

/* ExgNative.madeAdd. Calls np_host_made_add. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeAdd(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_made_add();
}

/* ExgNative.madeDel. Calls np_host_made_del. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeDel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_made_del(i);
}

/* ExgNative.madeCh. Calls np_host_made_ch. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeCh(JNIEnv *env, jclass cls, jint cube, jint q)
{
    (void)env;
    (void)cls;
    return np_host_made_ch(cube, q);
}

/* ExgNative.madeSetCh. Calls np_host_made_set_ch. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeSetCh(JNIEnv *env, jclass cls, jint cube, jint q, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_made_set_ch(cube, q, ch);
}

/* ExgNative.madeRgb. Packs r, g, b with no alpha. A missing color stays 255, 20, 40. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeRgb(JNIEnv *env, jclass cls, jint cube)
{
    int r = 255, gch = 20, b = 40;
    (void)env;
    (void)cls;
    np_host_made_rgb(cube, &r, &gch, &b);
    return (r << 16) | (gch << 8) | b;
}

/* ExgNative.madeSetRgb. Splits a packed color for np_host_made_set_rgb. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_madeSetRgb(JNIEnv *env, jclass cls, jint cube, jint rgb)
{
    (void)env;
    (void)cls;
    np_host_made_set_rgb(cube, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

/* ExgNative.madeQSel. Calls np_host_made_qsel. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeQSel(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_made_qsel();
}

/* ExgNative.madeSetQSel. Calls np_host_made_set_qsel. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_madeSetQSel(JNIEnv *env, jclass cls, jint q)
{
    (void)env;
    (void)cls;
    np_host_made_set_qsel(q);
}

/* ExgNative.madeSrc. Calls np_host_made_src and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_madeSrc(JNIEnv *env, jclass cls, jint cube, jint q)
{
    char buf[NP_ALGO_SRC];
    (void)cls;
    np_host_made_src(cube, q, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.setMadeSrc. Copies the source. Returns the error text, or empty when the save succeeds. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_setMadeSrc(JNIEnv *env, jclass cls, jint cube,
                                            jint q, jstring src)
{
    char buf[NP_ALGO_SRC], err[80];
    (void)cls;
    jstr_to(env, src, buf, sizeof(buf));
    if (np_host_set_made_src(cube, q, buf, err, (int)sizeof(err)) != 0) {
        return jstr_from(env, err);
    }
    return jstr_from(env, "");
}

/* ExgNative.madeFold. Calls np_host_made_fold. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_madeFold(JNIEnv *env, jclass cls, jint cube)
{
    (void)env;
    (void)cls;
    return (jint)np_host_made_fold(cube);
}

/* ExgNative.elecSel. Calls np_host_elec_sel. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_elecSel(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_elec_sel();
}

/* ExgNative.setElecSel. Calls np_host_set_elec_sel. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setElecSel(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    np_host_set_elec_sel(ch);
}

/* ExgNative.elecLabel. Calls np_host_elec_label and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_elecLabel(JNIEnv *env, jclass cls, jint ch)
{
    char buf[16];
    (void)cls;
    np_host_elec_label(ch, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.elecName. Calls np_host_elec_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_elecName(JNIEnv *env, jclass cls, jint ch)
{
    char buf[16];
    (void)cls;
    np_host_elec_name(ch, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.elecXyz. Writes x, y, z. Returns if the array is null or shorter than 3. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_elecXyz(JNIEnv *env, jclass cls, jint ch, jfloatArray xyz)
{
    float x = 0, y = 0, z = 0;
    jfloat v[3];
    (void)cls;
    if (!xyz || (*env)->GetArrayLength(env, xyz) < 3) {
        return;
    }
    np_host_elec_xyz(ch, &x, &y, &z);
    v[0] = x;
    v[1] = y;
    v[2] = z;
    (*env)->SetFloatArrayRegion(env, xyz, 0, 3, v);
}

/* ExgNative.siteFocus. Calls np_host_site_focus. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_siteFocus(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_site_focus();
}

/* ExgNative.siteStep. Calls np_host_site_step. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_siteStep(JNIEnv *env, jclass cls, jint dir)
{
    (void)env;
    (void)cls;
    np_host_site_step(dir);
}

/* ExgNative.assignSite. Calls np_host_assign_site. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_assignSite(JNIEnv *env, jclass cls, jint site)
{
    (void)env;
    (void)cls;
    np_host_assign_site(site);
}

/* ExgNative.siteN. Calls np_host_site_n. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_siteN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_site_n();
}

/* ExgNative.siteName. Calls np_host_site_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_siteName(JNIEnv *env, jclass cls, jint i)
{
    char buf[8];
    (void)cls;
    np_host_site_name(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.siteCore. Calls np_host_site_core and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_siteCore(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_site_core(i) ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.siteCh. Calls np_host_site_ch. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_siteCh(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_site_ch(i);
}

/* ExgNative.siteFlat. Writes two floats. Returns if the array is null or shorter than 2. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_siteFlat(JNIEnv *env, jclass cls, jint i, jfloatArray xy)
{
    float fx = 0, fy = 0;
    jfloat v[2];
    (void)cls;
    if (!xy || (*env)->GetArrayLength(env, xy) < 2) {
        return;
    }
    np_host_site_flat(i, &fx, &fy);
    v[0] = fx;
    v[1] = fy;
    (*env)->SetFloatArrayRegion(env, xy, 0, 2, v);
}

/* ExgNative.siteXyz. Writes x, y, z. Returns if the array is null or shorter than 3. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_siteXyz(JNIEnv *env, jclass cls, jint i, jfloatArray xyz)
{
    float x = 0, y = 0, z = 0;
    jfloat v[3];
    (void)cls;
    if (!xyz || (*env)->GetArrayLength(env, xyz) < 3) {
        return;
    }
    np_host_site_xyz(i, &x, &y, &z);
    v[0] = x;
    v[1] = y;
    v[2] = z;
    (*env)->SetFloatArrayRegion(env, xyz, 0, 3, v);
}

/* ExgNative.siteFocusLabel. Formats the focus site as its name and i, j, k. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_siteFocusLabel(JNIEnv *env, jclass cls)
{
    char name[8], buf[48];
    int x = 0, y = 0, z = 0;
    (void)cls;
    np_host_site_name(np_host_site_focus(), name, sizeof(name));
    np_host_site_ijk(np_host_site_focus(), &x, &y, &z);
    snprintf(buf, sizeof(buf), "%s  %d,%d,%d", name, x, y, z);
    return jstr_from(env, buf);
}

/* ExgNative.vizCells. Caps the copy at 40. Returns 0 if any array is null, xyz is shorter than 3 per cell, or rgba is short. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_vizCells(JNIEnv *env, jclass cls, jfloatArray xyz,
                                           jfloatArray size, jintArray rgba)
{
    float xyzb[40 * 3], sz[40];
    int col[40], n, cap;
    (void)cls;
    if (!xyz || !size || !rgba) {
        return 0;
    }
    cap = (*env)->GetArrayLength(env, size);
    if (cap > 40) {
        cap = 40;
    }
    if ((*env)->GetArrayLength(env, xyz) < cap * 3 || (*env)->GetArrayLength(env, rgba) < cap) {
        return 0;
    }
    n = np_host_viz_cells(xyzb, sz, col, cap);
    if (n > 0) {
        (*env)->SetFloatArrayRegion(env, xyz, 0, n * 3, xyzb);
        (*env)->SetFloatArrayRegion(env, size, 0, n, sz);
        (*env)->SetIntArrayRegion(env, rgba, 0, n, col);
    }
    return n;
}

/* ExgNative.smxSeq. Calls np_host_smx_seq. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_smxSeq(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)np_host_smx_seq();
}

/* ExgNative.smxFold. Calls np_host_smx_fold. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_smxFold(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)np_host_smx_fold();
}

/* ExgNative.profExport. Copies the Java string and calls np_host_prof_export. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profExport(JNIEnv *env, jclass cls, jstring path)
{
    char buf[256];
    (void)cls;
    jstr_to(env, path, buf, sizeof(buf));
    return np_host_prof_export(buf);
}

/* ExgNative.profImport. Copies the Java string and calls np_host_prof_import. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profImport(JNIEnv *env, jclass cls, jstring path)
{
    char buf[256];
    (void)cls;
    jstr_to(env, path, buf, sizeof(buf));
    return np_host_prof_import(buf);
}

/* ExgNative.idLine. Calls np_host_id and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_idLine(JNIEnv *env, jclass cls)
{
    char buf[64];
    (void)cls;
    np_host_id(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.recMs. Calls np_host_rec_ms. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_recMs(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_rec_ms();
}

/* ExgNative.matchLine. Atom id line when a take exists. Else empty if MATCH is off, "now —" while unnamed, or "now" plus the pose. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_matchLine(JNIEnv *env, jclass cls)
{
    char line[64];
    (void)cls;
    if (np_host_atom_count() > 0) {
        np_host_atom_id_line(line, sizeof(line));
        return jstr_from(env, line);
    }
    if (!np_host_match()) {
        return jstr_from(env, "");
    }
    {
        char buf[24];
        int i = np_host_learn_best();
        if (i < 0) {
            if (np_host_learn_n() > 0) {
                return jstr_from(env, "now —");
            }
            return jstr_from(env, "");
        }
        np_host_learn_name(i, buf, 24);
        snprintf(line, sizeof(line), "now %s", buf);
        return jstr_from(env, line);
    }
}

/* ExgNative.learnN. Calls np_host_learn_n. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_learnN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_learn_n();
}

/* ExgNative.learnName. Calls np_host_learn_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_learnName(JNIEnv *env, jclass cls, jint i)
{
    char buf[24];
    (void)cls;
    np_host_learn_name(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.learnScore. Calls np_host_learn_score. */
JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_learnScore(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_learn_score(i);
}

/* ExgNative.learnScoreCube. Calls np_host_learn_score_cube. */
JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_learnScoreCube(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_learn_score_cube(i);
}

/* ExgNative.learnBest. Calls np_host_learn_best. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_learnBest(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_learn_best();
}

/* ExgNative.learnSel. Calls np_host_learn_sel. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_learnSel(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_learn_sel();
}

/* ExgNative.learnSelect. Calls np_host_learn_select. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_learnSelect(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_learn_select(i);
}

/* ExgNative.learnDel. Calls np_host_learn_del. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_learnDel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_learn_del(i);
}

/* ExgNative.imuOk. Calls np_host_imu, drops the nine values, and returns whether a sample is present. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_imuOk(JNIEnv *env, jclass cls)
{
    float a[3], gyr[3], mag[3];
    (void)env;
    (void)cls;
    return np_host_imu(a, gyr, mag) ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.imu. Writes acc, gyr, and mag. Returns if the array is null or shorter than 9. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_imu(JNIEnv *env, jclass cls, jfloatArray dst)
{
    float a[3], gyr[3], mag[3], v[9];
    int k;
    (void)cls;
    if (!dst || (*env)->GetArrayLength(env, dst) < 9) {
        return;
    }
    np_host_imu(a, gyr, mag);
    for (k = 0; k < 3; k++) {
        v[k] = a[k];
        v[3 + k] = gyr[k];
        v[6 + k] = mag[k];
    }
    (*env)->SetFloatArrayRegion(env, dst, 0, 9, v);
}

/* ExgNative.boardImu. Calls np_host_board_imu and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_boardImu(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_board_imu() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.cycleBoard. Calls np_host_cycle_board. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleBoard(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_board();
}

/* ExgNative.uiScale. Calls np_host_ui_scale. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_uiScale(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_ui_scale();
}

/* ExgNative.cycleUiScale. Calls np_host_cycle_ui_scale. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleUiScale(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_ui_scale();
}

/* ExgNative.toggleAtom. Calls np_host_toggle_atom. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleAtom(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_atom();
}

/* ExgNative.atomStart. Calls np_host_atom_start. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_atomStart(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_atom_start();
}

/* ExgNative.atomStop. Calls np_host_atom_stop. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomStop(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_stop();
}

/* ExgNative.atomRef. Calls np_host_atom_ref and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomRef(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_atom_ref(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.atomOn. Calls np_host_atom and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_atomOn(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.atomN. Calls np_host_atom_n. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_n();
}

/* ExgNative.atomSave. Calls np_host_atom_save. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomSave(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_save();
}

/* ExgNative.atomLoad. Calls np_host_atom_load. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomLoad(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_load();
}

/* ExgNative.atomUnity. Calls np_host_atom_unity. */
JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_atomUnity(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_unity();
}

/* ExgNative.atomLine. Calls np_host_atom_line and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomLine(JNIEnv *env, jclass cls)
{
    char buf[64];
    (void)cls;
    np_host_atom_line(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.atomCount. Calls np_host_atom_count. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomCount(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_count();
}

/* ExgNative.atomAt. Calls np_host_atom_at and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomAt(JNIEnv *env, jclass cls, jint i)
{
    char buf[24];
    (void)cls;
    np_host_atom_at(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.atomSecs. Calls np_host_atom_secs. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomSecs(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_atom_secs(i);
}

/* ExgNative.atomSelect. Calls np_host_atom_select. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomSelect(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_atom_select(i);
}

/* ExgNative.atomDel. Calls np_host_atom_del. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_atomDel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_atom_del(i);
}

/* ExgNative.atomDiscard. Calls np_host_atom_discard. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_atomDiscard(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_atom_discard();
}

/* ExgNative.atomPick. Calls np_host_atom_pick. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_atomPick(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_atom_pick(i);
}

/* ExgNative.atomPair. Calls np_host_atom_pair and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomPair(JNIEnv *env, jclass cls)
{
    char buf[64];
    (void)cls;
    np_host_atom_pair(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.atomSlotA. Calls np_host_atom_slot_a and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomSlotA(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_atom_slot_a(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.atomSlotB. Calls np_host_atom_slot_b and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomSlotB(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_atom_slot_b(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.atomIdBest. Calls np_host_atom_id_best. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomIdBest(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_id_best();
}

/* ExgNative.atomIdScore. Calls np_host_atom_id_score. */
JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_atomIdScore(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_atom_id_score(i);
}

/* ExgNative.apiOn. Calls np_host_api_on and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_apiOn(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_api_on() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.setApiOn. Passes the boolean as 0 or 1 to np_host_api_set_on. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setApiOn(JNIEnv *env, jclass cls, jboolean on)
{
    (void)env;
    (void)cls;
    np_host_api_set_on(on ? 1 : 0);
}

/* ExgNative.apiLan. Calls np_host_api_lan and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_apiLan(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_api_lan() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.setApiLan. Passes the boolean as 0 or 1 to np_host_api_set_lan. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setApiLan(JNIEnv *env, jclass cls, jboolean lan)
{
    (void)env;
    (void)cls;
    np_host_api_set_lan(lan ? 1 : 0);
}

/* ExgNative.apiHz. Calls np_host_api_hz. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_apiHz(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_api_hz();
}

/* ExgNative.setApiHz. Calls np_host_api_set_hz. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setApiHz(JNIEnv *env, jclass cls, jint hz)
{
    (void)env;
    (void)cls;
    np_host_api_set_hz(hz);
}

/* ExgNative.apiHttp. Calls np_host_api_http. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_apiHttp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_api_http();
}

/* ExgNative.setApiHttp. Calls np_host_api_set_http. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setApiHttp(JNIEnv *env, jclass cls, jint port)
{
    (void)env;
    (void)cls;
    np_host_api_set_http(port);
}

/* ExgNative.apiUdp. Calls np_host_api_udp. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_apiUdp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_api_udp();
}

/* ExgNative.setApiUdp. Calls np_host_api_set_udp. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setApiUdp(JNIEnv *env, jclass cls, jint port)
{
    (void)env;
    (void)cls;
    np_host_api_set_udp(port);
}

/* ExgNative.apiTcp. Calls np_host_api_tcp. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_apiTcp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_api_tcp();
}

/* ExgNative.setApiTcp. Calls np_host_api_set_tcp. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setApiTcp(JNIEnv *env, jclass cls, jint port)
{
    (void)env;
    (void)cls;
    np_host_api_set_tcp(port);
}

/* ExgNative.apiToken. Calls np_host_api_token and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_apiToken(JNIEnv *env, jclass cls)
{
    char buf[32];
    (void)cls;
    np_host_api_token(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.setApiToken. Copies the Java string and calls np_host_api_set_token. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setApiToken(JNIEnv *env, jclass cls, jstring s)
{
    char buf[32];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    np_host_api_set_token(buf);
}

/* ExgNative.apiPush. Calls np_host_api_push and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_apiPush(JNIEnv *env, jclass cls)
{
    char buf[64];
    (void)cls;
    np_host_api_push(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.setApiPush. Copies the Java string and calls np_host_api_set_push. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setApiPush(JNIEnv *env, jclass cls, jstring s)
{
    char buf[64];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    np_host_api_set_push(buf);
}

/* ExgNative.apiLine. Calls np_host_api_line and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_apiLine(JNIEnv *env, jclass cls)
{
    char buf[160];
    (void)cls;
    np_host_api_line(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.linkApi. True when np_host_link is LAN rather than USB. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_linkApi(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_link() != 0 ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.linkPath. Calls np_host_link. 0 is USB, 1 is LAN. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_linkPath(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_link();
}

/* ExgNative.cycleLink. Calls np_host_cycle_link. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleLink(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_link();
}

/* ExgNative.setLinkApi. Passes the boolean as 0 or 1 to np_host_set_link. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setLinkApi(JNIEnv *env, jclass cls, jboolean api)
{
    (void)env;
    (void)cls;
    np_host_set_link(api ? 1 : 0);
}

/* ExgNative.setLinkPath. Calls np_host_set_link. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setLinkPath(JNIEnv *env, jclass cls, jint path)
{
    (void)env;
    (void)cls;
    np_host_set_link(path);
}

/* ExgNative.setSelf. Copies the Java string and calls np_host_set_self. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setSelf(JNIEnv *env, jclass cls, jstring s)
{
    char buf[24];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    np_host_set_self(buf);
}

/* ExgNative.linkDest. Calls np_host_link_dest and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_linkDest(JNIEnv *env, jclass cls)
{
    char buf[64];
    (void)cls;
    np_host_link_dest(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.setLinkDest. Copies the Java string and calls np_host_set_link_dest. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setLinkDest(JNIEnv *env, jclass cls, jstring s)
{
    char buf[64];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    np_host_set_link_dest(buf);
}

/* ExgNative.linkToken. Calls np_host_link_token and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_linkToken(JNIEnv *env, jclass cls)
{
    char buf[32];
    (void)cls;
    np_host_link_token(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.setLinkToken. Copies the Java string and calls np_host_set_link_token. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setLinkToken(JNIEnv *env, jclass cls, jstring s)
{
    char buf[32];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    np_host_set_link_token(buf);
}

/* ExgNative.followN. Calls np_host_follow_n. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_followN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_follow_n();
}

/* ExgNative.followName. Calls np_host_follow_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_followName(JNIEnv *env, jclass cls, jint i)
{
    char buf[24];
    (void)cls;
    np_host_follow_name(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.followDest. Calls np_host_follow_dest and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_followDest(JNIEnv *env, jclass cls, jint i)
{
    char buf[64];
    (void)cls;
    np_host_follow_dest(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.followUse. Calls np_host_follow_use. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_followUse(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_follow_use(i);
}

/* ExgNative.followDel. Calls np_host_follow_del. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_followDel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_follow_del(i);
}

/* ExgNative.allowN. Calls np_host_allow_n. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_allowN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_allow_n();
}

/* ExgNative.allowName. Calls np_host_allow_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_allowName(JNIEnv *env, jclass cls, jint i)
{
    char buf[24];
    (void)cls;
    np_host_allow_name(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.allowDel. Calls np_host_allow_del. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_allowDel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_allow_del(i);
}

/* ExgNative.followRemember. Copies the name, dest, and grant, then calls np_host_follow_remember. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_followRemember(JNIEnv *env, jclass cls, jstring name,
                                                 jstring dest, jstring grant)
{
    char nbuf[24], dbuf[64], gbuf[32];
    (void)cls;
    jstr_to(env, name, nbuf, sizeof(nbuf));
    jstr_to(env, dest, dbuf, sizeof(dbuf));
    jstr_to(env, grant, gbuf, sizeof(gbuf));
    np_host_follow_remember(nbuf, dbuf, gbuf);
}

/* ExgNative.followGrant. Copies the name and returns the grant text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_followGrant(JNIEnv *env, jclass cls, jstring name)
{
    char nbuf[24], gbuf[32];
    (void)cls;
    jstr_to(env, name, nbuf, sizeof(nbuf));
    np_host_follow_grant(nbuf, gbuf, sizeof(gbuf));
    return jstr_from(env, gbuf);
}

/* ExgNative.grantOk. Copies the grant and returns whether it is on the allow list. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_grantOk(JNIEnv *env, jclass cls, jstring grant)
{
    char buf[32];
    (void)cls;
    jstr_to(env, grant, buf, sizeof(buf));
    return np_host_grant_ok(buf) ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.pairBegin. Copies the name and calls np_host_pair_begin. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_pairBegin(JNIEnv *env, jclass cls, jstring name)
{
    char buf[24];
    (void)cls;
    jstr_to(env, name, buf, sizeof(buf));
    return np_host_pair_begin(buf);
}

/* ExgNative.pairState. Calls np_host_pair_state. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_pairState(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_pair_state();
}

/* ExgNative.pairName. Calls np_host_pair_name and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_pairName(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_pair_name(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.pairAccept. Calls np_host_pair_accept. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_pairAccept(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_pair_accept();
}

/* ExgNative.pairReject. Calls np_host_pair_reject. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_pairReject(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_pair_reject();
}

/* ExgNative.pairGrant. Calls np_host_pair_grant and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_pairGrant(JNIEnv *env, jclass cls)
{
    char buf[32];
    (void)cls;
    np_host_pair_grant(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.copyExg1. Needs room for one EXG1 frame. Returns 0 if the array is null or short. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_copyExg1(JNIEnv *env, jclass cls, jbyteArray dst)
{
    unsigned char raw[NP_API_FRAME];
    int n;
    (void)cls;
    if (!dst || (*env)->GetArrayLength(env, dst) < NP_API_FRAME) {
        return 0;
    }
    n = np_host_copy_exg1(raw, NP_API_FRAME);
    if (n > 0) {
        (*env)->SetByteArrayRegion(env, dst, 0, n, (jbyte *)raw);
    }
    return n;
}

/* ExgNative.feedExg1. Copies one EXG1 frame. Returns -1 if the array is null or short. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_feedExg1(JNIEnv *env, jclass cls, jbyteArray src)
{
    unsigned char raw[NP_API_FRAME];
    (void)cls;
    if (!src || (*env)->GetArrayLength(env, src) < NP_API_FRAME) {
        return -1;
    }
    (*env)->GetByteArrayRegion(env, src, 0, NP_API_FRAME, (jbyte *)raw);
    return np_host_feed_exg1(raw, NP_API_FRAME);
}

/* ExgNative.applyCfgJson. Copies the Java string and calls np_host_apply_cfg_json. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_applyCfgJson(JNIEnv *env, jclass cls, jstring js)
{
    char buf[1600];
    (void)cls;
    jstr_to(env, js, buf, sizeof(buf));
    np_host_apply_cfg_json(buf);
}

/* ExgNative.viewJson. Calls np_host_view_json and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_viewJson(JNIEnv *env, jclass cls)
{
    char buf[1400];
    (void)cls;
    buf[0] = 0;
    np_host_view_json(buf, sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.linkWire. Passes the boolean as 0 or 1 to np_host_link_wire. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_linkWire(JNIEnv *env, jclass cls, jboolean on)
{
    (void)env;
    (void)cls;
    np_host_link_wire(on ? 1 : 0);
}

/* ExgNative.kitExport. Returns the kit text, or empty when np_host_kit_export writes nothing. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_kitExport(JNIEnv *env, jclass cls)
{
    char buf[8192];
    int n;
    (void)cls;
    n = np_host_kit_export(buf, (int)sizeof(buf));
    if (n < 1) {
        return jstr_from(env, "");
    }
    return jstr_from(env, buf);
}

/* ExgNative.kitImport. Copies the Java string and calls np_host_kit_import. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_kitImport(JNIEnv *env, jclass cls, jstring s)
{
    char buf[8192];
    (void)cls;
    jstr_to(env, s, buf, sizeof(buf));
    return np_host_kit_import(buf, (int)strlen(buf));
}

/* ExgNative.boardMode. Calls np_host_board_mode. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_boardMode(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_board_mode();
}

/* ExgNative.setBoardMode. Calls np_host_set_board_mode. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setBoardMode(JNIEnv *env, jclass cls, jint mode)
{
    (void)env;
    (void)cls;
    np_host_set_board_mode(mode);
}

/* ExgNative.streamMode. Calls np_host_stream_mode. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_streamMode(JNIEnv *env, jclass cls, jint mode)
{
    (void)env;
    (void)cls;
    np_host_stream_mode(mode);
}

/* ExgNative.modeLabel. Calls np_host_mode_label and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_modeLabel(JNIEnv *env, jclass cls)
{
    char buf[64];
    (void)cls;
    buf[0] = 0;
    np_host_mode_label(buf, (int)sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.designSps. Calls np_host_design_sps. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_designSps(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_design_sps();
}

/* ExgNative.streamCold. Calls np_host_stream_cold and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_streamCold(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_stream_cold() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.fwNeed. Calls np_host_fw_need. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_fwNeed(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_fw_need();
}

/* ExgNative.fwHave. Calls np_host_fw_have. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_fwHave(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_fw_have();
}

/* ExgNative.fwSeen. Calls np_host_fw_seen. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_fwSeen(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_fw_seen();
}

/* ExgNative.fwBehind. Calls np_host_fw_behind and returns the boolean. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_fwBehind(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_fw_behind() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.fwMode. Calls np_host_fw_mode. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_fwMode(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_fw_mode();
}

/* ExgNative.setFwMode. Calls np_host_set_fw_mode. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setFwMode(JNIEnv *env, jclass cls, jint mode)
{
    (void)env;
    (void)cls;
    np_host_set_fw_mode(mode);
}

/* ExgNative.fwLabel. Calls np_host_fw_label and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_fwLabel(JNIEnv *env, jclass cls, jint mode)
{
    char buf[96];
    (void)cls;
    buf[0] = 0;
    np_host_fw_label(mode, buf, (int)sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.flashLog. Returns the flash log. Calls np_host_log_copy with which 0. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_flashLog(JNIEnv *env, jclass cls)
{
    char buf[4800];
    (void)cls;
    buf[0] = 0;
    np_host_log_copy(0, buf, (int)sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.flashState. Calls np_host_flash_state and returns the text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_flashState(JNIEnv *env, jclass cls)
{
    char buf[200];
    (void)cls;
    buf[0] = 0;
    np_host_flash_state(buf, (int)sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.debugLog. Returns the debug log. Calls np_host_log_copy with which 1. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_debugLog(JNIEnv *env, jclass cls)
{
    char buf[4800];
    (void)cls;
    buf[0] = 0;
    np_host_log_copy(1, buf, (int)sizeof(buf));
    return jstr_from(env, buf);
}

/* ExgNative.flashUpload. Calls np_host_flash_upload. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_flashUpload(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_flash_upload();
}

/* ExgNative.flashPreset. Passes the boolean as 0 or 1 to np_host_flash_preset. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_flashPreset(JNIEnv *env, jclass cls, jint mode, jboolean confirmed)
{
    (void)env;
    (void)cls;
    np_host_flash_preset(mode, confirmed ? 1 : 0);
}

/* ExgNative.flashModeOnly. Passes the boolean as 0 or 1 to np_host_flash_mode_only. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_flashModeOnly(JNIEnv *env, jclass cls, jint mode, jboolean confirmed)
{
    (void)env;
    (void)cls;
    np_host_flash_mode_only(mode, confirmed ? 1 : 0);
}
#endif

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

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_windowS(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_window_s();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleWindow(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_window();
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
Java_com_abysscore_exgc_ExgNative_cycleColor(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    np_host_cycle_color(ch);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setColor(JNIEnv *env, jclass cls, jint ch, jint rgb)
{
    (void)env;
    (void)cls;
    np_host_set_color(ch, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setScaleUv(JNIEnv *env, jclass cls, jint uv)
{
    (void)env;
    (void)cls;
    np_host_set_scale_uv(uv);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setWindowS(JNIEnv *env, jclass cls, jint s)
{
    (void)env;
    (void)cls;
    np_host_set_window_s(s);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setNotch(JNIEnv *env, jclass cls, jint hz)
{
    (void)env;
    (void)cls;
    np_host_set_notch(hz);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setHp(JNIEnv *env, jclass cls, jint hz)
{
    (void)env;
    (void)cls;
    np_host_set_hp(hz);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setLp(JNIEnv *env, jclass cls, jint hz)
{
    (void)env;
    (void)cls;
    np_host_set_lp(hz);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setBand(JNIEnv *env, jclass cls, jint band)
{
    (void)env;
    (void)cls;
    np_host_set_band(band);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setAlgo(JNIEnv *env, jclass cls, jint id)
{
    (void)env;
    (void)cls;
    np_host_set_algo(id);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setUiScale(JNIEnv *env, jclass cls, jint tenths)
{
    (void)env;
    (void)cls;
    np_host_set_ui_scale(tenths);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setBoardImu(JNIEnv *env, jclass cls, jboolean imu)
{
    (void)env;
    (void)cls;
    np_host_set_board_imu(imu ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setGain(JNIEnv *env, jclass cls, jint ch, jint gain)
{
    (void)env;
    (void)cls;
    np_host_set_gain(ch, gain);
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
Java_com_abysscore_exgc_ExgNative_setPortI(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_set_port_i(i);
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

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_lp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_lp();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleLp(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_lp();
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_car(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_car() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleCar(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_car();
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_detrend(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_detrend() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleDetrend(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_detrend();
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_envelope(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_envelope() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleEnvelope(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_envelope();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_band(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_band();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleBand(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_band();
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_clipped(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    return np_host_ch_clip(ch) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_algo(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_algo();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleAlgo(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_algo();
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_algoName(JNIEnv *env, jclass cls)
{
    char buf[16];
    (void)cls;
    np_host_algo_name(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_togglePause(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_pause();
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_paused(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_paused() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_csvOn(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_csv() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleCsv(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_csv();
}

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

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_cubeView(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_cube_view();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setCubeView(JNIEnv *env, jclass cls, jint map)
{
    (void)env;
    (void)cls;
    np_host_set_cube_view(map);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cubeSpin(JNIEnv *env, jclass cls, jfloat yaw, jfloat pitch)
{
    (void)env;
    (void)cls;
    np_host_cube_spin(yaw, pitch);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cubeZoom(JNIEnv *env, jclass cls, jint dir)
{
    (void)env;
    (void)cls;
    np_host_cube_zoom(dir);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cubeFront(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cube_front();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_elecSel(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_elec_sel();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_setElecSel(JNIEnv *env, jclass cls, jint ch)
{
    (void)env;
    (void)cls;
    np_host_set_elec_sel(ch);
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_elecLabel(JNIEnv *env, jclass cls, jint ch)
{
    char buf[16];
    (void)cls;
    np_host_elec_label(ch, buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_elecName(JNIEnv *env, jclass cls, jint ch)
{
    char buf[16];
    (void)cls;
    np_host_elec_name(ch, buf, sizeof(buf));
    return jstr_from(env, buf);
}

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

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_siteFocus(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_site_focus();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_siteStep(JNIEnv *env, jclass cls, jint dir)
{
    (void)env;
    (void)cls;
    np_host_site_step(dir);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_assignSite(JNIEnv *env, jclass cls, jint site)
{
    (void)env;
    (void)cls;
    np_host_assign_site(site);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_siteN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_site_n();
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_siteName(JNIEnv *env, jclass cls, jint i)
{
    char buf[8];
    (void)cls;
    np_host_site_name(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_siteCore(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_site_core(i) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_siteCh(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_site_ch(i);
}

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

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_smxSeq(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)np_host_smx_seq();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_smxFold(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)np_host_smx_fold();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profExport(JNIEnv *env, jclass cls, jstring path)
{
    char buf[256];
    (void)cls;
    jstr_to(env, path, buf, sizeof(buf));
    return np_host_prof_export(buf);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_profImport(JNIEnv *env, jclass cls, jstring path)
{
    char buf[256];
    (void)cls;
    jstr_to(env, path, buf, sizeof(buf));
    return np_host_prof_import(buf);
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_idLine(JNIEnv *env, jclass cls)
{
    char buf[64];
    (void)cls;
    np_host_id(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_recMs(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_rec_ms();
}

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
        int i = np_host_learn_best(), pct, cpct;
        if (i < 0) {
            return jstr_from(env, "");
        }
        np_host_learn_name(i, buf, 24);
        pct = (int)(np_host_learn_score(i) * 100.f);
        cpct = (int)(np_host_learn_score_cube(i) * 100.f);
        snprintf(line, sizeof(line), "now %s %d%%  cube %d%%", buf, pct, cpct);
        return jstr_from(env, line);
    }
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_learnN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_learn_n();
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_learnName(JNIEnv *env, jclass cls, jint i)
{
    char buf[24];
    (void)cls;
    np_host_learn_name(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_learnScore(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_learn_score(i);
}

JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_learnScoreCube(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_learn_score_cube(i);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_learnBest(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_learn_best();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_learnSel(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_learn_sel();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_learnSelect(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_learn_select(i);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_learnDel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_learn_del(i);
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_imuOk(JNIEnv *env, jclass cls)
{
    float a[3], gyr[3], mag[3];
    (void)env;
    (void)cls;
    return np_host_imu(a, gyr, mag) ? JNI_TRUE : JNI_FALSE;
}

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

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_boardImu(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_board_imu() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleBoard(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_board();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_uiScale(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_ui_scale();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_cycleUiScale(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_cycle_ui_scale();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_toggleAtom(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_toggle_atom();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_atomStart(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_atom_start();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomStop(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_stop();
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomRef(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_atom_ref(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_atomOn(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomN(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_n();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomSave(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_save();
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomLoad(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_load();
}

JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_atomUnity(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_unity();
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomLine(JNIEnv *env, jclass cls)
{
    char buf[64];
    (void)cls;
    np_host_atom_line(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomCount(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_count();
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomAt(JNIEnv *env, jclass cls, jint i)
{
    char buf[24];
    (void)cls;
    np_host_atom_at(i, buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomSecs(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_atom_secs(i);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomSelect(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_atom_select(i);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_atomDel(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_atom_del(i);
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_atomDiscard(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    np_host_atom_discard();
}

JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_atomPick(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    np_host_atom_pick(i);
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomPair(JNIEnv *env, jclass cls)
{
    char buf[64];
    (void)cls;
    np_host_atom_pair(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomSlotA(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_atom_slot_a(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_atomSlotB(JNIEnv *env, jclass cls)
{
    char buf[24];
    (void)cls;
    np_host_atom_slot_b(buf, sizeof(buf));
    return jstr_from(env, buf);
}

JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_atomIdBest(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_host_atom_id_best();
}

JNIEXPORT jfloat JNICALL
Java_com_abysscore_exgc_ExgNative_atomIdScore(JNIEnv *env, jclass cls, jint i)
{
    (void)env;
    (void)cls;
    return np_host_atom_id_score(i);
}
#endif

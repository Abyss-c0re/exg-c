#include "np_mindstorm.h"

#include <jni.h>
#include <stdint.h>

/* JNI for ExgNative mindstorm* methods. The token is never logged. */

/* ExgNative.mindstormSetIdentity. 16-byte id, 32-byte token. Returns 0, or -1
 * when either array is null or the wrong length. Does not log the token. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormSetIdentity(JNIEnv *env, jclass cls,
                                                       jbyteArray id, jbyteArray token)
{
    jbyte *idb = 0;
    jbyte *tokb = 0;
    jsize idn;
    jsize tokn;
    jint rc;

    (void)cls;
    if (!id || !token) {
        return -1;
    }
    idn = (*env)->GetArrayLength(env, id);
    tokn = (*env)->GetArrayLength(env, token);
    if (idn != 16 || tokn != 32) {
        return -1;
    }
    idb = (*env)->GetByteArrayElements(env, id, NULL);
    tokb = (*env)->GetByteArrayElements(env, token, NULL);
    if (!idb || !tokb) {
        if (idb) {
            (*env)->ReleaseByteArrayElements(env, id, idb, JNI_ABORT);
        }
        if (tokb) {
            (*env)->ReleaseByteArrayElements(env, token, tokb, JNI_ABORT);
        }
        return -1;
    }
    rc = np_mindstorm_set_identity((const uint8_t *)idb, 16, (const uint8_t *)tokb, 32);
    (*env)->ReleaseByteArrayElements(env, id, idb, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, token, tokb, JNI_ABORT);
    return rc;
}

/* ExgNative.mindstormRx. A null array or a length under 1 returns. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormRx(JNIEnv *env, jclass cls, jbyteArray bytes)
{
    jbyte *b;
    jsize n;

    (void)cls;
    if (!bytes) {
        return;
    }
    n = (*env)->GetArrayLength(env, bytes);
    if (n < 1) {
        return;
    }
    b = (*env)->GetByteArrayElements(env, bytes, NULL);
    if (!b) {
        return;
    }
    np_mindstorm_rx((const uint8_t *)b, (int)n);
    (*env)->ReleaseByteArrayElements(env, bytes, b, JNI_ABORT);
}

/* ExgNative.mindstormTx. The next frame, or a zero-length array when the
 * queue is empty. Returns null only when the array cannot be allocated. */
JNIEXPORT jbyteArray JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormTx(JNIEnv *env, jclass cls)
{
    uint8_t buf[256];
    int n;
    jbyteArray out;

    (void)cls;
    n = np_mindstorm_tx(buf, (int)sizeof buf);
    if (n < 0) {
        n = 0;
    }
    out = (*env)->NewByteArray(env, n);
    if (!out || n < 1) {
        return out;
    }
    (*env)->SetByteArrayRegion(env, out, 0, n, (const jbyte *)buf);
    return out;
}

/* ExgNative.mindstormHello. Returns 0, or -1 when the identity is missing
 * or the frame queue is full. */
JNIEXPORT jint JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormHello(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_mindstorm_hello();
}

/* ExgNative.mindstormAuthed. True after AUTH_OK until the session is reset. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormAuthed(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_mindstorm_authed() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.mindstormWindEnabled. True when closed windows may be sent. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormWindEnabled(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_mindstorm_wind_enabled() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.mindstormSetWind. Does not change the exg-c traces. */
JNIEXPORT void JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormSetWind(JNIEnv *env, jclass cls, jboolean on)
{
    (void)env;
    (void)cls;
    np_mindstorm_set_wind(on ? 1 : 0);
}

/* ExgNative.mindstormRateOk. True when the last rate rounded to 125, 250, or 500. */
JNIEXPORT jboolean JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormRateOk(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return np_mindstorm_rate_ok() ? JNI_TRUE : JNI_FALSE;
}

/* ExgNative.mindstormWindow. Null when there is no new window. Otherwise
 * sps as a little-endian u16, then little-endian int16 channels. */
JNIEXPORT jbyteArray JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormWindow(JNIEnv *env, jclass cls)
{
    int16_t ch[8];
    uint8_t raw[2 + 16];
    int sps = 0;
    int n;
    int i;
    jbyteArray out;

    (void)cls;
    n = np_mindstorm_copy_window(ch, 8, &sps);
    if (n < 1) {
        return NULL;
    }
    raw[0] = (uint8_t)(sps & 0xff);
    raw[1] = (uint8_t)((sps >> 8) & 0xff);
    for (i = 0; i < n; i++) {
        uint16_t u = (uint16_t)ch[i];
        raw[2 + i * 2] = (uint8_t)(u & 0xff);
        raw[2 + i * 2 + 1] = (uint8_t)((u >> 8) & 0xff);
    }
    out = (*env)->NewByteArray(env, 2 + n * 2);
    if (out) {
        (*env)->SetByteArrayRegion(env, out, 0, 2 + n * 2, (const jbyte *)raw);
    }
    return out;
}

/* ExgNative.mindstormStatus. Empty rather than null. ASCII status text. */
JNIEXPORT jstring JNICALL
Java_com_abysscore_exgc_ExgNative_mindstormStatus(JNIEnv *env, jclass cls)
{
    char buf[64];

    (void)cls;
    buf[0] = 0;
    np_mindstorm_status(buf, (int)sizeof buf);
    return (*env)->NewStringUTF(env, buf);
}

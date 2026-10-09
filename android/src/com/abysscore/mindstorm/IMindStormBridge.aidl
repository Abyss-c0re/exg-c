package com.abysscore.mindstorm;

/* Client bridge exported by com.abysscore.mindstorm.ClientBridgeService.
 * status is 0 pending, 1 granted, 2 denied.
 * token() is null until granted. A grant is 48 bytes: client id[16], then
 * the HMAC token[32]. Bytes after those 48 are ignored. Do not log token(). */
interface IMindStormBridge {
    int requestAccess(String label);
    byte[] token();
    String deviceHost();
    String bleAddress();
    int status();

    /* Queue one window on MindStorm's already-open link. samples are
     * little-endian int16 values, 1 to 8 channels. Returns 0 when queued.
     * Returns -1 when this UID is not granted, the link is down, or the
     * size is wrong. Does not open a radio. */
    int wind(int sps, in byte[] samples);

    /* 1 when this UID is granted and MindStorm's ball link is already authed. */
    int ownerUp();

    /* Switch that same link into mode 4, which the drive screen calls EEG.
     * A dark ball is raised to brightness 180 first. Returns 0, or -1 when
     * this UID is not granted or the link is down. Does not open USB. */
    int eegOn();
}

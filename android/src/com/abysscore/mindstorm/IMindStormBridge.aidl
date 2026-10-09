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
}

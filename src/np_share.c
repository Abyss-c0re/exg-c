#include "np_local.h"
#include "np_mods.h"

/* API server, LAN link, and the pair grant. */

static char pair_name[NP_PEER_NAME];
static char pair_grant[NP_PEER_GRANT];
static int pair_dec; /* 0 idle 1 wait 2 allow 3 no */
static uint32_t pair_t0;
static pthread_mutex_t pair_mu = PTHREAD_MUTEX_INITIALIZER;

/* 1 if the API server is on. It stays 0 until something turns it on. */
int np_host_api_on(void)
{
    return g.api_on ? 1 : 0;
}

/* Turns the server on or off and saves. Refuses to turn on when HTTP, UDP, and
 * TCP are all 0. A failed bind leaves the flag on and sets status. */
void np_host_api_set_on(int on)
{
    if (on && g.api_http <= 0 && g.api_udp <= 0 && g.api_tcp <= 0) {
        set_status(0, "share needs a port — settings 8765");
        return;
    }
    g.api_on = on ? 1 : 0;
    cfg_save();
    api_apply();
    if (g.api_on && !np_api_on()) {
        set_status(0, "share did not bind — pick a free port");
    }
}

/* 1 if the bind is all interfaces, 0 if it is loopback. */
int np_host_api_lan(void)
{
    return g.api_lan ? 1 : 0;
}

/* Nonzero binds all interfaces, 0 binds loopback. Saves and rebinds. Does not by
 * itself require a token. */
void np_host_api_set_lan(int lan)
{
    g.api_lan = lan ? 1 : 0;
    cfg_save();
    api_apply();
}

/* Publish rate in Hz. A stored value under 1 reads as 125. */
int np_host_api_hz(void)
{
    return g.api_hz < 1 ? 125 : g.api_hz;
}

/* Clamps the publish rate to 1..500 Hz, saves, and rebinds. */
void np_host_api_set_hz(int hz)
{
    if (hz < 1) {
        hz = 1;
    }
    if (hz > 500) {
        hz = 500;
    }
    g.api_hz = hz;
    cfg_save();
    api_apply();
}

/* HTTP port. 0 means that listener is off. */
int np_host_api_http(void)
{
    return g.api_http;
}

/* 1 if any two of the positive ports are equal. A 0 port is not a clash. */
static int port_clash(int http, int udp, int tcp)
{
    if (http > 0 && udp > 0 && http == udp) {
        return 1;
    }
    if (http > 0 && tcp > 0 && http == tcp) {
        return 1;
    }
    if (udp > 0 && tcp > 0 && udp == tcp) {
        return 1;
    }
    return 0;
}

/* Sets the HTTP port and saves. Outside 0..65535 becomes 8765, a clash with UDP
 * or TCP keeps the old port, and 0 is allowed. */
void np_host_api_set_http(int port)
{
    if (port < 0 || port > 65535) {
        port = 8765;
    }
    if (port_clash(port, g.api_udp, g.api_tcp)) {
        set_status(0, "settings port already used by EXG or spare");
        return;
    }
    g.api_http = port;
    cfg_save();
    api_apply();
}

/* UDP port. 0 means that listener is off. */
int np_host_api_udp(void)
{
    return g.api_udp;
}

/* Sets the UDP port and saves. Outside 0..65535 becomes 8766. A clash with HTTP
 * or TCP is refused and the old port stays. */
void np_host_api_set_udp(int port)
{
    if (port < 0 || port > 65535) {
        port = 8766;
    }
    if (port_clash(g.api_http, port, g.api_tcp)) {
        set_status(0, "EXG port already used by settings or spare");
        return;
    }
    g.api_udp = port;
    cfg_save();
    api_apply();
}

/* TCP port for 68-byte EXG1 frames. 0 means that listener is off. */
int np_host_api_tcp(void)
{
    return g.api_tcp;
}

/* Sets the TCP port and saves. Outside 0..65535 becomes 8767. A clash with HTTP
 * or UDP is refused and the old port stays. */
void np_host_api_set_tcp(int port)
{
    if (port < 0 || port > 65535) {
        port = 8767;
    }
    if (port_clash(g.api_http, g.api_udp, port)) {
        set_status(0, "spare port already used by settings or EXG");
        return;
    }
    g.api_tcp = port;
    cfg_save();
    api_apply();
}

/* Shared secret, or empty. Loopback GET does not need one. A null or empty buffer returns. */
void np_host_api_token(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.api_token);
}

/* Stores the secret with spaces and newlines removed, saves, and rebinds. Empty
 * is legal. Loopback GET still does not check it. */
void np_host_api_set_token(const char *s)
{
    int i, o = 0;
    if (!s) {
        s = "";
    }
    for (i = 0; s[i] && o < (int)sizeof(g.api_token) - 1; i++) {
        char c = s[i];
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            continue;
        }
        g.api_token[o++] = c;
    }
    g.api_token[o] = 0;
    cfg_save();
    api_apply();
}

/* Extra send address, or empty. A null or empty buffer returns. */
void np_host_api_push(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.api_push);
}

/* host:port for an extra send, saved. A bad address is cleared and still saved,
 * and empty turns the extra send off. */
void np_host_api_set_push(const char *s)
{
    char host[128];
    int hp = 0, up = 0;
    snprintf(g.api_push, sizeof(g.api_push), "%s", s ? s : "");
    if (g.api_push[0] && np_link_parse_dest(g.api_push, host, (int)sizeof(host), &hp, &up) != 0) {
        g.api_push[0] = 0;
        set_status(0, "extra send is host:port");
        cfg_save();
        return;
    }
    cfg_save();
    api_apply();
}

/* One status line from the API server, into out. */
void np_host_api_line(char *out, int n)
{
    np_api_line(out, n);
}

/* 1 if the plot is following LAN, 0 if it is on USB. */
int np_host_link(void)
{
    return g.link;
}

/* 1 follows LAN, anything else is USB. Disconnects a live board first. The same
 * value returns without saving. */
void np_host_set_link(int path)
{
    int want = path == 1 ? 1 : 0;
    if (g.link == want) {
        return;
    }
    if (g.connected) {
        do_disconnect();
    }
    g.link = want;
    cfg_save();
    if (g.link == 0) {
        set_status(1, "USB — Knight on this device");
    } else {
        set_status(1, "LAN — type dest host:port");
    }
}

/* Local peer name. Spaces become underscores, other punctuation is dropped, and
 * an empty name becomes "exg". Does not save. */
void np_host_set_self(const char *s)
{
    int i, o = 0;
    if (!s) {
        s = "";
    }
    for (i = 0; s[i] && o < NP_PEER_NAME - 1; i++) {
        char c = s[i];
        if (c == ' ') {
            c = '_';
        }
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.') {
            self_name[o++] = c;
        }
    }
    self_name[o] = 0;
    if (!self_name[0]) {
        snprintf(self_name, sizeof(self_name), "exg");
    }
}

/* Flips USB and LAN. A live board is disconnected on the way either direction. */
void np_host_cycle_link(void)
{
    np_host_set_link(g.link ? 0 : 1);
}

/* Follow address, host or host:port, or empty. A short or null buffer returns. */
void np_host_link_dest(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.link_dest);
}

/* Sets the follow address and saves. A bt: prefix is stored as empty, a string
 * that is not host or host:port keeps the old address, and a changed address
 * clears the link token. */
void np_host_set_link_dest(const char *s)
{
    char next[NP_API_PUSH];
    char host[128];
    int hp = 8765, up = 8766;
    snprintf(next, sizeof(next), "%s", s ? s : "");
    if (strncmp(next, "bt:", 3) == 0) {
        next[0] = 0;
    }
    if (next[0] && np_link_parse_dest(next, host, (int)sizeof(host), &hp, &up) != 0) {
        set_status(0, "dest is host or host:8765");
        return;
    }
    if (strcmp(g.link_dest, next) != 0) {
        g.link_token[0] = 0;
    }
    snprintf(g.link_dest, sizeof(g.link_dest), "%s", next);
    cfg_save();
}

/* Token sent when following, or empty. A short or null buffer returns. */
void np_host_link_token(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.link_token);
}

/* Stores the follow token and saves. Does not check that the peer will accept
 * it. A null pointer clears it. */
void np_host_set_link_token(const char *s)
{
    snprintf(g.link_token, sizeof(g.link_token), "%s", s ? s : "");
    cfg_save();
}

/* How many remembered follow peers are stored. */
int np_host_follow_n(void)
{
    return peers.nfollow;
}

/* Follow peer name at i, or empty. A short or null buffer returns. */
void np_host_follow_name(int i, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    if (i >= 0 && i < peers.nfollow) {
        snprintf(out, (size_t)n, "%s", peers.follow[i].name);
    }
}

/* Follow address at i, or empty. A short or null buffer returns. */
void np_host_follow_dest(int i, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    if (i >= 0 && i < peers.nfollow) {
        snprintf(out, (size_t)n, "%s", peers.follow[i].dest);
    }
}

/* Copies that peer's address and grant into the live follow and forces LAN on.
 * Does not disconnect a board that is still open. A blank or bt: address returns
 * without saving. */
void np_host_follow_use(int i)
{
    if (i < 0 || i >= peers.nfollow) {
        return;
    }
    if (!strncmp(peers.follow[i].dest, "bt:", 3) || !peers.follow[i].dest[0]) {
        set_status(0, "type dest host:port");
        return;
    }
    snprintf(g.link_dest, sizeof(g.link_dest), "%s", peers.follow[i].dest);
    snprintf(g.link_token, sizeof(g.link_token), "%s", peers.follow[i].grant);
    g.link = 1;
    cfg_save();
}

/* Removes follow peer i and writes the peer file. A bad index does not write. */
void np_host_follow_del(int i)
{
    np_peers_follow_del(&peers, i);
    peers_flush();
}

/* How many peers this device has allowed to pull EXG. */
int np_host_allow_n(void)
{
    return peers.nallow;
}

/* Allowed peer name at i, or empty. A short or null buffer returns. */
void np_host_allow_name(int i, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    if (i >= 0 && i < peers.nallow) {
        snprintf(out, (size_t)n, "%s", peers.allow[i].name);
    }
}

/* Removes allow entry i and writes the peer file. A bad index does not write. */
void np_host_allow_del(int i)
{
    np_peers_allow_del(&peers, i);
    peers_flush();
}

/* Grant stored for that follow name, or empty if the name is unknown. A short,
 * null, or null-name buffer returns. */
void np_host_follow_grant(const char *name, char *out, int n)
{
    int i;
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    if (!name) {
        return;
    }
    for (i = 0; i < peers.nfollow; i++) {
        if (strcmp(peers.follow[i].name, name) == 0) {
            snprintf(out, (size_t)n, "%s", peers.follow[i].grant);
            return;
        }
    }
}

/* 1 if this grant is on the allow list. 0 if it is empty or unknown. */
int np_host_grant_ok(const char *grant)
{
    return host_grant_ok(grant);
}

/* Adds or updates a follow peer, copies dest and grant into the live link, and
 * writes both the peer file and the ini. */
void np_host_follow_remember(const char *name, const char *dest, const char *grant)
{
    np_peers_follow_add(&peers, name, dest, grant);
    if (dest && dest[0]) {
        snprintf(g.link_dest, sizeof(g.link_dest), "%s", dest);
    }
    if (grant && grant[0]) {
        snprintf(g.link_token, sizeof(g.link_token), "%s", grant);
    }
    peers_flush();
    cfg_save();
}

/* Starts a pair ask. 2 if that name is already allowed and the grant is ready,
 * 1 if the user must answer, and an empty name asks as "exg". */
int np_host_pair_begin(const char *name)
{
    int i;
    pthread_mutex_lock(&pair_mu);
    if (name && name[0]) {
        for (i = 0; i < peers.nallow; i++) {
            if (strcmp(peers.allow[i].name, name) == 0) {
                snprintf(pair_name, sizeof(pair_name), "%s", name);
                snprintf(pair_grant, sizeof(pair_grant), "%s", peers.allow[i].grant);
                pair_dec = 2;
                pthread_mutex_unlock(&pair_mu);
                return 2;
            }
        }
    }
    snprintf(pair_name, sizeof(pair_name), "%s", name && name[0] ? name : "exg");
    pair_grant[0] = 0;
    pair_dec = 1;
    pair_t0 = pair_now_ms();
    pthread_mutex_unlock(&pair_mu);
    set_status(1, "%s wants EXG", pair_name);
    return 1;
}

/* Begins an ask when name is set; an empty name only reports state. On state 2,
 * copies the grant. State is 0 idle, 1 waiting, 2 allowed, 3 refused. */
int np_host_pair_ask(const char *name, char *grant, int gn)
{
    int st;
    if (name && name[0]) {
        st = np_host_pair_begin(name);
    } else {
        st = np_host_pair_state();
    }
    if (grant && gn > 1) {
        grant[0] = 0;
        if (st == 2) {
            np_host_pair_grant(grant, gn);
        }
    }
    return st;
}

/* 0 idle, 1 waiting, 2 allowed, 3 refused. A wait older than 60 s becomes
 * refused and the grant is cleared. */
int np_host_pair_state(void)
{
    int d;
    pthread_mutex_lock(&pair_mu);
    if (pair_dec == 1 && pair_t0 && pair_now_ms() - pair_t0 > 60000u) {
        pair_dec = 3;
        pair_grant[0] = 0;
    }
    d = pair_dec;
    pthread_mutex_unlock(&pair_mu);
    return d;
}

/* Name of the peer in the current ask, or empty. A short or null buffer returns. */
void np_host_pair_name(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    pthread_mutex_lock(&pair_mu);
    snprintf(out, (size_t)n, "%s", pair_name);
    pthread_mutex_unlock(&pair_mu);
}

/* If a request is waiting, mints a grant, stores the allow, and writes the peer
 * file. The status line says allowed even when nothing was waiting. */
void np_host_pair_accept(void)
{
    pthread_mutex_lock(&pair_mu);
    if (pair_dec == 1) {
        np_peers_mkgrant(pair_grant, (int)sizeof(pair_grant));
        np_peers_allow_add(&peers, pair_name, pair_grant);
        peers_flush();
        pair_dec = 2;
    }
    pthread_mutex_unlock(&pair_mu);
    set_status(1, "EXG allowed");
}

/* If a request is waiting, state becomes refused and the grant is cleared. The
 * status line says refused even when nothing was waiting. */
void np_host_pair_reject(void)
{
    pthread_mutex_lock(&pair_mu);
    if (pair_dec == 1) {
        pair_dec = 3;
        pair_grant[0] = 0;
    }
    pthread_mutex_unlock(&pair_mu);
    set_status(0, "EXG refused");
}

/* Grant from the current ask, or empty. A short or null buffer returns. */
void np_host_pair_grant(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    pthread_mutex_lock(&pair_mu);
    snprintf(out, (size_t)n, "%s", pair_grant);
    pthread_mutex_unlock(&pair_mu);
}

/* Copies the latest 68-byte EXG1 frame. 0 if there is no sample or cap is under 68. */
int np_host_copy_exg1(unsigned char *dst, int cap)
{
    struct np_api_sample s;
    if (!np_api_latest(&s)) {
        return 0;
    }
    return np_api_pack(dst, cap, &s);
}

/* Feeds one 68-byte EXG1 frame into the LAN follow path. -1 if it is not an EXG1
 * frame, and 0 if it was accepted. */
int np_host_feed_exg1(const unsigned char *raw, int n)
{
    struct np_api_sample s;
    if (np_api_unpack(raw, n, &s) != NP_API_FRAME) {
        return -1;
    }
    link_on_sample(&s);
    return 0;
}

/* Applies view JSON for filters, scale in µV, window seconds, colors, sites, and
 * active flags, and returns on empty text. NEG RAIL forces bias and CAR off,
 * nothing is saved, and an elec list does change the map. */
void np_host_apply_cfg_json(const char *js)
{
    apply_link_cfg(js);
}

/* Writes a JSON object body, without braces, of the view: filters, scale in µV,
 * window in seconds, colors, sites, NEG RAIL, and the ID string. A buffer under
 * 8 bytes returns without writing. */
void np_host_view_json(char *out, int n)
{
    api_view_json(out, n);
}

/* Does nothing. The argument is ignored, and no socket is opened or closed. */
void np_host_link_wire(int on)
{
    (void)on;
}

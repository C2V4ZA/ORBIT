/* Privacy Shield (docs/next/PRIVACY-SHIELD.md) on the PC: address rule, the host side (direct
 * connections refused, LAN discovery silent) and two complete apps (no direct invitation, a direct
 * code is refused with a visible reason, never a silent direct fallback). The relay/tunnel paths
 * with the shield on are covered by tools/test_relay.sh and tools/test_tunnel.sh (test-app runs
 * with the shield on there). */
#include "i18n.h"
#include "ui.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static UiState view;
static int wait_for(OrbitApp *a, int (*cond)(const UiState *), int ms) {
    for (int t = 0; t < ms; t += 50) {
        view.revision = 0;
        app_snapshot(a, &view);
        if (cond(&view))
            return 1;
        usleep(50000);
    }
    return 0;
}
static int has_group(const UiState *s) { return s->group_count == 1; }
static int not_busy(const UiState *s) { return !s->busy; }
static int invite_done(const UiState *s) { return s->invite_state == 2 || s->invite_state == 3; }
static int invite_ready(const UiState *s) { return s->invite_state == 2; }
static int shield_refused(const UiState *s) {
    return strstr(s->link_error, T(T_SHIELD_BLOCKED)) || strstr(s->status, T(T_SHIELD_BLOCKED));
}
static int joined(const UiState *s) { return s->group_count == 1 && s->groups[0].state == LINK_ONLINE && !s->groups[0].own; }
static int shield_note(const UiState *s) { return strstr(s->status, "Privacy Shield") != NULL; }

/* a non-loopback address of this PC (no packet is sent: UDP connect only picks a route) */
static int own_lan_ip(char out[16]) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &a.sin_addr);
    socklen_t l = sizeof(a);
    int ok = fd >= 0 && !connect(fd, (struct sockaddr *)&a, sizeof(a)) &&
             !getsockname(fd, (struct sockaddr *)&a, &l) && (ntohl(a.sin_addr.s_addr) >> 24) != 127 &&
             a.sin_addr.s_addr != 0;
    if (ok)
        inet_ntop(AF_INET, &a.sin_addr, out, 16);
    if (fd >= 0)
        close(fd);
    return ok;
}

/* waits until `path` contains (want=1) or no longer contains (want=0) `needle` */
static int wait_for_file(const char *path, const char *needle, int want, int ms) {
    for (int t = 0; t < ms; t += 100) {
        FILE *f = fopen(path, "r");
        char buf[8192];
        size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
        if (f)
            fclose(f);
        buf[n] = 0;
        if (f && (strstr(buf, needle) != NULL) == want)
            return 1;
        usleep(100000);
    }
    return 0;
}
int main(void) {
    /* 1. address rule */
    uint32_t tun[2] = {0x01020304, 0};
    assert(orbit_shield_allows(0x7f000001, ORBIT_INVITE_RELAY | 31400, NULL, 0)); /* relay */
    assert(orbit_shield_allows(0x01020304, 41000, tun, 1));                        /* tunnel */
    assert(!orbit_shield_allows(0x05060708, 41000, tun, 1));                       /* a console */
    assert(!orbit_shield_allows(0xc0a80002, 31413, tun, 2));                       /* LAN console */
    assert(!orbit_shield_allows(0, 41000, tun, 2) && !orbit_shield_allows(0x01020304, 0, tun, 1));
    assert(!orbit_shield_allows(0x01020304, 41000, NULL, 0));
    puts("PASS: shield address rule: relay and own tunnel server allowed, any console address refused");

    /* 2. host side */
    OrbitStore *store = calloc(1, sizeof(*store));
    assert(!orbit_store_open(store, NULL));
    store->data.group[3].used = 1;
    store->data.group[3].members = 1;
    strcpy(store->data.group[3].invite, "QRST2345");
    uint8_t key[32] = {1};
    OrbitServer *srv = orbit_server_start(store, key, 31530);
    assert(srv);
    char tag[17], found[16], lan[16];
    orbit_group_tag(tag, "QRST2345");
    orbit_server_set_shield(srv, 1);
    assert(orbit_discover(tag, 31530, "", found, 600)); /* silent */
    OrbitLink *l = orbit_connect_group("127.0.0.1", 31530, "QRST-2345", NULL, 0);
    assert(l); /* what the own tunnel/relay splice does */
    assert(orbit_link_forward_secret(l)); /* 1.3 <-> 1.3: ECDHE-PSK */
    orbit_disconnect(l);
    orbit_tls_legacy = 1; /* an ORBIT 1.0-1.2 client: plain PSK only */
    l = orbit_connect_group("127.0.0.1", 31530, "QRST-2345", NULL, 0);
    assert(l && !orbit_link_forward_secret(l));
    orbit_disconnect(l);
    orbit_tls_legacy = 0;
    assert(orbit_server_shield_refused(srv) == 0);
    puts("PASS: TLS forward secrecy: 1.3 <-> 1.3 uses ECDHE-PSK (X25519, ChaCha20-Poly1305); an older "
         "client with plain PSK still connects");
    if (own_lan_ip(lan)) {
        assert(!orbit_connect_group(lan, 31530, "QRST-2345", NULL, 0));
        assert(orbit_server_shield_refused(srv) == 1);
        orbit_server_set_shield(srv, 0);
        l = orbit_connect_group(lan, 31530, "QRST-2345", NULL, 0);
        assert(l); /* shield off: the same direct connection works, so the shield refused it */
        orbit_disconnect(l);
        printf("PASS: shield on: direct connection to %s refused by the host, own splice (127.0.0.1) "
               "accepted, LAN discovery silent\n", lan);
    } else
        puts("PASS: shield on: own splice (127.0.0.1) accepted, LAN discovery silent (no LAN address on "
             "this PC: direct-refusal case skipped)");
    orbit_server_set_shield(srv, 0);
    assert(!orbit_discover(tag, 31530, "", found, 1500));
    orbit_server_stop(srv);
    orbit_store_close(store);
    free(store);

    /* 3. two apps, no tunnel, no relay */
    assert(!system("rm -rf /tmp/orbit-shield-a /tmp/orbit-shield-b && mkdir -p /tmp/orbit-shield-a /tmp/orbit-shield-b"));
    static OrbitApp A, B;
    app_shield_default = 1;
    assert(!app_start(&A, "/tmp/orbit-shield-a", "127.0.0.1", "255.0.0.0", "", "Alex", 31531));
    assert(!app_start(&B, "/tmp/orbit-shield-b", "127.0.0.1", "255.0.0.0", "", "Mika", 31532));
    app_snapshot(&A, &view);
    assert(view.set.shield == 1); /* on by default */
    for (int i = 0; i < 40 && app_create_group(&A, "Spieleabend"); i++)
        usleep(100000);
    assert(wait_for(&A, has_group, 8000) && wait_for(&A, not_busy, 4000));
    assert(!app_invite(&A));
    assert(wait_for(&A, invite_done, 20000));
    assert(view.invite_state == 3);
    assert(wait_for(&A, shield_note, 2000));
    assert(!view.friend_code[0]); /* no friend code with this console's own address either */
    puts("PASS: shield on without tunnel/relay: no invitation and no friend code with the console's "
         "own address; the reason is shown");

    app_set_shield(&A, 0); /* A hands out a direct (loopback) code */
    assert(wait_for(&A, not_busy, 4000));
    assert(!app_invite(&A));
    assert(wait_for(&A, invite_ready, 20000));
    char invite[ORBIT_INVITE_TEXT];
    strcpy(invite, view.invite_code);
    assert(!app_join(&B, invite));
    assert(wait_for(&B, shield_refused, 15000));
    app_snapshot(&B, &view);
    assert(!(view.group_count == 1 && view.groups[0].state == LINK_ONLINE));
    puts("PASS: shield on: a code with a console's direct address is refused with a visible "
         "\"Privacy Shield\" reason, no silent direct fallback");
    app_set_shield(&B, 0);
    assert(wait_for(&B, joined, 20000)); /* the same code works once the user turns the shield off */
    puts("PASS: shield off by the user: the same code connects (only then)");

    /* background notifications (PS3 plugin) dial on their own: with the shield back on, the
     * plugin config must not contain the host console's direct address any more */
    assert(wait_for_file("/tmp/orbit-shield-b/notify.cfg", "host\t127.0.0.1\t", 1, 15000));
    app_set_shield(&B, 1);
    assert(wait_for_file("/tmp/orbit-shield-b/notify.cfg", "host\t127.0.0.1\t", 0, 15000));
    puts("PASS: shield on: the background-notification plugin gets no direct console address either");
    app_set_shield(&B, 0); /* a non-default value for the restart check below */

    app_stop(&A);
    app_stop(&B);
    memset(&B, 0, sizeof(B)); /* the setting survives a restart */
    assert(!app_start(&B, "/tmp/orbit-shield-b", "127.0.0.1", "255.0.0.0", "", "Mika", 31532));
    app_snapshot(&B, &view);
    assert(view.set.shield == 0);
    app_stop(&B);
    puts("PASS: shield setting is stored");
    return 0;
}

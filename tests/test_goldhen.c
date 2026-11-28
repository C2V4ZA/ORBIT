/* ORBIT 1.3 (PS4): install / remove of the GoldHEN plugin line, on the PC in a temporary root.
 * Checks that every other plugin, section and line of plugins.ini stays exactly as it was. */
#include "goldhen_plugins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define ME GOLDHEN_PLUGIN

static char out[8192];
static const char *add(const char *in) {
    int n = goldhen_ini_add(in, strlen(in), ME, out, sizeof(out));
    return n < 0 ? "<fail>" : out;
}
static const char *del(const char *in) {
    int n = goldhen_ini_remove(in, strlen(in), ME, out, sizeof(out));
    return n < 0 ? "<fail>" : out;
}
static void put_file(const char *p, const char *text) {
    FILE *f = fopen(p, "wb");
    fputs(text, f);
    fclose(f);
}
static char *get_file(const char *p) {
    static char buf[8192];
    FILE *f = fopen(p, "rb");
    if (!f)
        return NULL;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

int main(void) {
    /* ---------- text level ---------- */
    CHECK(!strcmp(add(""), "[default]\n" ME "\n"));
    CHECK(!strcmp(add("[default]\n/data/GoldHEN/plugins/game_patch.prx\n"),
                  "[default]\n" ME "\n/data/GoldHEN/plugins/game_patch.prx\n"));
    /* game sections stay; our line goes under [default], never under a CUSA section */
    const char *games = "; my plugins\n[CUSA00001]\n/data/GoldHEN/plugins/a.prx\n[Default]\r\n/data/GoldHEN/plugins/b.prx\r\n";
    CHECK(!strcmp(add(games), "; my plugins\n[CUSA00001]\n/data/GoldHEN/plugins/a.prx\n[Default]\r\n" ME
                              "\r\n/data/GoldHEN/plugins/b.prx\r\n"));
    CHECK(!strcmp(add("[CUSA00001]\n/data/GoldHEN/plugins/a.prx"),
                  "[CUSA00001]\n/data/GoldHEN/plugins/a.prx\n[default]\n" ME "\n"));
    CHECK(!strcmp(add("[default]"), "[default]\n" ME "\n"));
    /* only listed when under [default]; a CUSA entry of the same path does not count */
    const char *in_game = "[CUSA00001]\n" ME "\n";
    CHECK(!goldhen_ini_listed(in_game, strlen(in_game), ME));
    const char *twice = "[default]\n  " ME "  \n";
    CHECK(goldhen_ini_listed(twice, strlen(twice), ME) && !strcmp(add(twice), twice)); /* no duplicate */
    /* add then remove gives back the original byte for byte */
    const char *orig[] = {"", "[default]\n/data/GoldHEN/plugins/game_patch.prx\n", games,
                          "[default]\n/x.prx\n[CUSA1]\n/y.prx\n"};
    for (size_t i = 0; i < sizeof(orig) / sizeof(orig[0]); i++) {
        char added[8192];
        snprintf(added, sizeof(added), "%s", add(orig[i]));
        const char *back = del(added);
        if (orig[i][0])
            CHECK(!strcmp(back, orig[i]));
        else
            CHECK(!strcmp(back, "[default]\n")); /* the section we created stays, empty: harmless */
    }
    /* remove touches only our exact path, not a similar one */
    const char *similar = "[default]\n/data/GoldHEN/plugins/orbit_notify.prx.old\n" ME "\n";
    CHECK(!strcmp(del(similar), "[default]\n/data/GoldHEN/plugins/orbit_notify.prx.old\n"));
    /* too small an output buffer fails instead of writing half a file */
    CHECK(goldhen_ini_add(games, strlen(games), ME, out, 10) == -1);
    if (failures == 0)
    printf("PASS: plugins.ini text: line under [default] (created when missing, CRLF kept), CUSA sections "
           "untouched, no duplicate, add+remove gives the original back, similar paths untouched\n");

    int text_failures = failures;
    /* ---------- files in a temporary root ---------- */
    char root[] = "/tmp/orbit-goldhen-XXXXXX", p[600], src[600];
    CHECK(mkdtemp(root) != NULL);
    snprintf(src, sizeof(src), "%s/orbit_notify.prx", root);
    put_file(src, "PRX-BYTES");
    /* no GoldHEN folder: nothing is written */
    CHECK(goldhen_plugin_install(root, src) == -1 && !goldhen_plugin_installed(root));
    snprintf(p, sizeof(p), "%s/data", root);
    mkdir(p, 0700);
    snprintf(p, sizeof(p), "%s%s", root, GOLDHEN_DIR);
    mkdir(p, 0700);
    snprintf(p, sizeof(p), "%s/data/GoldHEN/plugins", root);
    CHECK(access(p, F_OK) != 0);
    const char *user_ini = "[default]\n/data/GoldHEN/plugins/game_patch.prx\n[CUSA07408]\n/data/GoldHEN/plugins/x.prx\n";
    snprintf(p, sizeof(p), "%s%s", root, GOLDHEN_INI);
    put_file(p, user_ini);
    CHECK(goldhen_plugin_install(root, src) == 0 && goldhen_plugin_installed(root));
    char *t = get_file(p);
    CHECK(t && !strcmp(t, "[default]\n" ME "\n/data/GoldHEN/plugins/game_patch.prx\n[CUSA07408]\n/data/GoldHEN/plugins/x.prx\n"));
    snprintf(p, sizeof(p), "%s%s", root, GOLDHEN_PLUGIN);
    t = get_file(p);
    CHECK(t && !strcmp(t, "PRX-BYTES"));
    snprintf(p, sizeof(p), "%s%s.orbit-backup", root, GOLDHEN_INI);
    t = get_file(p);
    CHECK(t && !strcmp(t, user_ini));
    /* a second install changes nothing and keeps the first backup */
    CHECK(goldhen_plugin_install(root, src) == 0);
    snprintf(p, sizeof(p), "%s%s", root, GOLDHEN_INI);
    t = get_file(p);
    CHECK(t && strstr(t, ME) && !strstr(strstr(t, ME) + 1, ME)); /* listed once */
    /* remove: our line and file go, everything else is exactly as the user had it */
    CHECK(goldhen_plugin_remove(root) == 0 && !goldhen_plugin_installed(root));
    t = get_file(p);
    CHECK(t && !strcmp(t, user_ini));
    snprintf(p, sizeof(p), "%s%s", root, GOLDHEN_PLUGIN);
    CHECK(access(p, F_OK) != 0);
    CHECK(goldhen_plugin_remove(root) == 1); /* nothing left to do */
    /* fresh GoldHEN without plugins.ini: created with our section */
    snprintf(p, sizeof(p), "%s%s", root, GOLDHEN_INI);
    remove(p);
    snprintf(p, sizeof(p), "%s%s.orbit-backup", root, GOLDHEN_INI);
    remove(p);
    CHECK(goldhen_plugin_install(root, src) == 0 && goldhen_plugin_installed(root));
    snprintf(p, sizeof(p), "%s%s", root, GOLDHEN_INI);
    t = get_file(p);
    CHECK(t && !strcmp(t, "[default]\n" ME "\n"));
    /* missing source file: fails, plugins.ini untouched */
    CHECK(goldhen_plugin_remove(root) == 0);
    CHECK(goldhen_plugin_install(root, "/nonexistent/orbit_notify.prx") == -1 && !goldhen_plugin_installed(root));
    t = get_file(p);
    CHECK(t && !strcmp(t, "[default]\n"));
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
    if (failures == text_failures)
    printf("PASS: GoldHEN files: nothing written without /data/GoldHEN, plugin copied, plugins.ini edited with "
           "one backup, second install no change, remove restores the user's file exactly\n");
    if (failures)
        printf("%d FAILED\n", failures);
    return failures != 0;
}

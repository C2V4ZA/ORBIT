#ifndef ORBIT_GOLDHEN_PLUGINS_H
#define ORBIT_GOLDHEN_PLUGINS_H
/* ORBIT 1.3 (PS4): installs / removes the background-notification plugin for GoldHEN.
 *
 * GoldHEN 2.3+ loads the .prx files listed in /data/GoldHEN/plugins.ini into games:
 *   [default]                  <- loaded into every game
 *   /data/GoldHEN/plugins/x.prx
 *   [CUSA12345]                <- only into that game
 * We add exactly one line (our absolute path) directly under [default] (the section is created at
 * the end when there is none) and on removal delete exactly that line; every other line, section
 * and plugin stays as it was. Before the first change a copy plugins.ini.orbit-backup is kept.
 *
 * root is prepended to every path: "" on the PS4, a temporary folder in the PC tests. */
#include <stddef.h>

#define GOLDHEN_DIR "/data/GoldHEN"
#define GOLDHEN_INI GOLDHEN_DIR "/plugins.ini"
#define GOLDHEN_PLUGIN GOLDHEN_DIR "/plugins/orbit_notify.prx" /* also the line in plugins.ini */

/* text level (no files): out gets the new plugins.ini; returns its length, -1 if cap is too small.
 * Adding a line that is already listed under [default] returns the text unchanged. */
int goldhen_ini_add(const char *in, size_t n, const char *line, char *out, size_t cap);
int goldhen_ini_remove(const char *in, size_t n, const char *line, char *out, size_t cap);
/* 1 when line is listed in the [default] section */
int goldhen_ini_listed(const char *in, size_t n, const char *line);

/* files: 0 = done, 1 = nothing to do (remove only), -1 = failed (no GoldHEN folder, not writable) */
int goldhen_plugin_install(const char *root, const char *prx_source);
int goldhen_plugin_remove(const char *root);
int goldhen_plugin_installed(const char *root);
#endif

/* Orbit UI localization. Only Orbit's own interface text is translated: user messages, names,
 * server/channel names, bios and custom statuses are always shown exactly as written. */
#ifndef ORBIT_I18N_H
#define ORBIT_I18N_H
enum { LANG_DE, LANG_EN, LANG_FR, LANG_ES, LANG_COUNT };
#define X(key, de, en, fr, es) key,
enum {
#include "i18n_keys.h"
    T_COUNT
};
#undef X
/* Current UI language (default English). */
void i18n_set(int lang);
int i18n_lang(void);
/* PS3 system language (SYSUTIL_LANG_*) -> Orbit language; unsupported ones fall back to English. */
int i18n_from_ps3(int ps3_lang);
const char *T(int key);
/* Same text in a given language (e.g. host-side error texts). */
const char *T_in(int lang, int key);
/* Stable text key name ("E_FULL") <-> id, used in the network protocol. -1 if unknown. */
const char *i18n_name(int key);
int i18n_find(const char *name);
/* printf-style formatting of a translated template ("%s", "%d" placeholders). */
int TF(char *out, unsigned cap, int key, ...);
const char *i18n_lang_name(int lang); /* native name, e.g. "Français" */
#endif

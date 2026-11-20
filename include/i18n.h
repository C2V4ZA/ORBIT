/* Orbit UI localization. Only Orbit's own interface text is translated: user messages, names,
 * server/channel names, bios and custom statuses are always shown exactly as written.
 *
 * 1.2 (languages): every language of the registry has a stable id below. German, English,
 * French, Spanish, Italian and Portuguese are built in (compiled tables). Further languages come
 * from UTF-8 resource files "<locale>.lang" (lang/ in the repository, USRDIR/lang/ on the PS3),
 * loaded and checked at start-up; any text a file does not have falls back to English. */
#ifndef ORBIT_I18N_H
#define ORBIT_I18N_H
#include <stddef.h>
/* Stored language ids never change: new languages are only ever appended. */
enum {
    LANG_DE,
    LANG_EN,
    LANG_FR,
    LANG_ES,
    LANG_IT,
    LANG_PT,
    LANG_PT_BR,
    LANG_RU,
    LANG_UK,
    LANG_NB,
    LANG_SV,
    LANG_DA,
    LANG_FI,
    LANG_NL,
    LANG_PL,
    LANG_CS,
    LANG_SK,
    LANG_TR,
    LANG_RO,
    LANG_HU,
    LANG_EL,
    LANG_JA,
    LANG_KO,
    LANG_ZH_CN,
    LANG_ZH_TW,
    LANG_AR,
    LANG_HI,
    LANG_COUNT
};
#define LANG_BASE 4    /* languages with a column in i18n_keys_*.h */
#define LANG_BUILTIN 6 /* DE..PT: compiled in; the rest come from .lang files */
#define X(key, de, en, fr, es) key,
enum {
#include "i18n_keys.h"
    T_COUNT
};
#undef X

/* writing systems; the bundled font (DejaVu-based OrbitSans) covers Latin, Greek and Cyrillic */
enum { SCRIPT_LATIN, SCRIPT_GREEK, SCRIPT_CYRILLIC, SCRIPT_CJK, SCRIPT_HANGUL, SCRIPT_ARABIC, SCRIPT_DEVANAGARI };
/* CLDR plural rule families (whole numbers) */
enum { PLR_ONE_OTHER, PLR_FRENCH, PLR_EAST_SLAVIC, PLR_POLISH, PLR_CZECH, PLR_ROMANIAN, PLR_NONE, PLR_ARABIC, PLR_HINDI };
/* CLDR plural categories */
enum { PL_ZERO, PL_ONE, PL_TWO, PL_FEW, PL_MANY, PL_OTHER, PL_COUNT };
typedef struct {
    const char *id;      /* BCP 47 locale id: "de", "pt-BR", "nb-NO", "zh-CN" */
    const char *native;  /* own name in its own script: "Русский" */
    const char *english; /* English name: "Russian" */
    int script, rtl, plural;
    int parent; /* language used for missing texts before English (pt-BR -> pt), -1 = English */
} OrbitLocale;
const OrbitLocale *i18n_locale(int lang); /* NULL for an unknown id */
int i18n_find_locale(const char *id);     /* "ru" -> LANG_RU, -1 if unknown */

/* why a language cannot be chosen (0 = it can) */
enum { LANG_OK, LANG_NO_FONT, LANG_NO_SHAPING, LANG_NO_TEXT };
int i18n_unavailable(int lang);
int i18n_script_supported(int script);
/* share of the interface texts the language has itself, 0..100 (100 only when every text is) */
int i18n_complete(int lang);

/* Current UI language (default English). */
void i18n_set(int lang);
int i18n_lang(void);
/* PS3 system language (SYSUTIL_LANG_*) -> Orbit language; languages that cannot be shown
 * (no text, no font) fall back to English. */
int i18n_from_ps3(int ps3_lang);
const char *T(int key);
/* Same text in a given language (e.g. host-side error texts). */
const char *T_in(int lang, int key);
/* Stable text key name ("E_FULL") <-> id, used in the network protocol. -1 if unknown. */
const char *i18n_name(int key);
int i18n_find(const char *name);
/* printf-style formatting of a translated template ("%s", "%d" placeholders). */
int TF(char *out, unsigned cap, int key, ...);
/* the same, with the plural form for n ("KEY.few = ..." in a .lang file). n only selects the
 * form: pass every argument (n included, where the template shows it) as for TF. */
int TN(char *out, unsigned cap, int key, long n, ...);
int i18n_plural(int lang, long n); /* PL_* category of n */
const char *i18n_lang_name(int lang); /* native name, e.g. "Français" */
int i18n_has(int lang, int key);

/* on-screen keyboard layouts (and word-suggestion lists); the first six match LANG_DE..LANG_PT */
enum { KB_DE, KB_EN, KB_FR, KB_ES, KB_IT, KB_PT, KB_RU, KB_UK, KB_NORDIC, KB_COUNT };
int i18n_keyboard(int lang); /* layout that fits a language (English for the rest) */

/* --- resource files ---
 * Format (UTF-8, one entry per line):
 *   # comment
 *   @locale ru
 *   T_NAV_FRIENDS = Друзья
 *   T_MEMBERS_N.one = %d участник
 * Escapes: \n, \t, \\ and \s (a space, for leading/trailing spaces). Every line is checked:
 * known key, valid UTF-8 without control characters, at most I18N_TEXT_MAX bytes, and exactly
 * the same placeholders (%s, %d, %n ...) in the same order as English - a translation can never
 * change how its arguments are read. Bad lines are skipped and counted. */
#define I18N_TEXT_MAX 600
#define I18N_FILE_MAX (512 * 1024)
typedef struct {
    int lang;       /* language of the file, -1 if the header was missing or unknown */
    int texts;      /* lines accepted */
    int plurals;    /* plural forms accepted */
    int errors;     /* lines refused */
    int first_line; /* line of the first refused entry */
    char first_error[96];
} I18nReport;
/* loads one file's text (replaces what that language had from an earlier file);
 * returns 0 when the file was used */
int i18n_load_text(const char *text, size_t n, I18nReport *rep);
int i18n_load_file(const char *path, I18nReport *rep);
/* loads "<dir>/<locale>.lang" for every registry language; returns the number of files used */
int i18n_load_dir(const char *dir);
void i18n_unload(void);
#endif

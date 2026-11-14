#include "i18n.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define X(key, de, en, fr, es) {de, en, fr, es},
static const char *const table[T_COUNT][LANG_COUNT] = {
#include "i18n_keys.h"
};
#undef X
#define X(key, de, en, fr, es) #key,
static const char *const names[T_COUNT] = {
#include "i18n_keys.h"
};
#undef X
static int current = LANG_EN;
void i18n_set(int lang) {
    current = lang >= 0 && lang < LANG_COUNT ? lang : LANG_EN;
}
int i18n_lang(void) {
    return current;
}
int i18n_from_ps3(int l) {
    switch (l) {
    case 4: /* SYSUTIL_LANG_GERMAN */
        return LANG_DE;
    case 2: /* FRENCH */
        return LANG_FR;
    case 3: /* SPANISH */
        return LANG_ES;
    default: /* English (US/GB) and every language without a translation yet */
        return LANG_EN;
    }
}
const char *T_in(int lang, int key) {
    if (key < 0 || key >= T_COUNT)
        return "";
    if (lang < 0 || lang >= LANG_COUNT)
        lang = LANG_EN;
    const char *s = table[key][lang];
    return s && s[0] ? s : table[key][LANG_EN];
}
const char *T(int key) {
    return T_in(current, key);
}
const char *i18n_name(int key) {
    return key >= 0 && key < T_COUNT ? names[key] : "";
}
int i18n_find(const char *name) {
    if (!name || !name[0])
        return -1;
    for (int i = 0; i < T_COUNT; i++)
        if (!strcmp(names[i], name))
            return i;
    return -1;
}
int TF(char *out, unsigned cap, int key, ...) {
    va_list ap;
    va_start(ap, key);
    int n = vsnprintf(out, cap, T(key), ap);
    va_end(ap);
    return n;
}
const char *i18n_lang_name(int lang) {
    static const char *const n[LANG_COUNT] = {"Deutsch", "English", "Français", "Español"};
    return lang >= 0 && lang < LANG_COUNT ? n[lang] : "";
}

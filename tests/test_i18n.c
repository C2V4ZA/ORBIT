/* translations: built-in languages complete, language registry, the strict .lang reader (a
 * translation can never change how printf reads its arguments), plural rules, fallbacks,
 * the shipped language files, PS3 language mapping, fuzzing of the reader */
#include "i18n.h"
#include "utf8copy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* sequence of printf conversions ("%s%d") of a template; "%%" is skipped */
static void conversions(const char *s, char *out, size_t cap) {
    size_t n = 0;
    for (; *s && n + 2 < cap; s++) {
        if (*s != '%')
            continue;
        if (s[1] == '%') {
            s++;
            continue;
        }
        out[n++] = s[1];
        if (s[1])
            s++;
    }
    out[n] = 0;
}
static int load(const char *text, I18nReport *r) {
    return i18n_load_text(text, strlen(text), r);
}
/* every text of every language has the English placeholders */
static int placeholders_ok(void) {
    for (int lang = 0; lang < LANG_COUNT; lang++)
        for (int k = 0; k < T_COUNT; k++) {
            char a[32], b[32];
            conversions(T_in(LANG_EN, k), a, sizeof(a));
            conversions(T_in(lang, k), b, sizeof(b));
            if (strcmp(a, b)) {
                printf("placeholders differ: %s (%s)\n", i18n_name(k), i18n_locale(lang)->id);
                return 0;
            }
        }
    return 1;
}

int main(void) {
    /* 1. built-in languages: complete, placeholders match */
    for (int lang = 0; lang < LANG_BUILTIN; lang++)
        for (int k = 0; k < T_COUNT; k++)
            if (!i18n_has(lang, k)) {
                printf("missing %s in %s\n", i18n_name(k), i18n_locale(lang)->id);
                abort();
            }
    assert(placeholders_ok());
    for (int lang = 0; lang < LANG_BUILTIN; lang++)
        assert(i18n_complete(lang) == 100 && !i18n_unavailable(lang));
    printf("PASS: %d built-in languages x %d texts complete, placeholders match English\n", LANG_BUILTIN, T_COUNT);

    /* 2. registry: 27 languages with ids, native names, scripts; honest availability */
    assert(LANG_COUNT == 27);
    for (int i = 0; i < LANG_COUNT; i++) {
        const OrbitLocale *l = i18n_locale(i);
        assert(l && l->id && l->native && l->english && i18n_find_locale(l->id) == i);
    }
    assert(i18n_find_locale("pt-BR") == LANG_PT_BR && i18n_find_locale("nb-NO") == LANG_NB &&
           i18n_find_locale("zh-TW") == LANG_ZH_TW && i18n_find_locale("xx") == -1 && i18n_find_locale(NULL) == -1);
    assert(i18n_unavailable(LANG_JA) == LANG_NO_FONT && i18n_unavailable(LANG_KO) == LANG_NO_FONT &&
           i18n_unavailable(LANG_ZH_CN) == LANG_NO_FONT && i18n_unavailable(LANG_HI) == LANG_NO_FONT &&
           i18n_unavailable(LANG_AR) == LANG_NO_SHAPING);
    assert(i18n_unavailable(LANG_RU) == LANG_NO_TEXT && i18n_complete(LANG_RU) == 0); /* no file loaded yet */
    assert(i18n_locale(LANG_AR)->rtl && !i18n_locale(LANG_RU)->rtl);
    assert(!strcmp(i18n_lang_name(LANG_JA), "Japanese"));          /* the font cannot draw 日本語 */
    assert(!strcmp(i18n_lang_name(LANG_RU), "Русский"));           /* Cyrillic: native name */
    assert(!strcmp(i18n_lang_name(LANG_EL), "Ελληνικά"));          /* Greek: native name */
    assert(i18n_from_ps3(8) == LANG_EN && i18n_from_ps3(0) == LANG_EN); /* RU without file, JA: English */
    assert(i18n_from_ps3(5) == LANG_IT && i18n_from_ps3(7) == LANG_PT && i18n_from_ps3(17) == LANG_PT);
    assert(i18n_from_ps3(4) == LANG_DE && i18n_from_ps3(1) == LANG_EN && i18n_from_ps3(99) == LANG_EN);
    i18n_set(LANG_IT);
    assert(!strcmp(T(T_NAV_FRIENDS), "Amici"));
    i18n_set(LANG_RU);
    assert(!strcmp(T(T_NAV_FRIENDS), "Friends")); /* no Russian file: English */
    printf("PASS: registry of %d languages (ids, native names, scripts), CJK/Hangul/Devanagari need a font, Arabic "
           "needs shaping, missing languages fall back to English\n", LANG_COUNT);

    /* 3. the reader refuses everything that could hurt */
    I18nReport r;
    assert(load("T_NAV_FRIENDS = Друзья\n", &r) && r.lang == -1);                      /* no @locale */
    assert(load("@locale xx\nT_NAV_FRIENDS = a\n", &r) && r.lang == -1);                /* unknown locale */
    assert(!load("@locale ru\nT_NAV_FRIENDS = Друзья\nT_NAV_FRIENDS = Двойной\n", &r) && r.errors == 1);
    assert(!strcmp(T_in(LANG_RU, T_NAV_FRIENDS), "Друзья"));
    const char *attacks[] = {
        "T_HELLO = Привет, %n\n",      /* %n would write memory */
        "T_HELLO = Привет, %d\n",      /* %d instead of %s */
        "T_HELLO = Привет\n",          /* placeholder dropped: later arguments would shift */
        "T_HELLO = %s %s\n",           /* one too many */
        "T_HELLO = Привет, %.999s\n",  /* different conversion */
        "T_HELLO = Привет, %\n",       /* lone percent */
        "T_SERVER_STATS = %d · %s\n",  /* types swapped */
        "T_NOPE = x\n",                /* unknown key */
        "T_HELLO.lots = %s\n",         /* unknown plural category */
        "T_HELLO = \xC0\xAF%s\n",      /* overlong UTF-8 */
        "T_HELLO = \xED\xA0\x80%s\n",  /* surrogate */
        "T_HELLO = \xFF%s\n",          /* invalid byte */
        "T_HELLO = a\x01%s\n",         /* control character */
        "T_HELLO = \xC2\x85%s\n",      /* C1 control */
        "T_HELLO = a\\q%s\n",          /* bad escape */
        "T_HELLO =   \n",              /* empty */
        "T_HELLO\n",                   /* no '=' */
    };
    for (size_t i = 0; i < sizeof(attacks) / sizeof(attacks[0]); i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "@locale ru\nT_NAV_HOME = Главная\n%s", attacks[i]);
        assert(!load(buf, &r) && r.errors == 1 && r.texts == 1);
        assert(!strcmp(T_in(LANG_RU, T_HELLO), "Hi, %s")); /* falls back to English */
    }
    /* too long */
    {
        char *big = malloc(I18N_TEXT_MAX + 64);
        int n = snprintf(big, 64, "@locale ru\nT_NAV_HOME = ");
        memset(big + n, 'a', I18N_TEXT_MAX + 1);
        big[n + I18N_TEXT_MAX + 1] = 0;
        assert(load(big, &r) && r.errors == 1);
        free(big);
        char *huge = calloc(1, I18N_FILE_MAX + 2);
        memset(huge, '#', I18N_FILE_MAX + 1);
        assert(i18n_load_text(huge, I18N_FILE_MAX + 1, &r) && r.errors == 1);
        free(huge);
    }
    /* escapes, BOM, CRLF, comments */
    assert(!load("\xEF\xBB\xBF# c\r\n@locale ru\r\nT_YOU_SUFFIX = \\s(вы)\r\nT_NAV_HOME = a\\\\b\\nc\r\n", &r) && r.texts == 2 &&
           !r.errors);
    assert(!strcmp(T_in(LANG_RU, T_YOU_SUFFIX), " (вы)") && !strcmp(T_in(LANG_RU, T_NAV_HOME), "a\\b\nc"));
    printf("PASS: language file reader refuses %d kinds of bad lines (%%n, changed/missing/extra placeholders, "
           "invalid UTF-8, control characters, unknown keys, duplicates, size limits); escapes, BOM, CRLF\n",
           (int)(sizeof(attacks) / sizeof(attacks[0])) + 3);

    /* 4. plural rules (CLDR) and TN */
    struct {
        int lang;
        long n;
        int cat;
    } pl[] = {
        {LANG_EN, 1, PL_ONE},       {LANG_EN, 0, PL_OTHER},     {LANG_EN, 2, PL_OTHER},     {LANG_FR, 0, PL_ONE},
        {LANG_FR, 1, PL_ONE},       {LANG_FR, 2, PL_OTHER},     {LANG_RU, 1, PL_ONE},       {LANG_RU, 21, PL_ONE},
        {LANG_RU, 11, PL_MANY},     {LANG_RU, 2, PL_FEW},       {LANG_RU, 24, PL_FEW},      {LANG_RU, 12, PL_MANY},
        {LANG_RU, 5, PL_MANY},      {LANG_RU, 0, PL_MANY},      {LANG_RU, 111, PL_MANY},    {LANG_RU, 101, PL_ONE},
        {LANG_UK, 22, PL_FEW},      {LANG_PL, 1, PL_ONE},       {LANG_PL, 21, PL_MANY},     {LANG_PL, 22, PL_FEW},
        {LANG_CS, 3, PL_FEW},       {LANG_CS, 5, PL_OTHER},     {LANG_RO, 0, PL_FEW},       {LANG_RO, 19, PL_FEW},
        {LANG_RO, 20, PL_OTHER},    {LANG_JA, 1, PL_OTHER},     {LANG_AR, 0, PL_ZERO},      {LANG_AR, 2, PL_TWO},
        {LANG_AR, 3, PL_FEW},       {LANG_AR, 11, PL_MANY},     {LANG_AR, 100, PL_OTHER},   {LANG_HI, 0, PL_ONE},
        {LANG_NB, 1, PL_ONE},       {LANG_NB, 3, PL_OTHER},     {LANG_PT_BR, 0, PL_ONE},    {LANG_PT, 0, PL_OTHER},
    };
    for (size_t i = 0; i < sizeof(pl) / sizeof(pl[0]); i++)
        if (i18n_plural(pl[i].lang, pl[i].n) != pl[i].cat) {
            printf("plural %s %ld -> %d, want %d\n", i18n_locale(pl[i].lang)->id, pl[i].n,
                   i18n_plural(pl[i].lang, pl[i].n), pl[i].cat);
            abort();
        }
    printf("PASS: CLDR plural rules for %d cases (one/few/many/zero/two/other, 8 rule families)\n",
           (int)(sizeof(pl) / sizeof(pl[0])));

    /* 5. the shipped language files */
    i18n_unload();
    int used = i18n_load_dir("lang");
    assert(used >= 9);
    assert(placeholders_ok());
    int full[] = {LANG_RU, LANG_UK, LANG_NB, LANG_PL};
    for (int i = 0; i < 4; i++)
        assert(i18n_complete(full[i]) == 100 && !i18n_unavailable(full[i]));
    for (int i = 0; i < LANG_COUNT; i++) /* nothing else claims to be translated */
        if (i >= LANG_BUILTIN && i != LANG_RU && i != LANG_UK && i != LANG_NB && i != LANG_PL)
            assert(i18n_unavailable(i) && i18n_complete(i) == 0);
    i18n_set(LANG_RU);
    assert(!strcmp(T(T_NAV_FRIENDS), "Друзья"));
    char b[96];
    TN(b, sizeof(b), T_GAMES_COUNT, 1, 1);
    assert(!strcmp(b, "1 игра"));
    TN(b, sizeof(b), T_GAMES_COUNT, 3, 3);
    assert(!strcmp(b, "3 игры"));
    TN(b, sizeof(b), T_GAMES_COUNT, 11, 11);
    assert(!strcmp(b, "11 игр"));
    TN(b, sizeof(b), T_GAMES_COUNT, 21, 21);
    assert(!strcmp(b, "21 игра"));
    i18n_set(LANG_UK);
    TN(b, sizeof(b), T_MOD_OPEN, 2, 2);
    assert(!strcmp(b, "2 відкриті скарги"));
    i18n_set(LANG_EN);
    TN(b, sizeof(b), T_GAMES_COUNT, 1, 1);
    assert(!strcmp(b, "1 game"));
    TN(b, sizeof(b), T_GAMES_COUNT, 0, 0);
    assert(!strcmp(b, "0 games"));
    i18n_set(LANG_FR);
    TN(b, sizeof(b), T_GAMES_COUNT, 0, 0);
    assert(!strcmp(b, "0 jeu"));
    i18n_set(LANG_DE);
    TN(b, sizeof(b), T_MOD_OPEN, 1, 1);
    assert(!strcmp(b, "1 offene Meldung"));
    i18n_set(LANG_NB);
    assert(!strcmp(T(T_NAV_FRIENDS), "Venner"));
    i18n_set(LANG_PL);
    assert(!strcmp(T(T_NAV_FRIENDS), "Znajomi"));
    TN(b, sizeof(b), T_GAMES_COUNT, 1, 1);
    assert(!strcmp(b, "1 gra"));
    TN(b, sizeof(b), T_GAMES_COUNT, 22, 22);
    assert(!strcmp(b, "22 gry"));
    TN(b, sizeof(b), T_GAMES_COUNT, 12, 12);
    assert(!strcmp(b, "12 gier"));
    TN(b, sizeof(b), T_GAMES_COUNT, 21, 21);
    assert(!strcmp(b, "21 gier"));
    assert(i18n_from_ps3(16) == LANG_PL);
    assert(i18n_from_ps3(8) == LANG_RU && i18n_from_ps3(15) == LANG_NB && i18n_from_ps3(0) == LANG_EN);
    /* a file whose header names another language is not used for either */
    {
        FILE *f = fopen("build/lang-test/sv.lang", "w");
        if (!f) {
            assert(system("mkdir -p build/lang-test") == 0);
            f = fopen("build/lang-test/sv.lang", "w");
        }
        fputs("@locale da\nT_NAV_FRIENDS = Venner\n", f);
        fclose(f);
        assert(i18n_load_dir("build/lang-test") == 0);
        assert(i18n_unavailable(LANG_SV) && i18n_unavailable(LANG_DA));
        remove("build/lang-test/sv.lang");
    }
    printf("PASS: shipped files: ru, uk, nb-NO, pl 100 %% (placeholders checked), every other language honestly "
           "unavailable, plural forms (1 игра / 3 игры / 11 игр / 21 игра, 0 jeu), file name must match @locale\n");

    /* 6. fuzzing: mutated Russian file never crashes the reader, and whatever it accepts keeps the
     * English placeholders */
    FILE *f = fopen("lang/ru.lang", "rb");
    assert(f);
    static char src[I18N_FILE_MAX];
    size_t n = fread(src, 1, sizeof(src) - 1, f);
    fclose(f);
    char *m = malloc(n + 1);
    unsigned seed = 1234567u;
    for (int round = 0; round < 300; round++) {
        memcpy(m, src, n);
        for (int k = 0; k < 40; k++) {
            seed = seed * 1103515245u + 12345u;
            size_t at = (seed >> 8) % n;
            seed = seed * 1103515245u + 12345u;
            static const char pick[] = "%%%sdn\\\n=.\x80\xC0\xFF\x01 #@";
            m[at] = (seed >> 16) & 1 ? (char)(seed >> 9) : pick[(seed >> 10) % (sizeof(pick) - 1)];
        }
        m[n] = 0;
        I18nReport fr;
        i18n_load_text(m, n, &fr);
        assert(placeholders_ok());
    }
    free(m);
    i18n_unload();
    printf("PASS: fuzzing: 300 mutated language files, no crash, accepted texts always keep English placeholders\n");

    /* UTF-8-safe copies into fixed buffers (names, messages, notes): never cut a character */
    {
        char b[8];
        assert(orbit_copy(b, sizeof(b), "abc") == 3 && !strcmp(b, "abc"));
        assert(orbit_copy(b, sizeof(b), NULL) == 0 && !b[0]);
        assert(orbit_copy(b, 1, "abc") == 0 && !b[0]);
        /* 4 Cyrillic letters (8 bytes), 7 bytes room -> 3 letters */
        assert(orbit_copy(b, sizeof(b), "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2") == 6 &&
               !strcmp(b, "\xD0\x9F\xD1\x80\xD0\xB8"));
        /* 4-byte emoji after 4 ASCII bytes, 7 bytes room -> the emoji is dropped whole */
        assert(orbit_copy(b, sizeof(b), "abcd\xF0\x9F\x98\x80") == 4 && !strcmp(b, "abcd"));
        assert(orbit_copy(b, sizeof(b), "ab\xC5\x82\xC4\x85x") == 7); /* fits exactly */
        char ov[16] = "xx\xC3\xA4\xC3\xB6\xC3\xBC";
        orbit_copy(ov, 5, ov + 2); /* overlapping move */
        assert(!strcmp(ov, "\xC3\xA4\xC3\xB6"));
        printf("PASS: orbit_copy cuts fixed text buffers only between UTF-8 characters\n");
    }
    return 0;
}

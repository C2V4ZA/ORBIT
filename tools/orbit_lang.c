/* ORBIT language files: template and completeness check (uses the console's own parser).
 *   orbit-lang template [locale]   all texts as "KEY = English text" (start of a new language)
 *   orbit-lang check FILE...       refused lines, completeness, missing keys; exit 1 on errors
 *   orbit-lang missing FILE        the keys a file does not have yet, with the English text */
#include "i18n.h"
#include <stdio.h>
#include <string.h>

static void put_escaped(const char *s) {
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n')
            fputs("\\n", stdout);
        else if (c == '\t')
            fputs("\\t", stdout);
        else if (c == '\\')
            fputs("\\\\", stdout);
        else if (c == ' ' && (i == 0 || i == n - 1))
            fputs("\\s", stdout);
        else
            putchar(c);
    }
}
int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "template")) {
        printf("# ORBIT interface texts - English reference\n@locale %s\n", argc > 2 ? argv[2] : "en");
        for (int k = 0; k < T_COUNT; k++) {
            printf("%s = ", i18n_name(k));
            put_escaped(T_in(LANG_EN, k));
            putchar('\n');
        }
        return 0;
    }
    if (argc >= 3 && (!strcmp(argv[1], "check") || !strcmp(argv[1], "missing"))) {
        int bad = 0;
        for (int i = 2; i < argc; i++) {
            I18nReport r;
            if (i18n_load_file(argv[i], &r)) {
                printf("%s: NOT USED (%s, line %d)\n", argv[i], r.errors ? r.first_error : "cannot read",
                       r.first_line);
                bad = 1;
                continue;
            }
            const OrbitLocale *lc = i18n_locale(r.lang);
            int own = 0;
            for (int k = 0; k < T_COUNT; k++)
                own += i18n_has(r.lang, k);
            printf("%s: %s (%s) %d texts, %d plural forms, %d of %d keys = %d %%%s", argv[i], lc->id, lc->english,
                   r.texts, r.plurals, own, T_COUNT, i18n_complete(r.lang),
                   i18n_complete(r.lang) == 100 ? " COMPLETE" : "");
            if (r.errors) {
                printf(", %d refused (first: line %d, %s)", r.errors, r.first_line, r.first_error);
                bad = 1;
            }
            putchar('\n');
            if (!strcmp(argv[1], "missing"))
                for (int k = 0; k < T_COUNT; k++)
                    if (!i18n_has(r.lang, k)) {
                        printf("%s = ", i18n_name(k));
                        put_escaped(T_in(LANG_EN, k));
                        putchar('\n');
                    }
        }
        return bad;
    }
    fprintf(stderr, "usage: orbit-lang template [locale] | check FILE... | missing FILE\n");
    return 2;
}

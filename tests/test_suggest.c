/* word suggestions: built-in lists, learning, next-word pairs, case, apply, persistence */
#include "i18n.h"
#include "suggest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int has(char out[SUGGEST_MAX][SUGGEST_WORD], int n, const char *w) {
    for (int i = 0; i < n; i++)
        if (!strcmp(out[i], w))
            return 1;
    return 0;
}

int main(void) {
    char out[SUGGEST_MAX][SUGGEST_WORD];
    suggest_reset();
    int n = suggest_get(LANG_DE, "Wollen wir heute zo", out);
    assert(n >= 1 && has(out, n, "zocken"));
    n = suggest_get(LANG_IT, "gio", out);
    assert(has(out, n, "giocare") || has(out, n, "gioco"));
    n = suggest_get(LANG_PT, "obr", out);
    assert(has(out, n, "obrigado"));
    n = suggest_get(LANG_EN, "To", out);
    assert(has(out, n, "Today") || has(out, n, "Tonight")); /* follows the typed capital */
    n = suggest_get(LANG_EN, "", out);
    assert(n == 0); /* nothing typed, nothing learned */
    /* 1.2: Russian, Ukrainian and Nordic lists, Cyrillic capitals */
    n = suggest_get(KB_RU, "При", out);
    assert(has(out, n, "Привет") || has(out, n, "Пригласить"));
    n = suggest_get(KB_RU, "спа", out);
    assert(has(out, n, "спасибо"));
    n = suggest_get(KB_UK, "Дяк", out);
    assert(has(out, n, "Дякую"));
    n = suggest_get(KB_UK, "ґ", out); /* no word starts with it: nothing, and no crash */
    assert(n == 0);
    n = suggest_get(KB_NORDIC, "ta", out);
    assert(has(out, n, "takk"));
    n = suggest_get(KB_NORDIC, "Ve", out);
    assert(has(out, n, "Venner") || has(out, n, "Vent"));
    assert(i18n_keyboard(LANG_RU) == KB_RU && i18n_keyboard(LANG_UK) == KB_UK && i18n_keyboard(LANG_NB) == KB_NORDIC &&
           i18n_keyboard(LANG_PL) == KB_PL && i18n_keyboard(LANG_SV) == KB_NORDIC && i18n_keyboard(LANG_JA) == KB_EN && i18n_keyboard(LANG_DE) == KB_DE);
    /* learning: own words beat the built-in list, pairs predict the next word */
    for (int i = 0; i < 3; i++)
        suggest_learn("Gran Turismo heute? Gran Turismo!");
    suggest_learn("1234 https://x.y @mika \xF0\x9F\x8E\xAE"); /* nothing of this is learned */
    n = suggest_get(LANG_DE, "gr", out);
    assert(n >= 1 && !strcmp(out[0], "gran"));
    n = suggest_get(LANG_DE, "Gran ", out);
    assert(n >= 1 && !strcmp(out[0], "turismo"));
    n = suggest_get(LANG_DE, "Gran T", out);
    assert(n >= 1 && !strcmp(out[0], "Turismo"));
    n = suggest_get(LANG_DE, "12", out);
    assert(n == 0);
    /* Cyrillic words are learned in lower case and follow the typed capital */
    for (int i = 0; i < 3; i++)
        suggest_learn("Катка сегодня? КАТКА!");
    n = suggest_get(KB_RU, "ка", out);
    assert(n >= 1 && !strcmp(out[0], "катка"));
    n = suggest_get(KB_RU, "Ка", out);
    assert(n >= 1 && !strcmp(out[0], "Катка"));
    suggest_learn("Їжак Ґанок Єдність Ёлка");
    n = suggest_get(KB_UK, "їж", out);
    assert(n >= 1 && !strcmp(out[0], "їжак"));
    n = suggest_get(KB_UK, "Ґа", out);
    assert(n >= 1 && !strcmp(out[0], "Ґанок"));
    n = suggest_get(KB_RU, "Ёл", out);
    assert(n >= 1 && !strcmp(out[0], "Ёлка"));
    /* apply replaces the partial word and adds a space */
    char text[64] = "Bis gle";
    assert(suggest_apply(text, sizeof(text), "gleich"));
    assert(!strcmp(text, "Bis gleich "));
    char small[8] = "ab cd";
    assert(!suggest_apply(small, sizeof(small), "verylongword"));
    /* persistence round trip */
    int before = suggest_learned();
    assert(!suggest_save("build/test-words.txt"));
    suggest_reset();
    assert(suggest_learned() == 0);
    assert(!suggest_load("build/test-words.txt"));
    assert(suggest_learned() == before);
    n = suggest_get(LANG_DE, "Gran ", out);
    assert(n >= 1 && !strcmp(out[0], "turismo"));
    /* Polish: built-in list, Latin Extended-A case (Z-dot, L-stroke, S-acute) both ways */
    n = suggest_get(KB_PL, "dzi", out);
    assert(n >= 2 && !strcmp(out[0], "dziś"));
    n = suggest_get(KB_PL, "Dzię", out);
    assert(n >= 1 && !strcmp(out[0], "Dzięki"));
    for (int i = 0; i < 3; i++)
        suggest_learn("Żółw Łódź Świetnie");
    n = suggest_get(KB_PL, "żó", out);
    assert(n >= 1 && !strcmp(out[0], "żółw"));
    n = suggest_get(KB_PL, "Łó", out);
    assert(n >= 1 && !strcmp(out[0], "Łódź"));
    n = suggest_get(KB_PL, "ŚW", out);
    assert(n >= 1 && !strncmp(out[0], "Św", 3));
    printf("PASS: word suggestions: 10 built-in lists (incl. RU, UK, Nordic, PL), Polish case, Cyrillic case, learned words and next-word pairs, case, apply, save/load\n");
    return 0;
}

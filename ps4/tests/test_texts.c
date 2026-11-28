/* PS4 wording (i18n.c built with -DORBIT_PS4): texts about this console say "PS4", texts that name
 * both consoles (Discover: PS3 and PS4 communities) stay unchanged, neutral texts stay neutral. */
#include "i18n.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *locale) {
    int lang = i18n_find_locale(locale);
    assert(lang >= 0);
    i18n_set(lang);
    const char *games = T(T_GAMES_TITLE), *title = T(T_DISCOVER_TITLE), *hero = T(T_HERO_EMPTY_TITLE);
    assert(strstr(games, "PS4") && !strstr(games, "PS3"));     /* "your PS4 games" */
    assert(strstr(title, "PS3") && strstr(title, "PS4"));      /* both consoles */
    assert(strstr(hero, "PS3") && strstr(hero, "PS4"));
    if (!strcmp(locale, "ru")) /* really the Russian file, not the English fallback */
        assert(strstr(title, "Сообщества"));
    const int neutral[] = {T_DISCOVER_NONE, T_DISC_SAVED_LIST, T_DISC_NO_CONNECTION, T_LEFT_OFFLINE};
    for (size_t i = 0; i < sizeof(neutral) / sizeof(neutral[0]); i++)
        assert(!strstr(T(neutral[i]), "PS3") && !strstr(T(neutral[i]), "PS4"));
}

int main(void) {
    assert(i18n_load_dir("../lang") >= 4); /* ru, uk, nb-NO, pl come from .lang files */
    const char *locales[] = {"de", "en", "fr", "es", "it", "pt", "ru", "uk", "nb-NO", "pl"};
    for (size_t i = 0; i < sizeof(locales) / sizeof(locales[0]); i++)
        check(locales[i]);
    printf("PASS: PS4 texts: own console = PS4, Discover names PS3 and PS4, offline texts neutral (10 languages)\n");
    return 0;
}

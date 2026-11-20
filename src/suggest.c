/* Local word suggestions: prefix completion and next-word prediction.
 * Ranking: learned words (count) outrank the built-in list; next-word pairs come first when
 * the user has written them before. Case: suggestions follow the case of what was typed. */
#include "suggest.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* built-in common chat words, most frequent first (original lists, written for ORBIT) */
static const char *const seed[KB_COUNT] = {
    /* DE */
    "ich du ja nein und ist das nicht die der zu mit auf es bin hast hab gleich jetzt noch auch "
    "heute morgen abend spielen spiel online zocken lust komm kommst danke bitte gut super cool "
    "geil haha okay ok genau warum wann wer was wie wo vielleicht gerne gern sorry kurz später "
    "zusammen runde match server voice chat freunde einladen warte warten bereit fertig gewonnen "
    "verloren nochmal team party treffen uhr minuten schon wieder einfach echt mal",
    /* EN */
    "i you the and to a is it yes no ok okay that what when where who why how not just now "
    "today tonight tomorrow play playing game games online join come coming thanks thank please "
    "good great nice cool lol haha sure maybe later soon ready done won lost again team party "
    "server voice chat friends invite wait sorry back here there with for on in see let's lets "
    "gg brb match round minutes really still",
    /* FR */
    "je tu oui non et est le la les de un une pas ça ce que qui quoi quand où comment pourquoi "
    "maintenant aujourd'hui demain soir jouer jeu jeux en ligne viens venir merci stp svp bien "
    "super cool mdr ok d'accord peut-être plus tard bientôt prêt fini gagné perdu encore équipe "
    "partie serveur vocal chat amis inviter attends désolé avec pour sur dans voir allez",
    /* ES */
    "yo tú sí no y es el la los las de un una que qué quién cuándo dónde cómo por porque ahora "
    "hoy mañana noche jugar juego juegos en línea ven vienes gracias por favor bien genial guay "
    "jaja vale ok quizás luego pronto listo terminado ganado perdido otra vez equipo partida "
    "servidor voz chat amigos invitar espera perdón con para en ver vamos",
    /* IT */
    "io tu sì no e è il la lo i le di un una che chi cosa quando dove come perché non adesso ora "
    "oggi domani stasera giocare gioco giochi online vieni venire grazie per favore bene grande "
    "bello ahah ok va bene forse dopo presto pronto finito vinto perso ancora squadra partita "
    "server vocale chat amici invitare aspetta scusa con per su in vedere andiamo",
    /* PT */
    "eu tu sim não e é o a os as de um uma que quem quando onde como porquê porque agora hoje "
    "amanhã noite jogar jogo jogos online vem vens obrigado obrigada por favor bem fixe top "
    "haha ok talvez depois logo pronto acabei ganhei perdi outra vez equipa partida servidor "
    "voz chat amigos convidar espera desculpa com para em ver vamos",
    /* RU */
    "я ты да нет и это не что как где когда кто почему сейчас сегодня завтра вечером играть "
    "игра игры онлайн давай заходи иду спасибо пожалуйста хорошо отлично круто ок ладно может "
    "позже скоро готов готово выиграли проиграли ещё раз команда пати сервер голос чат друзья "
    "пригласить жди подожди извини привет пока тоже уже здесь там вместе минут матч катка",
    /* UK */
    "я ти так ні і це не що як де коли хто чому зараз сьогодні завтра ввечері грати гра ігри "
    "онлайн давай заходь іду дякую будь ласка добре чудово круто ок гаразд може пізніше скоро "
    "готовий готово виграли програли ще раз команда паті сервер голос чат друзі запросити чекай "
    "зачекай вибач привіт бувай теж вже тут там разом хвилин матч",
    /* Nordic (Norwegian words; Danish and Swedish share many) */
    "jeg du ja nei og er det ikke hva når hvor hvem hvorfor hvordan nå i dag i morgen kveld "
    "spille spill online kom kommer takk vær så snill bra kult ok kanskje senere snart klar "
    "ferdig vant tapte igjen lag gjeng server tale chat venner invitere vent sorry hei ha det "
    "også allerede her der sammen minutter kamp runde",
};

#define LEARN_MAX 1500
typedef struct {
    char w[SUGGEST_WORD * 2]; /* a word, or "prev next" for a pair */
    unsigned count;
} Entry;
static Entry learned[LEARN_MAX];
static int learned_n;

void suggest_reset(void) {
    learned_n = 0;
}
int suggest_learned(void) {
    return learned_n;
}
static int is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n';
}
static int is_punct(unsigned char c) {
    return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' || c == '"' || c == '(' || c == ')';
}
/* lower-case for ASCII, the Latin-1 letters (UTF-8 C3 xx, also æ ø å) and, since 1.2, Cyrillic
 * (Russian and Ukrainian: А-Я, Ѐ-Џ incl. Ё Є І Ї, Ґ). Every mapping keeps the byte length. */
static void lower(char *s) {
    unsigned char *p = (unsigned char *)s;
    for (; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            *p = (unsigned char)(*p + 32);
        else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0x9E && p[1] != 0x97) {
            p[1] = (unsigned char)(p[1] + 0x20);
            p++;
        } else if (*p == 0xD0 && p[1] >= 0x80 && p[1] <= 0xAF) {
            if (p[1] <= 0x8F) { /* U+0400..040F -> U+0450..045F */
                p[0] = 0xD1;
                p[1] = (unsigned char)(p[1] + 0x10);
            } else if (p[1] <= 0x9F) /* А..П -> а..п */
                p[1] = (unsigned char)(p[1] + 0x20);
            else { /* Р..Я -> р..я */
                p[0] = 0xD1;
                p[1] = (unsigned char)(p[1] - 0x20);
            }
            p++;
        } else if (*p == 0xD2 && p[1] == 0x90) { /* Ґ -> ґ */
            p[1] = 0x91;
            p++;
        } else if (*p >= 0xC0 && p[1])
            p++; /* other two-byte letters stay as they are */
    }
}
static int is_upper_start(const char *s) {
    unsigned char c = (unsigned char)s[0], d = (unsigned char)s[1];
    return (c >= 'A' && c <= 'Z') || (c == 0xC3 && d >= 0x80 && d <= 0x9E) || (c == 0xD0 && d >= 0x80 && d <= 0xAF) ||
           (c == 0xD2 && d == 0x90);
}
static void capitalise(char *s) {
    unsigned char *p = (unsigned char *)s;
    if (*p >= 'a' && *p <= 'z')
        *p = (unsigned char)(*p - 32);
    else if (*p == 0xC3 && p[1] >= 0xA0 && p[1] <= 0xBE && p[1] != 0xB7)
        p[1] = (unsigned char)(p[1] - 0x20);
    else if (*p == 0xD0 && p[1] >= 0xB0 && p[1] <= 0xBF) /* а..п */
        p[1] = (unsigned char)(p[1] - 0x20);
    else if (*p == 0xD1 && p[1] >= 0x80 && p[1] <= 0x8F) { /* р..я */
        p[0] = 0xD0;
        p[1] = (unsigned char)(p[1] + 0x20);
    } else if (*p == 0xD1 && p[1] >= 0x90 && p[1] <= 0x9F) { /* ѐ..џ */
        p[0] = 0xD0;
        p[1] = (unsigned char)(p[1] - 0x10);
    } else if (*p == 0xD2 && p[1] == 0x91)
        p[1] = 0x90;
}
/* splits the last two words of text: prev (complete word) and cur (being typed) */
static void tail_words(const char *text, char *prev, char *cur) {
    prev[0] = cur[0] = 0;
    int n = (int)strlen(text), e = n;
    int s = e;
    while (s > 0 && !is_space((unsigned char)text[s - 1]))
        s--;
    int len = e - s < SUGGEST_WORD - 1 ? e - s : SUGGEST_WORD - 1;
    memcpy(cur, text + s, (size_t)len);
    cur[len] = 0;
    int pe = s;
    while (pe > 0 && is_space((unsigned char)text[pe - 1]))
        pe--;
    int ps = pe;
    while (ps > 0 && !is_space((unsigned char)text[ps - 1]))
        ps--;
    len = pe - ps < SUGGEST_WORD - 1 ? pe - ps : SUGGEST_WORD - 1;
    memcpy(prev, text + ps, (size_t)len);
    prev[len] = 0;
    while (prev[0] && is_punct((unsigned char)prev[strlen(prev) - 1]))
        prev[strlen(prev) - 1] = 0;
    lower(prev);
}
static int add(char out[SUGGEST_MAX][SUGGEST_WORD], int n, const char *w, const char *typed) {
    if (n >= SUGGEST_MAX || !w[0] || strlen(w) >= SUGGEST_WORD)
        return n;
    char c[SUGGEST_WORD];
    snprintf(c, sizeof(c), "%s", w);
    if (typed && is_upper_start(typed))
        capitalise(c);
    if (typed && !strcmp(c, typed))
        return n; /* already typed completely */
    for (int i = 0; i < n; i++)
        if (!strcmp(out[i], c))
            return n;
    snprintf(out[n], SUGGEST_WORD, "%s", c);
    return n + 1;
}
static int by_count(const void *a, const void *b) {
    const Entry *x = *(const Entry *const *)a, *y = *(const Entry *const *)b;
    return x->count < y->count ? 1 : x->count > y->count ? -1 : 0;
}
int suggest_get(int lang, const char *text, char out[SUGGEST_MAX][SUGGEST_WORD]) {
    char prev[SUGGEST_WORD], cur[SUGGEST_WORD], low[SUGGEST_WORD];
    tail_words(text ? text : "", prev, cur);
    snprintf(low, sizeof(low), "%s", cur);
    lower(low);
    size_t lc = strlen(low);
    int n = 0;
    static const Entry *cand[LEARN_MAX];
    int nc = 0;
    /* 1. learned pairs "prev x" where x starts with what is typed */
    if (prev[0]) {
        size_t lp = strlen(prev);
        for (int i = 0; i < learned_n; i++) {
            const char *w = learned[i].w;
            if (!strncmp(w, prev, lp) && w[lp] == ' ' && !strncmp(w + lp + 1, low, lc))
                cand[nc++] = &learned[i];
        }
        qsort(cand, (size_t)nc, sizeof(cand[0]), by_count);
        for (int i = 0; i < nc; i++)
            n = add(out, n, cand[i]->w + strlen(prev) + 1, cur);
    }
    if (!lc) /* nothing typed: only next-word predictions */
        return n;
    /* 2. learned single words with the prefix */
    nc = 0;
    for (int i = 0; i < learned_n; i++)
        if (!strchr(learned[i].w, ' ') && !strncmp(learned[i].w, low, lc))
            cand[nc++] = &learned[i];
    qsort(cand, (size_t)nc, sizeof(cand[0]), by_count);
    for (int i = 0; i < nc; i++)
        n = add(out, n, cand[i]->w, cur);
    /* 3. built-in list of the keyboard language, in order */
    if (lang < 0 || lang >= KB_COUNT)
        lang = KB_EN;
    const char *p = seed[lang];
    while (*p && n < SUGGEST_MAX) {
        const char *e = p;
        while (*e && *e != ' ')
            e++;
        char w[SUGGEST_WORD];
        size_t len = (size_t)(e - p) < sizeof(w) - 1 ? (size_t)(e - p) : sizeof(w) - 1;
        memcpy(w, p, len);
        w[len] = 0;
        if (!strncmp(w, low, lc))
            n = add(out, n, w, cur);
        p = *e ? e + 1 : e;
    }
    return n;
}
static void count(const char *w) {
    for (int i = 0; i < learned_n; i++)
        if (!strcmp(learned[i].w, w)) {
            learned[i].count++;
            return;
        }
    if (learned_n >= LEARN_MAX) { /* full: replace the least used entry */
        int low = 0;
        for (int i = 1; i < learned_n; i++)
            if (learned[i].count < learned[low].count)
                low = i;
        snprintf(learned[low].w, sizeof(learned[low].w), "%s", w);
        learned[low].count = 1;
        return;
    }
    snprintf(learned[learned_n].w, sizeof(learned[learned_n].w), "%s", w);
    learned[learned_n++].count = 1;
}
/* a word worth learning: 2..31 bytes, letters only (no numbers, codes, links or @names) */
static int learnable(const char *w) {
    size_t n = strlen(w);
    if (n < 2 || n >= SUGGEST_WORD)
        return 0;
    for (const unsigned char *p = (const unsigned char *)w; *p; p++)
        if ((*p < 128 && !((*p >= 'a' && *p <= 'z') || *p == '\'' || *p == '-')) || *p >= 0xE0) /* no emoji/symbols */
            return 0;
    return 1;
}
void suggest_learn(const char *text) {
    if (!text)
        return;
    char prev[SUGGEST_WORD] = "";
    const char *p = text;
    while (*p) {
        while (*p && is_space((unsigned char)*p))
            p++;
        const char *s = p;
        while (*p && !is_space((unsigned char)*p))
            p++;
        char w[SUGGEST_WORD];
        size_t len = (size_t)(p - s);
        if (!len || len >= sizeof(w)) {
            prev[0] = 0;
            continue;
        }
        memcpy(w, s, len);
        w[len] = 0;
        int ends = 0;
        while (w[0] && is_punct((unsigned char)w[strlen(w) - 1])) {
            w[strlen(w) - 1] = 0;
            ends = 1;
        }
        while (w[0] && is_punct((unsigned char)w[0]))
            memmove(w, w + 1, strlen(w));
        lower(w);
        if (!learnable(w)) {
            prev[0] = 0;
            continue;
        }
        count(w);
        if (prev[0]) {
            char pair[SUGGEST_WORD * 2];
            snprintf(pair, sizeof(pair), "%s %s", prev, w);
            count(pair);
        }
        snprintf(prev, sizeof(prev), "%s", ends ? "" : w);
    }
}
int suggest_apply(char *text, int cap, const char *word) {
    int n = (int)strlen(text), s = n;
    while (s > 0 && !is_space((unsigned char)text[s - 1]))
        s--;
    int need = s + (int)strlen(word) + 1;
    if (need >= cap)
        return 0;
    snprintf(text + s, (size_t)(cap - s), "%s ", word);
    return 1;
}
int suggest_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    learned_n = 0;
    char line[160];
    while (learned_n < LEARN_MAX && fgets(line, sizeof(line), f)) {
        char *tab = strchr(line, '\t');
        if (!tab)
            continue;
        *tab = 0;
        unsigned long c = strtoul(tab + 1, NULL, 10);
        if (!line[0] || strlen(line) >= sizeof(learned[0].w) || !c)
            continue;
        snprintf(learned[learned_n].w, sizeof(learned[learned_n].w), "%s", line);
        learned[learned_n++].count = c > 1000000 ? 1000000 : (unsigned)c;
    }
    fclose(f);
    return 0;
}
int suggest_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    for (int i = 0; i < learned_n; i++)
        fprintf(f, "%s\t%u\n", learned[i].w, learned[i].count);
    return fclose(f);
}

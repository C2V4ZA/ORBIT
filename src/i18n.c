#include "i18n.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define X(key, de, en, fr, es) {de, en, fr, es},
static const char *const table[T_COUNT][LANG_BASE] = {
#include "i18n_keys.h"
};
#undef X
/* 1.2: Italian and Portuguese, indexed by key (designated initialisers) */
#define L(key, it, pt) [key] = {it, pt},
static const char *const extra[T_COUNT][LANG_BUILTIN - LANG_BASE] = {
#include "i18n_itpt_a.h"
#include "i18n_itpt_b.h"
#include "i18n_itpt_c.h"
};
#undef L
#define X(key, de, en, fr, es) #key,
static const char *const names[T_COUNT] = {
#include "i18n_keys.h"
};
#undef X

/* ---------- registry ---------- */
static const OrbitLocale locales[LANG_COUNT] = {
    [LANG_DE] = {"de", "Deutsch", "German", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_EN] = {"en", "English", "English", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_FR] = {"fr", "Français", "French", SCRIPT_LATIN, 0, PLR_FRENCH, -1},
    [LANG_ES] = {"es", "Español", "Spanish", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_IT] = {"it", "Italiano", "Italian", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_PT] = {"pt", "Português", "Portuguese", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_PT_BR] = {"pt-BR", "Português (Brasil)", "Portuguese (Brazil)", SCRIPT_LATIN, 0, PLR_FRENCH, LANG_PT},
    [LANG_RU] = {"ru", "Русский", "Russian", SCRIPT_CYRILLIC, 0, PLR_EAST_SLAVIC, -1},
    [LANG_UK] = {"uk", "Українська", "Ukrainian", SCRIPT_CYRILLIC, 0, PLR_EAST_SLAVIC, -1},
    [LANG_NB] = {"nb-NO", "Norsk bokmål", "Norwegian Bokmål", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_SV] = {"sv", "Svenska", "Swedish", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_DA] = {"da", "Dansk", "Danish", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_FI] = {"fi", "Suomi", "Finnish", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_NL] = {"nl", "Nederlands", "Dutch", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_PL] = {"pl", "Polski", "Polish", SCRIPT_LATIN, 0, PLR_POLISH, -1},
    [LANG_CS] = {"cs", "Čeština", "Czech", SCRIPT_LATIN, 0, PLR_CZECH, -1},
    [LANG_SK] = {"sk", "Slovenčina", "Slovak", SCRIPT_LATIN, 0, PLR_CZECH, -1},
    [LANG_TR] = {"tr", "Türkçe", "Turkish", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_RO] = {"ro", "Română", "Romanian", SCRIPT_LATIN, 0, PLR_ROMANIAN, -1},
    [LANG_HU] = {"hu", "Magyar", "Hungarian", SCRIPT_LATIN, 0, PLR_ONE_OTHER, -1},
    [LANG_EL] = {"el", "Ελληνικά", "Greek", SCRIPT_GREEK, 0, PLR_ONE_OTHER, -1},
    [LANG_JA] = {"ja", "日本語", "Japanese", SCRIPT_CJK, 0, PLR_NONE, -1},
    [LANG_KO] = {"ko", "한국어", "Korean", SCRIPT_HANGUL, 0, PLR_NONE, -1},
    [LANG_ZH_CN] = {"zh-CN", "简体中文", "Chinese (Simplified)", SCRIPT_CJK, 0, PLR_NONE, -1},
    [LANG_ZH_TW] = {"zh-TW", "繁體中文", "Chinese (Traditional)", SCRIPT_CJK, 0, PLR_NONE, -1},
    [LANG_AR] = {"ar", "العربية", "Arabic", SCRIPT_ARABIC, 1, PLR_ARABIC, -1},
    [LANG_HI] = {"hi", "हिन्दी", "Hindi", SCRIPT_DEVANAGARI, 0, PLR_HINDI, -1},
};
const OrbitLocale *i18n_locale(int lang) {
    return lang >= 0 && lang < LANG_COUNT ? &locales[lang] : NULL;
}
int i18n_find_locale(const char *id) {
    if (!id)
        return -1;
    for (int i = 0; i < LANG_COUNT; i++)
        if (!strcmp(locales[i].id, id))
            return i;
    return -1;
}
/* OrbitSans is DejaVu Sans: Latin, Greek and Cyrillic glyphs. Arabic glyphs exist but the
 * renderer has no shaping or right-to-left layout; CJK, Hangul and Devanagari have no glyphs. */
int i18n_script_supported(int script) {
    return script == SCRIPT_LATIN || script == SCRIPT_GREEK || script == SCRIPT_CYRILLIC;
}

int i18n_keyboard(int lang) {
    if (lang >= 0 && lang < LANG_BUILTIN)
        return lang;
    switch (lang) {
    case LANG_PT_BR:
        return KB_PT;
    case LANG_RU:
        return KB_RU;
    case LANG_UK:
        return KB_UK;
    case LANG_NB:
    case LANG_SV:
    case LANG_DA:
    case LANG_FI:
        return KB_NORDIC;
    default:
        return KB_EN;
    }
}

/* ---------- texts loaded from .lang files ---------- */
typedef struct {
    int key, cat;
    char *text;
} Plural;
typedef struct {
    char *text[T_COUNT];
    Plural *pl;
    int npl, cap;
} LangFile;
static LangFile *files[LANG_COUNT];

static const char *builtin(int lang, int key) {
    if (lang < LANG_BASE)
        return table[key][lang];
    if (lang < LANG_BUILTIN)
        return extra[key][lang - LANG_BASE];
    return NULL;
}
static const char *own(int lang, int key) {
    if (files[lang] && files[lang]->text[key])
        return files[lang]->text[key];
    const char *s = builtin(lang, key);
    return s && s[0] ? s : NULL;
}

static int current = LANG_EN;
void i18n_set(int lang) {
    current = lang >= 0 && lang < LANG_COUNT ? lang : LANG_EN;
}
int i18n_lang(void) {
    return current;
}
int i18n_from_ps3(int l) {
    static const int map[20] = {LANG_JA, LANG_EN, LANG_FR, LANG_ES, LANG_DE, LANG_IT, LANG_NL,
                                LANG_PT, LANG_RU, LANG_KO, LANG_ZH_TW, LANG_ZH_CN, LANG_FI, LANG_SV,
                                LANG_DA, LANG_NB, LANG_PL, LANG_PT_BR, LANG_EN, LANG_TR};
    int lang = l >= 0 && l < 20 ? map[l] : LANG_EN;
    if (lang == LANG_PT_BR && i18n_unavailable(lang))
        lang = LANG_PT; /* closer than English */
    return i18n_unavailable(lang) ? LANG_EN : lang;
}
const char *T_in(int lang, int key) {
    if (key < 0 || key >= T_COUNT)
        return "";
    if (lang < 0 || lang >= LANG_COUNT)
        lang = LANG_EN;
    const char *s = own(lang, key);
    if (!s && locales[lang].parent >= 0)
        s = own(locales[lang].parent, key);
    return s ? s : table[key][LANG_EN];
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

/* ---------- plural rules (CLDR, whole numbers) ---------- */
int i18n_plural(int lang, long n) {
    if (lang < 0 || lang >= LANG_COUNT)
        lang = LANG_EN;
    long a = n < 0 ? -n : n, m10 = a % 10, m100 = a % 100;
    switch (locales[lang].plural) {
    case PLR_FRENCH:
    case PLR_HINDI:
        return a <= 1 ? PL_ONE : PL_OTHER;
    case PLR_EAST_SLAVIC:
        if (m10 == 1 && m100 != 11)
            return PL_ONE;
        if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14))
            return PL_FEW;
        return PL_MANY;
    case PLR_POLISH:
        if (a == 1)
            return PL_ONE;
        if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14))
            return PL_FEW;
        return PL_MANY;
    case PLR_CZECH:
        return a == 1 ? PL_ONE : a >= 2 && a <= 4 ? PL_FEW : PL_OTHER;
    case PLR_ROMANIAN:
        return a == 1 ? PL_ONE : a == 0 || (m100 >= 2 && m100 <= 19) ? PL_FEW : PL_OTHER;
    case PLR_NONE:
        return PL_OTHER;
    case PLR_ARABIC:
        return a == 0     ? PL_ZERO
               : a == 1   ? PL_ONE
               : a == 2   ? PL_TWO
               : m100 >= 3 && m100 <= 10 ? PL_FEW
               : m100 >= 11 ? PL_MANY
                            : PL_OTHER;
    default:
        return a == 1 ? PL_ONE : PL_OTHER;
    }
}
static const char *plural_text(int lang, int key, int cat) {
    const LangFile *f = files[lang];
    if (!f)
        return NULL;
    for (int i = 0; i < f->npl; i++)
        if (f->pl[i].key == key && f->pl[i].cat == cat)
            return f->pl[i].text;
    return NULL;
}
static const char *TN_template(int key, long n) {
    if (key < 0 || key >= T_COUNT)
        return "";
    int lang = current;
    if (!own(lang, key)) /* the text falls back: so does its plural form */
        lang = locales[lang].parent >= 0 && own(locales[lang].parent, key) ? locales[lang].parent : LANG_EN;
    const char *s = plural_text(lang, key, i18n_plural(lang, n));
    return s ? s : T_in(lang, key);
}
int TN(char *out, unsigned cap, int key, long n, ...) {
    va_list ap;
    va_start(ap, n);
    int r = vsnprintf(out, cap, TN_template(key, n), ap);
    va_end(ap);
    return r;
}

const char *i18n_lang_name(int lang) {
    if (lang < 0 || lang >= LANG_COUNT)
        return "";
    /* a name the font cannot draw would show as empty boxes: then the English name */
    return i18n_script_supported(locales[lang].script) ? locales[lang].native : locales[lang].english;
}
/* 1 if the language has its own text for the key (no fallback) */
int i18n_has(int lang, int key) {
    if (key < 0 || key >= T_COUNT || lang < 0 || lang >= LANG_COUNT)
        return 0;
    return own(lang, key) != NULL;
}
int i18n_complete(int lang) {
    if (lang < 0 || lang >= LANG_COUNT)
        return 0;
    int have = 0;
    for (int k = 0; k < T_COUNT; k++)
        have += i18n_has(lang, k);
    return have == T_COUNT ? 100 : have * 100 / T_COUNT > 99 ? 99 : have * 100 / T_COUNT;
}
int i18n_unavailable(int lang) {
    if (lang < 0 || lang >= LANG_COUNT)
        return LANG_NO_TEXT;
    int sc = locales[lang].script;
    if (sc == SCRIPT_ARABIC)
        return LANG_NO_SHAPING;
    if (!i18n_script_supported(sc))
        return LANG_NO_FONT;
    if (lang < LANG_BUILTIN)
        return LANG_OK;
    return files[lang] && i18n_complete(lang) > 0 ? LANG_OK : LANG_NO_TEXT;
}

/* ---------- .lang parser ---------- */
/* placeholder sequence of a template: "%s", "%.90s", "%d" ... each followed by '|'; "%%" skipped */
static int placeholders(const char *s, char *out, size_t cap) {
    size_t o = 0;
    out[0] = 0;
    for (; *s; s++) {
        if (*s != '%')
            continue;
        if (s[1] == '%') {
            s++;
            continue;
        }
        const char *b = s++;
        while (*s && strchr("-+ #0123456789.*lhz", *s))
            s++;
        if (!*s)
            return -1; /* a lone '%' at the end */
        size_t len = (size_t)(s - b + 1);
        if (o + len + 2 > cap)
            return -1;
        memcpy(out + o, b, len);
        o += len;
        out[o++] = '|';
        out[o] = 0;
    }
    return 0;
}
/* strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF, no control characters
 * other than newline and tab */
static int utf8_ok(const unsigned char *s) {
    while (*s) {
        unsigned c = *s;
        int len;
        unsigned cp;
        if (c < 0x80) {
            if (c < 0x20 && c != '\n' && c != '\t')
                return 0;
            if (c == 0x7F)
                return 0;
            s++;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            len = 1;
            cp = c & 0x1F;
        } else if (c >= 0xE0 && c <= 0xEF) {
            len = 2;
            cp = c & 0x0F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            len = 3;
            cp = c & 0x07;
        } else
            return 0;
        for (int i = 1; i <= len; i++) {
            if ((s[i] & 0xC0) != 0x80)
                return 0;
            cp = cp << 6 | (s[i] & 0x3Fu);
        }
        if ((len == 2 && cp < 0x800) || (len == 3 && (cp < 0x10000 || cp > 0x10FFFF)) ||
            (cp >= 0xD800 && cp <= 0xDFFF) || (cp >= 0x80 && cp < 0xA0))
            return 0;
        s += len + 1;
    }
    return 1;
}
static void lang_free(LangFile *f) {
    if (!f)
        return;
    for (int k = 0; k < T_COUNT; k++)
        free(f->text[k]);
    for (int i = 0; i < f->npl; i++)
        free(f->pl[i].text);
    free(f->pl);
    free(f);
}
static void refuse(I18nReport *r, int line, const char *why) {
    if (!r->errors++) {
        r->first_line = line;
        snprintf(r->first_error, sizeof(r->first_error), "%s", why);
    }
}
static const char *const cat_names[PL_COUNT] = {"zero", "one", "two", "few", "many", "other"};
static int load_text(const char *text, size_t n, I18nReport *rep, int expect) {
    I18nReport dummy;
    I18nReport *r = rep ? rep : &dummy;
    memset(r, 0, sizeof(*r));
    r->lang = -1;
    if (!text || n > I18N_FILE_MAX) {
        refuse(r, 0, "file too large");
        return -1;
    }
    LangFile *f = calloc(1, sizeof(*f));
    char *line = malloc(4 * I18N_TEXT_MAX + 256), *val = malloc(I18N_TEXT_MAX + 1);
    if (!f || !line || !val) {
        free(f);
        free(line);
        free(val);
        refuse(r, 0, "out of memory");
        return -1;
    }
    size_t pos = 0;
    if (n >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3))
        pos = 3;
    int ln = 0, lang = -1;
    const size_t line_cap = 4 * I18N_TEXT_MAX + 255;
    while (pos < n) {
        ln++;
        size_t e = pos;
        while (e < n && text[e] != '\n')
            e++;
        size_t len = e - pos;
        int too_long = len > line_cap;
        if (!too_long) {
            memcpy(line, text + pos, len);
            line[len] = 0;
        }
        pos = e + 1;
        if (too_long) {
            refuse(r, ln, "line too long");
            continue;
        }
        if (len && line[len - 1] == '\r')
            line[--len] = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '#')
            continue;
        if (*p == '@') {
            if (!strncmp(p, "@locale ", 8) && lang < 0) {
                char *id = p + 8;
                while (*id == ' ')
                    id++;
                char *end = id + strlen(id);
                while (end > id && (end[-1] == ' ' || end[-1] == '\t'))
                    *--end = 0;
                lang = i18n_find_locale(id);
                if (lang < 0 || (expect >= 0 && lang != expect)) {
                    refuse(r, ln, lang < 0 ? "unknown locale" : "locale does not match the file name");
                    lang = -1;
                    break;
                }
            } else
                refuse(r, ln, "unknown directive");
            continue;
        }
        if (lang < 0) {
            refuse(r, ln, "text before @locale");
            break;
        }
        char *eq = strchr(p, '=');
        if (!eq) {
            refuse(r, ln, "missing '='");
            continue;
        }
        char *ke = eq;
        while (ke > p && (ke[-1] == ' ' || ke[-1] == '\t'))
            ke--;
        *ke = 0;
        int cat = -1;
        char *dot = strchr(p, '.');
        if (dot) {
            *dot = 0;
            for (int c = 0; c < PL_COUNT; c++)
                if (!strcmp(dot + 1, cat_names[c]))
                    cat = c;
            if (cat < 0) {
                refuse(r, ln, "unknown plural category");
                continue;
            }
        }
        int key = i18n_find(p);
        if (key < 0) {
            refuse(r, ln, "unknown key");
            continue;
        }
        /* value: trimmed, then unescaped */
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t')
            v++;
        char *ve = v + strlen(v);
        while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t'))
            *--ve = 0;
        size_t o = 0;
        int bad = 0;
        for (const char *s = v; *s && !bad; s++) {
            char c = *s;
            if (c == '\\') {
                s++;
                c = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s == 's' ? ' ' : *s == '\\' ? '\\' : 0;
                if (!c)
                    bad = 1;
            }
            if (!bad) {
                if (o >= I18N_TEXT_MAX)
                    bad = 2;
                else
                    val[o++] = c;
            }
        }
        val[o] = 0;
        if (bad) {
            refuse(r, ln, bad == 2 ? "text too long" : "bad escape");
            continue;
        }
        if (!o) {
            refuse(r, ln, "empty text");
            continue;
        }
        if (!utf8_ok((const unsigned char *)val)) {
            refuse(r, ln, "invalid UTF-8 or control character");
            continue;
        }
        char want[160], got[160];
        if (placeholders(table[key][LANG_EN], want, sizeof(want)) || placeholders(val, got, sizeof(got)) ||
            strcmp(want, got)) {
            refuse(r, ln, "placeholders differ from English");
            continue;
        }
        if (cat < 0) {
            if (f->text[key]) {
                refuse(r, ln, "duplicate key");
                continue;
            }
            f->text[key] = strdup(val);
            r->texts++;
        } else {
            int dup = 0;
            for (int i = 0; i < f->npl; i++)
                dup |= f->pl[i].key == key && f->pl[i].cat == cat;
            if (dup) {
                refuse(r, ln, "duplicate plural form");
                continue;
            }
            if (f->npl == f->cap) {
                int nc = f->cap ? f->cap * 2 : 32;
                Plural *np = realloc(f->pl, (size_t)nc * sizeof(*np));
                if (!np) {
                    refuse(r, ln, "out of memory");
                    continue;
                }
                f->pl = np;
                f->cap = nc;
            }
            f->pl[f->npl].key = key;
            f->pl[f->npl].cat = cat;
            f->pl[f->npl].text = strdup(val);
            f->npl++;
            r->plurals++;
        }
    }
    free(line);
    free(val);
    r->lang = lang;
    if (lang < 0 || (!r->texts && !r->plurals)) {
        if (lang < 0 && !r->errors)
            refuse(r, ln, "missing @locale");
        lang_free(f);
        return -1;
    }
    lang_free(files[lang]);
    files[lang] = f;
    return 0;
}
int i18n_load_text(const char *text, size_t n, I18nReport *rep) {
    return load_text(text, n, rep, -1);
}
static int load_file(const char *path, I18nReport *rep, int expect) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (rep) {
            memset(rep, 0, sizeof(*rep));
            rep->lang = -1;
        }
        return -1;
    }
    char *buf = malloc(I18N_FILE_MAX + 1);
    size_t n = buf ? fread(buf, 1, I18N_FILE_MAX + 1, fp) : 0;
    fclose(fp);
    int rc = buf ? load_text(buf, n, rep, expect) : -1; /* n > I18N_FILE_MAX is refused there */
    free(buf);
    return rc;
}
int i18n_load_file(const char *path, I18nReport *rep) {
    return load_file(path, rep, -1);
}
int i18n_load_dir(const char *dir) {
    int used = 0;
    for (int i = 0; i < LANG_COUNT; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.lang", dir, locales[i].id);
        I18nReport r;
        if (!load_file(path, &r, i)) /* the header must name the file's own language */
            used++;
    }
    return used;
}
void i18n_unload(void) {
    for (int i = 0; i < LANG_COUNT; i++) {
        lang_free(files[i]);
        files[i] = NULL;
    }
}

/* ORBIT word suggestions for the on-screen keyboard. Completely local: a small built-in list of
 * common chat words per language plus the words of the user's own sent messages (learned on the
 * console only, never sent anywhere, can be switched off and cleared). */
#ifndef ORBIT_SUGGEST_H
#define ORBIT_SUGGEST_H
#define SUGGEST_MAX 3
#define SUGGEST_WORD 32
void suggest_reset(void);
/* the word being typed is the text after the last space; prev = the word before it (or "")
 * returns the number of suggestions written to out */
/* lang = keyboard layout (KB_*, i18n.h): picks the built-in word list */
int suggest_get(int lang, const char *text, char out[SUGGEST_MAX][SUGGEST_WORD]);
/* learn the words (and word pairs) of a sent message */
void suggest_learn(const char *text);
/* text with its last (partial) word replaced by word plus a space; returns 0 if it did not fit */
int suggest_apply(char *text, int cap, const char *word);
/* persistence: one "word<TAB>count" or "a b<TAB>count" line per entry */
int suggest_load(const char *path);
int suggest_save(const char *path);
int suggest_learned(void);
#endif

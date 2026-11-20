/* ORBIT phone upload: a tiny, temporary web page on the home network for sending one picture
 * from a phone to the console (profile pictures).
 *
 * - Only while the "picture from phone" screen is open; it stops after one picture, when the
 *   screen closes, or after 10 minutes.
 * - The address contains a random 128-bit token; any other path gets a plain 404.
 * - One connection at a time, bounded headers, bounded body (IMG_MAX_UPLOAD), timeouts.
 * - No port forwarding is set up for it, so it is reachable from the home network only.
 * - The received bytes are only handed to the strict PNG/JPEG readers; nothing is stored as is. */
#ifndef ORBIT_UPLOAD_H
#define ORBIT_UPLOAD_H
#include <stddef.h>
#include <stdint.h>

enum { UPLOAD_WAITING, UPLOAD_RECEIVING, UPLOAD_DONE, UPLOAD_STOPPED };
typedef struct OrbitUpload OrbitUpload;
/* page texts are given by the caller (already translated, plain text) */
OrbitUpload *upload_start(int port, const char *title, const char *pick, const char *sent,
                          const char *failed);
int upload_port(const OrbitUpload *u);
/* shortens the 10-minute lifetime (tests) */
void upload_set_lifetime(OrbitUpload *u, uint32_t ms);
const char *upload_token(const OrbitUpload *u);
int upload_state(OrbitUpload *u);
/* takes the received file (caller frees); 0 when nothing arrived yet */
int upload_take(OrbitUpload *u, uint8_t **data, size_t *n);
void upload_stop(OrbitUpload *u);
#endif

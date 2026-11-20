#ifndef ORBIT_PRESENCE_WORKER_H
#define ORBIT_PRESENCE_WORKER_H
/* The presence plugin's loop (freestanding; runs in orbit_presence.sprx and in the PC tests). */
#include "notify_core.h"
#include "presence.h"
/* fills g with the running game; returns 0 when detection worked (g->playing may still be 0) */
typedef int (*PresenceDetect)(PresenceGame *g);
/* presence.cfg allows sharing ("share 1") */
int presence_enabled(const NotifyPlatform *p);
/* one report to one meeting point; 0 = the host confirmed it */
int presence_send(const NotifyPlatform *p, const NotifyHost *h, const PresenceGame *g);
/* polls detect, reports changes (and refreshes while a game runs) to every host in notify.cfg,
 * until *p->stop. Reports "no game" once when the game ends or sharing is switched off. */
void presence_worker(const NotifyPlatform *p, PresenceDetect detect);
#endif

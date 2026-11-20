/* ORBIT UI sounds: short tones synthesized at run time (no sample files, original sounds).
 * Mixed as 48 kHz stereo float like the voice chat. On the PS3 they play through their own
 * audio port (sfx_device_*); during a call they are ducked so voices stay clear. */
#ifndef ORBIT_SFX_H
#define ORBIT_SFX_H
enum {
    SFX_MOVE,    /* focus moved */
    SFX_SELECT,  /* confirm */
    SFX_BACK,    /* back / cancel */
    SFX_TAB,     /* L1/R1 section change */
    SFX_MESSAGE, /* new message */
    SFX_NOTIFY,  /* friend request, invite, mention */
    SFX_ERROR,   /* refused action */
    SFX_SEND,    /* message sent */
    SFX_COUNT
};
void sfx_reset(void);
void sfx_play(int id);
/* 0 = off, 100 = full (UI sounds are designed to sit well below the voice chat) */
void sfx_set_volume(int percent);
int sfx_volume(void);
void sfx_set_duck(int in_call);
int sfx_active(void);
/* adds to the buffer (does not clear it) */
void sfx_mix48(float *stereo, int frames);
/* PS3 device layer; host build: no-ops */
int sfx_device_start(void);
void sfx_device_stop(void);
#endif

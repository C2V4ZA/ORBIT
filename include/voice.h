/* Orbit voice chat: 16 kHz mono, 20 ms frames, IMA-ADPCM (64 kbit/s), relayed by the group host. */
#ifndef ORBIT_VOICE_H
#define ORBIT_VOICE_H
#include <stddef.h>
#include <stdint.h>
#define VOICE_RATE 16000
#define VOICE_FRAME 320
#define VOICE_PACKET 168
#define VOICE_USERS 32
enum { VF_AUDIO = 1, VF_PING = 2 };
/* packet: type, user, seq(2), predictor(2), index, 0, 160 bytes ADPCM */
void voice_reset(void);
/* capture: 16 kHz mono samples from the microphone thread */
void voice_capture(const int16_t *pcm, int n);
int voice_take_packet(uint8_t out[VOICE_PACKET]);
void voice_set_muted(int muted);
int voice_muted(void);
int voice_mic_level(void);
int voice_self_speaking(void);
/* playback: packets from the network, pulled as 48 kHz stereo float by the audio thread */
void voice_receive(const uint8_t *packet, int n);
void voice_mix48(float *stereo, int frames);
int voice_speaking(int user);
/* per-person playback volume in percent: 0 = muted locally, 100 normal, max 200 */
void voice_set_gain(int user, int percent);
int voice_gain(int user);
/* helpers shared with the device layer */
int voice_resample_to16k(const int16_t *in, int n, int rate, int16_t *out, int cap);
/* microphone bytes -> samples with automatic byte-order detection (state starts at 0) */
int voice_pcm16_order(const uint8_t *raw, int samples, int stride, int *state);
void voice_pcm16_decode(const uint8_t *raw, int samples, int stride, int big_endian, int16_t *out);
/* microphone test: your own voice is played back after the codec, nothing is sent */
void voice_set_loopback(int on);
int voice_loopback(void);
int voice_noise_floor(void);
/* short description of the microphone stream (device, rate, byte order) for the settings */
const char *voice_mic_info(void);
/* device layer (PS3: cellMic + cellAudio; host: silent) */
int voice_device_start(char *message, size_t capacity);
void voice_device_stop(void);
int voice_device_has_mic(void);
/* EXPERIMENTAL PS5 DualSense over USB (dualsense.c, PS3 only): speaker and microphone of the
 * controller for the voice chat. Off by default; falls back to TV sound and the headset. */
void voice_ds_configure(int audio, int mic);
void voice_ds_call(int active);
int voice_ds_audio_active(void);
int voice_ds_mic_active(void);
unsigned voice_ds_buttons(void); /* same bits as the Orbit pad buttons */
const char *voice_ds_note(void);
#endif

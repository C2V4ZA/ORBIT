/* Desktop test build: no audio devices. Tests feed voice_capture() directly. */
#include "voice.h"
#include <stdio.h>
int voice_device_start(char *message, size_t capacity) {
    if (capacity)
        message[0] = 0;
    return 0;
}
void voice_device_stop(void) {
}
int voice_device_has_mic(void) {
    return 1;
}
/* no DualSense on the desktop build */
void voice_ds_configure(int audio, int mic) {
    (void)audio;
    (void)mic;
}
void voice_ds_call(int active) {
    (void)active;
}
int voice_ds_audio_active(void) {
    return 0;
}
int voice_ds_mic_active(void) {
    return 0;
}
unsigned voice_ds_buttons(void) {
    return 0;
}
const char *voice_ds_note(void) {
    return "";
}
const char *voice_mic_info(void) {
    return "";
}

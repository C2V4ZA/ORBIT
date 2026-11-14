# Audio & voice chat

[← Back to README](../README.md)

## How voice chat works

- 16 kHz voice, compressed to about 64 kbit/s, sent only while you speak (voice activity detection).
- A jitter buffer smooths network hiccups; several speakers are mixed.
- Sound plays through the normal PS3 audio output (TV, sound system, or what you selected in the PS3 settings).
- Voice keeps running while the XMB is open (PS button). It does **not** run while a game is running – the PS3 runs one application at a time.

## Microphone

ORBIT uses the microphone the PS3 offers: a USB headset, a Bluetooth headset supported by the PS3, or a PS5 controller connected by USB. Choose the device in the PS3 system menu:

**Settings › Accessory Settings › Audio Device Settings**

## PS5 controller (DualSense)

| | |
|---|---|
| ✅ **Supported** | Speaking through the **PS5 controller microphone**. Connect the controller by USB cable; the PS3 handles it as a USB audio device. |
| ❌ **Not supported** | Playing voice chat audio through the **controller's built-in speaker**. |

Use your TV or another supported PS3 audio output to hear the others. Playback through headphones plugged into the controller is not verified and therefore not supported.

> **Technical note:** ORBIT 1.0.3 does not take control of the DualSense itself. It relies on the PS3's own USB audio handling. An experimental direct driver from the internal 1.0 builds was removed because it stopped the PS3 from using the controller's microphone.

**Deutsch:** Der PS5-Controller kann in ORBIT als Mikrofon zum Sprechen verwendet werden. Die Stimmen anderer Teilnehmer können jedoch nicht über den Lautsprecher des Controllers wiedergegeben werden. Verwende dafür den Fernseher oder ein unterstütztes Audiogerät.

**Français :** Le microphone de la manette PS5 peut être utilisé pour parler dans ORBIT. Cependant, les voix des autres participants ne peuvent pas être diffusées par le haut-parleur intégré de la manette. Utilisez votre téléviseur ou une autre sortie audio compatible.

**Español:** Puedes utilizar el micrófono del mando PS5 para hablar en ORBIT. Sin embargo, las voces de otros participantes no se pueden reproducir a través del altavoz integrado del mando. Utiliza el televisor u otra salida de audio compatible.

## Test your microphone

**Settings › Audio › Test microphone** – you hear yourself exactly as your friends hear you (after compression). Nothing is sent. The screen shows:

- **Level** – how loud your voice arrives,
- **Noise floor** – the background noise ORBIT learned (a quiet room is usually well below 300),
- **Microphone data** – device, sample rate and format, useful for bug reports.

## Voice channels

- Each server can have several voice channels; switch by selecting another one.
- **Participants & volume** (○ menu during a call): mute a person **for yourself only**, or set their volume from **0 to 200 %**.
- Moderators with the *Mute members* permission can mute someone **for the whole server**; the host PS3 then stops forwarding that person's voice.
- Private voice rooms: restrict who can see a voice channel with roles (server settings).

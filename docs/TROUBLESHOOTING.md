# Troubleshooting

[← Back to README](../README.md)

| Problem | What to do |
|---|---|
| **“Host offline” / server greyed out** | ORBIT must be running on the PS3 that hosts it. It reconnects automatically within seconds once it is back. |
| **“Code not recognised”** | Type all 20 characters exactly; dashes are added automatically, case does not matter. A typo is caught by the checksum. |
| **The code only works at home** | ORBIT was still checking the internet connection. Wait a few seconds after start, then create the invite again (Settings › Connection shows “Internet ready”). |
| **“Your connection has no public IPv4 address (CGNAT/DS-Lite)”** | Normal for some providers – the built-in tunnel takes care of it. |
| **FRITZ!Box: automatic port opening refused** | Not required (the tunnel is used). If you want it anyway, ORBIT shows the exact one-time setting in Settings › Connection. |
| **“Your friend has never been online on your PS3”** | After accepting a friend request, both PS3s must be online once at the same time before private chats can be started. |
| **“The other PS3 runs an older Orbit”** | That console still uses 0.9.x – it works with basic features; update it to 1.0.3. |
| **No microphone found** | Plug in the headset or PS5 controller *before* joining voice chat, then join again. Check PS3 Settings › Accessory Settings › Audio Device Settings. |
| **My voice sounds wrong / constant noise** | Settings › Audio › **Test microphone**. A noise floor far above ~300 means the selected device delivers noise – choose another device in the PS3 audio device settings. Report the *Microphone data* line in an issue. |
| **I hear nothing through the PS5 controller** | Expected: playback through the controller speaker is not supported. Use the TV or a headset. |
| **Message stays “Waiting for delivery”** | The host of that chat is offline. It is sent automatically when it is back (up to 32 messages wait). |
| **Interface in the wrong language** | Settings › Language › choose a language or “Automatic”. |

Still stuck? Open an issue with the [bug report form](https://github.com/C2V4ZA/ORBIT/issues/new/choose). Please include the ORBIT version, CFW/HEN version and what the screen shows (a photo helps).

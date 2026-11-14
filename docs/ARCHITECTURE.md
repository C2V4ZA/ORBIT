# Architecture

[← Back to README](../README.md) · German technical notes: [ORBIT-1.0.md](ORBIT-1.0.md)

## Principle: every PS3 is its own server

There is no central ORBIT server. Each console runs its own **meeting point** in the background. Servers and private chats you create live on your PS3; the ones you joined live on the PS3 that created them. The app keeps one encrypted link to each PS3 it talks to (up to 16) and polls it for changes.

## Source layout

| Module | File(s) | Role |
|---|---|---|
| Meeting point (host) | `src/store.c` | Users, servers, channels, roles, messages, friends; JSON requests; every permission check; atomic save to `data.json` with backup |
| Network | `src/network.c` | TLS 1.2 PSK (mbedTLS), one thread per peer, voice relay per channel, LAN discovery |
| Identity, codes | `src/platform.c` | Ed25519 identity, Orbit ID, 20-character codes, Argon2id key stretching |
| App core | `src/app.c` | Links to other PS3s, sign-in, friends sync, offline queue, notifications, voice call thread |
| Reachability | `src/nat.c`, `src/bore.c`, `src/relay.c` | UPnP/PCP, built-in bore tunnel, optional relay |
| Interface | `src/ui.c`, `src/main_ps3.c` | Software renderer (1280×720 virtual canvas, scaled to 480p–1080p), input, menus, keyboard, intro |
| Languages | `src/i18n.c`, `include/i18n_keys_a.h`, `include/i18n_keys_b.h` | All interface texts in DE/EN/FR/ES |
| Voice | `src/voice_core.c`, `src/voice_ps3.c` | IMA-ADPCM codec, voice activity detection, jitter buffer, mixer; cellMic + cellAudio |
| Relay server (PC/Linux) | `relay/orbit_relay.py` | Optional self-hosted meeting point |

`src/dualsense.c` (experimental direct DualSense driver) is still compiled but **disabled** in 1.0.3 and never started. `src/ds_probe.c` and `src/audio_probe.c` are separate diagnostic programs.

## Protocol

- Length-prefixed JSON over TLS; voice as 168-byte frames on a separate connection.
- `ORBIT_PROTOCOL` = 10. Clients send `{"op":"hello","protocol":10}`; a 0.9.x client (protocol 9) gets the old view – servers only, first text channel.
- Host errors carry a stable key (`ekey`) so each reader sees them in their own language.

## Data

`data.json` version 2. Version 1 files (0.9.x) are converted on load: each group becomes a server with a text and a voice channel and default roles. Saving is atomic (temporary file, then rename, previous copy kept as `.bak`).

## Limits (from the source)

| Constant | Value |
|---|---|
| Servers + private chats + friend inbox per PS3 (`ORBIT_GROUPS`) | 48 |
| People per PS3 (`ORBIT_USERS`) | 32 |
| Stored messages per PS3 (`ORBIT_MESSAGES`) | 512 |
| Channels / categories / roles per server | 16 / 8 / 8 |
| Friends (`ORBIT_FRIENDS`) | 48 |
| Connected remote PS3s (`APP_LINKS`) | 16 |
| Offline queue (`UI_OUTBOX`) | 32 messages |
| Message length | 768 bytes |

## Building

Requirements:

- The open-source **ps3dev** toolchain with PSL1GHT and portlibs (FreeType, libpng, zlib, mbedTLS) – no Sony SDK. Release used: [ps3dev nightly-2026-10-12](https://github.com/ps3dev/ps3dev/releases/tag/nightly-2026-10-12).
- **Mbed TLS 2.28.10** built for the host (for the desktop tests).
- Linux (or WSL) with `make`, `gcc`, `python3`.

The Makefile expects the tools next to the repository in `../build-tools/` (`ps3dev/` and `mbedtls-2.28.10/`). Override with `make TOOLS=/path/to/build-tools` or `MBED=/path/to/mbedtls-2.28.10`.

```sh
make test                  # desktop test suite (needs host mbedTLS)
bash tools/package.sh      # PS3 build + PKG (needs the ps3dev toolchain)
python3 tools/verify_package.py
```

## Tests

See [tests/README.md](../tests/README.md). The host logic, protocol and app core are the same C code on the desktop and on the PS3; the renderer, input, audio devices and performance can only be verified on the console.

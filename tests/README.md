# Tests

The desktop tests compile the same C code that runs on the PS3 (host logic, protocol, app core, voice codec) and run it on Linux. Two complete ORBIT apps can run in one process on loopback, acting as two consoles.

Run everything:

```sh
make test MBED=/path/to/mbedtls-2.28.10      # or keep it in ../build-tools/
bash tools/test_relay.sh
bash tools/test_bore.sh
bash tools/test_tunnel.sh
bash tools/test_nat.sh                        # run on its own: the fake routers need free ports
```

| Test | What it checks |
|---|---|
| `test_core.c` | Accounts, group codes, persistence, crash recovery, write rollback, UTF-8, microphone PCM decoding |
| `test_network.c` | TLS 1.2 PSK handshake, wrong key rejected, two live clients |
| `test_sessions.c` | Invitations, remembered sessions, revoked tokens |
| `test_app.c` | Two apps: invite code, messages, voice chat relay, restart, host offline + reconnect |
| `test_social.c` | 1.0: migration from 0.9.5, protocol 9 compatibility, identity binding and forgery, friends, private chats, blocking, servers/channels/categories, roles/ranks/permissions, private channels, mute/ban, replies/edit/reactions/search, presence, persistence |
| `test_friends.c` | 1.0 end-to-end with two apps: languages, friend code → request → accept, presence, private chat, notification, server template, adding a friend, offline queue delivered after the host restarts |
| `test_voice.c` | Microphone byte order, noise gate against constant hum, microphone test loopback |
| `test_nat.c` + `fake_router.py` | UPnP, PCP, CGNAT detection, FRITZ!Box refusal (simulated routers) |
| `tools/test_relay.sh`, `test_bore.sh`, `test_tunnel.sh` | Full app test through the relay, the bore tunnel protocol (`fake_bore.py`) and a tunnel; relay load test |

Results for release 1.0.3 (desktop): all of the above pass. `test_nat.sh` occasionally fails when started directly after the other suites because the simulated routers still hold their ports; it passes when run on its own.

What the desktop tests **cannot** check: rendering on the PS3, controller input, real microphones and audio output, performance, and the PS3 network stack. Those are tested on real hardware by the developer.

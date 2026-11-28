<div align="center">

<img src="assets/branding/orbit-banner.svg" alt="ORBIT — Modern Communication for PlayStation 3 and PlayStation 4" width="100%">

<br>

**Text. Voice. Friends. Communities. PS3 and PS4, connected.**

[![Latest release](https://img.shields.io/github/v/release/C2V4ZA/ORBIT?label=latest&color=4f8cff&style=flat-square)](https://github.com/C2V4ZA/ORBIT/releases/latest)
![Platform](https://img.shields.io/badge/platform-PS3%20%7C%20PS4-0d0f13?style=flat-square)
![CFW / HEN](https://img.shields.io/badge/runs%20on-CFW%20%7C%20HEN-a66bff?style=flat-square)
![Languages](https://img.shields.io/badge/languages-10-39d98a?style=flat-square)
[![Desktop tests](https://img.shields.io/github/actions/workflow/status/C2V4ZA/ORBIT/tests.yml?label=tests&style=flat-square)](https://github.com/C2V4ZA/ORBIT/actions)
[![License](https://img.shields.io/badge/license-GPL--3.0--or--later-4f8cff?style=flat-square)](LICENSE)

**English** · [Deutsch](README-DE.md) · [Français](README-FR.md) · [Español](README-ES.md)

[**⬇ Download ORBIT 1.3.0**](https://github.com/C2V4ZA/ORBIT/releases/tag/v1.3.0) &nbsp;·&nbsp; [Installation](docs/INSTALLATION.md) &nbsp;·&nbsp; [Features](docs/FEATURES.md) &nbsp;·&nbsp; [Controls](docs/CONTROLS.md) &nbsp;·&nbsp; [FAQ](docs/FAQ.md) &nbsp;·&nbsp; [Changelog](CHANGELOG.md)

</div>

---

> [!IMPORTANT]
> **New official account.** ORBIT was first published by its creator under the GitHub account **C2V3ZA**. Access to that account was lost because of a problem with its e-mail address, and the account was deleted. **C2V4ZA is the new official account of the same creator** – this is not a fork or a copy, and ORBIT is not abandoned. [Read the full announcement](docs/ACCOUNT-MIGRATION.md).

## What is ORBIT?

ORBIT is an independent homebrew app that brings modern communication to the PlayStation 3 and PlayStation 4: text chat, voice chat, friends, private messages and community servers with channels and roles – console to console, with PS3 ↔ PS4 crossplay.

- **No account, no password.** Each console signs in with its own secret key.
- **No router settings, no PC.** ORBIT opens its own tunnel over the internet. The **Privacy Shield** (on by default) makes sure no other console ever connects to your own IP address.
- **Your data stays on your consoles.** Every server lives on the PS3 or PS4 that created it. There is no ORBIT company server.
- **Speaks your language.** 10 interface languages, chosen automatically from the system language.

<div align="center">
<img src="docs/screenshots/home.png" alt="ORBIT 1.3.0 Home: Spotlight with the game in the drive, friends online, messages and your servers" width="88%">
<br><sub>ORBIT 1.3.0 Home – rendered by the app's own interface code with sample data.</sub>
</div>

## New in 1.3.0

| | |
|---|---|
| **ORBIT for PS4** | First public build for jailbroken PS4 consoles (GoldHEN), built with the open-source OpenOrbis toolchain – native 1080p, transitions, frosted glass. Experimental. Source: [`ps4/`](ps4/) |
| **Crossplay PS3 ↔ PS4** | Same servers, friends, private chats and voice channels; a small PS3 / PS4 tag next to names |
| **Discover across consoles** | Public PS3 and PS4 servers in one list, each tagged PS3 or PS4 |
| **Privacy Shield** | On by default: no console ever connects to your own IP address – everything goes through the Orbit tunnel or a relay. [Details](docs/PRIVACY-SHIELD.md) |
| **Forward secrecy** | A fresh key per connection between two 1.3 consoles |
| **Covers: ask first** | Covers already on your console are always shown; official covers are only downloaded after you say *Yes* |
| **PS4 background messages** | Experimental GoldHEN plugin: new messages as notifications while you play |
| **Fixes** | Voice could turn into noise between different compilers (PS3 ↔ PS4), shield gap in the PS3 notification plugin, thread-safety fixes, PS4 sound output |

> [!NOTE]
> **1.3.0 is a preview and has not been tested on a real PS3 or PS4 yet** (only on the desktop: 107 automated tests, sanitizers, fuzzing). It includes everything from 1.2.1, which was not released separately. Please report problems in [Issues](https://github.com/C2V4ZA/ORBIT/issues).

### PS4: install and background messages
1. Install **ORBIT-PS4-1.3.0-TEST.pkg** with GoldHEN's package installer (or Debug Settings › Package Installer).
2. Background messages while you play (optional, experimental): needs **GoldHEN 2.3+** with *Load plugins* switched on. In ORBIT: **Settings › Notifications › Install background plugin**. ORBIT copies `orbit_notify.prx` to `/data/GoldHEN/plugins/` and adds one line under `[default]` in `/data/GoldHEN/plugins.ini` (a backup `plugins.ini.orbit-backup` is kept). *Remove background plugin* deletes exactly that line again. Active from the next game you start; every step is logged to `/data/orbit/plugin-log.txt`.

## New in 1.2.1

| | |
|---|---|
| **Game covers** | Box art for your installed games, downloaded on your PS3 from [GameTDB](https://www.gametdb.com) – your language's cover first |
| **Games page** | Portrait tiles filled by the cover; ✕ on a game opens *Find servers for this game*, *Invite friends*, *Discover communities* |
| **Home** | Shows your **last played game automatically**; new **Your servers** row for quick access to your communities |
| **Leave servers** | Works even while the host PS3 is offline – the host is told when it is back |
| **Polski** | 10th interface language with Polish keyboard and word suggestions |
| **Fixes** | Long Russian/Ukrainian texts no longer cut, UTF-8-safe text handling everywhere, fewer redraws |

> [!NOTE]
> **1.2.1 is a preview.** It was tested on the desktop (automated tests, sanitizers) and in the RPCS3 emulator, not yet on a real PS3. Please report problems in [Issues](https://github.com/C2V4ZA/ORBIT/issues). ORBIT 1.2.0 offers this update by itself: *Settings › About Orbit › Check for updates*.

## New in 1.2.0

| | |
|---|---|
| **New interface** | Glass design, shared top bar (L1/R1), Spotlight Home, 8 themes, new intro, UI sounds, emoji keyboard, Do not disturb |
| **Discover** | Browse public communities on connected PS3s – search, filters, sorting, details, online/offline badges, saved list when a host is offline |
| **Games** | Your installed PS3 games, and which friends list or play them |
| **Background notifications** | New messages appear top right in the XMB and in games – even when ORBIT is closed (plugin) |
| **Profile pictures** | From a USB stick or from your **phone** (scan the QR code) |
| **Animated pictures** | GIF avatars and server icons (max. 10 fps, 15 s, 150 frames), pause under load, *Reduce motion* setting |
| **Server Appearance** | Own still or animated icon, symbol, colour, banner, reset, preview with crop, **change log** – only owner and *Manage server* roles, checked by the host |
| **Moderation** | Reports, moderator report list, timed bans (1 / 7 / 30 days / until lifted), name filter |
| **Languages** | English, Deutsch, Français, Español, Italiano, Português, **Русский, Українська, Norsk bokmål** – plural forms, Cyrillic and Nordic keyboards, local word suggestions |
| **Signed updates** | ORBIT checks for Ed25519-signed updates; nothing is installed on its own |

Everything from 1.0.3 is still there: friends, private and group chats, offline messages, community servers with categories, roles and permissions, voice channels, profiles. Full list: [CHANGELOG.md](CHANGELOG.md) · [docs/FEATURES.md](docs/FEATURES.md)

> [!NOTE]
> **1.2.0 is new.** Background notifications were confirmed on a real PS3. Most other 1.2 features were tested on the desktop (automated tests, sanitizers, fuzzing) and are still being tested on real PS3 hardware. Please report problems in [Issues](https://github.com/C2V4ZA/ORBIT/issues).

## Screenshots

<table>
<tr>
<td width="50%"><img src="docs/screenshots/discover.png" alt="Discover: PS3 and PS4 community cards with online, host-offline and PS3/PS4 badges"><br><sub><b>Discover</b> – find PS3 and PS4 communities, even when a host is offline</sub></td>
<td width="50%"><img src="docs/screenshots/games.png" alt="Games: installed PS3 games grid"><br><sub><b>Games</b> – your PS3 games and what friends play</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/servers.png" alt="Community server with categories, text and voice channels and a chat"><br><sub><b>Community servers</b> – channels, roles, voice</sub></td>
<td><img src="docs/screenshots/messages.png" alt="Private message conversation between friends"><br><sub><b>Messages</b> – private one-to-one and group chats</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/phone-picture.png" alt="Profile picture from the phone: QR code and waiting state"><br><sub><b>Profile picture from your phone</b> – scan, choose, done</sub></td>
<td><img src="docs/screenshots/keyboard-ru.png" alt="Russian on-screen keyboard with word suggestions"><br><sub><b>Русский</b> – Cyrillic keyboard with word suggestions</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/friends.png" alt="Friend profile with status, Orbit ID and shared servers"><br><sub><b>Friends</b> – profile, status, Orbit ID, shared servers</sub></td>
<td><img src="docs/screenshots/voice.png" alt="Voice channel showing the people inside"><br><sub><b>Voice chat</b> – voice channels with live participants</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/privacy-shield.png" alt="Privacy settings with the Privacy Shield switched on"><br><sub><b>Privacy Shield</b> – your IP stays hidden, only tunnel or relay</sub></td>
<td><img src="docs/screenshots/covers-question.png" alt="First-start question about downloading official game covers"><br><sub><b>Covers</b> – asks before anything is downloaded</sub></td>
</tr>
</table>

<sub>All screenshots are produced by ORBIT 1.3.0's own interface renderer (the same C code that runs on the PS3) with sample names and messages. More: [docs/screenshots](docs/screenshots).</sub>

## Languages

| Available | Status |
|---|---|
| English · Deutsch · Français · Español · Italiano · Português | complete |
| Русский · Українська · Norsk bokmål | complete (new in 1.2.0) |
| Polski | complete (new in 1.2.1) |

The Language page also lists the languages that are **not available yet** and says why: Japanese, Korean, Chinese and Hindi need a font ORBIT does not ship yet; Arabic needs right-to-left text and letter shaping; the other European languages are not translated yet. ORBIT never marks a language as complete when it is not.

Translations are plain UTF-8 files (`lang/<locale>.lang`). Every line is checked when ORBIT starts – placeholders must match English, so a broken translation can never crash the app. Want to help translate? `make build/orbit-lang`, then `build/orbit-lang template <locale>` gives you every English text to translate, and `build/orbit-lang check lang/<locale>.lang` shows completeness and any refused line.

## Coming soon: 24/7 servers

Today a server is only online while its host PS3 runs ORBIT. We are working on an **always-on ORBIT server**: communities will **stay online even when the host's PS3 is switched off** – messages, members and channels reachable around the clock. It will be optional; ORBIT keeps working PS3-to-PS3 as before.

## Download & install

1. Download **[Orbit-PS3-1.3.0.pkg](https://github.com/C2V4ZA/ORBIT/releases/tag/v1.3.0)** (title ID `ORB000001`).
2. Copy it to a FAT32 USB stick and install it with the **Package Manager** on a PS3 running **CFW or HEN**.
3. Start **Orbit Chat** from the Game column.

Your data from 1.0.x is kept. Step-by-step guide and first steps: **[docs/INSTALLATION.md](docs/INSTALLATION.md)**

> [!WARNING]
> Updating keeps your friends, servers, messages and name. **Going back to an older version is not supported.**

## Background notifications, game presence & covers

Nothing extra to download: **the plugins are inside the ORBIT package** and ORBIT installs them for you.

**Background notifications** (new messages top right in the XMB and in games, even when ORBIT is closed)
1. Start ORBIT › **Settings › Notifications** › **Install background plugin**.
2. Turn on **Show new messages top right** (optionally *Show message text*).
3. **Restart the PS3.** Needs Cobra/webMAN (CFW) or PS3HEN – ORBIT writes `/dev_hdd0/plugins/orbit_notify.sprx` and adds it to `boot_plugins.txt` / `boot_plugins_hen.txt`.
4. To remove it: same menu › **Remove background plugin**, then restart.

**Game presence** (experimental, friends only, off by default): Settings › Notifications › **Install the detection plugin** (a separate `orbit_presence.sprx`), then restart the PS3. Not yet tested on a real PS3.

**Game covers**: nothing to install. With internet, ORBIT downloads the cover of each installed game by its Title ID from [GameTDB](https://www.gametdb.com) – only the Title ID leaves your PS3. Switch it off or reload all covers in **Settings › Privacy** (*Download game covers*, *Refresh covers*). Games without a cover on GameTDB keep their normal icon; missing covers can be added on GameTDB. No cover images are included in the package.

## Good to know

- A server is only reachable while ORBIT is running on the PS3 that hosts it (24/7 servers are planned, see above).
- Connections between consoles are encrypted, but the **host PS3 can read the messages of its own servers and private chats** – ORBIT does not claim end-to-end encryption. See [docs/PRIVACY.md](docs/PRIVACY.md).
- Pictures are checked by ORBIT's own strict PNG, JPEG and GIF readers; only a re-encoded 96 × 96 version leaves your PS3, never the original file.
- No voice chat while a game is running (the PS3 runs one application at a time). Background notifications do work in games.
- The PS5 controller microphone can be used to speak; its speaker cannot play voice chat. Details: [docs/AUDIO.md](docs/AUDIO.md)
- All limits and known issues: [docs/FAQ.md](docs/FAQ.md) · [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)

## Documentation

| | |
|---|---|
| [Installation](docs/INSTALLATION.md) | Install, update, first steps |
| [Features](docs/FEATURES.md) | What ORBIT can do |
| [Controls](docs/CONTROLS.md) | Every button |
| [Friends](docs/FRIENDS.md) · [Servers](docs/SERVERS.md) · [Audio](docs/AUDIO.md) | Guides |
| [Privacy & security](docs/PRIVACY.md) | Identity, encryption, what the host can see |
| [Architecture](docs/ARCHITECTURE.md) | How ORBIT works inside, protocol, limits, tests |
| [Roadmap](docs/ROADMAP.md) · [Changelog](CHANGELOG.md) | Plans and history |
| [Anleitung (Deutsch)](docs/Anleitung-DE.md) | German user guide |

Some guides still describe 1.0.3 and are being updated for 1.2.0.

## Building from source

ORBIT is written in C and built with the open-source [ps3dev / PSL1GHT](https://github.com/ps3dev/ps3dev) toolchain – no Sony SDK. The desktop test suite (`make test`) runs on Linux, partly with AddressSanitizer/UBSan and fuzzing. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#building) and [tests/README.md](tests/README.md).

## Made with AI assistance

ORBIT has been developed with **extensive help from artificial intelligence**: architecture, C programming, features, interface, networking, audio, bug investigation, tests, translations, documentation and release preparation. The idea, the creative direction and the important decisions come from the human creator, who tests releases on real PlayStation 3 hardware. We say this openly because you should know how the software you install was made.

## License & legal

ORBIT is free software under the [GPL-3.0-or-later](LICENSE). Third-party components and their licenses: [THIRD-PARTY.md](THIRD-PARTY.md).

ORBIT is an independent homebrew project. It is **not affiliated with, endorsed by or connected to Sony Interactive Entertainment**. “PlayStation” and “PS3” are trademarks of their respective owners and are used here only to describe compatibility.

<div align="center"><br><img src="assets/branding/orbit-mark.svg" alt="ORBIT mark" width="56"><br><sub>ORBIT — Connecting PlayStation 3 players again.</sub></div>

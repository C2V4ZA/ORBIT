<div align="center">

<img src="assets/branding/orbit-banner.svg" alt="ORBIT — Modern Communication for PlayStation 3" width="100%">

<br>

**Text. Voice. Friends. Communities. One connected PS3 experience.**

[![Latest release](https://img.shields.io/github/v/release/C2V4ZA/ORBIT?label=latest&color=4f8cff&style=flat-square)](https://github.com/C2V4ZA/ORBIT/releases/latest)
![Platform](https://img.shields.io/badge/platform-PlayStation%203-0d0f13?style=flat-square)
![CFW / HEN](https://img.shields.io/badge/runs%20on-CFW%20%7C%20HEN-a66bff?style=flat-square)
![Hardware tested](https://img.shields.io/badge/1.0.3-tested%20on%20real%20PS3-39d98a?style=flat-square)
[![License](https://img.shields.io/badge/license-GPL--3.0--or--later-4f8cff?style=flat-square)](LICENSE)

**English** · [Deutsch](README-DE.md) · [Français](README-FR.md) · [Español](README-ES.md)

[**⬇ Download ORBIT 1.0.3**](https://github.com/C2V4ZA/ORBIT/releases/tag/v1.0.3) &nbsp;·&nbsp; [Installation](docs/INSTALLATION.md) &nbsp;·&nbsp; [Features](docs/FEATURES.md) &nbsp;·&nbsp; [Controls](docs/CONTROLS.md) &nbsp;·&nbsp; [FAQ](docs/FAQ.md)

</div>

---

> [!IMPORTANT]
> **New official account.** ORBIT was first published by its creator under the GitHub account **C2V3ZA**. Access to that account was lost because of a problem with its e-mail address, and the account was deleted. **C2V4ZA is the new official account of the same creator** – this is not a fork or a copy, and ORBIT is not abandoned. [Read the full announcement](docs/ACCOUNT-MIGRATION.md).

## What is ORBIT?

ORBIT is an independent homebrew app that brings modern communication to the PlayStation 3: text chat, voice chat, friends, private messages and community servers with channels and roles – directly from PS3 to PS3.

- **No account, no password.** Each PS3 signs in with its own secret key.
- **No router settings, no PC.** Consoles on the same network find each other; over the internet ORBIT opens its own tunnel.
- **Your data stays on PS3s.** Every server lives on the PS3 that created it. There is no ORBIT company server.
- **Speaks your language.** The interface follows the PS3 system language: English, German, French or Spanish.

<div align="center">
<img src="docs/screenshots/servers.png" alt="ORBIT 1.0.3: a community server with categories, text and voice channels, a live voice card and a chat with replies and reactions" width="88%">
<br><sub>Community server in ORBIT 1.0.3 – rendered by the app's own interface code with sample data.</sub>
</div>

## Highlights of 1.0.3

| | |
|---|---|
| **Friends** | 20-character friend code, requests, accept/decline, remove, block, a persistent Orbit ID per PS3 |
| **Messages** | One-to-one and group chats, replies, editing, deleting, 6 reactions, search, mute, timestamps |
| **Offline messages** | Messages wait on your PS3 (“Waiting for delivery”) and are delivered when the other PS3 is reachable |
| **Community servers** | Categories, text and voice channels, templates, description, welcome message, icon, colour, banner, public or private |
| **Roles & permissions** | Owner, Admin, Moderator, Member + custom roles, 10 permissions – enforced by the host PS3 |
| **Voice channels** | Switch channels, mute people for yourself, volume per person 0–200 %, server-wide moderator mute |
| **Profiles** | Avatar, profile colour, about me, favourite game, custom status, Online / Away / Busy / Invisible |
| **Notifications** | Friend requests, DMs, mentions, calls, invitations, moderation – each type can be switched off |
| **Themes** | Dark, Blue, Purple, Minimal |
| **Languages** | English, German, French, Spanish – automatic from the PS3 system language |

Full list: [docs/FEATURES.md](docs/FEATURES.md)

## Screenshots

<table>
<tr>
<td width="50%"><img src="docs/screenshots/home.png" alt="Home: greeting, friends online, requests, unread chats and recent activity"><br><sub><b>Home</b> – your friends, chats and servers at a glance</sub></td>
<td width="50%"><img src="docs/screenshots/friends.png" alt="Friend profile with status, Orbit ID, about me, favourite game and shared servers"><br><sub><b>Friends</b> – profile, status, Orbit ID, shared servers</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/messages.png" alt="Private message conversation between friends"><br><sub><b>Messages</b> – private one-to-one and group chats</sub></td>
<td><img src="docs/screenshots/server-overview.png" alt="Server overview with banner, description, welcome message and member list with roles"><br><sub><b>Community servers</b> – overview, members, roles</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/voice.png" alt="Voice channel showing the people inside and the active voice card"><br><sub><b>Voice chat</b> – voice channels with live participants</sub></td>
<td><img src="docs/screenshots/settings.png" alt="Settings: interface language follows the PS3 system language"><br><sub><b>Settings</b> – language, look, notifications, audio, privacy</sub></td>
</tr>
</table>

<sub>All screenshots are produced by ORBIT 1.0.3's own interface renderer (the same C code that runs on the PS3) with sample names and messages. More: [docs/screenshots](docs/screenshots).</sub>

## Download & install

1. Download **[Orbit-PS3-1.0.3.pkg](https://github.com/C2V4ZA/ORBIT/releases/tag/v1.0.3)** (title ID `ORB000001`).
2. Copy it to a FAT32 USB stick and install it with the **Package Manager** on a PS3 running **CFW or HEN**.
3. Start **Orbit Chat** from the Game column and confirm your name.

Step-by-step guide, updating from 0.9.5 and first steps: **[docs/INSTALLATION.md](docs/INSTALLATION.md)**

> [!WARNING]
> Updating from 0.9.5 keeps your groups, messages and name – they become servers automatically. **Going back from 1.0.3 to 0.9.5 is not supported.**

## PS5 controller

The **PS5 controller microphone can be used to speak** in ORBIT – the PS3 handles it as a USB audio device. **Voice chat audio cannot be played through the controller's built-in speaker.** Use your TV or another supported PS3 audio output. Details: [docs/AUDIO.md](docs/AUDIO.md)

## Good to know

- A server is only reachable while ORBIT is running on the PS3 that hosts it.
- Connections between consoles are encrypted, but the **host PS3 can read the messages of its own servers and private chats** – ORBIT does not claim strict end-to-end encryption. See [docs/PRIVACY.md](docs/PRIVACY.md).
- No voice chat while a game is running (the PS3 runs one application at a time).
- All limits and known issues: [docs/FAQ.md](docs/FAQ.md) · [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)

## Documentation

| | |
|---|---|
| [Installation](docs/INSTALLATION.md) | Install, update from 0.9.5, first steps |
| [Features](docs/FEATURES.md) | Everything ORBIT 1.0.3 can do |
| [Controls](docs/CONTROLS.md) | Every button |
| [Friends](docs/FRIENDS.md) · [Servers](docs/SERVERS.md) · [Audio](docs/AUDIO.md) | Guides |
| [Privacy & security](docs/PRIVACY.md) | Identity, encryption, what the host can see |
| [Architecture](docs/ARCHITECTURE.md) | How ORBIT works inside, protocol, limits, tests |
| [Roadmap](docs/ROADMAP.md) · [Changelog](CHANGELOG.md) | Plans and history |
| [Anleitung (Deutsch)](docs/Anleitung-DE.md) | German user guide |

## Building from source

ORBIT is written in C and built with the open-source [ps3dev / PSL1GHT](https://github.com/ps3dev/ps3dev) toolchain – no Sony SDK. The desktop test suite runs on Linux. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#building) and [tests/README.md](tests/README.md).

## Made with AI assistance

ORBIT has been developed with **extensive help from artificial intelligence**: architecture, C programming, features, interface, networking, audio, bug investigation, tests, translations, documentation and release preparation. The idea, the creative direction and the important decisions come from the human creator, who also tests every release on real PlayStation 3 hardware. We say this openly because you should know how the software you install was made.

## License & legal

ORBIT is free software under the [GPL-3.0-or-later](LICENSE). Third-party components and their licenses: [THIRD-PARTY.md](THIRD-PARTY.md).

ORBIT is an independent homebrew project. It is **not affiliated with, endorsed by or connected to Sony Interactive Entertainment**. “PlayStation” and “PS3” are trademarks of their respective owners and are used here only to describe compatibility.

<div align="center"><br><img src="assets/branding/orbit-mark.svg" alt="ORBIT mark" width="56"><br><sub>ORBIT — Connecting PlayStation 3 players again.</sub></div>

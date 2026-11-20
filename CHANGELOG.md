# Changelog

All public releases of ORBIT. Downloads: [Releases](https://github.com/C2V4ZA/ORBIT/releases).

## [1.2.0] — Next Update · preview

Preview build: most new features are tested on the desktop (automated tests, sanitizers, fuzzing), not yet on real PS3 hardware.

### Added
- Glass interface with a shared top bar, Spotlight Home, Discover and Games grids, 8 themes, new intro, UI sounds, emoji keyboard, Do not disturb.
- Discover: public communities on connected PS3s with search, filters, sorting, details, saved list for offline hosts; public directory support (server not online yet).
- Games page (installed PS3 games, friends who list or play them); experimental game presence plugin (friends only, off by default).
- Background notifications plugin: new messages top right in the XMB and in games.
- Profile pictures from USB or phone (QR code); animated GIF pictures for profiles and server icons (10 fps, 15 s, 150 frames), Reduce motion and Animated pictures settings.
- Server Appearance: own still/animated icon, symbol, colour, banner, reset, preview with crop, change log; permission checked by the host.
- Reports, moderator report list, timed bans, name rules and filter.
- Signed updates (Ed25519), nothing installs on its own.
- Languages: Italiano, Português, Русский, Українська, Norsk bokmål (9 in total); Language page with all 27 planned languages and their honest status; plural rules; Cyrillic and Nordic keyboards; local word suggestions.

### Security
- Own strict PNG, JPEG and GIF readers with limits against decompression bombs; only re-encoded 96 × 96 pictures leave the PS3; fuzz tests.
- Language files are checked line by line (placeholders must match English, strict UTF-8).
- Phone upload page: single-use token, 10-minute expiry, home network only.

### Known limits
- Servers are only online while the host PS3 runs ORBIT (24/7 servers are planned).
- Japanese, Korean, Chinese, Hindi (font) and Arabic (shaping) are not available yet.

## [1.0.3] — Major Community Update

The step from a simple group chat to a friends-and-communities platform for the PS3. Tested by the developer on real PlayStation 3 hardware.

### Added
- **Interface in four languages** (English, German, French, Spanish), chosen automatically from the PS3 system language; English fallback; manual choice in Settings.
- **New navigation** with five sections (Home, Friends, Messages, Servers, Settings) and a **message mode** (L2).
- **Friends:** 20-character friend code, requests (accept / decline), remove, block, persistent **Orbit ID** (Ed25519 identity bound to each PS3), shared servers, message, call, add to your server.
- **Private messages:** one-to-one and group chats, replies, editing, deleting, six reactions, search, mute, unread markers.
- **Offline queue:** messages wait on your PS3 (“Waiting for delivery”) and are delivered once the host is reachable.
- **Community servers:** categories, multiple text and voice channels, server template, description, welcome message, icon, colour, banner, public/private, private and announcement channels.
- **Roles & permissions:** Owner, Admin, Moderator, Member, custom roles, ten permissions – all checked by the host PS3.
- **Profiles:** avatar symbol, colour, about me, favourite game, custom status, Online / Away / Busy / Invisible.
- **Voice channels:** switch channels, per-person mute and volume (0–200 %), server-wide moderator mute.
- **Notifications** for requests, messages, mentions, calls, invitations and moderation, individually switchable.
- **Four themes:** Dark, Blue, Purple, Minimal.
- **Microphone test** (Settings › Audio) with level, noise floor and device information.
- **Keyboard layouts** for German, English, French and Spanish.

### Improved
- Voice playback runs in a high-priority thread with a larger buffer; if it is ever late, silence is played instead of repeated sound.
- Noise gate learns the background noise, so a constant hum is no longer transmitted.
- Lighter background work on the host (no full data copy for read-only requests, faster channel overview).
- Error messages from another PS3 appear in your own language.

### Changed
- 0.9.5 groups are converted to servers on first start (text channel with the old messages + voice channel). Data format version 2 – **no downgrade to 0.9.5**.
- Protocol version 10; 0.9.x clients are still served with basic features.
- The PS5 controller is used through the PS3's own USB audio handling (microphone supported, controller speaker not supported).

### Fixed
- Microphone audio could turn into constant noise in the internal 1.0 builds because an experimental DualSense driver took the controller away from the PS3; that driver is disabled in 1.0.3.
- Voice could turn into a buzzing sound in the internal 1.0 builds when the interface used too much processing time.

### Known limitations
- Servers are only available while ORBIT runs on the host PS3.
- No voice chat while a game is running.
- The host PS3 can read the messages it hosts (not strict end-to-end encryption).
- No Japanese, Korean or Chinese characters in the bundled font.
- Limits per PS3: 48 slots (servers, private chats, friend inbox), 32 people, 512 stored messages, 16 connected PS3s.

## Internal development (not released)

**1.0, 1.0.1 and 1.0.2** were internal development milestones on the way to 1.0.3. They were never published.

## [0.9.5] — Original Release

The first official public version.

- Text chat in groups with timestamps and saved history
- Voice chat (headset), keeps running in the XMB
- Groups with up to 32 people per host and 24 groups per PS3
- 20-character invitation codes
- Local network discovery, automatic UPnP/PCP, built-in internet tunnel (no PC, no router settings), optional relay
- Animated intro, own on-screen keyboard
- Saved conversations with backup copy, automatic reconnection
- German user interface

[1.0.3]: https://github.com/C2V4ZA/ORBIT/releases/tag/v1.0.3
[0.9.5]: https://github.com/C2V4ZA/ORBIT/releases/tag/v0.9.5

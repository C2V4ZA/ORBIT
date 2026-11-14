# ORBIT 1.0.3 – all features

[← Back to README](../README.md)

## Basics (since 0.9.5)

- **No account, no password:** every PS3 signs in automatically with its own secret key.
- **Text chat** with timestamps, grouped messages, up to 768 characters, saved history.
- **Voice chat:** 16 kHz voice, ~64 kbit/s, voice activity detection, jitter buffer, several speakers mixed, keeps running in the XMB.
- **Invites:** one 20-character code – address and group included, typos detected by a checksum.
- **Connection without setup:** same-network discovery, automatic UPnP/PCP port opening, built-in internet tunnel (no PC, no router settings), optional own relay.
- **Automatic reconnect** after a connection loss.
- **Own on-screen keyboard** (no dimmed system keyboard), **animated intro**, **everything saved** with a backup copy.

## Interface languages

- Follows the **PS3 system language**: English, German, French, Spanish. Other system languages show English.
- Manual choice in **Settings › Language**; a changed system language is picked up on the next start.
- **Only the interface is translated** – messages, names, server and channel names, bios and custom statuses always stay exactly as written.
- The keyboard has German, English, French and Spanish layouts (R1 switches).

## Navigation

Five sections switched with ◀ ▶: **Home** (overview, recent activity) · **Friends** · **Messages** · **Servers** · **Settings**. Message mode with **L2**. All buttons: [CONTROLS.md](CONTROLS.md)

## Friends — [FRIENDS.md](FRIENDS.md)

20-character friend code · persistent Orbit ID · send / accept / decline requests · remove · block · shared servers · direct message · call · add to your server · online status and profile.

## Private messages

One-to-one and group chats · replies with quote · edit own messages · delete · 6 reactions · search · mute · timestamps · unread markers · offline queue (“Waiting for delivery”).

## Community servers — [SERVERS.md](SERVERS.md)

Categories (8) · text and voice channels (16) · templates · description · welcome message · icon · colour · banner · public or private · private channels (by role) · announcement channels (write by role) · move/rename/describe channels · member management · bans · new invite code.

## Roles & permissions

Owner · Admin · Moderator · Member · custom roles (up to 8 roles) with name, colour, rank and **10 permissions**: manage server, manage channels, edit roles, invite, send messages, delete messages, remove members, ban members, manage voice channels, mute members. **Checked by the host PS3.**

## Profiles & status

Avatar symbol · profile colour · about me · favourite game · custom status · Online / Away / Busy / Invisible · member since.

## Voice channels — [AUDIO.md](AUDIO.md)

Several voice channels per server · switch channels · voice in private chats (calls) · see who is talking · mute someone for yourself · volume per person 0–200 % · server-wide mute by moderators · microphone test · PS5 controller microphone via the PS3's USB audio (no playback through the controller speaker).

## Notifications

Friend requests · accepted requests · private messages · server messages · server invitations · incoming calls · @mentions · moderation (muted, removed). Each type can be switched off; muted chats stay quiet except for mentions.

## Themes

ORBIT Dark · ORBIT Blue · ORBIT Purple · Minimal – flat colours only, no extra load on the PS3.

## Compatibility

- Protocol 10. A 1.0.3 host still serves 0.9.x clients (servers only, first channel).
- 0.9.5 data is converted automatically on the first start. No downgrade.

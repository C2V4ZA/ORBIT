# Installing ORBIT

[← Back to README](../README.md) · German guide: [Anleitung-DE.md](Anleitung-DE.md)

## 1. Requirements

| | |
|---|---|
| Console | PlayStation 3 (any model) |
| System | Custom firmware (CFW) **or** HEN (Homebrew Enabler) |
| Network | Wi-Fi or cable; internet for chatting outside your home |
| Optional | USB/Bluetooth headset or a PS5 controller (USB) for voice chat |

ORBIT does **not** need a PSN account, a PC or router settings.

## 2. Download

Get the package from the official release page:

- **ORBIT 1.0.3 (latest):** https://github.com/C2V4ZA/ORBIT/releases/tag/v1.0.3 → `Orbit-PS3-1.0.3.pkg`
- ORBIT 0.9.5 (original release, for reference): https://github.com/C2V4ZA/ORBIT/releases/tag/v0.9.5

Compare the SHA-256 checksum shown on the release page if you want to be sure the file is genuine.

## 3. Copy it to the PS3

- **USB stick:** format it as FAT32, copy the `.pkg` file to the root of the stick, plug it into the PS3.
- **FTP (e.g. with webMAN MOD):** upload the file to `/dev_hdd0/packages/`.

## 4. Install

1. **HEN users:** enable HEN first (as you always do).
2. In the XMB, open **Game › Package Manager › Install Package Files** (the exact menu names depend on your CFW/HEN version).
3. Select **Orbit-PS3-1.0.3.pkg** and confirm.

The app appears in the Game column as **Orbit Chat** (title ID `ORB000001`).

## 5. First start

1. Start **Orbit Chat**. A short intro plays (press any button to skip it).
2. Your PS3 user name is suggested – keep it or change it, then choose **Let's go**.
3. The interface uses your PS3 system language (English, German, French, Spanish; other languages show English). Change it any time in **Settings › Language**.

## 6. Create a server

**Servers** section (◀ ▶) › **Create Server** › choose *With template* (Information, Community and voice channels) or *Simple* (one text and one voice channel) › type a name.

Invite people with △ (**Invite**): ORBIT shows a 20-character code like `EZB3C-R6AE2-MJ9N8-XFMSG`. Your friend presses □ and types it.

## 7. Add friends

1. **Friends › Your friend code** – send this 20-character code to your friend.
2. Your friend opens **Friends › Add Friend** and types it.
3. The request appears at the top of your **Friends** list – press ✕ to accept.
4. Compare the **Orbit ID** (e.g. `PQ96-D2FM-5DBS`) with your friend to be sure you added the right person.

More: [FRIENDS.md](FRIENDS.md)

## 8. Text and voice

- ✕ on a text channel or chat opens the keyboard; START sends.
- SELECT joins/leaves voice chat; R2 mutes your microphone.
- On a friend: ✕ = message, SELECT = call.

All buttons: [CONTROLS.md](CONTROLS.md)

## 9. Audio devices

Plug in a headset or a PS5 controller by USB. Choose which device is used for the microphone and for sound in the PS3 system menu **Settings › Accessory Settings › Audio Device Settings**. In ORBIT, **Settings › Audio › Test microphone** lets you hear yourself the way your friends hear you. More: [AUDIO.md](AUDIO.md)

## 10. Updating from 0.9.5 to 1.0.3

1. Install `Orbit-PS3-1.0.3.pkg` over the old version (same title ID) – no need to delete anything.
2. On first start ORBIT converts your data automatically:
   - every 0.9.5 **group becomes a server** with a text channel (your old messages) and a voice channel,
   - your **name, messages and joined groups** are kept,
   - friends still on 0.9.5 can keep writing in your servers with basic features.
3. Optional but recommended: before updating, back up the folder `/dev_hdd0/game/ORB000001/USRDIR/data` via FTP.

> [!WARNING]
> **Downgrading from 1.0.3 to 0.9.5 is not supported.** Version 0.9.5 cannot read the converted data. Only go back if you restore the backup of the `data` folder you made before updating.

## 11. Uninstall

XMB › Game › **Orbit Chat** › △ › Delete. This also deletes your servers, messages and keys stored on this PS3.

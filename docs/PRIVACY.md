# Privacy & security

[← Back to README](../README.md)

This page describes what ORBIT actually does – no more, no less.

## Identity

- On first start every PS3 creates two random 32-byte secrets in `/dev_hdd0/game/ORB000001/USRDIR/data/`: `device.key` (your identity) and `server.key` (your own meeting point).
- From `device.key` ORBIT derives an **Ed25519 key pair**. Your **Orbit ID** is a short fingerprint of the public key.
- When you sign in on another PS3, you prove your identity with a signature that is only valid for **that** PS3. A host cannot reuse it elsewhere, and a forged signature is rejected.
- Your name can change; your Orbit ID does not.

## Friend codes and invite codes

A code contains a network address and an 8-character group code (40 random bits). Anyone who knows a code can **connect** to that PS3 – being a member of a server or friend still requires the host's rules (invites, acceptance, bans). Code guessing is slowed down: each guess costs an Argon2id computation and the host limits attempts.

## Connection encryption

- Connections between consoles use **TLS 1.2 with a pre-shared key** (AES-128-GCM), derived from the group code with Argon2id.
- The built-in internet tunnel (bore.pub) and the optional relay only forward these encrypted bytes; they can see IP addresses and traffic volume, not content.

> [!IMPORTANT]
> **This is not strict end-to-end encryption.** The **host PS3** decrypts and stores the messages of the servers and private chats it hosts – it has to, because the host is the server. Only chat in servers and private chats whose host you trust.

## Local storage

| File | Content |
|---|---|
| `data.json` (+ `.bak`) | Everything this PS3 hosts: servers, channels, roles, members, messages, friend list |
| `profile.json` (+ `.bak`) | Your name, profile, settings, connected PS3s, read markers, offline message queue |
| `device.key`, `server.key` | Your secrets |

Files are stored **unencrypted** on the PS3 hard disk. Anyone with access to your console's file system (e.g. via FTP) can read them. Never share these files.

## Permissions and moderation

Server permissions, bans, mutes and private channels are **enforced by the host PS3**, not only by hiding buttons.

## Blocking

Blocking is enforced on your own PS3 (no friend requests, removed from your private chats, cannot write there). In servers hosted by others, messages of blocked people are hidden on your screen.

## Offline messages

Undelivered messages wait **only on your PS3** until the receiving host is reachable. No third party stores them.

## Network dependencies

| Service | Used for | Required? |
|---|---|---|
| Local network broadcast | Finding PS3s in the same home | No |
| UPnP / PCP on your router | Opening a port automatically, if the router allows it | No |
| bore.pub (public tunnel) | Reaching your PS3 from the internet without router settings | Only for internet use without an open port |
| Your own relay (`relay/`) | Alternative meeting point you run yourself | No |

## Known limitations

- The host of a server or private chat can read its messages.
- Keys and data are not encrypted at rest.
- bore.pub is a free third-party service without guarantees.
- A malicious host could show a wrong member list for its own servers; friendships store the full public key of your friend.
- ORBIT has not been audited by an independent security reviewer.

Report security problems: [SECURITY.md](../SECURITY.md)

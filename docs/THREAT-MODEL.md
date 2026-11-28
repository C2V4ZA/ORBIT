# ORBIT threat model and security status (1.3, PS3 + PS4)

Written 2026-11-27 from the source tree, not from an external audit. Every "done" below points at
code and a PC test; **nothing here was verified on a console**, and nothing has been reviewed by
anyone outside this project. Status words: **done** (implemented + tested on the PC), **partial**
(some of it), **open** (not implemented).

## 1. System and trust boundaries

* Every console runs a full ORBIT: client **and** host ("meeting point") for the servers it created.
  Members connect to the host console. The host stores messages for its servers in `data.json`.
* Transport: TLS 1.2 with a PSK derived from the group code (Argon2), since 1.3 ECDHE-PSK when both
  sides run 1.3 (forward secrecy), plain PSK with older versions.
* Middle points: Orbit tunnel (bore protocol, default `bore.pub`, third party) and Orbit relay
  (self-hosted `relay/orbit_relay.py`). Both splice TCP; they never see plaintext.
* Identity: Orbit ID = Ed25519 key from a random device key; per host a device token
  (BLAKE2b(device key, server id)) plus a signature binding the key to that host.
* Other network endpoints (all optional or user-triggered): game-cover website (Title IDs only),
  update manifest on GitHub (off by default; PS4 build has updates disabled), public directory (only
  if `directory.cfg` names one), PS4 DNS fallback 1.1.1.1 / 8.8.8.8 (only "bore.pub" is asked).

## 2. Adversaries

| # | Adversary | Typical goal |
|---|---|---|
| A1 | Another ORBIT user (friend, server member, someone with an invite code) | learn your IP (doxxing, DDoS), crash your console, spam |
| A2 | The host console of a server you are in | read messages, forge members, track you |
| A3 | Network observer (Wi-Fi, ISP) | read or alter traffic, see who talks to whom |
| A4 | Tunnel / relay operator | see IPs, timing, volume; drop traffic |
| A5 | Someone with your console's data (stolen USB backup, shared console) | read history, steal identity |
| A6 | Update or directory server compromise | push malicious code or entries |

## 3. Status against the brief (sections 8 and 9)

| Requirement | Status | Where / evidence |
|---|---|---|
| Hide IP addresses from other users | **done** (with Privacy Shield on) | `docs/next/PRIVACY-SHIELD.md`, `tests/test_shield.c` |
| Relay-based communication, no direct P2P with the shield | **done** | `src/app.c` (`own_endpoint`, `link_open`), `src/network.c` (loopback-only accept); full app test via relay and tunnel with the shield on |
| No silent fallback to direct when no relay | **done** | `test_shield`: visible reason, no connection until the user turns it off |
| IP protection for voice and server connections | **done** | voice uses the same links (`link_open(..., voice=1)`) |
| No hardware IDs, serial numbers, MAC addresses | **done** (verified by source search) | no MAC/PSID/IDPS/serial API is called; device key is random (`orbit_keyfile`) |
| No unnecessary location data | **done** | none is collected; console language only picks the cover region |
| Strip image metadata | **done** | pictures are re-encoded from pixels (`avatar_make`, `anim_make_gif`); `test_image` injects EXIF/GPS and a GIF comment and checks they are gone |
| No automatic external image fetches that reveal the IP | **done** | profile pictures come from the host over TLS; GameTDB covers only after an explicit yes at the first start (the question names the IP) |
| Protection against tracking links | **done** (nothing to open) | ORBIT has no browser and builds no link previews; links are plain text |
| No unnecessary storage of personal data | **partial** | no e-mail/password/real name; but hosts keep full message history in `data.json` |
| No hidden telemetry | **done** (verified by source search) | every outgoing connection is listed in section 1 |
| Privacy-friendly defaults | **done** | shield on, presence sharing off, directory listing off, auto-update off |
| Relay infrastructure prepared, locally testable | **done** | `relay/orbit_relay.py`, `relay/install.sh`, `tools/test_relay.sh`; IPs no longer logged by default |
| E2EE for private messages | **open** | crypto core `src/sealed.c` exists and is unit-tested, **not wired into DMs** (`docs/next/E2EE.md`) |
| E2EE for voice | **open** | voice is TLS between console and host; the host can decode it |
| Secure key management | **partial** | keys are random, wiped after use in many places; key files are not encrypted at rest |
| Perfect forward secrecy | **partial** | transport: **done** for 1.3 <-> 1.3 (ECDHE-PSK, `test_shield`); messages: open (needs E2EE) |
| Encrypted local message history | **open** | `data.json` is plain JSON on the host console |
| Secure identity verification | **partial** | Ed25519 binding per host, impersonation refused (`test_social`); no safety-number UI yet |
| MITM protection | **partial** | TLS-PSK: a MITM needs the group code; anyone who has the code (members) could MITM |
| Replay protection | **done** for transport (TLS); sealed-message replay protection exists in the unwired core |
| Identity theft | **partial** | device token + signature; a stolen data directory is a stolen identity |
| Manipulated network packets | **partial** | TLS AEAD rejects tampering; request JSON is validated and fuzzed |
| Buffer overflows / RCE | **partial** | bounded parsers, fuzzing with ASan/UBSan (section 4), no external code review; the PS3 has no ASLR, so a memory bug would be serious |
| Path traversal | **partial** | picture names must be 32 hex characters (`avatar_hash_valid`, tested); no full audit of every path |
| Manipulated GIFs and images | **done** (PC) | strict decoders with size/frame/time budgets, decompression-bomb checks, fuzzed |
| Spam and flooding, rate limiting | **partial** | 1 message per 300 ms per session, 32 peers per host, relay 64 connections per IP; no per-user rate limit on other operations yet |
| Server-side DDoS protection | **open** | relay limits only; a real DDoS needs network-level protection from the relay hoster |
| Signed updates | **done** | Ed25519-signed manifest, SHA-256 of the package (`src/update.c`, `test_update`) |
| Validation of all network data | **partial** | JSON fields are checked where used; fuzzed (section 4); not proven complete |
| Secure error handling | **partial** | errors are reported by key, no stack traces; not audited |
| No secrets or private keys in logs | **done** for the logs reviewed | `ps4-log.txt` (module, pad, dns, tunnel state), relay log (no IPs by default) |
| No home-made crypto algorithms | **done** | Monocypher and mbedTLS primitives only; `sealed.c` is our own *implementation* of the published X3DH / Double Ratchet specs (needs review before use) |

## 4. Fuzzing and automated security tests

* `make fuzz` (orbit-ps3): own mutation fuzzer with AddressSanitizer + UBSan, 11 targets: host
  request handler (signed-in and anonymous sessions, structure-aware JSON), invitation codes, JPEG/
  PNG decoders, GIF, profile-picture blobs, Base64, cover decoder + config, update manifest,
  directory entries, sealed-message decrypt, raw bytes into the host's TCP port (host must still
  accept a real connection afterwards). Each target is also a libFuzzer entry point
  (`-DFUZZ_LIBFUZZER`); libFuzzer itself is not installed in this WSL.
* `make fuzz` (orbit-ps4): DNS reply parser, 500 000 mutated replies.
* Results of the runs are recorded in `docs/next/PROGRESS.md` / the test folder notes, with the
  exact counts. A fuzz run without findings is evidence, not proof of absence of bugs.
* Not fuzzed yet: voice frame parsing inside an authenticated link, the bore/relay control
  protocol parsers, the update download path, the phone-upload HTTP server.

## 5. Next steps (in order of risk reduction)

1. Wire E2EE into 1:1 DMs (E2EE.md checklist), then safety numbers + "key changed" warning.
2. Encrypt `data.json` and key files at rest (key from the device key; honest limit: an attacker
   with the full console still gets both).
3. Per-user rate limits on all write operations on the host.
4. Fuzz the remaining parsers (voice frames, bore/relay control lines, upload HTTP).
5. External review of `sealed.c` and of the network code.

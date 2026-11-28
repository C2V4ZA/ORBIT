# ORBIT Privacy Shield (1.3)

Status: implemented in the shared core (PS3 and PS4), tested on the PC (`tests/test_shield.c`,
`tools/test_relay.sh` and `tools/test_tunnel.sh` run the full app test with the shield on). **Not
tested on a console yet.** On by default.

## What it guarantees

With the shield on, no other ORBIT console ever opens a connection to this console's own IP
address, and this console never opens one to another console's own IP address. Every connection
goes through a middle point that splices TCP streams:

| Path | Who sees whose IP address |
|---|---|
| Orbit tunnel (bore protocol, default server `bore.pub`) | the tunnel server sees both; the consoles see only the tunnel server |
| Orbit relay (`relay/orbit_relay.py`, self-hosted) | the relay operator sees both; the consoles see only the relay |
| direct (UPnP/PCP router port, LAN, an old invitation code) | **blocked** while the shield is on |

TLS stays end to end between the consoles on every path (the middle point sees only encrypted
bytes). Since 1.3 the TLS suite is ECDHE-PSK (forward secrecy) when both sides run 1.3.

## How it is enforced (src/app.c, src/network.c)

1. **Host side, hard rule:** `orbit_server_set_shield()` makes the server close every accepted
   connection whose peer is not `127.0.0.1` (tunnel and relay splices connect from loopback, see
   `src/bore.c`, `src/relay.c`). Refusals are counted (`orbit_server_shield_refused`).
2. **LAN discovery:** the host does not answer discovery broadcasts (an answer reveals its LAN
   address); the client does not send them.
3. **Router:** no UPnP/PCP port mapping is opened; an existing mapping is removed when the shield
   is turned on.
4. **Own address:** invitations and the friend code carry only the tunnel address or the relay
   (`own_endpoint`). With neither available there is no code at all, and a stale friend code is
   cleared. The reason is shown (`T_SHIELD_NO_ROUTE`).
5. **Dialling:** an invitation address is dialled only if it is a relay invitation or the address of
   this console's own tunnel server (`orbit_shield_allows`). A code that points at a console's own
   IP is refused with a visible notice (`T_SHIELD_BLOCKED`); it is never tried "just in case".
6. **No silent fallback:** if neither tunnel nor relay works, ORBIT stays offline for that
   connection and says so. Only the user can turn the shield off (Settings > Privacy).
7. **Voice and servers** use the same links, so the same rules apply to calls and to every server.
8. **External fetches:** nothing is fetched from a URL another user supplies (pictures come from the
   host over TLS). Official game covers from GameTDB are a separate, explicit choice: the first-start
   question says that GameTDB sees the console's IP address; without a yes nothing is downloaded
   (1.3, `docs/next/COVERS.md`). The shield does not decide this.

## What it does not hide (honest limits)

* The tunnel server or relay operator sees the IP addresses of both sides and the traffic volume
  and timing (not the content). `bore.pub` is a free third-party service; for full control run your
  own relay (`relay/install.sh`) or your own bore server.
* Only the tunnel server this console itself uses is trusted as "not a console": a friend who uses
  a different tunnel server needs a relay or a new code.
* A host can still see which ORBIT identity (Orbit ID) joins, and when.
* Turning the shield off restores direct connections (faster in the same home network), which
  reveal IP addresses to the other side again.

## Relay infrastructure

The relay already exists (`relay/orbit_relay.py`, asyncio, no dependencies) with per-IP connection
limits (64), host/pipe limits and a 10 s join timeout. 1.3 changes: the log contains no IP
addresses by default (salted per-start tags, `--log-ip` to opt in), the local status query masks
them too. Requirements for running one: a machine with a public IPv4 address (or a bore/playit
tunnel to it), Python 3.8+, one open TCP port (31400). Local test: `tools/test_relay.sh`.

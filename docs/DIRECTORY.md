# Public community directory (03) – orbit-dird

Status: implemented and tested **locally only** (`make test` → `build/test-directory`).
Nothing is deployed and the app has **no built-in directory address**: it uses one only when
`data/directory.cfg` exists. Running it publicly needs the project owner's decision (where, who
operates it, how reports are reviewed).

## What it does
* Hosts that opted in ("List my public servers in the directory", Discover › Square, **off by
  default**) announce their *open* community servers every 10 minutes: name, description, host
  name, member/online counts, icon, colour and the join code. Never messages, member lists,
  presence or private conversations.
* Discover fetches the signed list, merges it into the existing local cache (`directory.json`,
  which stays the offline fallback) and can join a listed server with its code even when no
  connection to that host existed before.
* "Report and hide" sends the report to the directory; after 3 reports from different addresses
  an entry is hidden and logged in `reports.log` for the operator.

## Security
| Threat | Measure |
|---|---|
| Fake listings for someone else's server | announcements are Ed25519-signed with a key derived from the host's `server.key`; a server id is bound to the first key (TOFU, persisted) |
| Replay / stale data | `time` must be within ±10 min and newer than the last accepted one; one announcement per 20 s |
| Tampered list (no CA store on the PS3, TLS not verified) | the list is signed by the directory key; the app pins it (`pub` in `directory.cfg`) |
| Offensive names / junk | same filter as the host (`orbit_text_offensive`), strict field ranges, valid UTF-8, home/private addresses refused |
| Report flooding | reporters counted by keyed hash of the address, one per entry; 60 requests/min/address |
| Abuse of the service | single-threaded, 64 KB bodies, 5 s read timeout; put it behind a TLS reverse proxy with its own limits |

## Running it locally
```
make build/orbit-dird
mkdir -p ~/orbit-dird && cd ~/orbit-dird
/path/to/build/orbit-dird --port 8470 --state dird.json --key dird.key --allow-private
/path/to/build/orbit-dird --key dird.key --print-pub         # for directory.cfg
/path/to/build/orbit-dird --state dird.json --unhide <server>:<id>   (service stopped)
```
`--allow-private` binds to 127.0.0.1 only and accepts home-network join codes (tests). Without it,
the service listens on all interfaces and refuses private addresses. Keep `dird.key` out of any
repository.

`data/directory.cfg` on the PS3 (`/dev_hdd0/game/ORB000001/USRDIR/data/`):
```
url https://directory.example.org
pub <64 hex characters from --print-pub>
```

## Open before going public
* Decide hosting and operator; TLS termination in front of orbit-dird.
* A review process for `reports.log` (unhide or keep hidden).
* Privacy note in the app text and README for listed servers.

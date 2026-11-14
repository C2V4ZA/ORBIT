# Security policy

## Supported versions

| Version | Supported |
|---|---|
| 1.0.3 | ✅ |
| 0.9.5 | ❌ (please update) |

## Reporting a vulnerability

Please **do not** open a public issue for security problems. Use GitHub's private vulnerability reporting on this repository (**Security › Report a vulnerability**) if it is enabled, or contact the maintainer through the GitHub profile [C2V4ZA](https://github.com/C2V4ZA) and ask for a private channel.

Please include the version, the steps to reproduce and the possible impact. You will get an answer as soon as possible; this is a hobby project without guaranteed response times.

## What ORBIT does and does not protect

Read [docs/PRIVACY.md](docs/PRIVACY.md). In short: connections between consoles are encrypted with TLS 1.2, but the host PS3 of a server or private chat can read its messages, and keys and data are stored unencrypted on the PS3 hard disk. ORBIT has not been independently audited.

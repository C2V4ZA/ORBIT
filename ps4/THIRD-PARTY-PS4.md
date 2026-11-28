# Third-party components of the ORBIT PS4 build

Everything from the PS3 build (see THIRD-PARTY.txt: cJSON, Monocypher, QR Code generator, the
ORBIT font, ...) plus the following. No Sony SDK, no Sony files: the package contains no
sce_module/*.prx and no right.sprx; ORBIT only calls system libraries already on the console,
through the open-source OpenOrbis stubs.

| Component | Version | License | Use |
|---|---|---|---|
| OpenOrbis PS4 Toolchain (crt, library stubs, create-fself, create-gp4, PkgTool/LibOrbisPkg) | v0.5.4 | GPL-3.0 (toolchain), LGPL-3.0 (PkgTool/LibOrbisPkg) | build, start-up code, packaging |
| OpenOrbis musl libc fork | v0.5.4 | MIT | C library |
| zlib | 1.3.1 | zlib | compression (FreeType, images) |
| FreeType | 2.13.3 | FreeType License (FTL) | font rendering. Portions of this software are copyright (c) The FreeType Project (www.freetype.org). All rights reserved. |
| Mbed TLS | 2.28.10 | Apache-2.0 | TLS-PSK between consoles, HTTPS |

Source of the libraries: the official release archives (SHA-256 checked in tools/build_deps.sh).

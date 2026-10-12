# Third-party notices

Original launcher code and procedural placeholder artwork: MIT, see LICENSE.
Game assets are not included; OpenBK2's license remains separate.

| Dependency | Pinned revision | License / linkage |
|---|---|---|
| SDL 3.2.28 | 7f3ae3d57459e59943a4ecfefc8f6277ec6bf540 | zlib, static |
| Dear ImGui 1.92.5 | 6d910d5487d11ca567b61c7824b0c78c569d62f0 | MIT, static |
| Embedded ProggyClean font | included with pinned ImGui | MIT |
| Embedded Roboto Medium, Christian Robertson / Google | included with pinned ImGui | Apache-2.0 |
| yyjson 0.12.0 | 7871d321ff4cd8068c1f777c97975dc2fb640ab3 | MIT, static |
| Monocypher 4.0.2 and optional Ed25519 | 0d85f98c9d9b0227e42cf795cb527dff372b40a4 | BSD-2-Clause OR CC0, static |
| PicoSHA2 1.0.1 | 161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29 | MIT, header-only |
| miniz 3.1.0 | 174573d60290f447c13a2b1b3405de2b96e27d6c | MIT, static, read/inflate only |
| curl 8.20.0 | 6e3f8dc1f173b47de9a68516ce4b95bf25598c2f | curl license, static |
| OpenSSL 3.0.2 with Ubuntu 22.04 security patches | native baseline system development package | Apache-2.0, static on Linux |

Python cryptography/cffi/pycparser are pinned developer dependencies only and
are not linked into or shipped with the launcher. Their distributions contain
their respective upstream license notices.

TLS uses Windows Schannel. The Linux build links static OpenSSL from the
distribution and uses its system certificate store. Native linkage is measured
in `docs/results/linux-binary-audit.json`. Non-HTTP curl protocols and optional compression/IDN/HTTP2/PSL
dependencies are disabled. HTTPS certificate/hostname verification is mandatory.

Full upstream notices are retained under `licenses/`. SDL's unused audio,
camera, game controller, haptics, sensors, GPU and Vulkan subsystems are disabled.
OpenGL/ES, X11/Wayland and SDL_Renderer remain available for Linux; D3D renderers remain
available on Windows. No runtime assets or font files are loaded from disk.

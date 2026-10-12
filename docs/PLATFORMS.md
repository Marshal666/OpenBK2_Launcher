# Platform builds and limitations

The delivered binaries are fixture/development builds of version 0.1.0. Production
builds require a real HTTPS endpoint, key identifier and Ed25519 public key at
configure time. They do not contain an operational production release endpoint.

| Target | Actual build and test environment | Result |
|---|---|---|
| Windows x64 | Windows 11, Visual Studio 2026/MSVC 19.51, CMake 4.3.1, MinSizeRel, static CRT | Native build, 9/9 CTest, native renderer, process/shortcut/relocation and offline fixture Play pass. |
| Linux x86-64 | Ubuntu 22.04 VM, GCC 11.4.0, CMake 3.31.6/Ninja, MinSizeRel, glibc 2.35 baseline | Native build, 9/9 CTest, process/permissions/desktop-entry/relocation tests pass. |
| X11 | Xvfb/Openbox/Zenity in Ubuntu VM | Real SDL X11 renderer and install; real pointer-driven folder callback, title drag, minimize/restore and close pass. |
| Wayland | Weston 9 headless compositor with pixman | Real SDL Wayland renderer and fixture installation pass. Pointer/compositor interactions not tested. |
| Windows 7 SP1 x64 | No VM run | **Current Windows artifact is incompatible:** imports `api-ms-win-core-synch-l1-2-0.dll` and WaitOnAddress/WakeByAddressAll through the modern C++ runtime. No verified Windows 7 support is claimed. |

Windows 7 remains a best-effort follow-up requiring a compatible toolchain/runtime
or reviewed synchronization implementation plus a real VM/TLS/render test. No
API shim, cryptographic downgrade or certificate bypass was added to claim it.
The project does not require a separate VC++ runtime installer on current Windows.

## Artifact audits

| Artifact | Bytes | MiB | SHA-256 |
|---|---:|---:|---|
| `OpenBK2Launcher-Windows-x64.exe` | 3,258,368 | 3.107 | `948c7362894f57db3607fbb1303f0046c6b0b6fec7f916a2763d56e5d8bee886` |
| `OpenBK2Launcher-Linux-x64` | 8,036,720 | 7.664 | `6379ed26be142d04baef6b15f5b00681afd106598a2f19b412208f7be40a165f` |

Both are below the 10 MiB target and 15 MiB enforced maximum. See
[Windows audit](results/windows-binary-audit.json) and
[Linux audit](results/linux-binary-audit.json). Application dependencies and
assets are statically linked/embedded; Python is developer-side only.

Windows imports ordinary system libraries: bcrypt, CRYPT32, Secur32, WS2_32,
IPHLPAPI, KERNEL32, USER32, GDI32, WINMM, IMM32, ole32, VERSION, ADVAPI32,
SETUPAPI, SHELL32 and the synchronization API set mentioned above.
[Exact imports](results/windows-binary-audit.imports.txt).

Linux ELF dependencies are `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`,
`libc.so.6` and the normal ELF loader. Highest required glibc symbol is **2.34**;
the tested distribution remains Ubuntu 22.04, not every distribution with glibc
2.34. SDL loads the system graphical stack/drivers as needed. Linux is not fully
static. Native folder picking requires a supported desktop portal or installed
dialog utility (Zenity was tested). `/usr/bin/env` supports desktop shortcut
execution. TLS uses statically linked Ubuntu OpenSSL with the system CA store.
[Imports](results/linux-binary-audit.imports.txt),
[symbol versions](results/linux-symbol-versions.txt),
[actual toolchain](results/linux-toolchain.txt).

The isolated VM used the Ubuntu Python 3.11.0rc1 developer package. Its actual
version is recorded rather than represented as a final Python release. Windows
tools used Python 3.13.7. CI requests Python 3.13; the declared supported tools
baseline is Python 3.11+. Pinned cryptography/cffi/pycparser requirements were
installed in both environments. The launcher has no dependency on this Python.

## Measurements

These are single-run synthetic measurements, not production-game benchmarks.

| Measurement | Observed result | Scope |
|---|---:|---|
| Windows visible native window | 0.072 seconds | Warm machine, small already-installed fixture. |
| Windows idle CPU | 1.25% of one logical core | Five-second sample after settling. |
| Windows working set / peak | 61.40 / 62.05 MiB | Visible native GUI, small fixture. |
| Windows streaming peak | 6.719 MiB | Native core downloads and hashes a 64 MiB ZIP over loopback. |
| Linux streaming peak | 7.426 MiB | Same native-core benchmark in VM. |

The 67,109,792-byte benchmark archive was received and SHA-256 verified in
0.534 seconds (Windows) and 0.602 seconds (Linux). Memory results exclude the
Python fixture server/publisher and, for the stream benchmark, the GUI.
[Windows GUI report](results/windows-performance.json),
[Windows stream report](results/windows-stream-memory.json),
[Linux stream report](results/linux-stream-memory.json).
Real-game startup, full-game peak memory, Internet throughput and Linux idle GUI
performance were not benchmarked.

## Reproducing display and memory tests

Install `xvfb weston openbox xdotool zenity libglib2.0-bin` on the Linux test host.
Run from the project root after building:

```text
python tests/integration/linux_displays.py dist/OpenBK2Launcher-Linux-x64 build/linux/fixture_game --backend x11
python tests/integration/linux_displays.py dist/OpenBK2Launcher-Linux-x64 build/linux/fixture_game --backend wayland
python tests/integration/streaming_memory.py build/linux/transfer_check build/linux-stream-memory.json
python tests/integration/windows_performance.py dist/OpenBK2Launcher-Windows-x64.exe build/windows/MinSizeRel/fixture_game.exe
```

Use an isolated desktop for input tests. All game/config data are disposable
workspace fixtures. Captured BMP frames remain under `build/display-artifacts`,
`build/lifecycle-artifacts` and `build/native-audit`; they are not runtime assets.
On this Windows automation desktop, SetCursorPos is unavailable, so real mouse
interaction was exercised on X11 instead. Wayland uses SDL/compositor hit testing;
it does not attempt to set top-level window coordinates.

Windows paths support Unicode but reject directory paths at 248 UTF-16 units,
including staging paths, with a clear error. Use a short game root. Managed ZIP
paths intentionally use portable ASCII; see [deviations](ARCHITECTURE.md).
Physical power loss, network/removable filesystem durability, adversarial live
path races and high-DPI multi-monitor configurations need additional qualification.
Real-game subset and full-install checks are **not run at the user's instruction**.

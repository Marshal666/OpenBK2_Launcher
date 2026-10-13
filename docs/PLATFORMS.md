# Platform builds and limitations

The delivered binaries are fixture/development builds of version 0.2.0. Production
builds require a real HTTPS endpoint, key identifier and Ed25519 public key at
configure time. They do not contain an operational production release endpoint.

| Target | Actual build and test environment | Result |
|---|---|---|
| Windows x64 | Windows 11, Visual Studio 2026/MSVC 19.51, CMake 4.3.1, MinSizeRel, static CRT | Native build, 10/10 CTest, shared installs, mod lifecycle, native renderer, process/shortcut/relocation and offline fixture Play pass. |
| Linux x86-64 | Ubuntu 22.04 VM, GCC 11.4.0, CMake 3.31.6/Ninja, MinSizeRel, glibc 2.35 baseline | Native build, 10/10 CTest, shared installs, mod lifecycle, process/permissions/desktop-entry/relocation tests pass. |
| X11 | Xvfb/Openbox/Zenity in Ubuntu VM | Real SDL X11 renderer and install; pointer-driven mod Download/Remove, preserved edits, folder callback, title drag, minimize/restore and close pass. |
| Wayland | Weston 9 headless compositor with pixman | Real SDL Wayland renderer and fixture installation pass. Pointer/compositor interactions not tested. |
| Windows 7 SP1 x64 | No VM run | **Current Windows artifact is incompatible:** imports `api-ms-win-core-synch-l1-2-0.dll` and WaitOnAddress/WakeByAddressAll through the modern C++ runtime. No verified Windows 7 support is claimed. |

Windows 7 remains a best-effort follow-up requiring a compatible toolchain/runtime
or reviewed synchronization implementation plus a real VM/TLS/render test. No
API shim, cryptographic downgrade or certificate bypass was added to claim it.
The project does not require a separate VC++ runtime installer on current Windows.

## Artifact audits

| Artifact | Bytes | MiB | SHA-256 |
|---|---:|---:|---|
| `OpenBK2Launcher-Windows-x64.exe` | 3,305,472 | 3.152 | `a13a9cdafc134bba0166ad244b94936d11104e9a37ee3031bc54ab13cefc1b6b` |
| `OpenBK2Launcher-Linux-x64` | 8,073,648 | 7.700 | `f810d85bcfc21ff8cf1086950a18a8296561c7d36bacf2d715ddf2ff3fbeb7e2` |

Both are below the 10 MiB target and 15 MiB enforced maximum. See
[Windows audit](results/gui-polish-windows-binary-audit.json) and
[Linux audit](results/gui-polish-linux-binary-audit.json). Application dependencies and
assets are statically linked/embedded; Python is developer-side only.

Windows imports ordinary system libraries: bcrypt, CRYPT32, Secur32, WS2_32,
IPHLPAPI, KERNEL32, USER32, GDI32, WINMM, IMM32, ole32, VERSION, ADVAPI32,
SETUPAPI, SHELL32 and the synchronization API set mentioned above.
[Exact imports](results/gui-polish-windows-binary-audit.imports.txt).

Linux ELF dependencies are `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`,
`libc.so.6` and the normal ELF loader. Highest required glibc symbol is **2.34**;
the tested distribution remains Ubuntu 22.04, not every distribution with glibc
2.34. SDL loads the system graphical stack/drivers as needed. Linux is not fully
static. Native folder picking requires a supported desktop portal or installed
dialog utility (Zenity was tested). `/usr/bin/env` supports desktop shortcut
execution. TLS uses statically linked Ubuntu OpenSSL with the system CA store.
[Imports](results/gui-polish-linux-binary-audit.imports.txt),
[symbol versions](results/linux-symbol-versions.txt),
[actual toolchain](results/linux-toolchain.txt).

The isolated VM used the Ubuntu Python 3.11.0rc1 developer package. Its actual
version is recorded rather than represented as a final Python release. Windows
tools used Python 3.13.7. CI requests Python 3.13; the declared supported tools
baseline is Python 3.11+. Pinned cryptography/cffi/pycparser requirements were
installed in both environments. The launcher has no dependency on this Python.

## Window and Settings update

The current artifacts also include rounded outer corners and corrected popup
resizing. Windows uses a native window region; X11 uses the system X Shape
extension through `libXext.so.6`; Wayland uses a transparent SDL window with
clipped corner pixels. This works with the software renderer used by the native
display checks. Settings starts at 900 by 560 logical units, bounded by the
available window size. Settings and Mods retain their resized dimensions during
the current launcher session. Closing the launcher resets these dimensions.

Both native builds passed the three relevant CTest checks (`renderer_smoke`,
`binary_size`, `launcher_lifecycle`): Windows in 12.91 seconds and Linux in
15.00 seconds. The full 10-test results above are from the preceding shared-data
and mods revision; the updater and publisher were unchanged by this GUI update.
The [X11 input checks](results/gui-polish-x11-results.json) and
[Wayland rendering check](results/gui-polish-wayland-results.json) also passed.
An additional isolated X11 interaction check dragged Settings from 900 by 560 to
780 by 490, released the mouse, and confirmed it retained that size in subsequent
frames. Both initial setup and configured Settings were visually inspected at
the default size and had no scrollbar. Smaller screens or manually reduced
dialogs may still need scrolling.

The native Windows region was queried: all four corner pixels are excluded and
the center remains included. Windows mouse-driven resizing could not be tested
reliably on the automation desktop; it was exercised through the same ImGui code
on X11. Wayland pointer interactions and mixed-DPI monitor transitions remain
unverified.

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
workspace fixtures. Display sockets use a private, automatically cleaned 0700
directory under `/tmp`: putting `XDG_RUNTIME_DIR` inside a long CI checkout can
exceed the Unix socket pathname limit and prevent Weston from starting. The
display tests report early compositor exits and print log tails on failure;
CI archives complete logs even when a test fails.
The CI startup fix was verified on Ubuntu 22.04 at the exact checkout path
`/home/runner/work/OpenBK2_Launcher/OpenBK2_Launcher`: the old 125-byte socket
path reproduced Weston's `File name too long` error; the corrected Wayland
installation and X11 input tests both passed. Early-exit logging and runtime
cleanup were also checked ([regression results](results/ci-display-startup-regression.json)).
This local reproduction does not claim a subsequent hosted Actions rerun.
Captured BMP frames remain under `build/display-artifacts`,
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

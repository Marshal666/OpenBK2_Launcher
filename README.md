# OpenBK2 Launcher

Independent MIT-licensed C++20/CMake launcher and Python release toolkit. Installs,
updates, repairs and launches signed game releases; preserves user files; supports
offline Play, managed language switching and interrupted-update recovery. The GUI
uses SDL3 and Dear ImGui. No game code or game assets are included.

Native Windows and Ubuntu 22.04 builds and fixture suites have been exercised.
See [milestone reports](docs/MILESTONES.md), [acceptance results](docs/ACCEPTANCE.md)
and [platform limitations](docs/PLATFORMS.md). The delivered executables are
**fixture/development builds**. Production requires your HTTPS manifest endpoint
and public verification key; no production trust values or private keys are bundled.

## Build and test

Use CMake 3.25+, a C++20 compiler, and Python 3.11+ for developer tests/tools.
Players need only the executable and ordinary OS components. Install developer
Python dependencies with `python -m pip install -r tools/requirements.txt`.

Windows: Visual Studio C++ x64 tools and Windows SDK. The preset uses VS 2026.
For VS 2022 configure manually with generator `Visual Studio 17 2022`, `-A x64`,
and `-DOBK_FIXTURE_BUILD=ON`.

```text
cmake --preset fixture-windows
cmake --build --preset fixture-windows --parallel 1
ctest --preset fixture-windows
cmake --install build/windows --config MinSizeRel --prefix dist
```

Linux baseline: Ubuntu 22.04 x86-64, GCC 11+, Ninja, CMake 3.25+. Required build
packages: `build-essential ninja-build pkg-config libssl-dev libx11-dev libxext-dev
libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libwayland-dev
libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev wayland-protocols libglib2.0-bin`.
Ubuntu's default CMake may be older; a developer venv can install `cmake==3.31.6`.

```text
cmake --preset fixture-linux
cmake --build --preset fixture-linux --parallel 4
ctest --preset fixture-linux
cmake --install build/linux --prefix dist
```

The first configure fetches dependency source archives pinned by commit and
SHA-256. Offline source names are `SDL`, `IMGUI_SOURCE`, `YYJSON_SOURCE`,
`MONOCYPHER_SOURCE`, `PICOSHA_SOURCE`, `MINIZ_SOURCE`, and `CURL`; prefix each with
`FETCHCONTENT_SOURCE_DIR_` for its CMake override. Point to the exact extracted
sources. Application libraries link statically; Linux retains system runtime and
graphical dependencies. Every build enforces the 15 MiB hard limit.

## Run a signed fixture

For a one-command isolated demo with relocation/shortcuts initially disabled:

```text
python tools/run_fixture.py --launcher dist/OpenBK2Launcher-Windows-x64.exe --game-binary build/windows/MinSizeRel/fixture_game.exe
```

Generate tiny releases containing the compiled synthetic game. The private fixture
key exists only in memory; the output JSON contains its public key.

```text
python tools/fixture_builder.py --out .fixtures/demo --native-game build/windows/MinSizeRel/fixture_game.exe
dist/OpenBK2Launcher-Windows-x64.exe --fixture-release <absolute-path>/.fixtures/demo/release-3 --fixture-public-key <public_key-from-trusted-public-key.json> --config-dir <absolute-test-config> --root <absolute-test-game-root>
```

On Linux substitute `build/linux/fixture_game` and
`dist/OpenBK2Launcher-Linux-x64`. A downloaded ELF may need
`chmod u+x dist/OpenBK2Launcher-Linux-x64`. The first-run screen chooses a root and language;
Install reconciles the signed target. Fixtures launch a small process that records
its cwd/arguments, then exits. For testing, disable relocation and desktop shortcut
checkboxes unless you intend to exercise those actions. Fixture-only
`--auto-install` runs the real controller and automatically disables those actions.
`--fixture-package-base http://127.0.0.1:PORT` serves fixture ZIPs over loopback.

The production configure defaults to fixture mode **off** and fails without all
three values:

```text
cmake -S . -B build/production -DOBK_MANIFEST_URL=https://example.org/manifest.envelope.json -DOBK_KEY_ID=release-key-2026-01 -DOBK_PUBLIC_KEY_HEX=<64-hex-public-key> -DBUILD_TESTING=OFF
```

Use a platform-appropriate generator and MinSizeRel configuration. These are
public trust inputs, never private signing material. Production accepts no fixture
key, package-directory or loopback override flags. Launcher upgrades/key rotation
require manual distribution of a new executable; there is no self-update.

## Operation and maintenance

Play verifies local managed bytes without requiring an online update check.
Updates are explicit; active games block changes. Settings distinguishes selecting
another existing installation from moving game files. Language changes remain
pending until verified commit succeeds. Repair and reinstall preserve unknown
files and back up modifications to managed files. A missing configured root asks
for reselection. Do not remove `.launcher/transaction.json` or its staging/backups
during recovery; see the [recovery protocol](docs/UPDATE_PROTOCOL.md).

Settings/logs live under `%LOCALAPPDATA%/OpenBK2Launcher` or
`$XDG_CONFIG_HOME/openbk2-launcher` (fallback `~/.config/openbk2-launcher`). Logs
rotate at approximately 1 MiB and redact query values. Backups live inside the
game root and are quota-limited without automatic deletion of user changes.

`--smoke-test` renders three frames without writing settings or installing files.
`--capture <file.bmp>` captures a rendered frame. Desktop integration tests are in
`tests/integration/linux_displays.py`; they require Xvfb/Openbox/xdotool/Zenity and
Weston. CI also checks executable size and native library imports. The default
CTest renderer uses SDL's dummy backend, separately from the native display tests.

* [Architecture and deviations](docs/ARCHITECTURE.md)
* [Final developer report](docs/DEVELOPER_REPORT.md)
* [Signed manifest contract](docs/MANIFEST_V1.md)
* [Python publishing and release checklist](docs/PUBLISHING.md)
* [Security guarantees and boundaries](SECURITY.md)
* [Third-party licenses](THIRD_PARTY_NOTICES.md)

The project does not modify or depend on a checkout of the OpenBK2 game repository.

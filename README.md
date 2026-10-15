# OpenBK2 Launcher

Independent MIT-licensed C++20/CMake launcher and Python release toolkit. Installs,
updates, repairs and launches signed game releases; preserves unrelated user files; supports
offline Play, managed language switching and interrupted-update recovery. Shared
game data is packaged once for both platforms. An experimental Mods panel downloads
signed optional mods and removes them after confirmation. Initial data such as
`Editor/` and `Profiles/` is installed once and then left to the game. The GUI
uses SDL3 and Dear ImGui. No game code or playable game data is included.

Native Windows and Ubuntu 22.04 builds and fixture suites have been exercised.
See [milestone reports](docs/MILESTONES.md), [acceptance results](docs/ACCEPTANCE.md)
and [shared-data/mod results](docs/SHARED_DATA_AND_MODS.md), with
[platform limitations](docs/PLATFORMS.md). The delivered executables are
**fixture/development builds**. Production requires your HTTPS manifest endpoint
and public verification key; no production trust values or private keys are bundled.

Launcher 0.3 adds initial data and fast startup; see its
[behavior and validation report](docs/INITIAL_DATA_AND_STARTUP.md).
Version 0.3.1 also allows initial defaults alongside managed files in `Data/`,
with path-collision checks and Reset limited to the initial files there.

**Publishing** Follow the [step-by-step publishing guide](docs/PUBLISHING.md)
to create the GitHub repository and signing key, connect a production launcher,
publish your first release, and ship later game-data updates.

Prefer a GUI? [OpenBK2 Publisher](docs/PUBLISHER_GUI.md) provides **First-time
setup** and **New update** tabs, folder pickers, saved settings and one button to
build, sign, verify and publish. It can also compile the native production launcher.

**Settings** opens in the main content area. **Back to launcher** returns without
saving edits; **Save** saves the preferences and returns. Installation and repair
actions return to the main screen to show progress. The first-run setup uses the
same page, and the window controls remain available in the header.

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

The launcher icon comes from `res/main.png`. Its Windows ICO and embedded pixel/PNG
data are checked in, so normal builds need no image tools. After replacing the
source image, regenerate them with:

```text
python -m pip install -r tools/requirements-icons.txt
python tools/create_icon.py
```

Windows uses this icon for the executable, application window and shortcuts.
When creating a Linux desktop shortcut, the launcher writes its embedded PNG to
`$XDG_DATA_HOME/openbk2-launcher/icon.png` (default
`~/.local/share/openbk2-launcher/icon.png`) and references it in the desktop entry.
The icon remains available when the launcher relocates. Existing shortcuts can
be refreshed using **Settings → Create desktop shortcut**.

The main artwork comes from `res/launcher_image.png`. After replacing it, use the
same Pillow dependency above and run `python tools/create_artwork.py`. This writes
`assets/hero.bmp`, which CMake embeds into the executable and SDL decodes directly.
The original resolution and pixels are retained; no external image file or extra
runtime image library is needed. The main view scales the image uniformly to fill
roughly 90% of the area below the header, cropping excess edges around the center.
Its title and installation status overlay the image. Play and Install/Update sit
in the bottom-right action strip, alongside launcher and installed-game versions.

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

On Windows, [tools/build_production.ps1](tools/build_production.ps1) combines
configure, build and install, and finds Visual Studio's bundled CMake if it is
not on PATH:

```powershell
./tools/build_production.ps1 -ManifestUrl $ManifestUrl -KeyId $KeyId -PublicKey $PublicKey
```

The executable goes to `dist/production/OpenBK2Launcher-Windows-x64.exe`.
See [publishing step 7](docs/PUBLISHING.md#step-7-build-the-production-launcher-with-your-url-and-public-key)
for the values to supply, optional paths and terminal setup.

## Operation and maintenance

Startup and Play validate installed signed metadata and the launch file's basic
properties without scanning or hashing game assets. A background update check
runs on every launch, with a 5-second connection / 10-second transfer limit and
no retries. Failure is logged and does not block Play. The status is **Ready for
playing**; multiplayer compatibility is the game's responsibility.
Updates are explicit; active games block changes. Settings distinguishes selecting
another existing installation from moving game files. Language changes remain
pending until verified commit succeeds. Repair and reinstall preserve unknown
files and leave initialized user folders untouched. Full managed-file integrity
checks run during installation, update, Repair and recovery. A missing configured root asks
for reselection. Do not remove active journals or their temporary recovery data
during recovery; see the [recovery protocol](docs/UPDATE_PROTOCOL.md).

Publish defaults with `--shared-init-data <folder>` containing game-relative
`Editor/` and `Profiles/` folders. Existing files are preserved during initial
installation/adoption; updates never restore deleted defaults. **Settings > Reset
initial data** replaces the entire declared folders after a destructive-action
confirmation. See [initial-data publishing](docs/PUBLISHING_INPUTS.md#initial-data).

Retained backups are disabled by default. Temporary recovery copies are still
required during operations and cleaned up afterward. Reset and mod removal
permanently discard the affected personal data; only published defaults can be
downloaded again. The backup implementation remains available to developers via
`-DOBK_RETAIN_USER_BACKUPS=ON`. Existing backups made by older builds are left
alone and can be removed manually after inspection.

The default game folder is `~/Games/OpenBK2`: usually
`C:/Users/<user>/Games/OpenBK2` on Windows and `/home/<user>/Games/OpenBK2` on Linux.
You can choose another folder in Settings. A writable folder is required for
installing, updating, managing mods and acquiring the Play lock. Windows Program
Files is normally protected, so it is not the default; the launcher does not
request administrator elevation.
Filesystem/drive/share roots are rejected; choose a dedicated game subfolder.
An executable already in that folder is left in place. Relocation preserves a
different existing launcher and refuses an already-running destination.
See the [filesystem safety review](docs/SECURITY_AUDIT.md) for missing-cache,
adoption and relocation behavior and the actual test results.

Displayed and saved paths use forward slashes. Windows folder input also converts
typed or pasted backslashes, and older settings remain readable. Native Windows
API calls retain the platform's required path format. On Linux, backslashes that
are part of a filename are preserved, since only `/` separates directories.

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
* [Step-by-step publishing guide](docs/PUBLISHING.md)
* [Desktop publishing app](docs/PUBLISHER_GUI.md)
* [Publishing input and package reference](docs/PUBLISHING_INPUTS.md)
* [Security guarantees and boundaries](SECURITY.md)
* [Third-party licenses](THIRD_PARTY_NOTICES.md)

The project does not modify or depend on a checkout of the OpenBK2 game repository.

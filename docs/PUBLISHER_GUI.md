# Publishing with the desktop app

**OpenBK2 Publisher** is a small developer tool with two tabs: **First-time
setup** and **New update**. Fill in the entries, choose your folders, and click
the main button. It runs the existing Python release toolkit and shows its
progress in the window.

## Before you start

1. Create a public GitHub repository with a README and a `main` branch.
2. Create a GitHub token with Contents read/write access to that repository.
3. Prepare the unpacked game files and language packs.
4. Install Python 3.11+ and run `python -m pip install -r tools/requirements.txt`
   from the launcher source checkout.

The [publishing walkthrough](PUBLISHING.md) explains the repository and token
setup. The [input reference](PUBLISHING_INPUTS.md) explains the game folder
layout. The app needs this source checkout and Python installed; it does not
create GitHub repositories or bundle Python.

## Build and open the publisher

Use the same native compiler and SDL/ImGui prerequisites as the
[launcher build](../README.md#build-and-test). The publisher is a separate
executable and is enabled with `OBK_BUILD_PUBLISHER=ON`.

On Windows, with the Visual Studio version used by the preset:

```powershell
cmake --preset fixture-windows -DOBK_BUILD_PUBLISHER=ON
cmake --build build/windows --config MinSizeRel --target OpenBK2Publisher --parallel 2
cmake --install build/windows --config MinSizeRel --component Publisher --prefix dist
```

Open **`dist/OpenBK2Publisher-Windows-x64.exe`**. For VS 2022, configure a separate
build directory with `-G "Visual Studio 17 2022" -A x64 -DOBK_FIXTURE_BUILD=ON
-DOBK_BUILD_PUBLISHER=ON` instead of the VS 2026 preset.

On Linux:

```bash
cmake --preset fixture-linux -DOBK_BUILD_PUBLISHER=ON
cmake --build build/linux --target OpenBK2Publisher --parallel 4
cmake --install build/linux --component Publisher --prefix dist
./dist/OpenBK2Publisher-Linux-x64
```

The fixture setting is only for configuring this developer build without
production trust values. **The publisher can publish real releases.** Its
optional production-launcher build explicitly disables fixture mode.

## First release

1. Open **First-time setup**.
2. Enter the GitHub **owner** and **game repository** names separately.
3. Choose your **private key file**. Select your existing `release.pem` if you
   already followed the command-line guide. For a new setup, type a new PEM
   filename in a private folder outside the checkout, game inputs and releases.
   The app generates it if missing and reuses it if present. Back it up securely;
   protect its folder with Windows permissions or Linux file permissions.
4. Paste your **GitHub token**. It stays in memory for this session. Leaving the
   entry blank uses `GITHUB_TOKEN` from the environment that launched the app.
5. Enter your **game version**, **sequence** and **releases folder**. For example,
   `0.1.0`, `1` and `C:/Users/you/OpenBK2-Publishing/releases`. The app creates
   output under `releases/0.1.0` and publishes tag `v0.1.0`.
6. Browse to the **shared**, **Windows**, **Linux**, **language** and optional
   **mod** folders. For one-time defaults, set **Init shared files** to a separate
   folder containing game-relative paths such as `Editor/default.cfg` and
   `Profiles/default.cfg`. Leave it blank if unused. Leave an unused platform
   blank; supply at least one platform and one language. Scroll the form to reach
   all entries.
7. If needed, expand **More options** to choose your launcher source checkout
   and Python executable. This also contains the key ID, saved public key,
   separate manifest repository/branch, official profile opt-ins and exclusions.
   Preserve the key ID and public key from your existing production setup.
8. Optionally select **Build production launcher for this OS**. This needs
   CMake and the native compiler. The app fills in your manifest URL and public
   key, builds in `build/publisher-production`, and installs the launcher into
   `dist/production`. The first configure may download build dependencies.
9. Click **Set up and publish**. Keep the app open while it works. It stops on
   errors and reports the cause in the log.
10. After **Published successfully**, test installation and Play using your
    production launcher. Keep the complete release output folder for later
    updates and distribute the production launcher separately.

The sequence is the increasing release number used by the launcher. The game
version is the label displayed to players. They are independent of the launcher
app's own version.

**Init shared files** uses the toolkit's `--shared-init-data` feature and needs
launcher **0.3.0 or newer**. These files become user-owned after initialization;
ordinary updates do not overwrite them or restore deleted defaults. Keep their
top-level folders out of the managed shared/platform inputs and official-file
opt-ins. See [initial-data inputs](PUBLISHING_INPUTS.md#initial-data) for details.

To try the process first, select **Local check only (no upload)**. The main
button becomes **Build and verify locally**. After success, the app enables
**Reuse signed output**. Clear Local check only and click the main button to
publish those exact packages without rebuilding them.

## Later updates

1. Replace changed files in the input folders and keep all unchanged files.
   Continue supplying every supported platform, language and desired mod.
   Keep supplying **Init shared files** when used: its current defaults are for
   fresh installs and explicit **Reset initial data** operations.
2. Open **New update**. Settings from the earlier run are already filled in.
3. Choose the **previous release** output folder if needed. Selecting it suggests
   the next patch version and sequence; you can change those suggestions.
4. Check the new version and sequence. A successful publication already prepares
   these values for the next update. Each new version gets a new output folder
   and GitHub tag.
5. Expand **Publishing account and key** to enter the token again if this is a
   new session. Keep your original private key and key ID.
6. Click **Publish update**, then test **Check for updates > Update** in the
   player launcher. Also test a fresh installation.

Ordinary game-data updates do not need another launcher build. The app builds
full snapshots and, using the previous output, patches containing whole changed
files. The signed manifest stays at the same URL.

## Save and load settings files

1. Fill in your entries and checkbox choices on either tab.
2. Click **Save settings...** above the tabs and choose a `.json` filename, such
   as `my-game-publishing.json`.
3. To restore it later, click **Load settings...** and select that file. The saved
   entries, tab and checkbox choices are restored without starting publication.
4. Supply your token when needed. Loading a file leaves the current session's
   token unchanged; the token is never included in the settings file.

The file includes the signing-key **path** and public key, but never the private
key's contents. When using it on another machine, adjust paths and supply the
original private key separately. Automatic settings saving still works as before.

## Retrying and troubleshooting

| Situation | Action |
|---|---|
| Python cannot start or a Python module is missing | Choose the Python 3.11+ executable under More options and install `tools/requirements.txt` with that interpreter. |
| You already have a signing key | Browse to that file. Use your existing key ID and public key under More options. |
| A key or package check fails | Fix the reported input or key mismatch. The workflow stops before publication. |
| An upload or optional launcher build failed after signing | Keep the same version/sequence and enable **More options > Reuse signed output** to retry. This uses frozen output; edits to inputs are not included. |
| Building failed before signing completed | Inspect the incomplete output. Choose a new version/output for a fresh build; the app never deletes or replaces an existing release folder. |
| A publication incident is reported | The fixed manifest may already have advanced. Inspect the endpoint and retry the same verified release. Do not roll back to an older sequence. |
| A published release is missing an asset | Publish a fresh version/tag and higher sequence; the toolkit refuses to modify published release assets. |
| The production build reports a generator mismatch | Use the generator previously used for `build/publisher-production`, or prepare a fresh build directory manually. |

Closing is deferred while a job is running, so the app does not interrupt a
publication halfway through. **Copy log** copies the visible progress log.

Paths and public settings are saved automatically when starting a job and when
closing. On Windows they are normally under `%APPDATA%/OpenBK2/Publisher`; on
Linux, `$XDG_DATA_HOME/OpenBK2/Publisher` or `~/.local/share/OpenBK2/Publisher`.
The folder contains `publisher.json`, `last-run.log` and per-run requests/results.
The GitHub token and private-key contents are never saved there.

The optional launcher build is native: Windows builds the Windows launcher;
Linux builds the Linux launcher. Use the same public trust values on both.

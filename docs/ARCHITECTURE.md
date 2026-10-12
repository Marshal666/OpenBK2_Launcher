# Architecture and deliberate decisions

`launcher_core` has no SDL or ImGui dependency. Its public headers define signed
inventories, a release provider, a streaming downloader, a ZIP reader, planning,
transaction/recovery, settings and native process/shortcut services. The controller
owns separate update-check and operation workers and publishes mutex-protected
snapshots. Rendering, event handling and folder-dialog initiation stay on the UI
thread. Folder callbacks only transfer copied data through a mutex.

The runtime fetches one envelope from a configured HTTPS endpoint. GitHub release
assets are ordinary immutable HTTPS URLs; changing storage providers does not
change signature verification, package checks, filesystem operations or launch
profiles. Python publication is deliberately outside the runtime library.

Decisions that narrow or adjust the specification:

* Production trust is supplied at configure time. The user approved explicitly
  separate fixture builds until a real URL and public key are supplied. Fixture
  command-line trust, local archives and loopback HTTP do not exist in production
  application flows. Development builds are visibly marked.
* Managed relative archive paths use printable ASCII with conservative portable
  names. Selected root paths and JSON labels use UTF-8/native Unicode paths.
  This avoids incomplete Unicode normalization/case-fold defenses; see ADR 0002.
* ZIP central directories must be ordinary ZIP32 with at most 60,000 entries;
  ZIP64 local size fields are supported. The Python builder splits accordingly.
* Windows directory paths of 248 or more UTF-16 units are rejected explicitly,
  including staging paths. Choose a short game root. This conservative policy
  avoids dependence on machine-wide long-path registry settings and preserves
  a possible Windows 7 baseline. The current toolchain's synchronization import
  still blocks Windows 7; see the platform matrix rather than assuming support.
* UI translations are complete in English; German action labels use English for
  untranslated text. Other game locales use English UI. The embedded Roboto font
  does not provide universal script coverage; in particular CJK requires a future
  licensed font extension. Game-language choices come only from signed metadata.
* Native CreateProcessW/posix_spawn implement game launch without a shell. Linux
  desktop entries use `/usr/bin/env --` followed by the quoted final executable:
  GLib 2.72 checks argv[0] before expanding literal-percent field escapes. The
  standard native utility avoids that parser problem without shell execution.
  This behavior is tested using the actual `gio launch` parser on Ubuntu 22.04.
* Orphan staging is reclaimed only with no active journal. Preserved backups and
  backups from interrupted preparation are retained under a quota; exhaustion
  fails safely and asks the operator to archive them. No automatic backup purge
  can discard a user's changes.

The original embedded hero art and icon are code-generated project artwork.
No upstream game artwork, executable or game data is redistributed here.

Relevant contracts: [update protocol](UPDATE_PROTOCOL.md),
[manifest v1](MANIFEST_V1.md), [security](../SECURITY.md).

Desktop behavior was checked against the [freedesktop Exec specification](https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html)
and [GLib 2.72 parser](https://gitlab.gnome.org/GNOME/glib/-/blob/2.72.4/gio/gdesktopappinfo.c).

# Initial data, fast startup and optional retained backups

The maintainer requested mutable first-install defaults, no whole-game scans at
startup/Play, and no retained backups by default. These explicitly supersede the
original specification's full pre-Play integrity check and automatic retention
of modified files. The backup implementation remains present and can be enabled
at build time with `OBK_RETAIN_USER_BACKUPS=ON`.

Manifest v3 gives initial data a separate signed ownership scope. Seeding uses
the existing file transaction; a local marker prevents future reconciliation.
Reset is a separate journaled directory replacement authorized by the installed
signature and an explicit UI confirmation. Complete initial folders are replaced,
including user-created content. Updates and Repair cannot implicitly reset them.

Launcher 0.3.1 adds a Data-only exception: initial and managed files may share
Data directories when their full paths do not collide, including aliases,
file/directory conflicts and language packs. Data defaults retain the existing
once-per-folder seeding rule, with persistent per-file ownership guards. Reset
replaces only the signed initial files in Data, using the same recoverable
staging/original mechanism; it never swaps the whole Data directory. Other
initial folders keep their existing whole-folder ownership and Reset behavior.

Startup/Play still authenticate local metadata, reject pending recovery, validate
the install root and launch profile, safely open the executable and check its
size/native format (and Linux execute bit). They do not hash game files, including
the executable. Corrupted or edited game assets may therefore reach the game;
use Repair to detect and restore them. Multiplayer compatibility is left to the
game. Downloads, updates, repairs and unfinished transaction recovery still
verify authenticated bytes. Manifest signatures, TLS and filesystem confinement
are unchanged.

Startup update discovery runs in its own worker with one request attempt, a
5-second connect timeout and 10-second transfer timeout. Failure is logged
without a blocking error in the GUI. Explicit checks can still display errors.
Play does not depend on successful discovery or on that worker completing.

Temporary original copies remain necessary to recover interrupted replacements:
network availability cannot be assumed after a crash, and GitHub cannot restore
personal settings, saves or edited mods. These copies are cleaned after a
successful commit/rollback and reclaimed after interrupted cleanup. Retained
backups are disabled separately; legacy backups are not retroactively deleted.
Unknown files outside explicitly reset/removed folders remain protected.

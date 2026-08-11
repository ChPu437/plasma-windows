# Windows porting patches

Canonical patches for building KDE components on Windows. Each patch is
kept next to the component it modifies and is applied with `patch -p1`
from the unpacked source root (Craft `patchToApply` for craft-managed
packages; manual `patch -p1` for plain CMake builds).

## kded (6.28.0)

* `0001-skip-posix-signal-handling-on-windows.patch`
  `src/kded.cpp`: guard the `signal(SIGTERM/SIGHUP)` calls with
  `#ifdef Q_OS_UNIX` (MSVC has no `SIGHUP`).
  Applied via Craft recipe `patchToApply["6.28.0"]` (blueprint
  `kde/frameworks/tier3/kded`, local edit: platform restriction removed).

## kactivitymanagerd (6.7.4)

Built as a plain CMake project (no Craft blueprint) from the release
tarball into `D:\Projects\CraftRoot\build\custom\kactivitymanagerd`:

* `0001-sqlite-plugin-portable-sleep.patch`
  `ResourceScoreMaintainer.cpp`: replace `unistd.h`/`sleep(1)` with
  `QThread::sleep(1)`.
* `0002-version-header-case-collision.patch`
  `src/service/Application.cpp`: include the generated version header by
  its unique name. On Windows the source tree `src/service/Version.h`
  shadows the generated `build/version.h` (case-insensitive lookup).
* `0003-version-header-case-collision-cmake.patch`
  `CMakeLists.txt`: rename the generated header to
  `kactivitymanagerd_version.h` to avoid the collision above.
* `0004-sqlite-plugin-optional.patch`
  `src/service/plugins/CMakeLists.txt`: make the sqlite resource
  scoring/linking plugin optional (`BUILD_KAMD_SQLITE_PLUGIN`, default
  OFF). It requires KIO's `kdirnotify.h` which KIO does not install on
  Windows.

## Applying to a fresh tarball

```bat
for %%f in (patches\kactivitymanagerd\*.patch) do patch -p1 -d <src-dir> -i %%f
```

## Craft notes

* Blueprint local edits (platform restrictions) live in
  `CraftRoot\etc\blueprints\locations\craft-blueprints-kde\...`; daily
  blueprint updates are disabled (`EnableDailyUpdates = False` in
  `CraftSettings.ini`) so local edits persist.
* `CraftRoot\etc\BlueprintSettings.ini` carries per-package build args:
  `KF_IGNORE_PLATFORM_CHECK=ON` (KDEPlatformCheck escape hatch),
  `WITH_X11=OFF`, `buildTests = False`.

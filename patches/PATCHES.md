# Windows porting patches

Canonical patches for building KDE components on Windows. Each patch is
kept next to the component it modifies and is applied with `patch -p1`
from the unpacked source root (Craft `patchToApply` for craft-managed
packages; manual `patch -p1` for plain CMake builds).

Design, usage and implementation notes for everything we built on top of
the stock KDE sources: see `docs/windows-port-notes.md`.

## kwindowsystem (6.28.0) - Windows backend

`0001-windows-backend.patch` adds a complete Windows platform backend
(Phase 3 M2):

* framework core: `Platform::Windows` + `isPlatformWindows()`;
  `KWindowInfo` Windows data support (caption/pid/geometry/state via
  Win32/DWM, snapshots in the existing private class, `CHECK_PLATFORM`
  guard); new public `KWindowSystemWindows` (window list, stacking order,
  active window, work area, change signals) + private interface and plugin
  interface extension `createWindowList()`.
* plugin `KF6WindowSystemWindowsPlugin` (`src/platforms/windows/`):
  `KWindowSystemPrivateV5` impl (activateWindow via the classic
  foreground-stealing dance, showingDesktop via minimize/restore, XDG
  token APIs as documented no-ops), `KWindowEffects` (DWM accent policy:
  acrylic blur behind, blur as background-contrast approximation),
  `WindowList` (EnumWindows + SetWinEventHook out-of-context hooks,
  queued signal delivery; hook handler methods must be `Q_INVOKABLE` for
  string-based `QMetaObject::invokeMethod`).
* build wiring: `KWINDOWSYSTEM_WINDOWS` option (auto-ON on WIN32),
  sources/headers/install rules, plugin into
  `kf6/org.kde.kwindowsystem.platforms`.

Applied via Craft recipe `patchToApply["6.28.0"]`.

## qtbase (6.11.1) - SDK 19041 fallback

`0001-modernwindows-style-sdk19041-fallback.patch` (in
`libs/qt6/qtbase/.craft/`, applied for all versions):
`qwindows11style.cpp` uses `DWMWA_WINDOW_CORNER_PREFERENCE` and friends
which only exist in Windows SDK >= 22000; the MSVC branch now falls back
to the literal values (mirroring the existing MinGW fallback) when the
constants are not defined.

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

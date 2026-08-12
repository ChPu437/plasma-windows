# Windows porting patches

Canonical patches for building KDE components on Windows. Each patch is
kept next to the component it modifies and is applied with `patch -p1`
from the unpacked source root (Craft `patchToApply` for craft-managed
packages; manual `patch -p1` for plain CMake builds).

Design, usage and implementation notes for everything we built on top of
the stock KDE sources: see `docs/windows-port-notes.md`.

## plasma-workspace (6.7.4) - Phase 3 M3.1 minimal shell

Applied via Craft recipe `patchToApply["6.7.4"]` (blueprint
`kde/plasma/plasma-workspace`, local edit: platform restriction removed):

* `0001-windows-build-conditions.patch` - top-level `CMakeLists.txt`:
  add `WITH_WAYLAND` option (default OFF on Windows), guard
  `find_package(X11)`/wayland-only subdirectories, skip Linux-only
  components (`appmenu`, `xembed-sni-proxy`, `ksmserver`,
  `logout-greeter`, `klipper`, `devicenotifications`, `kcminit`),
  demote KSysGuard/Canberra to OPTIONAL in `feature_summary`.
* `0002-subdir-wayland-guards.patch` - per-file `Q_OS_WIN`/`WITH_WAYLAND`
  guards across libkworkspace, libtaskmanager, libnotificationmanager,
  libclock, libklookandfeel, components (dbus, keyboardlayout,
  sessionsprivate), shell, krunner, ksplash, startkde, applets
  (notifications, systemtray, kicker, digital-clock), wallpapers:
  * KX11Extras/KStartupInfo/KWaylandExtras/LayerShellQt includes and
    usage sites,
  * Wayland protocol header includes (`qwayland-*-v1.h`) and
    `QWaylandClientExtension` classes,
  * `kdisplaymanager.cpp` replaced by a Windows stub (X11 session
    manager API), `outputorderwatcher.cpp` falls back to the base class,
  * `virtualdesktopinfo.cpp` gets a `WindowsPrivate` fallback and the
    `tasktools.cpp`/`utils.cpp`/`servicesFromPid` `environ` name clash
    with MSVC CRT is renamed,
  * DBus interface XML for KWin VirtualKeyboard and StatusNotifier*
    installed into `CraftRoot\bin\data\dbus-1\interfaces`,
  * MSVC `QByteArrayView` range-for fixes.
* `0003-minimal-shell-subsets.patch` - M3.1 scope reduction: applets
  (8 of 19 kept: calendar, digital-clock, kicker, lock_logout,
  panelspacer, margins-separator, notifications, systemtray), all
  runners deferred, containmentactions reduced to contextmenu/paste/
  applauncher, optional top-level components deferred (marked
  `# deferred: enabled in later M3 stages`).

M3.1 acceptance: `plasmashell.exe` builds and starts, creates the
full-screen desktop window (`Desktop @ QRect(0,0 2560x1440)`) and stays
alive. The shell package (`org.kde.plasma.desktop`) is taken from the
plasma-desktop 6.7.4 tarball (not yet built) and installed at
`%LOCALAPPDATA%\plasma\shells\org.kde.plasma.desktop`; the session bus
address must be `tcp:host=127.0.0.1,port=12443` (see
`tools/start-plasma-session.cmd`).

## plasma-desktop (6.7.4) - Phase 3 M3.2 desktop layout

Applied via Craft recipe `patchToApply["6.7.4"]` (blueprint
`kde/plasma/plasma-desktop`, local edit: platform restriction removed):

* `0001-windows-minimal-scope.patch` - top-level `CMakeLists.txt`:
  `BUILD_KCM_TABLET`/`BUILD_KCM_TOUCHPAD_X11` default OFF, Qt6WaylandClient
  and X11/XCB/Plasma5Support/KSysGuard/KSMServerDBusInterface demoted to
  QUIET/OPTIONAL, `xkbregistry`/`xkb_base` checks skipped on WIN32,
  `ConfigureChecks.cmake` includes `CheckFunctionExists` (CMake 4);
  scope reduction: only layout-templates, containments (desktop/panel/
  folder) and applets (kickoff, trash, pager, showdesktop, minimizeall,
  activitypager, showActivityManager, icontasks) are built; runners,
  kcms, toolboxes, knetattach, emojier, kaccess and friends deferred;
  taskmanager/window-list/kimpanel/keyboardlayout applets deferred
  (KSysGuard/wayland dependencies); folder plugin/tests get `Qt::DBus`,
  `foldermodel.cpp` unistd guard, `pagermodel.cpp` wayland guard +
  `abstracttasksmodel.h` include.

M3.2 acceptance: plasmashell loads the full default layout
(`plasma-org.kde.plasma.desktop-appletsrc` contains desktopcontainment +
org.kde.panel + kickoff/pager/icontasks/showdesktop), desktop window
shows and the process stays alive. Package data must be mirrored to
`%LOCALAPPDATA%\plasma\` (QStandardPaths on Windows uses LOCALAPPDATA;
craft installs under `CraftRoot\bin\data`). Panel window rendering is
follow-up (M3.3).

## libplasma (6.7.4)

Applied via Craft recipe `patchToApply["6.7.4"]` (blueprint
`kde/plasma/libplasma`, local edit: platform restriction removed):

* `0001-wayland-optional.patch` - `WITH_WAYLAND` option default OFF on
  Windows; guard the Wayland-only find_package blocks.
* `0002-subdir-wayland-guards.patch` - Q_OS_WIN guards for KX11Extras
  includes in the blur/theme code, a Windows stub for
  `plasmashellwaylandintegration` and the CMake wayland source lists.

## plasma-activities-stats (6.7.4)

* `0001-export-const-iterator-members.patch` - `resultset.h`: annotate
  the nested `ResultSet::Result` and `ResultSet::const_iterator` member
  functions with `PLASMAACTIVITIESSTATS_EXPORT`. MSVC only exports
  explicitly marked members, so the upstream DLL is missing e.g.
  `const_iterator::operator++`; kicker fails to link without this.
  Applied via Craft recipe `patchToApply["6.7.4"]`.

## kauth (6.28.0)

* `0001-windows-build-fixes.patch` - compile fixes for the Windows
  backend (`QMetaTypeModuleHelper` guarded, missing includes).
* `0002-qmetatype-gui-helper-qt610.patch` - `QMetaTypeModuleHelper`
  was removed in Qt 6.10; use plain `QMetaType` on Qt >= 6.10.
* `0001-dbus-backend-find-qt6dbus.patch` - find Qt6DBus for the DBus
  backend (as `qt_add_dbus_interface` requires).
  Applied via Craft recipe `patchToApply["6.28.0"]`.

## krunner (6.28.0)

* `0001-find-qt6dbus.patch` - link the DBus runner against Qt6DBus.
* `0002-dbusrunner-wayland-guard.patch` - guard the Wayland-only
  startup-notification include with `Q_OS_WIN`.
  Applied via Craft recipe `patchToApply["6.28.0"]`.

## kwindowsystem (6.28.0) - Windows backend`0001-windows-backend.patch` adds a complete Windows platform backend
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

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

## M3.3 - panel window + applets

* `plasma-workspace 0002` (shell/main.cpp): on Windows a debug
  `QtMessageHandler` writes all Qt logging to `%TEMP%\plasmashell-debug.log`
  (Qt's default handler only writes to OutputDebugString on Windows GUI
  programs, so QML errors were invisible otherwise).
* `plasma-desktop 0001` (desktoppackage/contents/views/Panel.qml): guard
  `KX11Extras.compositingActive` with `KWindowSystem.isPlatformX11`
  (KX11Extras QML singleton is not registered on Windows).
* Wallpaper configuration: `[Containments][n][Wallpaper][org.kde.image][General]`
  `Image=` must point at an existing file; craft installs no wallpaper
  images, so a generated gradient PNG is used
  (`CraftRoot\share\wallpapers\plasma-windows-default.png`).

## plasma5support (6.7.4)

`0001-windows-build-fixes.patch` - `CMakeLists.txt`: add Qt6 DBus
component (dataengines use `qt_add_dbus_interface`), `WITH_X11` default
OFF; `src/CMakeLists.txt`: skip dataengines (X11-era weather/geolocation
engines with MSVC narrowing errors). Provides the
`org.kde.plasma.plasma5support` QML module required by kickoff.

M3.3 acceptance: plasmashell shows desktop + wallpaper + panel window
with kickoff/pager/icontasks/showdesktop applets loaded (no applet load
errors in the debug log).

## kcoreaddons (6.28.0) - startup hang fix

`0001-skip-sam-user-picture.patch` - `src/lib/util/kuser_win.cpp`:
`KUser::faceIconPath()` returns an empty string on Windows. The upstream
implementation calls `SHGetUserPicturePath`, which walks the SAM account
database over RPC (`SamConnect`) and hangs plasmashell's main thread at
startup (CPU 100%, window not responding; recovered with WinDbg thread
stack: `KAboutData::setProgramLogo -> SHGetUserPicturePath -> SamConnect`).
Applied via Craft recipe `patchToApply["6.28.0"]` (blueprint
`kde/frameworks/tier1/kcoreaddons`).

## plasma-workspace M3.3 follow-up

* `shell/panelshadows.cpp`: `hasShadows()` returns false on Windows
  (KWindowShadow is not implemented by the Windows backend; every failed
  `create()` logged a warning and panels re-triggered it constantly).
  Shadows are deferred to a visual-polish milestone.

M3.3b acceptance: plasmashell starts, stays responsive (CPU idle when
not interacting), desktop + panel windows show, no KWindowShadow spam.

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
  backend (`QMetaTypeModuleHelper` guarded, missing includes) plus
  `find_package(Qt6DBus)` for the DBus backend (as
  `qt_add_dbus_interface` requires).
* `0002-qmetatype-gui-helper-qt610.patch` - `QMetaTypeModuleHelper`
  was removed in Qt 6.10; use plain `QMetaType` on Qt >= 6.10.
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

## plasma-workspace (6.7.4) - M3.6+ shell work

`0004-windows-m36-shell.patch` (applied after 0001-0003) - all shell /
taskbar work of M3.6-M3.7:

* `shell/shellcorona.cpp`: `setDashboardShown` is a no-op on Windows
  ("show desktop" is a KWin effect; the KWindowSystem Windows backend
  minimized every top-level window including the desktop itself).
* `shell/desktopview.cpp`: keep the desktop at the bottom of the window
  stack (`WS_EX_NOACTIVATE` + `HWND_BOTTOM`, re-asserted on
  Expose/ActivationChange/WindowActivate).
* `shell/panelview.cpp/.h`: `updateWorkArea()` ->
  `SystemParametersInfo(SPI_SETWORKAREA)` so maximized windows stop at
  the panel edge (show/hide/move/resize update; hide restores).
* `shell/main.cpp`: re-assert `QIcon::setThemeSearchPaths` with
  `<appdir>/data/icons` after the Kirigami controls plugin overwrote
  them during QML engine setup (Windows).
* `libtaskmanager/windowswindowtasksmodel.{h,cpp}` (new): the Windows
  window tasks model (Win32 icons, activate/close/minimize/maximize).
  2026-08-14 porting-review pass (A1/A2/C3/C4/C5): the model now consumes
  `KWindowSystemWindows` events (windowAdded/windowRemoved/
  activeWindowChanged) instead of its own EnumWindows + taskbar-candidate
  copy on a 500ms polling timer - the duplicated candidate filter is
  gone, the initial snapshot goes through the same
  `KWindowSystemWindows::windows()` path, and a 2s refresh timer keeps
  title/icon/state roles current (WinEventHook does not report those).
  `iconForWindow` no longer destroys WM_GETICON/GCLP_HICON handles
  (shared, owned by window/class - only the SHGetFileInfo fallback
  handle is owned).
* `libtaskmanager/windowtasksmodel.cpp`: create `WindowsWindowTasksModel`
  on Windows (Wayland/X11 branches excluded).
* `libtaskmanager/concatenatetasksproxymodel.{h,cpp}`: on Windows a
  hand-rolled aggregation (Qt 6.11 `QConcatenateTablesProxyModel`
  fails to map Windows models - data() reads empty).
* `shell/CMakeLists.txt`, `libtaskmanager/CMakeLists.txt`: link
  KF6::IconThemes / dwmapi; add the Windows model sources.

2026-08-13 hygiene pass (review-driven): dropped the encoding-damaged
first hunk of `shell/main.cpp` (a UTF-8 BOM had sneaked into the source
line `/*`, producing a mojibake-only change; the BOM was removed from
the work tree too), and re-synced the `windowswindowtasksmodel.cpp`
hunk line count (457 -> 456 after the PLASMA-DEBUG removal). Reverse
dry-run against the work tree now passes for the whole patch.

`0005-windows-panel-activatable.patch` (applied after 0004) - keep the
panel activatable on Windows so third-party tray popup menus dismiss
correctly:

* `shell/panelview.cpp`: do not set `Qt::WindowDoesNotAcceptFocus` on
  Windows (constructor and both `refreshStatus` branches). The
  constructor flag maps to `WS_EX_NOACTIVATE`; with it set, clicking the
  panel never produces a focus change, so `TrackPopupMenu` menus (and
  Chromium self-drawn menus) opened from tray icons stay open until an
  item is clicked. Keeping the panel activatable mirrors how the real
  Explorer taskbar behaves.

Note: the desktop window keeps its `WS_EX_NOACTIVATE` (0004) on purpose.
Making it activatable (or activating the panel from a desktop click)
causes every window to flash: activation raises the full-screen desktop
and the HWND_BOTTOM re-assertion pulls it back (and any deferred
SetForegroundWindow triggers a DWM animation). Trade-off accepted for
now: tray popup menus are dismissed by clicking other windows or the
panel, not the desktop.

## kwindowsystem (6.28.0) - 0001 updated

`0001-windows-backend.patch` regenerated from the clean 6.28.0 source;
adds `windowslist.cpp::setShowingDesktop` skipping windows of the
current process (so "show desktop" never hides the plasma desktop/panel).

2026-08-14 (A2 from the porting review): `EVENT_OBJECT_HIDE` now routes to
`handleWindowRemoved` instead of `handleWindowAdded` - routing HIDE to
the add-path dropped the window in `isTaskbarCandidate`
(`IsWindowVisible == false`) and never emitted `windowRemoved`, leaving
stale entries in the taskbar. SHOW re-adds; behaviour matches Explorer.
Also widened the object hook range from `EVENT_OBJECT_CREATE..DESTROY`
to `EVENT_OBJECT_CREATE..HIDE`: SHOW(0x8002)/HIDE(0x8003) are outside
the old range, so windows created while invisible were never re-added on
SHOW (taskbar did not grow when opening windows).
Note: `setShowingDesktop` remains unused by the shell (KWin effect
semantics differ); documented so nobody misuses it.

Also carries (2026-08-13, re-verified by craft rebuild from clean tar):

* `slideWindow` popup animation: KWin slide emulation for popups/dialogs
  only (180 ms OutCubic position slide from the panel edge + opacity
  fade-in; panels are excluded - a fade would break the floating panel's
  height animation).
* **AccentState enum fix**: `AccentEnableBlurbehind` is 3 and
  `AccentEnableAcrylicBlurBehind` is 4 (the implementation used 4/5,
  which map to ACRYLIC/HOSTBACKDROP; blur-behind popups fell back to a
  plain translucent surface). Verified with `probe/blurprobe.cpp` and a
  `GetWindowCompositionAttribute` readback.

## kiconthemes (6.28.0)

`0001-windows-icon-theme-paths.patch` (new):

* `src/kicontheme.cpp`: add `QCoreApplication::applicationDirPath()/data/icons`
  to the icon theme dir list (ctor + `KIconTheme::list()`) - Craft
  bundles data next to the executable, QStandardPaths does not know it.
* `KIconTheme::current()`: skip the KIconEngine virtual theme names
  (`KIconEngine` / `breeze-internal`) on Windows and fall through to
  the configured (kdeglobals) theme.

## libplasma (6.7.4) - 0003 regenerated

`0003-windows-popup-positioning.patch` regenerated; adds
`PopupPlasmaWindow::updatePosition()` `Q_OS_WIN` branch (apply the
TransientPlacementHelper rect like X11), visualParent recovery from the
QML parent chain, popup-only off-screen parking at componentComplete
(anti-flicker; must not affect the desktop window), delayed
re-positioning in `PlasmaWindow::showEvent`, and removal of the
`socketWindowPositionChanged` call in `updateVisibility` (it squashed
the widget explorer).

2026-08-13 update (craft-rebuild verified):

* all `PLASMA-DEBUG` ad-hoc logging removed (the "visualParent NULL"
  and "windowStateChanged" qWarning calls were never meant to stay).
* **`Dialog::showEvent` re-applies `updateTheme()` on Windows**: the
  popup's HWND only exists once the window is shown, so the
  blur/background-contrast request made earlier (when QML set
  backgroundHints) was dropped by the KWindowEffects backend and never
  retried - popups stayed semi-transparent instead of blurred.

## libplasma (6.7.4) - 0004 window thumbnail

`0004-windows-window-thumbnail.patch` - `WindowThumbnail` gets a
Windows branch: live-ish window preview via `PrintWindow`
(PW_RENDERFULLCONTENT -> RGB32 -> texture, refreshed every 500 ms while
visible); icon fallback for minimized/invalid windows.

2026-08-13 fix: the Windows icon-fallback path used
`QStringLiteral(plasma)` (missing quotes - upstream writes
`QStringLiteral("plasma")`); it only compiled because the object file
was never rebuilt from the patched source. Fixed and verified by a
craft rebuild from the clean tarball.

`0005-windows-thumbnail-cache.patch` (2026-08-14, porting review C2) -
move `PrintWindow` out of the QSG render thread: `updatePaintNode` runs
on the render thread and `PrintWindow` synchronously waits for the
target window to process WM_PRINT - a hung target stalled the whole
scene graph. The 500ms `refreshThumbnail` timer now captures into a
cached QImage on the GUI thread; `windowsThumbnailToTexture` only
consumes the cache (icon fallback when empty).

## kio (6.28.0) - Dolphin prerequisite

`0001-windows-export-all-symbols.patch` - `CMakeLists.txt` sets
`CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON` on Windows. MSVC does not export
move special members of dllexport classes (`KFileItem(KFileItem&&)`,
`operator=(KFileItem&&)`), which makes Dolphin fail to link
("undefined symbol: QList<KFileItem>::QList(QList&&)"). Member-level
export is impossible (C2487), hence the whole-library export. See
`docs/dolphin-windows.md` for the Dolphin build.

## kconfig (6.28.0)

`0001-windows-trust-desktop-files.patch` (new, 2026-08-13): let
`KDesktopFile` accept the generated `.desktop` bridge files used to
expose native Windows apps (notepad/calc/...) in kickoff. Applied via
Craft recipe `patchToApply["6.28.0"]` (blueprint
`kde/frameworks/tier1/kconfig`).

## Runtime data (not source patches)

* **`CraftRoot\bin\data\wallpapers\Next` wallpaper package** (2026-08-13):
  `defaultWallpaperPackage()` falls back to `wallpapers/Next`, which
  Craft never installs; without it the image wallpaper's `providerType`
  stays `Unknown`, `loadWallpaper()` never completes and **the panel is
  never created** (UiReady never fires). The package contains
  `metadata.json` (KPackageStructure `Wallpaper/Images`) and
  `contents/images/1920x1080.png` (size-encoded filename so
  `findPreferredImageInPackage` picks it). The older
  `share\wallpapers\plasma-windows-default.png` + `Image=` config entry
  is superseded by this (the config entry is lost on config restore,
  the package is found automatically).

## Build notes (craft rebuild, 2026-08-13)

* Patches are applied by Craft with GNU `patch.exe` (MSYS build) from
  the **blueprint copies** under
  `CraftRoot\etc\blueprints\locations\craft-blueprints-kde\...` - keep
  them in sync with `patches/` (there is no shared link).
* **Never rebuild `work\build` trees with a long cwd**: paths like
  `...\CMakeFiles\plasmashell-6.0-...-panels.dir\..._autogen\mocs_compilation.cpp.obj`
  exceed MAX_PATH (260) and cl.exe fails with a misleading
  `C1083: 无法打开源文件 ... Invalid argument`. Always drive ninja from
  the Craft short path instead:
  `ninja -C D:\_\9ad84e1c\build install` (short paths are per-package
  junctions listed in `D:\_`).
* A craft build from a clean tarball is the only reliable patch
  validation (`craft --ignoreInstalled --no-cache <pkg>` after deleting
  `build\kde\plasma\<pkg>\work` and the `image-*` dirs); the binary
  cache and installdb otherwise report "up to date" and never reapply
  the patches.

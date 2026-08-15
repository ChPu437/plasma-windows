# Windows Port Notes - our changes on top of stock KDE sources

This document covers everything we implemented on top of unmodified KDE
source code: what each piece does, how to use it, how it is implemented,
and its known limitations. Build/apply instructions for the patches live
in `patches/PATCHES.md`; this document focuses on design and usage.

## 1. KWindowSystem Windows backend (M2)

Stock KF6 KWindowSystem has no Windows backend: with X11/Wayland disabled
on WIN32 the framework builds only stubs. In addition, the classic
window-list API (`KWindowSystem::windows()` etc.) was removed in KF6 and
`KWindowInfo` is hardcoded X11 (not even compiled on Windows).

Our patch (`patches/kwindowsystem/0001-windows-backend.patch`) adds:

### 1.1 Platform detection
`KWindowSystem::Platform::Windows` + `KWindowSystem::isPlatformWindows()`.
The platform plugin json declares `"platforms": ["windows"]` and is loaded
automatically by the existing plugin wrapper when
`QGuiApplication::platformName()` is `windows`.

### 1.2 KWindowSystemWindows - the window list data source (new public API)

Why it exists: KF6 removed the window list from KWindowSystem; on X11 the
data comes from KX11Extras, on Wayland from the plasma window management
protocol. This class is the Windows equivalent and mirrors KX11Extras'
shape (static API + signals).

Usage:

```cpp
#include <KWindowSystemWindows>

// enumerate taskbar-visible top-level windows (top-most first, Z order)
const QList<WId> wins = KWindowSystemWindows::windows();

// active (foreground) window, or 0
const WId active = KWindowSystemWindows::activeWindow();

// primary work area
const QRect wa = KWindowSystemWindows::workArea();

// change notifications (driven by SetWinEventHook)
QObject::connect(KWindowSystemWindows::self(), &KWindowSystemWindows::windowAdded,
                 [](WId id) { ... });
QObject::connect(KWindowSystemWindows::self(), &KWindowSystemWindows::windowRemoved,
                 [](WId id) { ... });
QObject::connect(KWindowSystemWindows::self(), &KWindowSystemWindows::activeWindowChanged,
                 [](WId id) { ... });
QObject::connect(KWindowSystemWindows::self(), &KWindowSystemWindows::stackingOrderChanged,
                 []() { ... });
```

Implementation: `WindowList` in the plugin enumerates with `EnumWindows`,
filters through the taskbar-candidate rule, and registers
out-of-context WinEvent hooks (EVENT_SYSTEM_FOREGROUND,
EVENT_OBJECT_CREATE/DESTROY/SHOW/HIDE). Hook callbacks bounce into the Qt
event loop via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` and
then emit the signals. NOTE: the handler methods must stay `Q_INVOKABLE`
(string-based invokeMethod cannot call plain private methods - a real bug
we hit and fixed).

Taskbar-candidate filter (mirrors Windows taskbar semantics):
visible, top-level (root ancestor), no owner window, no WS_EX_TOOLWINDOW,
not DWM-cloaked.

`stackingOrder()` currently equals `windows()` (Z order, top first);
consumers expecting X11-style bottom-first order must adapt (M4's
WindowsTasksModel will define its own grouping).

### 1.3 KWindowSystem core (private API)

`activateWindow(QWindow*)` - brings the window to the foreground using the
classic Win32 "foreground stealing" dance: AttachThreadInput on the
foreground and target threads, restore if minimized, BringWindowToTop +
SetForegroundWindow + SetActiveWindow + SetFocus, detach afterwards.

`showingDesktop()` / `setShowingDesktop()` - implemented by minimizing
every taskbar-candidate window (remembered in a QSet) and restoring them.

XDG activation token methods (requestToken, setCurrentToken,
xdgActivationToken, exportToplevel, setXdgToplevelTag/Description,
setMainWindow, lastInputSerial, export/unexportWindow) are documented
no-ops - those are Wayland concepts with no Windows equivalent.

### 1.4 KWindowInfo on Windows

`KWindowInfo` snapshots Win32 state in its constructor and maps it to NET
semantics:

| NET concept            | Win32 source                                  |
|------------------------|-----------------------------------------------|
| name / visibleName     | GetWindowTextW                                |
| iconName               | same as name (no separate icon caption)       |
| geometry / frameGeometry | GetWindowRect / DWMWA_EXTENDED_FRAME_BOUNDS |
| pid                    | GetWindowThreadProcessId                      |
| state: MaxVert\|MaxHoriz | IsZoomed                                   |
| state: Focused         | GetForegroundWindow == hwnd                   |
| minimized              | mappingState == Iconic (IsIconic)             |
| windowType             | always Normal                                 |
| desktop / isOnCurrentDesktop | single virtual desktop (0 / true)        |
| transientFor           | GetWindow(GW_OWNER)                           |
| activities, groupLeader, window classes, appmenu, struts | empty/0/unsupported |

Methods are guarded by CHECK_PLATFORM (X11 or Windows); on other
platforms they warn and return defaults as before. The X11 code paths are
kept intact behind `#if KWINDOWSYSTEM_HAVE_X11` so the file compiles on
Windows builds.

### 1.5 KWindowEffects on Windows

- BlurBehind: DWM accent policy via SetWindowCompositionAttribute
  (undocumented API, ACCENT_ENABLE_ACRYLICBLURBEHIND).
- BackgroundContrast: approximated with blur-behind (Windows has no
  contrast API) - `isEffectAvailable()` reports it as available but the
  approximation is documented in code.
- slideWindow: unsupported.

### 1.6 Known limitations (Windows backend)

- Single virtual desktop; desktop() is always 0.
- No window-type heuristics (everything is Normal).
- Hook-based change notifications require an interactive window station;
  automation/CI sessions may fail SetWinEventHook.
- stackingOrder is top-first (X11 convention is bottom-first).

## 2. qtbase SDK 19041 fallback (Qt 6.11.1)

`patches/qtbase/0001-modernwindows-style-sdk19041-fallback.patch`
(in `libs/qt6/qtbase/.craft/`, applied to all versions): Qt 6.11's
qwindows11style.cpp uses DWMWA_WINDOW_CORNER_PREFERENCE /
DWMWCP_ROUND(SMALL) / DWMWCP_DEFAULT which only exist in Windows SDK >=
22000. Our toolchain uses SDK 10.0.19041, so the MSVC branch now falls
back to the literal values when the constants are undefined (mirroring
Qt's existing MinGW fallback). Rounded corners are a Windows 11 feature
anyway; on Win10 the values are accepted and ignored by DWM.

## 3. Platform-restricted recipes unlocked for Windows

- **kded** (`kde/frameworks/tier3/kded`): blueprint platform restriction
  removed (Linux|FreeBSD -> All); `KF_IGNORE_PLATFORM_CHECK=ON` in
  BlueprintSettings.ini (KDE's documented escape hatch for enabling a
  platform); POSIX signal handling guarded with Q_OS_UNIX
  (`patches/kded/0001...`). Result: kded6 runs and registers
  org.kde.kded6 on the session bus (M1).
- **kglobalaccel** (`kde/frameworks/tier3/kglobalaccel`): same recipe
  unlock + `-DWITH_X11=OFF` (the X11 path references a Qt6 header that
  does not exist); built from source (cache had none). Note: the daemon
  kglobalacceld is a separate repo (kde/plasma/kglobalacceld) and is a
  deferred M3 pre-flight item.

## 4. kactivitymanagerd port (6.7.4, custom build)

Plain CMake build (no blueprint); four patches in
`patches/kactivitymanagerd/`:

- 0001: replace `unistd.h`/`sleep(1)` with `QThread::sleep(1)`.
- 0002+0003: generated version header renamed to
  `kactivitymanagerd_version.h`; on Windows the source tree's
  `Version.h` shadows `build/version.h` (case-insensitive lookup), so
  `#include <version.h>` resolved to the wrong file.
- 0004: sqlite resource-scoring plugin made optional
  (BUILD_KAMD_SQLITE_PLUGIN, default OFF): it needs KIO's kdirnotify.h
  which KIO does not install on Windows. Activity tracking, activity
  templates and global shortcuts plugins remain enabled.

## 5. Craft configuration changes (all in CraftRoot, documented in PATCHES.md)

- Blueprint version bump: kde/plasma -> 6.7.4 (tarballs + defaulttarget).
- BlueprintSettings.ini: KF_IGNORE_PLATFORM_CHECK / WITH_X11=OFF /
  buildTests=False for specific packages.
- EnableDailyUpdates=False (protects local blueprint edits).
- [ShortPath] DriveLetter=W:/ (required for qtbase builds).
- KIO rebuilt from source: the binary cache package lacked dev headers
  (kdirnotify.h etc.); source build installs them.
- qtbase rebuilt from source: the cache Qt6Gui had a broken zlib linkage
  (imported inflateReset2 from libpng16.dll, which does not export it),
  crashing every Qt6Gui app at load.

## 6. Runtime/deployment notes for Qt apps on Windows

- Qt GUI apps must load the MSVC runtime set (msvcp140_1/2,
  vcruntime140_1, ...) either app-local or from CraftRoot\bin on PATH;
  System32 versions are usually fine but app-local is the reliable
  deployment (same as the Phase 1 deploy folder).
- Qt loads plugins from libraryPaths(); placing plugins under
  `<appdir>/kf6/org.kde.kwindowsystem.platforms/` works without touching
  the Qt install (QT_PLUGIN_PATH did not reliably surface in
  libraryPaths in our tests).
- Probe builds for KF6-on-Windows can mix E:\Qt headers/libs with
  CraftRoot KF6 packages when the Qt versions are identical (6.11.1).

## 7. Icon theme loading on Windows (M3.6)

The icon theme path has two independent search layers, and neither knows
about the Craft bundle layout (`<appdir>\data\...`) on Windows:

- Qt layer (`QIcon::fromTheme` -> QIconLoader): search paths come from
  `QStandardPaths::GenericDataLocation` (%APPDATA%/%ProgramData%).
- KDE layer (`KIconLoader` -> `KIconTheme`): `kicontheme.cpp` collects
  the same `QStandardPaths` locations plus the `:/icons` Qt resource.

Both miss `CraftRoot\bin\data\icons`. In addition:

- The **Kirigami controls plugin** (`kirigamicontrolsplugin.cpp`)
  overwrites `QIcon::setThemeSearchPaths` when the Qt theme name is
  empty (Windows) and sets `themeName = "breeze-internal"` (a virtual
  theme that resolves through the `breeze` fallback theme). Any fix
  applied in `main()` before the QML engine is created gets clobbered;
  a `QTimer` re-assertion (~100 ms) after QML engine setup works for
  later-rendered windows (settings dialogs) but not for the panel icons
  rendered during startup (their QML pixmap cache holds the empty
  result; `QPixmapCache::clear()` does not cover QQuickPixmapCache).
- `KIconTheme::current()` follows `QIcon::themeName()`; the KIconEngine
  plugin's virtual names (`KIconEngine` / `breeze-internal`) have no
  theme directory, so on Windows we skip them and fall through to the
  kdeglobals "breeze" setting.
- The `KIconEnginePlugin.dll` is installed by craft under
  `plugins\kiconthemes6\iconengines\` (per-library plugin dir) but Qt
  only scans `plugins\iconengines\`.

Fixes so far (patches/kiconthemes, patches/plasma-workspace 0001):

- `kicontheme.cpp`: add `applicationDirPath()/data/icons` to the icon
  dir list (ctor + `KIconTheme::list()`); skip the KIconEngine virtual
  theme names in `KIconTheme::current()` on Windows.
- plasmashell `main.cpp`: re-assert `QIcon::setThemeSearchPaths` with
  `<appdir>/data/icons` after QML engine setup.
- `KIconEnginePlugin.dll` copied into `plugins\iconengines\`.

Status: **QIcon layer works** (settings icons render). The panel icons
were fixed too: the KIconLoader path resolved the theme and found the
icon file but `QImageReader` failed on it - **breeze-icons ships
"redirect" files** (a `.svg` whose entire content is the name of
another icon, e.g. `start-here-kde.svg` contains `folder-activities.svg`).
The qrc build turns these into proper aliases but the filesystem
install keeps them as-is. `tools/fix-icon-redirects.py` resolves every
redirect to the target's real content (run it after installing
breeze-icons / as part of deployment). Remaining cosmetic issue:
panel icons rendered during startup may stay cached empty in the QML
pixmap cache if the 100 ms search-path re-assertion runs after the
panel rendered.

## 8. Window stacking and panel work area (M3.7)

KDE semantics implemented with native Win32 z-order:

- **Desktop below everything**: `DesktopView::showEvent` sets
  `WS_EX_NOACTIVATE` (clicks must not raise it) and re-asserts
  `HWND_BOTTOM`; `DesktopView::event()` repeats the re-assertion on
  Expose/ActivationChange/WindowActivate (owner-chain raises from
  dialogs, edit mode, snap layouts). Snap-layout overlay interference
  from a raised desktop was also reported.
- **Panel on top**: PanelView already carries `Qt::WindowStaysOnTopHint`
  (Windows branch).
- **Panel must NOT be Floating (drag performance)**: the default panel
  style is Floating + Adaptive opacity. On X11+NVIDIA Plasma detects
  this and warns ("poor window drag and resize performance", upstream
  `panelview.cpp`, BUG 475468); on Windows `isUnsupportedEnvironment()`
  is always false (X11-only check), so the bad combination is silently
  active. The floatingness animation (panel snapping to the screen edge
  when a window maximizes/touches it) runs per-frame
  `positionAndResizePanel()` and stalls window dragging until the
  animation finishes (~40% plasmashell CPU during the animation).
  Fix: set `floating=false` (+ optionally `panelOpacity=1`) in
  `[Containments][2]` of `%LOCALAPPDATA%\plasma-org.kde.plasma.desktop-appletsrc`.
- **Panel work area**: `PanelView::updateWorkArea()` calls
  `SystemParametersInfo(SPI_SETWORKAREA)` so maximized windows stop at
  the panel edge like the Windows taskbar (show/hide/move/resize
  update; hide restores the full screen). The dev machine fights the
  live Explorer taskbar - verify in the VM.
- **"Show desktop"**: `ShellCorona::setDashboardShown` is a no-op on
  Windows, and `KWindowSystem`'s Windows backend
  (`windowslist.cpp::setShowingDesktop`) skips windows of the current
  process, so other windows minimize/restore while the plasma
  desktop/panel stay visible.

## 9. Taskbar window model (M3.6)

libtaskmanager had no Windows source model: `WindowTasksModel::initSourceTasksModel`
only created Wayland/X11 models, leaving the source null (empty taskbar).

New `WindowsWindowTasksModel` (libtaskmanager):

- window list via `EnumWindows` with a taskbar-candidate filter
  (visible, top-level, no owner, not WS_EX_TOOLWINDOW, not cloaked) -
  `KWindowSystem::stackingOrder` does not exist outside X11.
- windows of the current process (the shell itself) are filtered out so
  the desktop/panel never appear in the taskbar.
- roles: title, icon (WM_GETICON -> class icon -> exe icon via
  SHGetFileInfo -> `QImage::fromHICON`), active/min/max/fullscreen
  (GetForegroundWindow/IsIconic/IsZoomed), geometry, screen
  (Qt-logical via `QGuiApplication::screens()` matching - physical
  rects break `filterByScreen` under DPI scaling), PID, launcher URL
  (exe path).
- actions: activate (restore + SetForegroundWindow), close (WM_CLOSE),
  toggle minimized/maximized, launch new instance.
- refresh by QTimer (500 ms) with **incremental** rowsInserted /
  rowsRemoved diffs - a full `beginResetModel()` breaks
  `QConcatenateTablesProxyModel`'s row mapping (data() forwards read
  empty), which is also why Qt 6.11's `QConcatenateTablesProxyModel`
  was replaced on Windows by a hand-rolled `ConcatenateTasksProxyModel`
  (QAbstractListModel + per-source lambdas + offset math).

## 10. Popup anchoring (M3.6b)

`PlasmaCore.AppletPopup` maps to `PlasmaQuick::AppletPopup` ->
`PopupPlasmaWindow`. `updatePosition()` computes the popup rect via
`TransientPlacementHelper` (anchored to `visualParent`, expanded to the
panel's window mask) but only applied it on X11/Wayland - on Windows
`setPosition` was never called and the popup stayed at the OS default
(screen center). Fix: `Q_OS_WIN` branch that applies the rect like X11.

Other pieces:

- `visualParent` QML bindings are never evaluated on Windows (lazy
  binding + C++ reading the member directly); set it explicitly:
  `CompactApplet.qml` assigns `dialog.visualParent = compactRepresentation`
  in `onCompactRepresentationChanged`, and `PopupPlasmaWindow` has a
  fallback that walks the QML parent chain for the
  `compactRepresentation` property.
- Anti-flicker: park popup windows off-screen
  (`setPosition(QPoint(-32000, -32000))` at `componentComplete`) -
  **popups only** (`qobject_cast<PopupPlasmaWindow*>`), otherwise the
  desktop window (also a Dialog) ends up parked off-screen and
  minimized after edit mode.
- Reposition once after layout settles (`QTimer::singleShot(0, ...)` in
  `PlasmaWindow::showEvent`) because the popup size is not final at
  first show (clock popup used to extend past the screen bottom).
- `updateVisibility()` must NOT call `slotWindowPositionChanged()` -
  it resizes mainItem to the window size, squashing the widget
  explorer when the window is still at its initial size on first show.

## 11. Edit mode and "show desktop" regression (M3.7)

"Enter Edit Mode" minimized the desktop itself: `ShellCorona::setDashboardShown`
-> `KWindowSystem::setShowingDesktop(true)` -> the Windows backend
minimized every top-level window, including the desktop view. Fixed by
the no-op + own-process skip described in section 8.

Widget explorer got squashed into 160x160 because
`updateVisibility()` called `slotWindowPositionChanged()` which resizes
mainItem to the current window size (see section 10).

## 12. Popup resize stutter (2026-08-15, unfixed - low priority)

Resizing the windowsmenu popup (drag its edge - WindowResizeHandler uses
startSystemResize, a native WM_SYSCOMMAND SC_SIZE loop) feels laggy. Not
caused by the software renderer (d3d11 shows the same), and unlike the
panel stall it is not an SPI_SETWORKAREA broadcast storm. Suspects:
per-frame QML relayout of the popup (GridView + delegates) and/or the
LOCATIONCHANGE win-event stream hitting plasmashell's own event hooks
during the system resize loop. Deferred - resizing the launcher is a
low-frequency operation.

## 13. Session environment: no XDG overrides (2026-08-15)

`pc_setup_env` used to set `XDG_CONFIG_HOME`/`XDG_DATA_HOME` to
`%LOCALAPPDATA%` for the whole session. That re-routes every
XDG-aware application (opencode, ...) into `%LOCALAPPDATA%`, so data
created under explorer is invisible under plasma and vice versa -
"lost sessions" after switching shells. Qt's QStandardPaths already
resolves GenericDataLocation/GenericConfigLocation to `%LOCALAPPDATA%`
on Windows (verified with qtpaths6, with and without XDG set), so the
overrides were redundant. Removed; KDE data/config stay in
`%LOCALAPPDATA%` via the Qt default.

## 14. Session autostart: shellswitch + bus readiness (2026-08-15)

After a reboot the shell switcher and its watchdogs (kded6/trayhost
restart) were gone and services died at logon:

- shellswitch is now started from two places (single-instance mutex
  `PlasmaWindowsShellSwitcher` in shellswitch.c): the HKCU Run key
  (explorer-shell case, processed by explorer) and session-shell.cmd
  (plasma-shell case). It lives in the package bin/ next to kded6.exe
  and trayhost.exe, which it restarts from its own directory.
- `pc_start_bus` now polls until port 12443 actually listens instead of
  a fixed 2s sleep: at logon the fixed sleep raced the daemon's TCP
  listener init, services connected nowhere and each process spawned a
  private `dbus-daemon --session` (a second bus appeared). Polling
  removed the race.

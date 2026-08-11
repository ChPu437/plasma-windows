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

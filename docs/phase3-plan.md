# Phase 3 Plan - plasmashell on Windows (FINAL)

Approved 2026-08-11. Locked decisions:

1. Plasma source: **6.7.4** (git tags, bump blueprint version.ini).
2. KWindowSystem Windows backend: written to **upstream standard**
   (complete API surface, documented, testable) - no upstream contribution planned.
3. First runnable target: **M3** (shell window + desktop containment/wallpaper).
4. WindowDecor (WS6): **later roadmap, optional**.
5. Dolphin (Phase 4): **later roadmap**.

Environment: Craft `D:\Projects\CraftRoot` (windows-cl-msvc2022-x86_64, binary
cache `files.kde.org/craft/Qt6/26.05/.../msvc2022/x86_64`), Qt 6.11.1, KF6 6.28.0.
DWM is the compositor; KWin/Wayland/X11/systemd/ksmserver are out of scope.

## Architecture summary

See `docs/architecture.md`. Key facts:

* plasmashell is a QML app (C++ engine: ShellCorona + libs). UI survives the
  port; the work is the C++ dependency chain + data sources on Windows.
* plasmashell 6.7 links: KF6 (all installed), Plasma::Plasma/PlasmaQuick/
  Activities (to build), PW::KWorkspace (self), Qt::Quick/DBus (installed),
  Wayland/LayerShellQt (must be patched out).
* KWindowSystem 6.28 has no Windows backend - the core compatibility layer
  to write (WS2).
* KDE Windows binary cache has no Plasma binaries; blueprint repo lacks
  plasma-desktop / kactivitymanagerd / Plasma 6.7.x -> custom recipes or
  plain CMake builds.
* Window decorations: DWM native chrome for third-party windows (dark title
  bars are per-app opt-in; no rounded corners on Win10 LTSC). Our own shell
  windows are frameless, self-drawn; panel blur via DWM accent API.
  Breeze-style decorations for ALL windows = optional WS6 (hook-based
  decoration proxy), after Phase 3.

## Execution order

### M1 - Platform prerequisites (DBus ecosystem)
1. `craft dbus` (libs/dbus recipe) -> dbus-daemon for Windows.
2. Session bus startup script (login startup, part of shell launch).
3. `craft kded6` (KDED framework) - verify it runs on the session bus.
4. kactivitymanagerd - custom recipe or plain CMake (blueprint missing);
   needs plasma-activities (blueprint exists).
5. Optional: `craft kdecoration` (kept for future WindowDecor reuse).
6. Gate: probe talks to kded6/kactivitymanagerd over QDBus.

**Status: DONE (2026-08-11).** Verified beyond the gate: kded6 method call
round-trip (`loadedModules`), DBus service activation (kactivitymanagerd
killed and auto-respawned by the bus from `.service` file), fixed-address
session bus, Qt QDBus client connectivity. Deliverables: `tools/dbus/`,
`tools/start-plasma-session.cmd`, `probe/dbus-probe/`, `patches/`.

**Deferred to M3 pre-flight (do not block M2 - orthogonal):**
- kded module loading (loadedModules is empty; no modules installed yet -
  re-verify once KIO/plasma kded modules exist).
- `kglobalacceld` (kde/plasma/kglobalacceld repo, no Craft blueprint -
  build like kactivitymanagerd; kglobalaccel framework ships only the lib).
- KIO smoke test (file:// worker process machinery on Windows).

### M2 - KWindowSystem Windows backend (upstream standard)
1. `craft --no-cache kwindowsystem` (source build 6.28).
2. New windows platform backend (Win32/DWM):
   - KWindowSystemPrivate: active window, window list, activate/raise,
     showOnDesktop, desktop geometry/struts.
   - KWindowInfo: caption/pid/geometry/state (active/max/min/geometry are
     mandatory - WindowDecor will consume them later).
   - KWindowEffects: blur/contrast via SetWindowCompositionAttribute.
3. Probe verifies: window enumeration -> active window -> activate/raise.
4. Gate: probe results match real Windows window state.

**Status: DONE (2026-08-11).** All probe checks PASS with craft-built
KWindowSystem + E:\Qt runtime (see environment notes below):
platform detection, windows(), activeWindow() == GetForegroundWindow,
workArea(), KWindowInfo (caption/pid/geometry/state/type), windowAdded and
activeWindowChanged signals (SetWinEventHook), activateWindow() actually
raising a minimized window. Patch: `patches/kwindowsystem/0001-windows-backend.patch`
(framework core + plugin + KWindowInfo Windows support + KWindowSystemWindows
public API). Also `patches/qtbase/0001-modernwindows-style-sdk19041-fallback.patch`
(Qt 6.11 needs Win11 SDK constants; literal fallback for SDK 19041).

**Environment notes (dev machine):**
- craft-built Qt6Gui apps crash at load unless the full craft runtime set
  (msvcp140_1/2, vcruntime140_1, etc.) is app-local or on PATH from
  CraftRoot\bin; the craft binary-cache Qt had a broken zlib/libpng
  linkage (inflateReset2 bound to libpng16.dll) - fixed by rebuilding
  qtbase from source (`craft --no-cache libs/qt6/qtbase`).
- QT_PLUGIN_PATH did not surface in libraryPaths here; the probe finds the
  KWindowSystem platform plugin by placing it under
  `<appdir>/kf6/org.kde.kwindowsystem.platforms/`.
- SetWinEventHook requires an interactive window station; automation
  sessions may fail hook registration (probe reports SKIP in that case).
- The M2 probe is built with CMAKE_PREFIX_PATH=E:\Qt...;CraftRoot (Qt
  headers/libs from E:\Qt, KF6 from CraftRoot - same Qt 6.11.1 version).

### M3 - plasma-workspace 6.7.4 (patched source build)
1. Blueprint: add 6.7.4 to kde/plasma version.ini tarballs, bump defaulttarget.
2. Patches: WITH_X11=OFF / WITH_X11_SESSION=OFF; make Wayland/LayerShellQt/
   KWayland/PlasmaWaylandProtocols conditional; stub KWinDBusInterface and
   ScreenSaverDBusInterface; drop UDev/systemd/Canberra/libxcrypt/PackageKit;
   skip ksmserver/kcms/ktimezoned/xembed proxies.
3. Build scope: shell/, components/, libkworkspace, libtaskmanager,
   libnotificationmanager, libklookandfeel, wallpapers/, minimal applets/runners.
4. Gate: plasmashell.exe links with no X11/Wayland dependencies.

**Status: DONE (M3.1-M3.3; follow-ups in M3.6+).** plasmashell.exe builds
and links with no X11/Wayland dependencies. Subsequent milestone work
(taskbar model, popup anchoring, edit mode, window stacking, icon theme)
lives in `patches/plasma-workspace/0001-...` (see PATCHES.md) - keep the
work-tree patches in sync when rebuilding from a fresh tarball.

### M4 - plasma-desktop 6.7.4 (custom recipe / plain CMake)
1. Desktop containment (wallpaper), Kicker, taskmanager, pager, systemtray.
2. Breeze theme/icons (blueprint exists).
3. Gate: QML components load completely.

**Status: IN PROGRESS.** Components delivered so far (M3.4-M3.6 of the
git milestone numbering):

* desktop containment + wallpaper + right-click menu + **edit mode**
  (fixed: "enter edit mode" used to minimize the desktop itself via
  `KWindowSystem::setShowingDesktop`; now a no-op on Windows and the
  backend skips our own windows)
* kickoff launcher with ksycoca application list (M3.4)
* **taskbar with real Windows window integration** (new
  `WindowsWindowTasksModel` in libtaskmanager: EnumWindows + taskbar
  candidate filter + Win32 icons + activate/close/minimize/maximize +
  incremental updates; custom `ConcatenateTasksProxyModel` because
  Qt 6.11's `QConcatenateTablesProxyModel` fails to map Windows models)
* popup anchoring (M3.6b): `PopupPlasmaWindow::updatePosition()` gets a
  `Q_OS_WIN` branch that applies the TransientPlacementHelper rect
* pager / showdesktop / margins separator / clock / system tray (base)

Remaining M4 items:

* icon theme integration (breeze icons installed under
  `CraftRoot\bin\data\icons`; QIcon layer works, panel icons still
  blocked - KIconLoader resolves the theme but fails to load icons;
  see `docs/windows-port-notes.md` section 7)
* taskbar polish: hover thumbnails (transparent - no DWM thumbnail
  bridge yet), grouping, pinning, context menu
* tray: network/volume applets need a Windows bridge (PulseAudio
  dependency)

### M5 - Integration and acceptance
1. P3-A: plasmashell runs windowed on dev machine (desktop + panel).
2. P3-B: VM shell replacement (snapshot -> switch-shell.cmd install -> relogin).
3. P3-C: lifecycle/recovery parity (exit -> switch-shell.cmd restore).
4. Tray: statusnotifierwatcher (SNI host via DBus); native Windows tray
   bridge optional later.
5. Gate: desktop/wallpaper/panel/launcher/taskmanager work in VM; recovery
   flow identical to Phase 0.5.

**Status: NOT STARTED.** P3-A is effectively reached on the dev machine
(desktop + panel + taskbar + launcher run windowed). P3-B/C and the
final gate require VM validation. Before VM work, verify on the VM:

* window stacking: desktop stays below other windows, panel stays on
  top (`HWND_BOTTOM` + `WS_EX_NOACTIVATE` on DesktopView; panel already
  `WS_EX_TOPMOST`) - dev machine fights the live Explorer taskbar
* panel work area (`SPI_SETWORKAREA` in PanelView) so maximized windows
  stop under the panel - same Explorer conflict on the dev machine
* "show desktop" keeps the desktop visible

### Known downgrades (documented in acceptance)
* Third-party windows: DWM native title bars, no rounded corners (Win10 LTSC
  has no API); dark title bars only where apps opt in.
* Our windows: frameless, self-drawn; optional acrylic panel blur.

## After Phase 3 (optional roadmap)
* WS6 WindowDecor: window subclassing hook proxy (WindowBlinds/Windhawk
  route, no DWM patching) for Breeze title bars + rounded corners on all
  windows; data source = M2 backend; exclude our own windows by stable
  class/title convention; prototype possible as a Windhawk mod.
* Native Windows tray bridge (optional).
* Phase 4 Dolphin: KIO Windows backend validation + Dolphin build
  (dependency chain already present).

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

### M3 - plasma-workspace 6.7.4 (patched source build)
1. Blueprint: add 6.7.4 to kde/plasma version.ini tarballs, bump defaulttarget.
2. Patches: WITH_X11=OFF / WITH_X11_SESSION=OFF; make Wayland/LayerShellQt/
   KWayland/PlasmaWaylandProtocols conditional; stub KWinDBusInterface and
   ScreenSaverDBusInterface; drop UDev/systemd/Canberra/libxcrypt/PackageKit;
   skip ksmserver/kcms/ktimezoned/xembed proxies.
3. Build scope: shell/, components/, libkworkspace, libtaskmanager,
   libnotificationmanager, libklookandfeel, wallpapers/, minimal applets/runners.
4. Gate: plasmashell.exe links with no X11/Wayland dependencies.

### M4 - plasma-desktop 6.7.4 (custom recipe / plain CMake)
1. Desktop containment (wallpaper), Kicker, taskmanager, pager, systemtray.
2. Breeze theme/icons (blueprint exists).
3. Gate: QML components load completely.

### M5 - Integration and acceptance
1. P3-A: plasmashell runs windowed on dev machine (desktop + panel).
2. P3-B: VM shell replacement (snapshot -> switch-shell.cmd install -> relogin).
3. P3-C: lifecycle/recovery parity (exit -> switch-shell.cmd restore).
4. Tray: statusnotifierwatcher (SNI host via DBus); native Windows tray
   bridge optional later.
5. Gate: desktop/wallpaper/panel/launcher/taskmanager work in VM; recovery
   flow identical to Phase 0.5.

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

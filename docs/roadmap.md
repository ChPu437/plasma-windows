# Plasma Windows - Roadmap / Known Issues

Tracking for issues that are understood but not yet fixed. Status as of
2026-08-13 (M4).

## Open runtime issues

### 1. Blur behind popups still not rendering (kickoff etc. stay translucent)

Investigated 2026-08-13; two real bugs were found and fixed:

* `AccentState` enum values in the kwindowsystem Windows backend were off
  by one (`AccentEnableBlurbehind` was 4, real value 3; acrylic was 5,
  real value 4) - fixed in `0001-windows-backend.patch`.
* Both popup window classes dropped the blur request when it arrived
  before the HWND existed:
  * `Dialog::showEvent` now re-applies `updateTheme()` (`0003`),
  * `PlasmaWindow::showEvent` now re-applies `handleFrameChanged()` (`0003`).

**Still translucent.** Remaining hypotheses (in order of likelihood):
(Status cross-referenced in `windows-port-notes.md` section 1.5 and
`architecture.md` section 7.)

1. `SetWindowCompositionAttribute` succeeds but DWM does not blur this
   particular window type (frameless + Qt transparent background flag).
   Test: apply `ACCENT_ENABLE_BLURBEHIND` to a known-good window
   (explorer) from a scratch program and compare with the popup.
2. The blur is applied but immediately overwritten (Qt/DWM repaint,
   `WS_EX_LAYERED` handling on show).
3. Software rendering (`QT_QUICK_BACKEND=software`) interferes - test on
   the VM (also software) and on a GPU machine.
4. `backgroundHints` for the kickoff popup include `SolidBackground` (the
   QML asks for an opaque dialog), making the blur invisible.

Verification notes: `GetWindowCompositionAttribute` returns
ERROR_INVALID_PARAMETER for WCA_ACCENT_POLICY on this Windows 10 build
(read direction is not supported reliably); visual confirmation is
authoritative. A `probe/blurprobe.cpp` was written and then removed
(0xC0000139 crash in the probe's Qt/ICU loading, unresolved; decided to
rely on visual checks).

### 2. Windows applets (volumewin, imewin) do not run yet

`src/applets/volumewin|imewin` - native volume control + input method
indicator. The package metadata is now normalized (metadata.json +
contents/ui/main.qml), but the applet C++ plugin DLL was never built and
installed:

* `org.kde.plasma.private.volumewin` QML module missing
  (`PlasmaCore.IconItem is not a type` in main.qml comes from the same
  root cause - the module that registers the types is absent).
* Building the applets needs a working CMake configure against
  CraftRoot (the ad-hoc `cmake -B ... -DCMAKE_PREFIX_PATH=CraftRoot`
  failed MSVC detection in the plain cmd environment - use the craft
  environment), then install to `plugins/plasma/applets` +
  `bin/data/plasma/plasmoids`.

### 3. Task.qml QML warnings

```
Task.qml:61:5: Unable to assign [undefined] to int
Task.qml:365: Unable to assign [undefined] to QString
```

Harmless-looking delegate warnings while the Windows window tasks model
populates; likely a role that is `undefined` before the first model
refresh. Low priority; re-check when taskbar grouping lands.

### 4. kactivitymanagerd database errors

```
PlasmaActivities: Database is not open: .../kactivitymanagerd/resources/database
```

The sqlite resource-scoring plugin is not built (KIO `kdirnotify.h`
dependency, see `0004-sqlite-plugin-optional.patch`); recent-files
degrade, activities themselves work. Optionally rebuild the plugin when
KIO ships the header on Windows.

### 5. Background contrast / adaptive wallpaper selectors

`BlurEffectWatcher` returns false on Windows, so theme selectors never
include "translucent" and adaptive wallpapers (day/night) never switch.
Not visible with the current Next wallpaper; revisit when wallpaper
variants matter.

## Deferred / planned

### Window flicker after "right-click panel, then click desktop twice" (2026-08-15, unfixed)

**Symptom**: the sequence 1) right-click the panel (context menu opens),
2) click the desktop once (menu closes), 3) click the desktop again
makes "all foreground apps flash" (windows vanish and reappear for one
frame).

**Diagnosis (verified with a SetWinEventHook monitor: HIDE/SHOW/
MINIMIZE/SWITCH/LOCATIONCHANGE/REORDER/STATECHANGE/FOREGROUND)**:

- No window is actually hidden, minimized, moved or reordered. The only
  anomaly: after the context menu closes, the system hands the
  foreground to the window under the mouse - the plasma **desktop view**
  - for ~4 ms (our handlers hand it back to the topmost non-shell
  window immediately). The desktop gets raised with that activation,
  covering every window for a frame: that is the visible "flash".
- Chrome/Steam also react to a desktop foreground as if "show desktop"
  was pressed and hide their windows for ~30-80 ms (they restore by
  themselves). This was the earlier, larger flash; the handlers below
  eliminated it, but the residual desktop-cover frame remains.

**Attempted fixes (all in the Craft work trees, NOT committed - none
fully solves the residual flash)**:

1. kwindowsystem Windows plugin `setShowingDesktop` log - not called
   during the sequence (so not a "show desktop" trigger).
2. PanelView `updateWorkArea` log - SPI_SETWORKAREA not called either.
3. kwindowsystem FOREGROUND hook: when the desktop becomes foreground,
   `SetForegroundWindow(prev)` back - too slow (foreground lock, took
   hundreds of ms; Chrome had already hidden).
4. Same with AttachThreadInput force - still not fast enough.
5. libplasma `Dialog::hideEvent`: hand the foreground to the topmost
   non-shell window when a popup closes - ineffective because by the
   time the panel context menu closes, the foreground is already not
   ours (condition `fgPid == GetCurrentProcessId()` not met).
6. plasma-workspace `DesktopView::event()` (Expose/ActivationChange/
   WindowActivate): hand the foreground back to the topmost non-shell
   window **and** `SetWindowPos(HWND_BOTTOM)` synchronously. This makes
   the hand-back take ~4 ms (Chrome/Steam no longer flash), but the
   single desktop-cover frame during activation remains visible.
7. qtbase `QWindowsWindow::requestActivateWindow`: early-return when
   the HWND has `WS_EX_NOACTIVATE` (the desktop view). The desktop still
   becomes foreground - the activation comes from the system's
   "foreground after window close" rule, not from Qt.

**Next ideas (not tried)**: intercept the system foreground hand-over
before the desktop is activated (e.g. from the context menu close path,
or a `WM_ACTIVATEAPP`/`WM_ACTIVATE` hook on the desktop HWND), or
accept the single frame (visually minor; only noticeable with many
windows open).

## Deferred / planned

* **kglobalacceld** (global shortcuts) - no DBus service on Windows yet.
* **Taskbar grouping / pinning / context menu validation** - the model
  stack exists; grouping proxy untested against the Windows model.
* **Live taskbar thumbnails** - `DwmRegisterThumbnail` bridge (current
  `PrintWindow` refresh is a placeholder).
* **Notifications** - KNotifications to Windows toast bridge.
* **kiconthemes / icon theme parity** - breeze redirect fix exists;
  full icon theme validation pending.
* **WS6 window decoration** (Breeze title bars) - optional roadmap.
* **Dolphin Phase 4** - KIO Windows backend validation.

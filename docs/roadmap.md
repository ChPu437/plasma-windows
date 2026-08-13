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

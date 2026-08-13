# Plasma Windows - Functionality Report (M4 status)

Status snapshot after M3.6/M3.7 shell work (2026-08-12). For each Plasma
feature: how KDE implements it on Linux, what we did on Windows, and its
state.

Legend: **[OK]** works, **[PARTIAL]** degraded/limited, **[TODO]** not
implemented.

---

## 1. Desktop

### 1.1 Wallpaper / desktop containment **[OK]**
- KDE: `plasma-desktop` desktop containment renders wallpaper + icons in
  the desktop view window.
- Windows: the desktop is a normal top-level window (`DesktopView`,
  2560x1440 on the test machine) kept at the bottom of the window stack
  (`HWND_BOTTOM` + `WS_EX_NOACTIVATE`; re-asserted on
  Expose/ActivationChange so owner-chain raises and edit mode cannot
  lift it above other windows). Wallpaper: a `Next` wallpaper package is
  installed at `CraftRoot\bin\data\wallpapers\Next` (metadata.json +
  contents/images/1920x1080.png). Without it the image wallpaper's
  providerType stays `Unknown`, `loadWallpaper()` never completes and
  **the panel is never created** (UiReady never fires).

### 1.2 Desktop right-click menu / edit mode **[OK]**
- KDE: containment context menu; edit mode shows the DesktopEditMode UI
  and the widget explorer sidebar.
- Windows: both work. Notable fix: entering edit mode used to minimize
  the desktop itself (`setDashboardShown` -> `KWindowSystem::setShowingDesktop`
  -> Windows backend minimized every top-level window). Now a no-op in
  `ShellCorona` and the backend skips the shell's own windows.

### 1.3 Desktop icons (folder containment) **[PARTIAL]**
- Folder containment loads; no thorough validation of icons/drag-drop on
  the native filesystem yet.

## 2. Panel

### 2.1 Panel window, position, thickness **[OK]**
- KDE: Dock-type window managed by KWin.
- Windows: normal frameless window with `Qt::WindowStaysOnTopHint`
  (kept above regular windows like a taskbar). Right-edge panel on the
  test machine.

### 2.2 Panel strut / work area **[PARTIAL - needs VM]**
- KDE: panel occupies a strut; maximized windows stop at the panel edge.
- Windows: `PanelView::updateWorkArea()` sets `SPI_SETWORKAREA` on
  show/move/resize (hide restores). Conflicts with the live Explorer
  taskbar on the dev machine; must be verified in the VM (no Explorer).

## 3. Applets

### 3.1 Kickoff (application launcher) **[OK]**
- KDE: ksycoca database + applications.menu -> app list.
- Windows: works (ksycoca rebuilt at session start; menu files mirrored
  to LOCALAPPDATA). Popup anchors to the panel button (see 5.1).

### 3.2 Taskbar (icontasks / taskmanager) **[OK - base]**
- KDE: `libtaskmanager` WindowTasksModel (X11/Wayland) + KWin
  integration.
- Windows: new `WindowsWindowTasksModel` (EnumWindows + taskbar-candidate
  filter; shell's own windows excluded; Win32 icons; activate/close/
  minimize/maximize/new-instance; 500 ms poll with incremental model
  updates). Qt 6.11 `QConcatenateTablesProxyModel` fails to map Windows
  models, so a hand-rolled `ConcatenateTasksProxyModel` is used.
  Clicking a button switches the foreground window.
- **[PARTIAL]** tooltip thumbnails now show the window icon
  (WindowThumbnail Windows branch; real live preview via
  `DwmRegisterThumbnail` deferred - it paints into the window background
  and needs the tooltip background punched out). Grouping, pinning and
  the context menu are not validated yet.

### 3.3 Pager, showdesktop, minimizeall, margins separator **[OK]**
- KDE: pager via virtual desktops (single desktop on Windows);
  showdesktop via KWin "show desktop" effect.
- Windows: pager renders (single desktop); showdesktop minimizes other
  windows while keeping the plasma desktop/panel visible (backend skips
  own-process windows).

### 3.4 Clock / system tray **[PARTIAL]**
- Clock works; tray shows SNI items (statusnotifierwatcher runs) but
  the **network and volume applets are missing** (PulseAudio
  dependency) - needs a native Windows bridge. The native volume /
  input-method applets (`src/applets/volumewin|imewin`) exist but do
  not run yet (C++ plugin DLL never built/installed - see
  `docs/roadmap.md`).

## 4. Popups and windows

### 4.1 Applet popups (kickoff/clock/tray) **[OK]**
- KDE: `AppletPopup` -> `PopupPlasmaWindow` -> TransientPlacementHelper.
- Windows: `updatePosition()` had no Windows branch (setPosition never
  called, popups stayed centered). Added a `Q_OS_WIN` branch applying
  the computed rect; visualParent set explicitly (QML assignment +
  parent-chain fallback) because QML bindings are never evaluated on
  Windows; popups parked off-screen at componentComplete to avoid the
  centered flash; repositioned once after layout settles.

### 4.2 Window stacking semantics **[PARTIAL - needs VM]**
- Desktop below / panel above implemented with native z-order (see 1.1
  and 2.1). Snap-layout interference from a raised desktop was reported
  and the re-assertion handles Expose/ActivationChange; VM verification
  pending.

## 5. Icon theme

### 5.1 Theme loading **[OK]**
- KDE: `QIcon`/`KIconLoader` search `QStandardPaths::GenericDataLocation`
  (XDG) plus the `:/icons` resource.
- Windows: none of those contain the Craft bundle path
  (`<appdir>/data/icons`). Fixed in kiconthemes (`applicationDirPath()/
  data/icons` added to the icon dir list; KIconEngine virtual theme
  names skipped in `KIconTheme::current()`), plus a 100 ms re-assertion
  of `QIcon::setThemeSearchPaths` in plasmashell (the Kirigami controls
  plugin overwrites it during QML engine setup).
- **breeze-icons redirect files** (a `.svg` containing the name of
  another icon) broke QImageReader - `tools/fix-icon-redirects.py`
  resolves them after install.

## 6. Session services

- DBus session bus (TCP) **[OK]**; kded6 **[OK]**; kactivitymanagerd
  **[PARTIAL]** (activity DB issues: sqlite resource plugin not built,
  KIO dependency; recent-files degrade). kglobalacceld **[TODO]** (no
  global shortcuts). KWin DBus interface stubbed (no compositor).

## 7. Compatibility layer summary (what we adapted and how)

| KDE mechanism | Windows adaptation | Where |
|---|---|---|
| Window list / stacking order / active window (KWindowSystem X11) | Win32 backend: EnumWindows + SetWinEventHook + GetForegroundWindow | kwindowsystem 0001 |
| KWindowInfo (caption/pid/geometry/state) | Win32/DWM snapshot fill | kwindowsystem 0001 |
| "Show desktop" (KWin effect) | no-op + backend skips own-process windows | plasma-workspace 0004 |
| Desktop layer / panel Dock layer | HWND_BOTTOM + WS_EX_NOACTIVATE / WS_EX_TOPMOST | plasma-workspace 0004 |
| Panel strut (work area) | SPI_SETWORKAREA | plasma-workspace 0004 |
| WindowTasksModel (X11/Wayland) | WindowsWindowTasksModel (EnumWindows) | plasma-workspace 0004 |
| QConcatenateTablesProxyModel | custom ConcatenateTasksProxyModel (Qt bug workaround) | plasma-workspace 0004 |
| Popup placement (TransientPlacementHelper) | Q_OS_WIN branch + visualParent fixes | libplasma 0003 |
| WindowThumbnail (XComposite) | Win32 window icon fallback (DWM thumbnail deferred) | libplasma 0004 |
| Icon theme paths (QStandardPaths) | Craft bundle path + Kirigami override workaround | kiconthemes 0001, plasma-workspace 0004 |

## 8. Not implemented / deferred

- Popup blur (kickoff etc.) - accent-state enum and HWND-timing bugs
  fixed, still translucent; hypotheses in `docs/roadmap.md`
- Native volume/IME applets (volumewin/imewin) - package metadata done,
  C++ plugin build missing
- Live taskbar thumbnails (DwmRegisterThumbnail + tooltip background
  punch-out)
- Tray network applet (PulseAudio dependency - native bridge)
- Taskbar grouping/pinning/context-menu validation
- Desktop icons drag-drop validation
- kglobalacceld (global shortcuts)
- Windows app integration in the launcher (`.desktop` bridge for
  notepad/calc/browsers)
- Window decoration (Breeze title bars / rounded corners) - out of
  scope (WS6 roadmap)
- Notifications: KNotifications to Windows toast bridge

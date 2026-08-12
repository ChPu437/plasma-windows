# Plasma Windows

A Windows-native port of the KDE Plasma desktop shell. The goal is to
replace `explorer.exe` as the interactive shell on Windows 10 IoT
Enterprise LTSC 2021 with `plasmashell` (KDE Plasma 6.7.4), keeping the
Windows kernel / Win32 / DWM / graphics stack untouched.

Status: **Phase 3 (M3/M4)** - plasmashell runs on Windows with the core
desktop working: wallpaper, panel, kickoff app launcher, taskbar with
real Windows window integration, correct popup anchoring, edit mode and
"show desktop". Session services (dbus, kded6, kactivitymanagerd) run.
Remaining M4 work: icon theme integration (panel icons), taskbar polish
(thumbnails), tray/volume alternatives; then VM acceptance (M5).

See `agents.md` for the full project plan and `patches/PATCHES.md` for
the porting patch catalogue.

## Milestones

| Phase | What                                   | Status |
|-------|----------------------------------------|--------|
| 0     | Pure Win32 shell (`shell.exe`)         | done   |
| 0.5   | Safe per-user shell switching          | done   |
| 1     | Qt 6 shell with the same lifecycle     | done   |
| 2     | KDE Craft + KF6 frameworks             | done   |
| 3 M1  | DBus session + kded6 + kactivitymanagerd | done |
| 3 M2  | KWindowSystem Windows backend          | done   |
| 3 M3.1| plasma-workspace builds, plasmashell starts | done |
| 3 M3.2| plasma-desktop, default layout loads   | done   |
| 3 M3.3| panel window + default applets         | done   |
| 3 M3.4| kickoff app listing (sycoca + menu)    | done   |
| 3 M3.5| Windows app integration in launcher    | todo   |
| 3 M3.6| Taskbar window integration + popup anchoring | done (base) |
| 3 M3.7| Edit mode, "show desktop", window stacking | done (dev machine) |
| 4     | Dolphin + KIO Windows backend          | todo   |
| 5     | KRunner, notifications, polish         | todo   |

## What works today

* `plasmashell` starts, stays responsive, and shows:
  * full-screen desktop with wallpaper and desktop icons containment
  * a right-edge panel (kept above the desktop window, `WS_EX_TOPMOST`)
  * kickoff (app launcher) with a working application list from ksycoca
  * **taskbar (icontasks) with real Windows window integration**: open
    windows appear as buttons, click switches the foreground window,
    middle/close and minimize/maximize actions work (via the new
    `WindowsWindowTasksModel` in libtaskmanager)
  * pager, showdesktop, margins separator, clock, system tray (base)
* **Popup anchoring**: kickoff/clock/tray popups anchor to their panel
  button (right edge, no overlap) via the Windows branch added to
  `PopupPlasmaWindow::updatePosition()` (TransientPlacementHelper)
* **Edit mode** works; the widget explorer sidebar no longer gets
  squashed
* **"Show desktop"** minimizes other windows but keeps the plasma
  desktop/panel visible (KWindowSystem backend skips our own windows)
* Session stack: `dbus-daemon` (session bus over TCP),
  `kactivitymanagerd`, `kded6` - all register on the bus.
* Debug logging: `%TEMP%\plasmashell-debug.log` captures all Qt logging
  (Qt's Windows default only writes to OutputDebugString).

## Known issues / next steps

1. **Icon theme on the panel**: QIcon-based icons work (settings
   dialogs) but the panel's kickoff/"show desktop" icons do not render
   yet - the KIconLoader path resolves the breeze theme but fails to
   load individual icons; see `docs/windows-port-notes.md` section 7.
2. **Taskbar thumbnails**: window hover tooltips show transparent
   thumbnail areas (no DWM thumbnail bridge yet).
3. **Window management (dev machine)**: desktop z-order hardening and
   panel work-area (`SPI_SETWORKAREA`) are implemented but fight the
   live Explorer taskbar on the dev machine - verify in the VM (no
   Explorer).
4. **Tray**: statusnotifierwatcher runs; network/volume applets are
   missing (PulseAudio dependency) - native Windows tray/volume bridge
   needed.
5. **Windows app integration**: the launcher only lists KDE `.desktop`
   entries; generate `.desktop` files (or a Windows app bridge) so
   notepad/calc/browsers are launchable.
6. **kglobalaccel** service not started (global shortcuts unavailable).
7. **VM validation (M5)**: run the full session in the LTSC VM
   (snapshot first), fix anything the VM exposes, then attempt shell
   replacement.

## Layout

```
plasma-windows/
    patches/                  (canonical porting patches, see PATCHES.md)
    tools/
        start-plasma-session.cmd   (dev-machine session bootstrap)
        deploy-vm.cmd              (VM: one-shot deploy + start, location-independent)
        make-vm-package.cmd        (dev-machine: build vm-package\plasma-vm.zip)
        switch-shell.cmd           (Phase 0.5 shell switcher)
        shell-registry.ps1
    README.md
```

## Development environment

* Craft root: `D:\Projects\CraftRoot`
  (ABI `windows-cl-msvc2022-x86_64`, binary cache
  `https://files.kde.org/craft/Qt6/26.05/.../msvc2022/x86_64`).
  Enter it with `. D:\Projects\CraftRoot\craft\craftenv.ps1`.
* Qt 6.11.1 (Craft), KF6 6.28.0, Plasma 6.7.4 (plasma-workspace,
  libplasma, plasma-desktop, plasma5support, plasma-activities-stats).
* MSVC v143 Build Tools, Windows SDK 10.0.19041, CMake, Ninja.

Build a package inside Craft:

```powershell
. D:\Projects\CraftRoot\craft\craftenv.ps1
craft plasma-workspace        # builds + installs into CraftRoot
```

Every porting patch lives in `patches\<component>\` and is wired into the
corresponding Craft blueprint (`patchToApply`); see `patches/PATCHES.md`.

## Running the session (development machine)

```bat
tools\start-plasma-session.cmd
```

This mirrors KDE package data into `%LOCALAPPDATA%`, writes
`%LOCALAPPDATA%\menus\applications.menu`, rebuilds the ksycoca service
database, then starts dbus + kactivitymanagerd + kded6 (foreground).
Start `plasmashell.exe` separately (from `D:\Projects\CraftRoot\bin`)
with `QT_PLUGIN_PATH=D:\Projects\CraftRoot\plugins`.

## VM deployment (one file)

1. On the dev machine: `tools\make-vm-package.cmd`
   -> `vm-package\plasma-vm.zip` (~1.5 GB, no hard-coded paths).
2. Copy the zip into the VM, extract anywhere:
   ```
   powershell Expand-Archive plasma-vm.zip -DestinationPath C:\plasma
   ```
3. Run `C:\plasma\deploy-vm.cmd` - environment, data mirror, menu,
   sycoca rebuild, then dbus + kactivitymanagerd + kded6 + plasmashell.

Always take a VMware snapshot before any shell-related experiment.

## Recovery (if plasmashell ever fails)

* `Ctrl+Shift+Esc` -> `File > Run new task` -> `cmd.exe`
* run `explorer.exe` manually, or restore the shell registry value with
  `tools\switch-shell.cmd restore`
* or restore the VMware snapshot

## Phase 0.5 shell switching

`tools/switch-shell.cmd` switches the current user's shell by editing
only `HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell`,
saving a backup first. It refuses to run outside a VMware VM unless
`/force` is given. See the header of the script for the safe test
procedure (`SWITCH_SHELL_TESTKEY`/`SWITCH_SHELL_DRYRUN`).

Never run shell-switch operations on the physical development machine;
they belong to the test VM only.

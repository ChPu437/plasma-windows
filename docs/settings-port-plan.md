# KDE Settings Port to Windows - Plan (2026-08-14)

Goal: let the user adjust Plasma's own settings from within the Plasma
shell on Windows. System-level settings (hardware/OS) are NOT ported -
they stay with Windows Control Panel / Settings and are listed in
section 4 for documentation only.

## 1. What "Plasma settings" means

Plasma settings are the KDE concepts that configure the shell itself.
On Windows the config backend is the same KConfig files under
`%LOCALAPPDATA%` (KDE on Windows maps `~/.config` there), so every
setting below is a file edit today and a UI tomorrow.

## 2. Already adjustable (config file, no UI needed yet)

| Setting | Config file / mechanism | Status |
|---|---|---|
| Panel: position/thickness/location | `plasma-org.kde.plasma.desktop-appletsrc` `[Containments][2]` + panel edit mode | works (floating must stay off, see windows-port-notes.md 8) |
| Panel opacity | `panelOpacity=0/1/2` (Adaptive/Opaque/Translucent) | works |
| Desktop layout / widgets | desktop containment + edit mode (widget explorer) | works |
| Wallpaper | `[Containments][1]...[Wallpaper][org.kde.image][General] Image=` | works (gradient PNG default) |
| Look-and-feel / theme package | `%LOCALAPPDATA%\plasma\look-and-feel`, `desktoptheme` | installed; switching = config |
| Icons | `QIcon::setThemeSearchPaths` + `data/icons` (0004 main.cpp patch) | works |
| Lock screen | `org.kde.plasma.lock_logout` applet | works |
| Notifications | `org.kde.plasma.notifications` + `org.freedesktop.Notifications` | works |
| Session shortcuts (shutdown/restart/logout) | `session-shortcuts-kded` (kded6) | works |
| Default apps / Start menu | `.desktop` bridges in `%LOCALAPPDATA%\plasma` (build-startmenu-desktops.ps1) | works |

## 3. Port candidates (future work, in order of value)

1. **Workspace appearance KCM** - theme / color scheme / fonts / icons.
   Needs `systemsettings` (or a minimal KCM host) + `kcmutils` +
   `kdeclarative` in the Craft build. Colors: KColorScheme reads
   `%LOCALAPPDATA%\kdeglobals` `[Colors]` - already consumed by
   plasmashell, so a theme-switch UI only writes KConfig + refreshes.
2. **Window decoration** - the current titlebar overlay is a native
   Win32 bar (src/titlebar). The Plasma-grade replacement is a QML
   title bar (PlasmaQuick::Dialog floating above the target, Breeze
   QML buttons). Config (decoration style, button order, double-click
   action) belongs in `kdeglobals` `[General]` (KDE 5 style keys) or a
   dedicated `plasmawindowdecorationrc`.
3. **Desktop behavior** - icon size/grid, sorting, double-click; lives
   in desktop containment config; UI = folder-view settings dialog
   (plasma-desktop folder plugin, already built).
4. **Input methods** - kimpanel config; the IME candidate-window issue
   (see titlebar-research.md) must be fixed first.
5. **Shortcuts** - kglobalaccel config (`kglobalshortcutsrc`) + KCM;
   kglobalaccel service must be running (currently missing - kded6
   logs "org.kde.kglobalaccel was not provided"; kglobalaccel binary
   exists in Craft but nothing starts it - start it in
   pc_start_services).
6. **KRunner** - krunner.exe builds already; wire its launch shortcut.

## 4. NOT ported (system settings - documented only)

These stay in Windows Settings / Control Panel; Plasma reads the OS
state where needed (e.g. `SPI_GETWORKAREA` for the panel, system fonts):

- Display: resolution, scaling, orientation, night light, HDR
- Network: wifi, ethernet, VPN, proxy
- Bluetooth / devices / printers / scanners
- Power & battery, sleep behavior
- Sound: devices, volumes (tray volume icons come via trayhost as SNI)
- Date & time, timezone, region/language
- User accounts, login, password
- Security: firewall, defender, encryption (BitLocker)
- Storage: disks, volumes, cleanup
- Windows Update

Plasma-facing reflections: panel work area follows the OS work area
(SPI_SETWORKAREA fights the live Explorer taskbar on the dev machine -
verify in the VM, see windows-port-notes.md 8).

## 5. Immediate follow-ups found while writing this

- kglobalaccel.exe is never started: add to `pc_start_services` in
  plasma-common.cmd (fixes the kded6 warning).
- `%LOCALAPPDATA%\menus\applications.menu` had a parse error ("Expected
  '=', got '[a-zA-Z]'" at line 1 col 15) - re-generate via
  `pc_write_menu` and check the generator's UTF-8 output.

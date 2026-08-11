# Plasma Windows - Architecture Report

Background document for non-KDE developers. Explains what Plasma is made of,
how it maps to Windows, and what we keep/replace/write.

## 1. Plasma on Linux: process landscape

The desktop is a family of independent processes communicating over **DBus**:

| Process | Role | On Windows |
|---|---|---|
| `plasmashell` | main shell: desktop, panels, widgets (QML) | ported (core of Phase 3) |
| `kwin` | compositor + window manager (effects, decorations) | **not ported** - DWM owns composition (mandatory since Win8) |
| `kded6` | service daemon (KIO activation, global shortcuts) | ported, needs DBus |
| `kactivitymanagerd` | activity tracking | ported (M1) |
| `krunner` / `klipper` | launcher / clipboard history | optional |
| `ksmserver` | X11 session manager | not needed - shell lifecycle handled by switch-shell.cmd |
| `kscreenlocker`, polkit agents | lock screen / auth | out of scope |
| `dbus-daemon` | IPC bus | **must be added** (Windows build via Craft) |

Key insight: Plasma is not one program. DBus is its bloodstream; a session
bus on Windows is a hard prerequisite (M1).

## 2. Software layers (bottom-up)

```
plasma-desktop (6.7)   desktop containment, Kicker, task manager, tray   [custom recipe]
plasmashell (6.7)      QML shell main process (ShellCorona)              [patched source]
Plasma Framework: libplasma + PlasmaQuick + PlasmaActivities             [blueprints exist]
KF6 6.28: KConfig KCoreAddons KWindowSystem KService KIO KNotifications  [installed]
         KPackage KDeclarative KGlobalAccel KDBusAddons KI18n ...        [installed]
Qt 6.11.1 (Widgets/Quick/QML/DBus)                                       [installed]
Windows: Win32 / DWM (compositor) / registry / filesystem
```

All Plasma UI is QML; C++ provides engines and data models. The port is
therefore mostly about making the C++ dependency chain build on Windows and
providing correct data - the QML layer works as-is.

## 3. Core concepts

* **Applet** (widget): one cell on the desktop/panel; a directory package
  (plasmoid): `metadata.json` + `contents/ui/main.qml` + optional C++ plugin.
* **Containment**: the canvas hosting applets. Desktop and panels are both
  containments (panel = containment with panel flags: autohide, struts).
  The wallpaper is the containment's background layer.
* **ShellCorona**: shell root object; owns containment creation/persistence
  (config in `plasma-org.kde.plasma.desktop-appletsrc`).
* **KPackage**: packaging for themes/widgets/layouts; loaded from
  user/system dirs by priority.
* **Task manager data**: `libtaskmanager` WindowTasksModel reads windows
  from KWin/X11 `_NET_WM` on Linux. On Windows there is no such source -
  this is exactly what the KWindowSystem Windows backend (WS2) provides.
* **System tray**: SNI (StatusNotifierItem) is a DBus protocol. Linux also
  supports legacy XEmbed. On Windows: SNI host over DBus; native Windows
  tray icons for legacy apps need a bridge (optional).
* **KIO**: file operations via `kio_<protocol>` worker processes + kded
  service activation. `file://` should work on Windows once DBus+kded run.

## 4. plasmashell 6.7 link inventory (verified)

Linked (installed): KF6 ConfigCore/ConfigGui/ConfigQml, CoreAddons, Crash,
DBusAddons, GlobalAccel, GuiAddons, I18n, KIOCore, Package, Notifications,
Service, Solid, StatusNotifierItem, Svg, WidgetsAddons, WindowSystem,
XmlGui; Qt Quick/DBus/GuiPrivate.

To build: Plasma::Plasma (libplasma), Plasma::PlasmaQuick, Plasma::Activities,
PW::KWorkspace (plasma-workspace itself).

Must be patched out on Windows: Wayland::Client, Qt::WaylandClient,
LayerShellQt::Interface, plasma-wayland-protocols generated code.

## 5. Target architecture on Windows

```
Winlogon -> shell config (HKCU) -> switch-shell.cmd mechanism
                  |
      [startup script] dbus-daemon (session bus)
            |                 |
         kded6.exe      kactivitymanagerd.exe
            \                 /
            plasmashell.exe (main process)
              |         |
      desktop/panel   tray (SNI host)
              |         |
      libtaskmanager (WindowTasksModel)
              |
      KWindowSystem Windows backend (self-written, WS2)
              Win32/DWM APIs
```

## 6. Component mapping (Linux -> Windows)

| Linux component | Windows replacement |
|---|---|
| KWin compositor/WM | DWM (system, mandatory, not portable) |
| X11 `_NET_WM` / Wayland protocols | self-written KWindowSystem backend (WS2) |
| systemd user session | login startup script + Winlogon shell mechanism |
| udev | Windows device APIs (Solid backend, verify) |
| dbus-daemon | Craft-built Windows dbus (M1) |
| ksmserver / X11 session | switch-shell.cmd lifecycle |
| notifications | KNotifications + SnoreToast backend (libsnoretoast present) |

## 7. Window decoration strategy

* Our windows (panels, desktop, OSD): frameless, fully self-drawn; optional
  acrylic blur via `SetWindowCompositionAttribute`.
* Third-party windows: DWM native chrome. Dark title bars are per-app opt-in
  (`DWMWA_USE_IMMERSIVE_DARK_MODE`); rounded corners have no Win10 LTSC API.
* Optional WS6 (after Phase 3): hook-based decoration proxy
  (window subclassing: WH_CALLWNDPROC/WH_GETMESSAGE, the WindowBlinds/
  Windhawk route - does not modify DWM itself) to draw Breeze title bars +
  rounded corners on all windows. Reuses kdecoration/Breeze rendering;
  consumes WS2 window state data; exempts our own windows via stable
  class/title convention. Prototype possible as a Windhawk mod.

## 8. Out of scope (explicit)

KWin, Wayland, X11, systemd, ksmserver, lock screen/polkit.

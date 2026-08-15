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
  STATUS (2026-08-15): the accent policy IS applied but blur/acrylic does
  NOT render yet (popups stay translucent) - investigation in
  `roadmap.md` section 1.
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

## 9. Our changes on top of stock KDE sources

Every modification we made to stock KDE/Qt code - what it does, how to
use it, how it is implemented, and its limitations - is documented in
`docs/windows-port-notes.md` (with patch files in `patches/`).

## 10. Porting playbook (how this port works)

There is no official KDE platform-porting manual. KDE provides the
**mechanisms**: Qt QPA plugins (window-system layer), KF6 backend
conventions (the source layout is the documentation), ECM
(`extra-cmake-modules`, build-time feature detection), Craft (build
pipeline). The playbook below is ours; it generalizes to any platform.

### 10.1 Porting principles

1. The shell is a family of processes on a session bus (DBus), not one
   program. The bus is the hard prerequisite - get it first (M1).
2. Never port the compositor: the host OS owns composition (DWM is
   mandatory since Win8; KWin is not ported).
3. Prefer native platform APIs over compatibility layers (Win32/DWM/
   registry, not WSL/Cygwin-style shims).
4. The QML layer is the last thing to touch: a port is mostly making
   the C++ dependency chain build and providing correct platform data.
5. Patch discipline: every source change is a patch applied to the
   pristine tarball, so rebuilds are reproducible and diffs reviewable
   (AGENTS.md section 13).

### 10.2 Keep / drop matrix (complete)

* **Keep (patched)**: plasmashell, libplasma/PlasmaQuick,
  plasma-desktop (subset), kded6, kactivitymanagerd,
  krunner/klipper (optional).
* **Drop**: KWin (-> DWM), X11/Wayland protocols (-> self-written
  KWindowSystem backend), systemd user session (-> logon script +
  shell registry key), ksmserver (-> shell lifecycle script),
  udev/Solid backends (-> Windows device APIs, verify), lock
  screen/polkit (-> Winlogon).
* **Replace with backends**: notifications (-> SnoreToast-based
  KNotifications backend), system tray (SNI over DBus + native
  `Shell_TrayWnd` host), KIO `file://` (needs kded + DBus activation).

Drop decisions must be explicit and documented; silent drops become
mystery regressions later.

### 10.3 Platform abstraction map (KF6 backends)

| Framework | Abstraction | Official backends |
|---|---|---|
| Qt (QPA) | platform plugins | qwindows, qxcb, qwayland |
| KWindowSystem | `src/platforms/<platform>` plugin | xcb, wayland (+ our windows) |
| KGlobalAccel | platform backends + daemon | x11, wayland |
| Solid | `backends/` | udev, udisks2, fstab, fake |
| KNotifications | `KNotificationBackend` plugins | xdg, freedesktop (+ snoretoast) |
| KIdleTime | `src/platforms/<platform>` | x11, wayland |
| KConfig | `KConfigBackend` plugins | ini, kconf_update |
| KWallet | backends | kwalletd, ksecret, file |
| KWin | platform backends + scenes | x11, wayland (not ported) |
| KIO | `kio_<protocol>` workers via kded | file:// generic, protocol-specific |

Platform selection and plugin install paths are ECM's job; runtime
discovery uses `libraryPaths()`/`QT_PLUGIN_PATH`. Pattern to copy
(KWindowSystem 6.28.0): a Qt plugin with `Q_PLUGIN_METADATA`
`"platforms": ["windows"]` auto-loaded when
`QGuiApplication::platformName()` matches.

### 10.4 Feature hookup map

| Feature | Seam (where KDE looks for platform support) | Hookup (what the backend provides) |
|---|---|---|
| Window list / taskbar | libtaskmanager WindowTasksModel (KX11Extras on X11, compositor protocol on Wayland) | `KWindowSystemWindows`: EnumWindows + taskbar filter + SetWinEventHook signals |
| Window actions | KWindowSystem core API | foreground-stealing dance, minimize/restore, show desktop |
| Window state | KWindowInfo NET semantics | mapping table, `windows-port-notes.md` section 1.4 |
| Effects (blur/acrylic) | KWindowEffects / KGuiAddons | `SetWindowCompositionAttribute` accent policy, runtime-loaded (see section 7 status) |
| Work area / struts | `KWindowSystem::workArea()` | `SPI_GET/SETWORKAREA` (no strut protocol on Windows) |
| System tray | SNI over DBus | watcher + native `Shell_TrayWnd` host (`tray-host-cairo-study.md`) |
| Notifications | KNotifications backends | native mapping (SnoreToast) |
| Startup items / hotkeys | no KDE seam (host's job) | session bootstrap; reference: ManagedShell `StartupRunner` |
| Persistence | KConfig | works out of the box; mind BOM and MAX_PATH |

### 10.5 Milestone shape and acceptance

```
M0  toolchain + build pipeline (Craft / binary cache / patches)
M1  session bus + core services (dbus-daemon, kded6, kactivitymanagerd)
M2  window-system abstraction (KWindowSystem Windows backend)
M3  plasmashell runs: desktop, panel, launcher, taskbar
M4  taskbar/window integration, popup anchoring, shell features
M5  shell replacement + explorer parity
```

A milestone is done only when it runs in the target environment (the
VM), not the dev machine: each feature exercised by a probe or manual
checklist, the parity checklist (`explorer-parity.md`) updated,
patches pass the static verifier and rebuild-from-clean, and
destructive experiments happen only in the disposable VM (snapshot
first).

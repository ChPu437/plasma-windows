# Tray host study - how Cairo Desktop implements the Windows notification area

Status: research report (2026-08-13). Sources cloned into
`research/cairoshell-master` and `research/ManagedShell-master` (both
gitignored). This is the in-depth companion to
`docs/tray-integration-research.md` section 3C2: how the
"become the notification-area host" approach actually works in a real
open-source shell, and how we should adapt it.

## 1. Architecture: two layers

Cairo's tray is split across two repositories:

* **Cairo Desktop** (`research/cairoshell-master`, MIT) - the WPF shell.
  Tray UI lives in `Cairo Desktop/CairoDesktop.MenuBarExtensions/`
  (`SystemTray.xaml`, `SystemTrayIcon.xaml`); it only does presentation
  and mouse forwarding.
* **ManagedShell** (`research/ManagedShell-master`, MIT) - the shell
  plumbing library (also used by RetroBar). The entire tray *protocol*
  implementation lives in `src/ManagedShell.WindowsTray/`. Cairo
  consumes it via NuGet; we cloned the source.

Wiring (`ManagedShell/ShellManager.cs:40-60`): one `TrayService`
(the Win32 window host) + one `ExplorerTrayService` (imports icons from
a live explorer) feed one `NotificationArea` (icon state + collections).
`SystemTrayMenuBarExtension` binds the WPF `SystemTray` control to it.
`NotificationArea.Initialize()` (`NotificationArea.cs:145-169`) starts
everything and, only when running as the shell, also starts
`ShellServiceObject` (the legacy system-icon COM object).

## 2. The protocol layer (TrayService.cs) - window host

Two window classes are registered, exactly as the Shell_NotifyIcon
receiver chain expects:

| Class | Flags | Role |
|---|---|---|
| `Shell_TrayWnd` | top-level, `WS_POPUP`, `WS_EX_TOPMOST`, `WS_EX_TOOLWINDOW`, 0,0, `SM_CXSCREEN` x `23*DPI` | the window shell32's `FindWindowW("Shell_TrayWnd", NULL)` resolves; receives `WM_COPYDATA` |
| `TrayNotifyWnd` | `WS_CHILD` of Shell_TrayWnd | child window (explorer's tray hierarchy mirror) |

(`TrayService.cs:13-14, 299-345`). The windows are kept hidden:
`WM_WINDOWPOSCHANGED` clears `WS_VISIBLE` if the tray ever becomes
visible (`TrayService.cs:237-248`).

### WM_COPYDATA dispatch (`TrayService.cs:158-234`)

`dwData` selects the payload:

* `0` - `APPBARMSGDATAV3` (AppBar registration protocol; forwarded to an
  optional AppBar manager - we do not need this initially).
* `1` - **`SHELLTRAYDATA`**: `{ int dwUnknown; uint dwMessage; NOTIFYICONDATA nid; }`
  (`NativeMethods.Shell32.cs:389-395`). `dwMessage` is the `NIM_*`
  action. This is the actual tray-icon protocol.
* `3` - **`WINNOTIFYICONIDENTIFIER`**:
  `{ int dwMagic; int dwMessage; int cbSize; int dwPadding; uint hWnd; uint uID; Guid guidItem; }`
  (`Shell32.cs:397-407`) - this is `Shell_NotifyIconGetRect`:
  `dwMessage == 1` asks for the icon rect top-left, `2` for
  bottom-right, as an LRESULT (`NotificationArea.cs:242-272`). Managed
  Shell answers from the icon's current pixel placement (updated on
  mouse enter from the UI layer). This matters for tooltips that
  position themselves (`SHOWTIP`/`NIF.SHOWTIP`, e.g. Steam's tooltips).

The callback returns TRUE/1 to signal "handled" (`SysTrayCallback`
returns bool; `TrayService.cs:209-211`).

### NIM_* handling (`NotificationArea.cs:274-449`)

`SysTrayCallback(uint message, SafeNotifyIconData nid)`:

* **NIM_ADD / NIM_MODIFY**: locate existing icon by `(hWnd,uID)` or
  GUID (`NotifyIcon.Equals`, `NotifyIcon.cs:569-579`); apply `NIF.*`
  flags: ICON (HICON -> image), TIP, STATE (hidden), MESSAGE
  (uCallbackMessage), GUID, `uVersion` (1-4). New icons get a default
  placement rect, `Path` via `ShellHelper.GetPathForWindowHandle(hWnd)`
  (for pinning), then are appended to `TrayIcons`. A NIM_MODIFY for an
  unknown icon returns FALSE (error to the app).
* **NIM_DELETE**: remove by equality; FALSE if unknown.
* **NIM_SETVERSION**: store `uVersion` per icon; rejects > 4.
* Icons without a valid `hWnd` are rejected (except GUID-only modifies).

`NIM_SETFOCUS` is not special-cased (default ok).

## 3. Callback semantics - mouse events (NotifyIcon.cs:384-559)

Events are delivered with `SendMessage(hWnd, uCallbackMessage, ...)` -
synchronous, because apps often show context menus from the callback
(`NotifyIcon.cs:506-509`):

| Event | v<=3 (wParam=UID, lParam=msg) | v4 (wParam=msg, lParam=UID<<16) |
|---|---|---|
| mouse down | `WM_LBUTTONDOWN`/`WM_RBUTTONDOWN`/`WM_MBUTTONDOWN` (DBLCLK within double-click time) | same |
| mouse up | `WM_LBUTTONUP` + `NIN_SELECT` (v>=3); `WM_RBUTTONUP` + `WM_CONTEXTMENU` (v>=3) | same |
| enter/leave/move | `WM_MOUSEHOVER`/`WM_MOUSELEAVE`/`WM_MOUSEMOVE` | same + `NIN_POPUPOPEN`/`NIN_POPUPCLOSE` (v4) |

Key detail: `AllowSetForegroundWindow(appPid)` is called **before**
sending mouse-down (`NotifyIcon.cs:412-414`) so the app may legally
steal focus to show its context menu. Stale icons are cleaned when
`IsWindow(hWnd)` is false (`RemoveIfInvalid`, `NotifyIcon.cs:531-540`).

## 4. Balloons (NIF_INFO) (`NotificationArea.cs:452-473`,
`NotificationBalloon.cs`)

* `szInfoTitle` empty -> ignore.
* Balloon payload: title, info text, `NIIF` flags (icon: ERROR/INFO/
  WARNING system icon, or USER icon via `hBalloonIcon`), timeout from
  `SPI_GETMESSAGEDURATION` (or `uVersion >= 1000` as ms).
* The UI shows a WPF popup, plays a sound unless `NIIF.NOSOUND`,
  clamps timeout to 12-30 s, and reports lifecycle to the app via the
  callback channel: `NIN_BALLOONSHOW`, `NIN_BALLOONHIDE`,
  `NIN_BALLOONTIMEOUT`, `NIN_BALLOONUSERCLICK`
  (`NotificationBalloon.cs:77-110`). `NotificationArea` raises a
  `NotificationBalloonShown` event so a shell can take over (e.g. Cairo
  promotes unpinned icons to the visible area, `SystemTray.xaml.cs:77-
  120`); unhandled balloons queue in `MissedNotifications`.

## 5. Session lifecycle

* **TaskbarCreated**: after `Run()`, `RegisterWindowMessage("TaskbarCreated")`
  is broadcast (`SendNotifyMessage(HWND_BROADCAST, ...)`,
  `TrayService.cs:116-126`) so apps re-register their icons. On
  dispose, it is broadcast again *unless* running as the shell (the
  replacement shell keeps being the tray).
* **Z-order battle**: when another `Shell_TrayWnd` exists (explorer),
  messages go to the topmost one. A 100 ms `DispatcherTimer` polls
  `FindWindow("Shell_TrayWnd")`; if it is not ours, ours is raised to
  TOPMOST (`TrayService.cs:347-385`); `Suspend()` pushes ours to
  HWND_BOTTOM. `WindowHelper.FindWindowsTray` finds the *other* tray via
  `FindWindowEx` skip (`WindowHelper.cs:210-222`).
* **Message forwarding**: unhandled `WM_COPYDATA`, `WM_ACTIVATEAPP`,
  `WM_COMMAND` and `WM_USER+372` are forwarded to the other tray window
  (explorer) - legacy AppBar traffic keeps working during coexistence
  (`TrayService.cs:251-282`).

## 6. Coexistence with explorer (ExplorerTrayService.cs)

When **not** running as the shell, icons that already live in
explorer's tray are *imported* at startup: walk
`Shell_TrayWnd` -> `TrayNotifyWnd` -> `SysPager` -> `ToolbarWindow32`,
ask `TB_BUTTONCOUNT`/`TB_GETBUTTON`, `ReadProcessMemory` the `TrayItem`
struct out of explorer, and synthesize NIM_ADD events
(`ExplorerTrayService.cs:61-110`, struct at `:250-295`). Auto-hide
("hidden icons") is toggled off/on around the read via the undocumented
`ITrayNotify` COM interface (`:229-248`). Icons added *after* our
startup arrive through the normal WM_COPYDATA path instead (both
windows exist; ours is topmost). We do not need this phase for the VM
target, only for the dev-machine coexistence scenario.

## 7. System icons (volume/network/power) - ShellServiceObject.cs

When running as the shell, ManagedShell instantiates the COM coclass
`SysTrayObject` (`{35CEC8A3-2BE6-11D2-8773-92E220524153}`) and calls
`Exec(CGID_SHELLSERVICEOBJECT, OLECMDID_NEW)` - the same
ShellServiceObject kickstart explorer performs, which makes Windows
register its built-in system icons. `OLECMDID_SAVE` on dispose. The
icons appear through the normal protocol (GUIDs are known constants,
`NotificationArea.cs:17-43`); Cairo hides the volume icon
(`VOLUME_GUID`) when running as shell and provides its own volume
widget instead. **For us: skip - we want our own plasma widgets, not
sndvol32-style system icons.**

## 8. Startup items (StartupRunner.cs - bonus)

Cairo runs `StartupRunner.Run()` when it is the shell
(`CairoApplication.xaml.cs:99-103`). It covers: `Run`/`RunOnce`/
`RunOnceEx` (HKLM+HKCU, 32-bit Wow6432Node variants), `Policies\Explorer\Run`,
Startup folders (machine+user), honours `StartupApproved\...` disabled
entries, deletes HKCU RunOnce values after execution, skips HKLM
RunOnce. This is the reference implementation for our
`session-shell.cmd` startup-items gap (`docs/explorer-parity.md` 3.2).

## 9. Adaptation plan for plasma-windows

We do not port C# code; we reimplement the same protocol in C++/Qt
inside the existing plasma stack. Proposed shape:

```
plasmashell (or a libplasma-adjacent module)
  +-- WindowsTrayHost (QObject, created at session start)
  |     +-- native hidden windows: Shell_TrayWnd + TrayNotifyWnd
  |     |     (RegisterClass + CreateWindowEx on the main thread;
  |     |      custom WndProc handles WM_COPYDATA directly - the Qt
  |     |      event loop runs on the same thread, so no pumping issues)
  |     +-- icon table: (hWnd, uID) | GUID -> {HICON, szTip, callback,
  |     |     version, hidden, placement}
  |     +-- per icon: QImage::fromHICON -> QImage
  |     +-- TaskbarCreated broadcast after creation
  |     +-- 100ms z-order guard (only while explorer coexists; skip
  |         in the VM where no other Shell_TrayWnd exists)
  +-- WindowsTrayModel (QAbstractListModel) exposed to QML
  +-- StatusNotifier bridge (preferred) OR custom QML widget:
        each Windows icon registers org.kde.StatusNotifierItem-<id>
        on the session DBus -> existing plasma system tray applet
        renders it (icons, tooltips, context menu) unchanged
  +-- click routing: plasma ActivateRequest/ContextMenuRequest ->
      NotifyIcon equivalent SendMessage(hWnd, uCallbackMessage, ...)
      (with AllowSetForegroundWindow beforehand)
  +-- balloons: NIF_INFO -> KNotifications (org.freedesktop.Notifications)
      + NIN_BALLOON* lifecycle reporting
```

Decisions to make:

1. **SNI bridge vs custom tray item**: the SNI route reuses the whole
   plasma tray UI (watcher already runs). StatusNotifierItem lacks
   fine-grained tooltips; acceptable. The custom route is more work but
   no protocol lossy-ness. Recommend SNI first.
2. **Where the host lives**: inside plasmashell keeps one process
   (window + tray icon table + DBus in one place); a separate daemon
   would survive plasmashell restarts. For M-level simplicity:
   plasmashell, revisit later.
3. **Skipped initially** (documented as later): AppBar message handling
   (dwData=0), `ShellServiceObject` system icons, explorer icon import
   (coexistence only), `Shell_NotifyIconGetRect` exact placement
   (answer with the icon's on-screen rect; needs QML position feed).

## 10. Spike checklist (what the protocol still needs proving on LTSC)

1. Register `Shell_TrayWnd` class + hidden window in a scratch Qt/Win32
   app; verify a real tray app's `Shell_NotifyIcon(NIM_ADD)` now returns
   TRUE (it returns FALSE today with explorer replaced).
2. Log `WM_COPYDATA` (dwData, SHELLTRAYDATA/NOTIFYICONDATA) from a real
   app; confirm `cbSize` for 32-bit apps (NOTIFYICONDATA is smaller -
   ManagedShell's fixed layout may misread; explorer handles both) and
   whether we must switch on `cbSize`.
3. Verify `TaskbarCreated` causes apps to re-add icons.
4. Verify click forwarding with a v3 app (wParam=UID) and a v4 app
   (wParam=msg) side by side.
5. In the VM only: full session with explorer replaced.

## 11. Pitfalls / notes for our port

* **Version semantics** are the #1 subtlety: v<=3 vs v4 swap
  wParam/lParam (`NotifyIcon.cs:511-529`). NIN_SELECT is sent for
  v>=3, WM_CONTEXTMENU for v>=3 - the docs say v4 but explorer does it
  for v3 too (`NotifyIcon.cs:479-500`).
* **`AllowSetForegroundWindow` before mouse-down** is required for
  context menus to appear.
* **SendMessage (sync) for clicks**, PostMessage only for the
  broadcast; deadlocks are possible if an app's callback blocks -
  explorer behaves the same way, so matching it is fine.
* **DPI**: tray window height uses `23 * DpiScale`; icon rendering must
  use the HICON's native size (16/32).
* **`FindWindow` ambiguity while explorer runs** - ManagedShell resolves
  it by z-order fights + message forwarding; in the VM the class is
  ours exclusively.
* **32-bit apps** send a smaller NOTIFYICONDATA; must validate on
  `cbSize` (ManagedShell does not, relying on same-bitness WPF hosts -
  verify in the spike).
* **Win10 LTSC specifics**: no Win11 action-center GUID overrides
  (`NotificationArea.cs:34-43` is Win11-only), `ITrayNotify` COM may
  behave differently - coexistence path is not critical for the VM.

## 12. Key file index (research/ mirrors)

* `ManagedShell-master/src/ManagedShell.WindowsTray/TrayService.cs`
* `ManagedShell-master/src/ManagedShell.WindowsTray/NotificationArea.cs`
* `ManagedShell-master/src/ManagedShell.WindowsTray/NotifyIcon.cs`
* `ManagedShell-master/src/ManagedShell.WindowsTray/NotificationBalloon.cs`
* `ManagedShell-master/src/ManagedShell.WindowsTray/ExplorerTrayService.cs`
* `ManagedShell-master/src/ManagedShell.WindowsTray/ShellServiceObject.cs`
* `ManagedShell-master/src/ManagedShell.Interop/NativeMethods.Shell32.cs`
  (NOTIFYICONDATA / SHELLTRAYDATA / WINNOTIFYICONIDENTIFIER / NIM / NIF /
  NIN / NIIF)
* `ManagedShell-master/src/ManagedShell.Common/SupportingClasses/StartupRunner.cs`
* `ManagedShell-master/src/ManagedShell.Common/Helpers/WindowHelper.cs`
* `cairoshell-master/Cairo Desktop/CairoDesktop.MenuBarExtensions/SystemTray*.xaml.cs`
* `cairoshell-master/Cairo Desktop/CairoDesktop.MenuBarExtensions/MenuBarExtensionsShellExtension.cs`

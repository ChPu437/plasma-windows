# Explorer parity - what a Plasma shell must provide on Windows

Status: analysis (2026-08-13, M4). Goal: define the minimum set of
explorer responsibilities that must land in the Plasma shell so the
replacement is not worse than explorer on Windows 10 LTSC 2021.

## 1. Explorer's responsibilities

1. **Desktop host**: wallpaper, desktop icons (shortcuts/files/folders),
   icon layout persistence, desktop context menu, redirection of
   `%USERPROFILE%\Desktop`.
2. **Taskbar**: running-window buttons, pinned apps, grouping, thumbnail
   previews, jump lists, show desktop, system tray
   (`Shell_NotifyIcon` protocol), multi-monitor taskbars.
3. **Start menu + search**: XAML processes (StartMenuExperienceHost)
   hosting Win+S search, pinned/recent apps, power menu.
4. **File manager (shell namespace)**: This PC, Recycle Bin, Control
   Panel, Network, `shell:` links, Quick access, drives; file operations
   via `IFileOperation`; third-party context-menu shell extensions
   (`IContextMenu`); OLE drag & drop.
5. **System-level shell services** (the part that is easy to forget):
   * startup items: `Run`/`RunOnce` registry keys and the Startup folder
     are executed by the shell at logon (a non-explorer shell does not
     get them for free),
   * `ShellExecuteEx` folder-open requests / `IShellWindows` / DDE
     "explore",
   * `RegisterShellHookWindow` / HSHELL messages,
   * `TaskbarCreated` broadcast (apps re-register taskbar/tray items
     after shell restart),
   * `SHChangeNotify` (desktop icon refresh), `WM_SETTINGCHANGE`
     broadcast responses,
   * system hotkeys: Win+D/E/R/number (Alt+Tab and Win+L are not its
     job).
6. **Notifications**: toasts (via StartMenuExperienceHost/WpnUserService)
   + tray balloons.
7. **Session behavior**: WM_QUERYENDSESSION handling, logoff dialog,
   window-state restore after reboot.

## 2. Parity checklist (Plasma side)

| Feature | explorer | Plasma now | Gap / implementation | Priority |
|---|---|---|---|---|
| Wallpaper / desktop window | yes | **done** (desktop containment) | - | - |
| Panel / taskbar window buttons | yes | **done (base)** (icontasks + WindowsWindowTasksModel) | pinning, grouping, thumbnails (`DwmRegisterThumbnail`), jump lists (`ICustomDestinationList`) | 2 |
| Show desktop | yes | **done** (KWindowSystem backend minimizes taskbar candidates) | - | - |
| Desktop icons | yes | no (folder containment exists, icons not populated) | FolderView applet or custom: layout persistence, drag & drop, context menu, `SHChangeNotify` refresh | 2 |
| System tray | yes | empty (SNI only) | **become the tray host**: register window of class `Shell_TrayWnd` and handle `WM_COPYDATA(NOTIFYICONDATA)` - see `tray-integration-research.md` section 3C2 | 1 |
| App launcher / search | Start menu + Win+S | **done** (kickoff + KRunner later) | - | - |
| Power menu (shutdown/reboot) | yes | no | Plasma power applet needs a Windows backend using `ExitWindowsEx` | 2 |
| Volume control | yes | UI exists, no backend (PulseAudio dep) | CoreAudio (`IAudioEndpointVolume`) backend | 3 |
| Notifications | toasts + balloons | KNotifications only | balloons via our tray host (NIN_BALLOON*); toasts have no public API for third-party shells - later | 3 |
| Startup items | yes | no | `session-shell.cmd` must enumerate `Run`/`RunOnce` keys + Startup folder | 1 |
| System hotkeys | yes | no | `RegisterHotKey`: Win+D/E/M; map Win+E to Dolphin | 2 |
| File manager | explorer.exe | Phase 4 Dolphin | KIO Windows backend: This PC / Recycle Bin / known folders / `shell:` links | 4 |
| Context-menu shell extensions | yes | no | `IContextMenu` integration - optional, later | 5 |
| HSHELL / shell hooks | yes | partial | KWindowSystem uses `SetWinEventHook` (enough for the taskbar); `RegisterShellHookWindow` optional | 3 |
| Window z-order / work area | yes | done (dev machine; VM pending) | - | - |

## 3. System protocol notes

### 3.1 System tray - becoming the notification-area host

`Shell_NotifyIcon` (in the calling process's shell32) resolves its
receiver by **window class name**: `FindWindowW(L"Shell_TrayWnd", NULL)`
and then `WM_COPYDATA` with `dwData = NIM_*` action and
`lpData = NOTIFYICONDATAW` (v3/v4 layout distinguished by `cbSize`).
When explorer is not running, a replacement shell can register that exact
class name and own the tray. Open-source proof: Cairo DE and RetroBar
both implement a working tray this way.

Minimal host duties:

* keep a per-`(hWnd, uID)` table: `HICON`, `szTip`, `uCallbackMessage`,
  `uVersion` (from `NIM_SETVERSION`),
* render `HICON` into the Plasma tray item (`QImage::fromHICON`),
* forward mouse events to `nid.hWnd` via `nid.uCallbackMessage`
  (`wParam = uID`, `lParam = mouse message`) - the app shows its own
  context menu,
* balloons (`NIIF_*`): older versions show a shell balloon (our own
  popup, or route into KNotifications); v4 apps send `NIN_BALLOON*` and
  draw their own UI,
* broadcast the `TaskbarCreated` message once the tray window exists so
  apps re-register their icons.

Caveats: the protocol is undocumented (evidence: ReactOS explorer,
Cairo, RetroBar - spike required on LTSC); ambiguous `FindWindow`
results while explorer is also running (co-existence phase, icons keep
going to explorer - acceptable); overflow/hide-inactive behavior must be
reimplemented.

### 3.2 Startup items

`HKCU\...\Run`, `HKCU\...\RunOnce`, `HKLM\...\Run` and the Startup folder
are executed by the shell at logon. `session-shell.cmd` must launch them
after the session services come up (or defer to a later milestone but
document the gap).

### 3.3 Hotkeys

Win+D (show desktop), Win+M (minimize all), Win+E (file manager), Win+R
(run dialog) are shell-registered. `RegisterHotKey` from plasmashell (or
kglobalacceld once it has a Windows backend); Win+E maps to Dolphin.

## 4. Deliberately NOT replaced (system-provided)

* Alt+Tab (DWM thumbnail switcher) - works without explorer; Win+Tab
  Task View is XAML/explorer and will be lost (KRunner/Kickoff cover it)
* Win+L, Ctrl+Alt+Del (Winlogon / SAS)
* Aero Snap, DWM composition, multi-monitor layout
* File dialogs and built-in context menus inside shell32
* `ShellExecuteEx` launching of files (associations resolve in shell32)

## 5. Suggested order

1. Tray host (`Shell_TrayWnd` window + NOTIFYICONDATA handling) - largest
   visible gap in the VM
2. Startup items + hotkeys (`session-shell.cmd` + `RegisterHotKey`)
3. Desktop icons (FolderView) + taskbar pinning/thumbnails
4. Power (`ExitWindowsEx`) and volume (CoreAudio) backends
5. Phase 4: Dolphin + KIO Windows backend
6. Optional: jump lists, `IContextMenu` shell extensions, toast bridge

## 6. Cross references

* `docs/tray-integration-research.md` - tray protocol research, new
  host approach in section 3C2
* `docs/roadmap.md` - open issues (thumbnails, kglobalaccel, ...)
* `docs/windows-port-notes.md` - KWindowSystem Windows backend design
* `docs/phase3-plan.md` - session bootstrap / dbus stack
* `README.md` - current status (M3/M4)

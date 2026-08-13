# Tray integration research - Windows tray icons into the Plasma system tray

Status: research (2026-08-13). **SHELVED** - the hook approach was
rejected (AV/anti-cheat risk); the only non-hook alternative (embedding
explorer's tray window, section 8) was parked. Documented limitation:
the plasma tray stays empty of native Windows icons; during the
explorer co-existence phase icons live in the explorer taskbar, and in
the VM (no explorer) Windows tray icons do not exist at all.

Update (2026-08-13): section 3C "become the notification-area host" was
re-evaluated - with explorer gone, a shell CAN host the tray by
registering a window of class `Shell_TrayWnd` (see section 3C2; what
Cairo DE and RetroBar do). Section 3C's "not possible" verdict only
holds while explorer is running. The host approach replaces hooking as
the preferred route for the VM scenario; it still needs a spike.

## 1. The problem

Plasma's system tray speaks the **StatusNotifierItem (SNI)** protocol over
DBus. Native Windows applications do not use SNI; they call
`Shell_NotifyIcon()` (Win32) which hands the icon to the **notification
area** owned by explorer's taskbar (`Shell_TrayWnd` -> `TrayNotifyWnd`).
The two worlds are completely unrelated, so the plasma tray shows only
SNI-capable items (basically nothing on Windows) while all Windows apps
(QQ, WeChat, Telegram, sync tools, ...) hide in explorer's tray.

## 2. Windows tray mechanics (what we have to interoperate with)

* Apps call `Shell_NotifyIcon(NIM_ADD, &nid)` with:
  * `nid.hWnd` - the app's own callback window (receives `WM_APP` messages:
    left/right click, balloon, etc.),
  * `nid.hIcon` - the icon (HICON),
  * `nid.uID` / `guid` - item identity,
  * `nid.szTip` - tooltip text.
* The OS (shell32) places the icon in the **notification area** of the
  **taskbar owned by explorer**. There is no public API to read the list
  of tray icons; the tray's contents are explorer-internal state
  (Win7+ tray buttons are custom-drawn, not child windows).
* **Without explorer** (the VM shell-replacement scenario): there is no
  notification area at all; `Shell_NotifyIcon(NIM_ADD)` **fails**
  (returns FALSE) and apps degrade in app-specific ways (some retry,
  some hide to the main window, some break).

## 3. Candidate approaches

### A. Hook `Shell_NotifyIcon` and bridge into SNI (recommended for study)

Intercept the API call in the *calling process* (Detours-style IAT hook
or a global API hook), translate each `NIM_ADD/UPDATE/DELETE` into an SNI
item (DBus `org.kde.StatusNotifierItem`), and let the plasma tray render
it natively.

* Pros:
  * Works **with or without explorer** (the hook does not depend on the
    notification area - it only needs the app to call the API, which it
    does in both environments). This is the only approach that covers the
    VM shell-replacement scenario.
  * Icon/tooltip/click messages map cleanly: `hIcon` -> image,
    `szTip` -> `Title`, `WM_APP` -> `ActivateRequest`/`ContextMenuRequest`.
* Cons:
  * Needs **per-process injection** (DLL into every tray-using app):
    AppInit_DLLs (registry, AV-sensitive), a launcher shim, or
    periodic process-watch + CreateRemoteThread. All have deployment
    costs and stability risks.
  * Must handle icon identity collisions (two items same hWnd/uID),
    legacy vs GUID `NIM_SETVERSION`, tooltips via `NIM_SETTIP`, and
    `Shell_NotifyIconW`/`A` variants.
  * Detours is MIT-licensed (fine) but is a new dependency.

### B. Enumerate explorer's tray via UI Automation (rejected for VM)

UI Automation exposes tray buttons (`UIA` peers) - names and basic
invocation work while explorer runs. But: no icon bitmap access, needs
polling, and **useless when explorer is replaced** (nothing to enumerate).
Only useful for the co-existence phase; dead end for the target
environment.

### C. Become the notification-area host (while explorer runs: not possible)

The notification area is explorer-internal; there is no supported way to
register a replacement host while explorer is running. `Shell_NotifyIcon`
always targets the shell's taskbar. Rejected - **but only for the
co-existence phase; see C2 for the explorer-free case.**

### C2. Become the tray host by impersonating `Shell_TrayWnd` (new candidate)

When explorer is **not** running, the "no receiver" problem disappears:
`Shell_NotifyIcon` in the calling process resolves its receiver by
**window class name** (`FindWindowW(L"Shell_TrayWnd", NULL)`) and sends
`WM_COPYDATA` with `dwData = NIM_*` action and `lpData = NOTIFYICONDATAW`
(v3/v4 layout via `cbSize`). A replacement shell can register that exact
class name and own the tray. This is how open-source shell replacements
implement a working tray without any hooking or injection:

* **Cairo Desktop** (`CairoSystemTray`): registers the `Shell_TrayWnd`
  class, creates a hidden window, handles `WM_COPYDATA(NOTIFYICONDATA)`.
* **RetroBar** (taskbar replacement): same technique.

Duties of the host (details in `explorer-parity.md` section 3.1):
per-`(hWnd,uID)` state table (HICON, tip, `uCallbackMessage`,
`NIM_SETVERSION` version), render `HICON` into the plasma tray item
(`QImage::fromHICON`), forward mouse events via `nid.uCallbackMessage`
(`wParam = uID`, `lParam = mouse message`; the app draws its own context
menu), balloons (`NIIF_*` / `NIN_BALLOON*` - own popup or KNotifications),
broadcast `TaskbarCreated` after the tray window exists.

* Pros: no injection, no API interception, AV/anti-cheat inert; works
  exactly in the target environment (VM, no explorer). Direct fix for
  "`Shell_NotifyIcon` returns FALSE" (section 2).
* Cons / risks (spike required on LTSC):
  * The protocol is undocumented - evidence base is ReactOS explorer,
    Cairo, RetroBar; must verify message routing on Win10 19044.
  * While explorer also runs (co-existence), `FindWindow` is ambiguous
    (both windows claim the class); icons keep going to explorer -
    acceptable, and irrelevant in the VM.
  * Reimplement overflow / hide-inactive-icons / balloon UI ourselves.
  * `NIM_SETVERSION` + GUID-based v4 items add protocol surface.

The full protocol study (how ManagedShell implements this, window
classes, WM_COPYDATA payloads, click forwarding semantics, balloons,
coexistence import, and our C++/Qt adaptation plan) is in
`docs/tray-host-cairo-study.md`; sources in `research/` (gitignored).

## 4. Why hooking is the (unwanted) consequence of the API shape

`Shell_NotifyIcon(NIM_ADD, &nid)` has **no receiver parameter**:
`nid.hWnd` is the *app's own* callback window, and the actual receiver
is implicit - the shell's notification area. shell32 talks to explorer's
taskbar over a private, undocumented protocol; there is no public API to
"point" the icon at another window, and no way to read the tray's
contents.

So the interception point can only be inside the calling process
(hooking), unless one either reverses the taskbar protocol (fragile,
version-coupled) or becomes explorer itself.

### What Wine does (and why it does not transfer)

Wine is a **full re-implementation of the Windows API**: a Windows app
under Wine calls *Wine's* shell32, which implements `Shell_NotifyIcon`
itself and translates the icon into an open protocol (X11 XEmbed system
tray, `_NET_SYSTEM_TRAY_S0`; on Wayland via XWayland/XEmbed - which is
why Wine trays are broken under Plasma 6 Wayland, which dropped XEmbed).

Neither side transfers to native Windows: we cannot *implement*
`Shell_NotifyIcon` (system apps load the system shell32), and Windows
has no open tray protocol on the output side. Wine's architecture is
only possible because it is the entire API surface.

## 5. Recommended route (as of 2026-08-13: hooking shelved, C2 preferred)

**Route A (hook -> SNI)** was the only route covering both the
co-existence and the VM scenario, but it requires per-process injection
(AppInit_DLLs is AV-flagged; process-watch+CreateRemoteThread is racy)
and API interception - **rejected for anti-cheat/AV false-positive
risk**. Shelved; revisit only if a non-injection interception mechanism
appears.

**Route C2 (impersonate `Shell_TrayWnd`, section 3C2) is now the
preferred route for the target environment** (VM without explorer): no
injection, no interception. Spike first:

1. **Spike** (~1-2 days, VM or dev machine with explorer killed):
   * Register class `Shell_TrayWnd`, create a hidden top-level window,
     log every `WM_COPYDATA` (`dwData`/`NIM_*`, struct size, `hWnd`,
     `uID`, icon handle, tip) from a real tray app (e.g. QQ, Telegram).
   * Confirm `Shell_NotifyIcon(NIM_ADD)` now returns TRUE (it fails
     today, section 2) and that callbacks can be forwarded.
   * Verify `TaskbarCreated` re-registration behaviour (kill/restart the
     host while a tray app runs).
2. **Tray widget backend** (if spike passes):
   * Qt class `TrayWindow` (Win32 native window) + per-icon state table,
   * `QImage::fromHICON` icon rendering into a plasma tray item,
   * mouse/balloon forwarding per `nid.uCallbackMessage`,
   * `TaskbarCreated` broadcast at startup.
3. **Deployment**: runs inside `plasmashell` (or a helper loaded at
   session start); VM test without explorer.

## 6. Alternative without hooking (parked): embed explorer's tray window

`FindWindow("TrayNotifyWnd")` + `SetParent` into the plasma panel
(reparenting the notification area into a plasma panel slot - the
technique tray-organizer tools use). No hooking, no API interception,
AV/anti-cheat inert.

* Covers only the **co-existence phase** (explorer running). In the VM
  (explorer replaced) there is no tray window to embed - and Windows
  apps cannot create tray icons there anyway (no notification area).
* Risks to verify in a spike: click/message routing (explorer still
  owns the tray), repaint/DPI, explorer restart behaviour.
* Parked: user decided to shelf tray work for now; revisit if the
  co-existence experience demands it.

## 7. Risks / open questions

* Injection reliability and AV interference (AppInit_DLLs is flagged by
  some AV products; process-watch+CreateRemoteThread is less invasive
  but racy).
* Apps that use GUID-based (`NOTIFYICON_VERSION_4`) identifiers and
  `NIM_SETVERSION` behaviour differences.
* Overlay icons / progress states (many apps use taskbar button overlay,
  not tray - out of scope; tray is the target).
* Whether to also surface balloon notifications (`NIIF_*`) as KDE
  notifications (later).

## 8. Conclusion (2026-08-13)

Shelved. Documented limitation: the plasma tray stays empty of native
Windows icons; during the co-existence phase icons live in the explorer
taskbar, and in the VM (no explorer) Windows tray icons do not exist at
all (apps' `Shell_NotifyIcon` calls fail). SNI-aware apps (none known)
would still appear via the already-running SNI watcher.

Re-evaluated same day: for the VM (explorer gone), the **host
impersonation route (section 3C2)** should fix exactly that - a
`Shell_TrayWnd` window receives the `NOTIFYICONDATA` traffic. Hooking
stays shelved; C2 needs a spike before implementation. See also
`docs/explorer-parity.md` for the full explorer-parity checklist.

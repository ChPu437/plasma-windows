# Titlebar research - making third-party Windows apps use a Plasma title bar

Status: research report (2026-08-13), R1 of a three-phase plan (R1 source
research, R2 VM spikes, R3 integration decision - R2/R3 deferred to
separate sessions). Scope decided with user: research all three
approaches L1/L3/L4 (L3 with a risk assessment), target apps = whitelist
of common apps. Sources cloned into `research/` (gitignored).

**2026-08-14 progress**: the L4 caption-removal engine (R1.3 core, the
"style surgery" building block) is implemented as `src/titlebar/titlebar.c`
(`tools/build-titlebar.cmd`, pure Win32): taxonomy classification
(A/B/C/none per section 4.6), `remove`/`restore` with cross-process style
persistence via window properties, and a `--watch` mode
(`EVENT_OBJECT_SHOW` + process whitelist -> auto-decorate). Verified on
notepad (classify A, remove, cross-process restore round-trip) and in
watch mode.

**2026-08-14 evening (R1.4)**: the overlay bar is now Breeze-styled and
fully interactive:

- owned window of the target (DWM keeps it above the owner, lowers it
  with the owner on other-window activation - no z-order races, no
  floating over other apps), click activates the owner (standard title
  bar behavior, no WS_EX_NOACTIVATE)
- manual maximize/restore (never SC_MAXIMIZE: the system-owned maximize
  raised the target over the bar and its restore rect tracked the moved
  rect, shrinking the window on every maximize cycle); drag of a
  maximized window restores it anchored to the cursor
- smooth drag: DeferWindowPos atomic target+bar moves, no re-entrant
  drag (WM_TIMER path), async event reposition skipped while dragging
- Breeze light palette (#EFF0F1 bar, #232629 text/glyphs, hairline
  border), three buttons (minimize / maximize-restore / close) with
  TrackMouseEvent hover (gray; close hover #E81123 with white glyph)
- `--watch` spawns a per-window overlay child so whitelisted apps get
  the full Breeze bar automatically

Drag-stall investigation (2026-08-14): the panel Floating-style
animation stalled window dragging - fixed by disabling floating in the
panel config, see windows-port-notes.md section 8.

Still deferred to R2 (VM spikes): a full QML Plasma title bar
(PlasmaQuick-based), edge-snap without explorer, and the B-detection
heuristic validation on real apps (open questions 2/5/8/10).

**Known issue (2026-08-14, unfixed)**: the IME candidate window
(TextInputHost, `Windows.UI.Core.CoreWindow` full-screen, vis=True)
does not display while the plasma shell is the active shell (explorer
closed). IME services are healthy (ctfmon/TextInputHost running, TSF
contexts per app). Suspected z-order/covering relationship with the
plasma desktop window; survives clean session restarts. Needs a live
debug session (typing while enumerating the candidate window's
z-order/placement). Tracked; not blocking.

## 1. The three approaches

| Approach | Mechanism | Interactive Plasma chrome? | Injection? |
|---|---|---|---|
| **L1 styling** | cross-process `DwmSetWindowAttribute` / `SetWindowCompositionAttribute` | No - styles the native caption only | No |
| **L3 injection** | per-process DLL + `GWL_WNDPROC` subclassing / function hooks | Yes - full | Yes |
| **L4 overlay** | cross-process style surgery (`GWL_STYLE`) + Plasma overlay window per app | Yes | No |

L2 (DWM injection, e.g. DWMBlurGlass) was excluded by decision.

---

## 2. R1.1 - L1 styling (safe, no injection)

Reference: **MicaForEveryone** (MIT) - the canonical "third-party title
bar styling" tool. Pure cross-process attribute setting; no injection.

### 2.1 Architecture (MicaForEveryone)

* Window watching: `SetWinEventHook(EVENT_OBJECT_SHOW)` filtered to
  `OBJID_WINDOW`, with a 10 ms retry for windows that are not yet
  queryable (`WindowOpenedEvent.cs`). On rule/settings change, a full
  `EnumWindows` pass re-applies (`RuleService.cs:79-106`).
* Rule engine: global / per-process / per-class rules, priority ordered
  (`RuleService.cs:53-77`), matched on hwnd/title/class/process
  (`TargetWindow.cs:21-36`).
* Window filter `IsWindowPatternValid()` (`Window.cs:433-461`): keep
  windows that are `WS_EX_APPWINDOW`, or top-level with a real title bar
  (`WS_BORDER|WS_DLGFRAME`); drop toolwindows/popups without captions,
  `WS_EX_NOACTIVATE`, zero-style windows.
* Apply: pure attribute calls (`TargetWindow.cs:91-105`).

### 2.2 Attribute availability matrix (critical: target = Win10 19044)

From `DesktopWindowManager.cs:17-36` (build gates):

| Attribute | DWMWA# | Min build | On 19044? |
|---|---|---|---|
| `DWMWA_USE_IMMERSIVE_DARK_MODE` | 20 | 19041 | **YES - dark caption** |
| `DWMWA_WINDOW_CORNER_PREFERENCE` | 33 | 22000 | no |
| `DWMWA_CAPTION_COLOR` | 35 | 22000 | **no (Win11)** |
| `DWMWA_TEXT_COLOR` | 36 | 22000 | **no (Win11)** |
| `DWMWA_SYSTEMBACKDROP_TYPE` | 38 | 22523 | no |
| `DWMWA_MICA` | 1029 | 22000 | no |
| `DwmEnableBlurBehindWindow` | - | any | **yes** |
| `SetWindowCompositionAttribute` accent policy (blur/acrylic + gradient tint) | - | any | **yes** (already used in our kwindowsystem backend) |
| `DwmExtendFrameIntoClientArea(MARGINS(-1))` | - | any | **yes** |
| `SetWindowThemeAttribute(WTA_NONCLIENT)` `WTNCA_NODRAWCAPTION/NODRAWICON/NOSYSMENU` | - | any | **yes - can hide native caption drawing without style surgery** (useful for L4 too) |

So on LTSC 2021, L1 can deliver: **dark Plasma-toned captions +
acrylic/blur tint across the whole window including the caption + no
caption drawing (WTNCA)**. It cannot deliver per-window caption colors
or rounded corners - those are Win11-only attributes.

The `EnableBlurBehind` recipe from MicaForEveryone
(`DesktopWindowManager.cs:165-187`): `DwmEnableBlurBehindWindow` +
`SetWindowCompositionAttribute(WCA_ACCENT_POLICY)` with
`ACCENT_ENABLE_BLURBEHIND | ACCENT_ENABLE_GRADIENT` and a
`GradientColor` (ARGB tint) - this is exactly the accent application
our KWindowSystem backend already implements for Plasma's own windows;
extending it cross-process is the L1 work.

TranslucentTB confirms cross-process accent application works in
practice: it applies `SetWindowCompositionAttribute` to explorer's
taskbar window (`taskbarattributeworker.cpp:602`).

---

## 3. R1.2 - L3 injection (full chrome, with risk assessment)

Two distinct injection families found in the references.

### 3.1 Open-Shell - hook-based injection

`SetWindowsHookEx(WH_GETMESSAGE)` on the target GUI thread makes the
system load the hook DLL into the target process; the hook callback
then runs arbitrary init code (`StartMenuDLL.cpp:4324-4329`,
`HookInject` -> `InitStartMenuDLL`). Plus `WH_CBT` for new-window
events and per-thread hooks for progman/AppManager. DLL is loaded by
normal `LoadLibrary` mechanics - visible in the process module list.

### 3.2 Windhawk - shellcode injection (the mature framework)

`engine/dll_inject.cpp` + `all_processes_injector.cpp`:

* `VirtualAllocEx` + `WriteProcessMemory` of hand-assembled x86/x64/
  ARM64 shellcode that resolves `LoadLibrary`/`GetProcAddress`/etc.
  manually (no hardcoded addresses), then:
  * new processes: **APC injection** - `NtQueueApcThread` into the
    suspended main thread (`InjectIntoNewProcess`, line 645), with
    Wow64 APC encoding for 32-bit targets,
  * running processes: **`NtCreateThreadEx`** remote thread (native
    API, stealthier than `CreateRemoteThread`), with a
    `CreateRemoteThread` fallback (`dll_inject.cpp:795-826`).
* Settings to opt out of critical processes / games / "incompatible
  programs" (`all_processes_injector.cpp:398-414`).
* Mods API: `Wh_SetFunctionHook` (inline hooks) + thread-local
  state - mods are in-process C/C++/Rust DLLs compiled on the fly.

### 3.3 Risk assessment

| Risk | Assessment |
|---|---|
| **AV/EDR detection** | HIGH. Hook-based injection = module load into every process (AppInit_DLLs is flagged by most AV; WH_GETMESSAGE hooks are monitored by EDRs). Shellcode injection (VirtualAllocEx+WriteProcessMemory+NtCreateThreadEx) is a textbook malware pattern; Windhawk only survives because it is widely known and signed - a custom injector would need a signing story and likely an AV exclusion process. |
| **Anti-cheat** | HIGH. VAC/EAC/BattlEye actively scan for injected modules and window-hook chains. Any L3 approach is incompatible with games/anti-cheat software - the whitelist must exclude them. |
| **Stability** | MEDIUM. `SetWindowLongPtr(GWL_WNDPROC)` chains are per-app fragile; a crash in the injected DLL takes down the target app. WindowBlinds (commercial) runs a driver + signed subclassing stack - years of edge-case fixes. Borderless-Gaming's per-engine delays (Unreal/GameMaker, `Manipulation.cs:388-394`) show app-specific quirks are the norm. |
| **Maintenance** | HIGH over time. Windows feature updates can change NC behavior; every whitelist app is a compat surface. |
| **Deployment** | MEDIUM. We control session start (shell), so a loader can inject at login; elevated processes (UIPI) need a matching-integrity loader or exclusion. |
| **Precedent** | WindowBlinds (commercial, works), Windhawk (open source, works), Open-Shell (open source, works in explorer). So the technique is proven - the question is purely risk posture. |

**Verdict**: technically feasible and proven, but it re-opens the same
AV/anti-cheat risk that rejected the tray hooking route
(`docs/tray-integration-research.md` section 5). **Recommended: do not
pursue** unless the user explicitly accepts the risk posture and the
signing/deployment burden. L4 is the no-injection path to the same
goal; L1 is the zero-risk subset.

---

## 4. R1.3 - L4 caption removal + Plasma overlay (no injection)

Two building blocks, both cross-process safe and both proven by
reference projects.

### 4.1 Style surgery (remove the native caption)

**Borderless-Gaming** (`Manipulation.cs:66-93, 143-145`): read styles
with `GetWindowLong(GWL_STYLE/GWL_EXSTYLE)`, clear
`WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX |
WS_MINIMIZEBOX` (+ ex-style `WS_EX_*EDGE|WS_EX_LAYERED|WS_EX_TOOLWINDOW|
WS_EX_APPWINDOW`), reapply with `SetWindowLong`, then
`SetWindowPos(SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER |
SWP_NOSENDCHANGING)` so the frame recalculates. Originals are saved
for restore. **ihateborders** (Rust) does the same
(`window_manager.rs:172-203`, `WS_BORDER|WS_CAPTION|WS_THICKFRAME|
WS_DLGFRAME`).

Alternative that avoids style surgery: `SetWindowThemeAttribute
(WTA_NONCLIENT, WTNCA_NODRAWCAPTION|WTNCA_NODRAWICON|WTNCA_NOSYSMENU)`
(`MicaForEveryone` PInvoke `WTNCA.cs`) - the caption is not drawn but
the non-client area still exists. Both variants must be compared in
the spike (client-area growth vs reserved NC space, maximized
behavior).

### 4.2 Message toolbox (all cross-process safe, verified in AltSnap)

AltSnap (single-file C window manager) proves the message set:

* `SendMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE|SC_MAXIMIZE|SC_CLOSE,
  0)` - lines 1842, 1854, 2590.
* `IsZoomed(hwnd)` for maximize state (line 747); custom maximize/
  restore via `WINDOWPLACEMENT` + `SetWindowPos` to monitor size
  (lines 1324, 3626) - needed because after caption removal the native
  maximize may not be desired.
* Snap/zones via `BeginDeferWindowPos`/`DeferWindowPos`/
  `EndDeferWindowPos` (lines 901-976) - template for our own snap.
* Double-click handling pattern at line 301-303.

Classic drag trick (not in AltSnap, it is keyboard-driven): cross-
process `SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION,
MAKELPARAM(x,y))` enters the target's own modal move loop (in
`DefWindowProc`), so drag + Win10 edge snap happen in the target
process. **Whether edge snap still works without explorer must be
verified in the VM** (snap-assist UI is explorer/XAML; the modal-loop
snap itself is user32).

Resize: keep `WS_THICKFRAME` or send `WM_NCLBUTTONDOWN` with
`HTBOTTOMRIGHT` etc. - same mechanism as drag.

### 4.3 Dragging and interaction via the overlay (yes, fully covered)

The overlay does not just draw - it receives all mouse input on the
former caption area, so move/resize/gestures are implementable. Two
mechanisms:

**A. Native message trick (preferred)**: `SendMessage(target,
WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(x,y))` makes the *target's own*
`DefWindowProc` enter its modal move loop (SetCapture + GetMessage
loop until button-up; the call blocks our thread for the drag
duration - acceptable, the overlay is repositioned meanwhile by the
SetWinEventHook thread that our KWindowSystem backend already runs).
Free features: native move animation, double-click HTCAPTION =
maximize/restore (`WM_NCLBUTTONDBLCLK`), and (to verify in the VM
without explorer) Win10 edge snap from the user32 modal loop. Caveat:
`SendMessage` from the overlay blocks until the drag ends, so the
overlay must not need to react mid-drag.

**B. Manual drag (fallback + full control)**: our own loop - record
cursor offset, poll `GetCursorPos` (timer or the win-event thread),
`SetWindowPos` each tick, clamp to monitor, implement our own snap
zones with `BeginDeferWindowPos`/`DeferWindowPos`/`EndDeferWindowPos`
(AltSnap lines 901-976). Needed for apps that override
`WM_NCLBUTTONDOWN` (custom-chrome frameworks), and gives us Plasma-style
snap zones/gestures that native doesn't. AltSnap also shows the
maximized-drag nuance: dragging a maximized window must first restore
it to its previous size (lines 1324, 3626) - copy that logic.

Resize is the same pair: `WM_NCLBUTTONDOWN` with
`HTLEFT/HTRIGHT/HTTOP/HTBOTTOM/HTBOTTOMRIGHT...` into the target's
modal resize loop, or manual `SetWindowPos` with min-size clamping.
The overlay covers the top edge, so the top resize edge must be
hit-tested by the overlay itself (forward `HTTOP` to the target or
manual).

Bonus: because the overlay owns the input, gestures that the native
caption never had become possible - shake-to-minimize, drag anywhere
on the window, scroll-wheel volume on the bar, snap-to-zone drag
preview, etc.

### 4.4 The overlay window

* One Plasma-styled (QML, frameless, `WS_EX_NOACTIVATE` +
  `WS_EX_TOOLWINDOW`) top-level window per decorated target.
* Z-order: keep it directly above the target with
  `SetWindowPos(hwndAfter=target)` on every
  `EVENT_OBJECT_LOCATIONCHANGE/REORDER/SHOW/HIDE/MINIMIZE` - our
  KWindowSystem backend already tracks create/destroy/show/hide/
  foreground via `SetWinEventHook`; the overlay needs the
  location-change and minimize events added to that infrastructure.
* Title text: `GetWindowTextW` + `EVENT_OBJECT_NAMECHANGE`.
* Activation: never activate the overlay (`WS_EX_NOACTIVATE`); buttons
  act on the target via the message toolbox; use
  `AllowSetForegroundWindow(pid)` before `SendMessage` when a click
  must give the target focus.
* Exclusions: UWP/cloaked windows (`DWMWA_CLOAKED`, already used in
  our backend), elevated windows (UIPI blocks our messages), layered
  windows, apps painting custom NC.

### 4.6 Window taxonomy - which windows L4 can decorate

Not all windows have a "native caption to remove". Three categories:

**A. Standard caption windows** (DefWindowProc non-client area):
Notepad, classic dialogs, 7-Zip, classic Win32 apps. L4 fully
applicable: WS_CAPTION surgery + overlay + HTCAPTION drag. This is
the L4 target set.

**B. Client-side-decorated windows** - the app draws its own chrome in
the *client* area: Electron (`titleBarStyle: hidden/custom`), WPF
WindowChrome, Chromium custom title bars, most "modern" webview apps
(VS Code, Slack, Discord, Teams, QQ NT, browsers in custom-titlebar
mode). This is the user's concern, and it is valid:

* Style surgery is useless: there is no native caption to remove -
  their own WndProc answers `WM_NCCALCSIZE` with a zero-size NC area,
  the client already covers the full window rect.
* Overlaying a Plasma bar on top is **actively harmful**: it covers
  their tab strips / menu / profile buttons, which are client-drawn
  and cannot be removed from outside - the result is double chrome
  and broken app UI.
* Per-app *configuration* matters, not just identity: an Electron app
  shows a native caption (`titleBarStyle: default`) or its own
  (`hidden`); Edge on Win10 defaults to the native caption but has
  self-drawn tabs-in-titlebar modes. The whitelist must be per
  (process, detected-mode).
* **Policy: exclude from decoration.** They are already
  "self-decorated" - treat them like KDE apps (self-decorated by
  definition). L1 styling can still dark-mode them if they do not
  theme themselves, but with double-backdrop caution.

**C. Hybrid / frame-extended windows**: apps that extend their frame
into the client area (`DwmExtendFrameIntoClientArea` negative margins,
Mica-style) while keeping native caption buttons. Caption removal
changes their client rect in ways the app did not plan for - exclude.

Detection heuristics (run per window before any surgery, both at
startup sweep and on each show event):

* **client-decorated (B)**: screen-mapped `GetClientRect` ==
  `GetWindowRect` while `WS_CAPTION` is still set (their
  `WM_NCCALCSIZE` zeroed the NC area); alternatively
  `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` == window rect
  with zero caption inset.
* **standard (A)**: client rect smaller than window rect by
  (caption + frame) - the normal case.
* Borderless-Gaming's `WindowHasTargetableStyles()` is the same
  spirit of pre-flight style validation; ours adds the rect test.

Consequence for the overlay manager: the win-event pipeline runs this
classification before applying anything; v1 policy decorates only
windows that pass the standard-caption check AND match a whitelist
rule.

### 4.7 Synergies with what we already have

* Window tracking: reuse the KWindowSystem `SetWinEventHook` backend.
* Work area: our panel already manages `SPI_SETWORKAREA` - maximized
  decorated windows can use our work area (native caption removal
  makes the whole rect usable).
* Tray: Borderless-Gaming's taskbar-hide/`WM_MOUSEMOVE`-over-tray
  refresh trick (`Manipulation.cs:577-613`) applies to our future
  tray host too.

---

## 5. Open questions for R2 (VM spikes)

1. `WTNCA_NODRAWCAPTION` vs `WS_CAPTION` removal - behavior diff on
   19044 (maximized, client-area growth, DWM blur over the old
   caption rect).
2. Does `WM_NCLBUTTONDOWN/HTCAPTION` drag + edge snap work with
   explorer replaced (snap lives in user32 modal loop or in
   explorer?).
3. Double-click HTCAPTION -> maximize without caption styles.
4. Resize via HT\* messages after caption removal.
5. Overlay z-order vs owned/modal windows of the target.
6. Accent blur applied cross-process (L1) over the overlay region -
   translucency over blurred background.
7. Whitelist app matrix: notepad, calculator, paint, notepad2, 7-Zip,
   browsers (Edge/Chrome/Firefox), file dialogs.
8. Drag: confirm the HTCAPTION modal-loop trick end-to-end through the
   overlay (button-down on overlay -> SendMessage -> move -> snap ->
   button-up), and the maximized-drag restore behavior; decide
   native-trick vs manual-drag for v1.
9. Apps that override `WM_NCLBUTTONDOWN` (custom chrome) - verify the
   manual-drag fallback on one such app.
10. Validate the B-detection heuristic on real apps: VS Code (custom
    title bar mode), Edge (native mode vs tabs-in-titlebar mode),
    Slack/Discord - confirm the client==window-rect test classifies
    correctly and does not misclassify A windows.

## 6. Sources (research/ mirrors)

* `MicaForEveryone-master` - L1 reference (MIT)
* `TranslucentTB-release` - cross-process accent precedent (GPL-3)
* `windhawk-main` - L3 shellcode injection framework (GPL-3)
* `Open-Shell-Menu-master` - L3 hook-based injection (MIT)
* `Borderless-Gaming-master` - L4 style surgery (GPL-3)
* `ihateborders-main` - L4 style surgery, minimal (MIT)
* `AltSnap-main` - L4 message toolbox + snap (GPL-3)
* See also `docs/shell-context-menu-study.md`, `docs/tray-host-cairo-study.md`

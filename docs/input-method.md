# Input methods without Explorer

Goal: IME (Chinese etc.) must work when plasmashell replaces
explorer.exe as the shell, with no Explorer taskbar/tray.

Status: 2026-08-15 - research report added (section "Research report");
two make-or-break spikes pending (see section "Verification spikes").

## Mechanism (Windows 10)

- Text services (TSF) are hosted by `ctfmon.exe` and
  `TextInputHost.exe`, which are **independent of Explorer**: they are
  started by the logon/userinit path (Task Scheduler / registry
  `Run`/service), not by the shell.
- Verified on the dev machine: both processes run while Explorer is
  the shell; nothing in the Plasma session starts or stops them.
- IME switching (Ctrl+Shift / Win+Space) is system-global keyboard
  handling, not Explorer UI.
- What Explorer provides that we lose: the language-bar indicator in
  the tray and the candidate window positioning contract (TSF
  candidate windows are topmost and work without Explorer).

## What to verify in the VM (no Explorer, plasmashell as shell)

1. After logon, `ctfmon.exe` and `TextInputHost.exe` are running
   (tasklist). If not, start `ctfmon.exe` from the session bootstrap.
2. Type in a Plasma text field (kickoff search, Dolphin address bar):
   Chinese IME candidate window appears and commits text.
3. Switch IME with the keyboard shortcut (needs a preload in
   `HKCU\Keyboard Layout\Preload` - first login normally populates it;
   check after the first shell-replacement login).
4. Optionally add a panel indicator: `org.kde.plasma.keyboardlayout`
   applet is X11-oriented and was excluded from the M3.1 build scope;
   a minimal native indicator (QML + TSF GetKeyboardLayout) can be
   added later if shortcut-only switching is not enough.

## Notes

- Do NOT set `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
  entries from the shell; ctfmon is managed by the system.
- If the IME is missing entirely after logon (no Preload key), running
  `ctfmon.exe` once (or a relogin) usually fixes it.

## Open issue: candidate window / number-key selection (2026-08-15)

**Status: deferred - direction chosen, not implemented.**

On Windows 10 the IME candidate window is hosted by `TextInputHost.exe`
(Client.CBS package, AppId "InputApp"), which is only activated from an
explorer-based input platform session. In a clean plasma-shell logon
(no explorer) it never starts:

- Direct `CreateProcess` of the SystemApps exe exits immediately.
- `IApplicationActivationManager::ActivateApplication` returns
  `0x87b20c15` (activation framework refuses; deps are all present).
- `ms-inputapp:` protocol activation fails with the same error.
- Cairo Desktop has the same problem (language bar / IME UI issues
  #212 open since 2018, #237 closed as duplicate) - it is a Win10
  system limitation, not a shell bug.

Consequences in a clean plasma session: the Microsoft IME candidate
window never appears, number-key selection stops working (only Space
commits the default candidate), and Win+Space switching is unreliable.
Even with TextInputHost running (explorer had logged in before), the
candidate window could be covered (z-order suspicion vs the plasma
desktop window, titlebar-research.md 2026-08-14, unfixed).

**Chosen direction (user, 2026-08-15):** build an independent frontend
for the Windows IME instead of relying on TextInputHost - i.e. talk to
TSF ourselves (a TSF text service / UI element sink, or a lightweight
IMM32-style composition window) and render the candidate list in our
own window. If that proves too complex, mitigate with a third-party
IME (Sogou etc. draw their candidate windows in-process and do not
depend on TextInputHost).

**Already fixed alongside:** imewin indicator read the shell thread's
HKL (stuck EN/中 display) - now samples the remembered user window
(imecontroller.cpp).

---

## Research report (2026-08-15)

Survey of existing documentation and projects for "render the IME
candidate panel ourselves when TextInputHost cannot run". Sources:
Microsoft Learn (TSF, Shell Launcher), GitHub searches (TextInputHost,
ms-inputapp, 0x87b20c15), Cairo issue #212, EasyIME/PIME,
Windows-classic-samples; local mirrors in `research/`.

### 1. Verified facts

1. **No public solution exists** for activating TextInputHost without
   explorer. GitHub issue/repo search for `TextInputHost`,
   `ms-inputapp` returns nothing relevant (only unrelated items). Our
   `0x87b20c15` activation failure is the state of the art - the
   input-platform session is started by the explorer input platform.
2. **Microsoft Shell Launcher docs** (the official "replace the shell"
   feature) contain no IME guidance and no IME limitation list - the
   problem is simply unaddressed by Microsoft documentation.
3. **Cairo Desktop issue #212** (2018, language-bar indicator) is the
   same problem family, still open; #237 closed as duplicate. Cairo
   users work around with third-party IMEs.
4. **Games render their own candidate panels** (industry-standard,
   documented approach): they read candidate data in-process via
   **IMM32** (`ImmGetContext` -> `ImmGetCandidateListW` /
   `ImmGetCompositionStringW`, plus `WM_IME_STARTCOMPOSITION` /
   `WM_IME_COMPOSITION` / `WM_IME_NOTIFY` handling), suppress the
   native UI by clearing `ISC_SHOWUICANDIDATEWINDOW` /
   `ISC_SHOWUICOMPOSITIONWINDOW` in `WM_IME_SETCONTEXT`, and draw the
   panel with their own renderer. The IMM32 API surface works for TSF
   IMEs (incl. Win10 Microsoft Pinyin) through the built-in
   IMM32<->TSF compatibility layer. **The native candidate window does
   not need to be rendering** - only the candidate *data* matters.
   Constraint: IMM32 state is per-thread/per-process; reading must
   happen inside the app that owns the focused window.
5. **TSF UI elements**: the IME (in the app process) creates candidate
   UI elements (`ITfCandidateListUIElement`) and publishes them on the
   thread's UI element manager (`ITfUIElementMgr`); TextInputHost is
   only the *renderer* of those elements. Any code in the same thread
   can enumerate them via `ITfUIElementMgr::EnumUIElements` - in
   principle even when no renderer exists.
6. **Third-party IMEs (Sogou, Weasel, PIME...) are TSF text services**:
   registered via `ITfInputProcessorProfiles::Register` (+ language
   profiles), the **system loads their DLL into every GUI process**
   that activates TSF. This is an official mechanism, not injection.
   Text services need not be IMEs (text expanders like PhraseExpress
   use the same registration), but non-selected services'
   load/activation behavior is unverified (spike 2).

### 2. Candidate solution matrix

| # | Solution | Coverage | Mechanism | Status |
|---|---|---|---|---|
| **A** | Qt-layer IMM32 integration | our Qt apps only | patch `QWindowsInputContext` (WM_IME handling) to read candidates + expose to QML; QML candidate panel; position via `QInputMethod::cursorRectangle` | recommended fallback; game-proven pattern |
| **B** | TSF helper text service + IPC + overlay frontend | **all apps** | registered text service DLL (Sogou-style, system-loaded); in-process `ITfUIElementMgr::EnumUIElements` + `ITfContextView::GetTextExt` for position; forward over IPC (WM_COPYDATA / named pipe) to a plasmashell-hosted QML overlay window (`WS_EX_NOACTIVATE`) | preferred; blocked on spikes 1+2 |
| C | third-party IME (Sogou etc.) | all apps | in-process candidate UI, no TextInputHost | immediate mitigation, already documented above |
| D | fix "covered" z-order when TextInputHost does run | sessions that logged in via explorer | our window management keeps the candidate window above the desktop/panel | cheap, do regardless |
| E | TSF UI-element sink inside our own apps | our Qt apps only | heavier COM than A | superseded by A |
| F | IMM32 for third-party apps (games-style) | - | cross-process IMM32 read does not exist | impossible |

B and A compose: B covers everything including our apps; A is the
guaranteed hedge if spike 1 or 2 fails.

### 3. Scheme B architecture

```
every GUI process (system-loaded)            frontend (plasmashell)
+------------------------------+   IPC   +--------------------------------+
| HelperTSF.dll                | ------->| QML candidate panel overlay    |
|  ITfTextInputProcessor       | WM_COPYDATA | WS_EX_NOACTIVATE top-level |
|  + ITfThreadMgr (given at    | or pipe | position from GetTextExt coords|
|    Activate)                 |         | Plasma-styled, number keys    |
|  + ITfUIElementMgr::EnumUIElements ->  | selection mirrored from       |
|    ITfCandidateListUIElement |         | IME's GetSelection            |
|  + ITfContextView::GetTextExt (pos)   | (IME itself handles keys)     |
+------------------------------+         +--------------------------------+
```

Input interaction stays in the IME (it is the active text service and
handles number/arrow/enter); the helper only mirrors selection state.

### 4. The two make-or-break questions (spikes)

1. **Does MS Pinyin create candidate UI elements when no UI host
   (TextInputHost) is running?** Element creation is the IME's internal
   composition logic, so it *should* be independent of the renderer -
   but if elements are only created when a UI host is present, scheme B
   collapses. Probe: standalone Win32 app enumerating
   `ITfUIElementMgr` while typing with MS Pinyin, with TextInputHost
   killed.
2. **Does the system load/activate a registered text service that is
   NOT the selected input method?** TSF activates services by language
   profile; if only the selected IME is loaded, a helper service never
   runs. Probe: minimal registered `ITfTextInputProcessor` writing a
   marker file; check load in notepad while another IME is active
   (registry change - VM only).

### 5. Verification spikes (status below)

* `probe/ime-probe` - risk 1 (dev machine, no system changes; kills
  TextInputHost temporarily)
* `probe/tsf-service-test` - risk 2 (VM; regsvr32 registers a minimal
  text service - registry change, VM only)

### 6. AV/EDR posture (to research after spikes)

Registering a system-loaded DLL in every process is behaviorally
similar to malware to heuristic scanners even though the mechanism is
official (IMEs do this daily). Research angle: open-source IME
projects' (PIME, Weasel, iBus on Windows) experience with AV
misdetection, code-signing requirements. Pending spike results.

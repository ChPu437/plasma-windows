# Input methods without Explorer

Goal: IME (Chinese etc.) must work when plasmashell replaces
explorer.exe as the shell, with no Explorer taskbar/tray.

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

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

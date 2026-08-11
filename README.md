# Plasma Windows

A Qt-based shell executable (`shell.exe`) that proves our own executable can
serve as the Windows desktop shell. See `agents.md` for the full project
plan.

Status: **Phase 1** (Qt shell) implemented; pending verification in the VM.

## Scope

Phase 1: a minimal Qt 6 shell with the same lifecycle as the Phase 0 Win32
shell:

* creates a top-level window covering the primary monitor work area
* processes the normal Qt event loop
* accepts keyboard (`ESC`/`Alt+F4` to quit) and mouse input
* exits cleanly
* logs startup information, errors and exit codes (`--debug` switch)

No KDE/Plasma dependencies yet.

## Layout

```
plasma-windows/
    CMakeLists.txt
    src/
        main.cpp
    tools/
        switch-shell.cmd       (Phase 0.5 shell switcher)
        shell-registry.ps1
    README.md
```

## Requirements

* Windows 10 (target: 10 IoT Enterprise LTSC 2021, build 19044)
* Visual Studio 2022 Build Tools (MSVC v143)
* Windows SDK 10.0.19041
* CMake 3.21+
* Ninja
* Qt 6.11.1 MSVC 2022 64-bit (this machine: `E:\Qt\6.11.1\msvc2022_64`).
  Plasma 6.7 (the Phase 3 target) requires Qt >= 6.10.

## Build

From a **Developer Command Prompt for VS 2022** (or PowerShell after running
`vcvars64.bat`):

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=E:\Qt\6.11.1\msvc2022_64
cmake --build build
```

The executable is produced at `build/shell.exe`.

Example with the VS Developer PowerShell helper:

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$env:CMAKE_PREFIX_PATH = "E:\Qt\6.11.1\msvc2022_64"
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build"
```

Adjust `$vs` and `CMAKE_PREFIX_PATH` if the tools are installed elsewhere.

The CMake cache pins `CMAKE_SYSTEM_VERSION=10.0.19041`; override it with
`-DCMAKE_SYSTEM_VERSION=10.0.xxxxx` if that SDK is not installed on your
machine.

## Run

```bat
build\shell.exe            :: normal run
build\shell.exe --debug    :: verbose debug logging
build\shell.exe --help     :: usage text
```

The Qt DLLs must be found at runtime. Either add the Qt bin directory to
`PATH`, or deploy them next to the exe:

```bat
set PATH=E:\Qt\6.11.1\msvc2022_64\bin;%PATH%
build\shell.exe
```

(For VM deployment: `build\deploy\` is a self-contained folder - Qt DLLs,
plugins, translations and the VC++ runtime sit next to `shell.exe`, so the
shell works even when launched at logon without `PATH` changes. To rebuild
it: run `windeployqt --release --compiler-runtime --dir build\deploy
build\shell.exe`, copy `shell.exe` next to the DLLs, and verify
`msvcp140.dll`/`vcruntime140*.dll` are present - if not, copy them from
`<VS>\VC\Redist\MSVC\<ver>\x64\Microsoft.VC143.CRT\`.)

The window covers the primary monitor work area (dark background with
"Plasma Windows" test UI). Press `ESC` or `Alt+F4` to exit.

## Diagnostics

* Log file `shell.log` is written next to the executable, plus
  `OutputDebugString` output (visible in DebugView).
* When launched from `cmd.exe`, logs also appear in the console.

| Exit code | Meaning                    |
|-----------|----------------------------|
| 0         | clean shutdown (user quit) |
| 1         | startup failure            |

## Testing in the VM

Test only inside the Windows 10 LTSC 2021 VMware VM. Never change the
physical machine's shell.

1. Take a **VMware snapshot** before any test.
2. Deploy the shell: run `windeployqt --release build\shell.exe` (from the
   Qt bin directory) so the Qt DLLs land next to `shell.exe`, then copy
   the folder into the VM.
3. Open `cmd.exe` and run `shell.exe --debug` from the shell's folder.
4. Verify:
   * the window covers the desktop work area
   * the "Plasma Windows" test UI is visible
   * the window reacts to mouse input (check the console for debug log
     lines)
   * pressing `ESC` (or `Alt+F4`) closes it and the command returns exit
     code 0: `echo %errorlevel%`
   * `shell.log` contains startup and shutdown lines
5. Repeat launch/close several times.

### If something goes wrong

* Task Manager: `Ctrl+Shift+Esc`
* run `cmd.exe` from Task Manager (`File > Run new task`)
* start `explorer.exe` manually, then close the test shell
* restore the VMware snapshot

Phase 1 is complete when the Qt shell passes these tests in the VM.

## Phase 2 - KDE Craft and Frameworks

* Craft environment: `D:\Projects\CraftRoot` (ABI `windows-cl-msvc2022-x86_64`,
  binary cache `https://files.kde.org/craft/Qt6/26.05/.../msvc2022/x86_64`).
  Enter it with `. D:\Projects\CraftRoot\craft\craftenv.ps1`.
* Installed KF6 6.28.0 frameworks: KConfig, KCoreAddons, KWindowSystem,
  KService, KIO, KNotifications (+ transitive dependencies).
* `probe/` is a Phase 2 acceptance program linking `KF6::ConfigCore` and
  `KF6::CoreAddons`; it verifies a KConfig read/write round-trip.
  Build it inside the Craft environment:
  ```powershell
  . D:\Projects\CraftRoot\craft\craftenv.ps1
  cmake -S probe -B probe\build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=D:\Projects\CraftRoot
  cmake --build probe\build
  ```
  Run with `D:\Projects\CraftRoot\bin` on `PATH`; success = exit 0 and
  `probe\build\kf6probe.ini` containing the written value.

## Phase 0.5 - Shell switching

`tools/switch-shell.cmd` is a safe per-user mechanism to switch the
current user's shell. It changes **only** the value

```
HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell
```

of the current user. It never modifies system binaries and never touches
Winlogon. Registry writes are performed by the companion script
`tools/shell-registry.ps1` (values are passed via environment variables,
so paths containing spaces work correctly).

### Commands

```bat
switch-shell.cmd status                       :: show current shell + backup
switch-shell.cmd install <path\shell.exe>     :: switch the user's shell
switch-shell.cmd install <path\shell.exe> /force
switch-shell.cmd restore                      :: roll back to the previous shell
```

* `install` refuses to run outside a VMware VM (manufacturer check) unless
  `/force` is given.
* Before changing anything, `install` saves the current value to
  `%USERPROFILE%\.plasma-windows\shell-backup.txt`.
* `restore` puts the backed-up value back; with no backup it restores
  `explorer.exe`.
* The change takes effect after logoff/logon or a VM restart.

### Testing the script safely

For verification without touching the real configuration, copy the script
to the VM and run:

```bat
set SWITCH_SHELL_TESTKEY=HKCU\Software\PlasmaWindows\Test
switch-shell.cmd status
switch-shell.cmd install C:\test\shell.exe /force
reg query HKCU\Software\PlasmaWindows\Test
switch-shell.cmd restore
reg query HKCU\Software\PlasmaWindows\Test
```

`SWITCH_SHELL_TESTKEY` redirects every write to a scratch key, so the real
`Winlogon\Shell` value is never touched. `SWITCH_SHELL_DRYRUN=1` prints
what would happen without writing anything (safe to use on any machine).

### Phase 0.5 test procedure in the VM

1. Take a **VMware snapshot**.
2. Complete the Phase 0 manual tests first.
3. Copy `shell.exe` and `switch-shell.cmd` into the VM
   (e.g. `C:\test\`).
4. Run `switch-shell.cmd install C:\test\shell.exe --debug`, verify with
   `switch-shell.cmd status`.
5. Log off, log back on. `shell.exe` should now be the desktop shell
   (no taskbar, no desktop icons - just the Plasma Windows test window).
6. Press `ESC`/`Alt+F4` to exit, then relaunch `explorer.exe` manually to
   confirm recovery works.
7. Run `switch-shell.cmd restore`, log off/on again, confirm Explorer is
   back.
8. If anything fails: `Ctrl+Shift+Esc` -> `File > Run new task` ->
   `cmd.exe` -> `switch-shell.cmd restore`, or restore the snapshot.

Never run `install` on the physical development machine. All shell-switch
operations belong to the test VM only.

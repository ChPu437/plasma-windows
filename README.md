# Plasma Windows - Phase 0

A minimal native Win32 shell executable (`shell.exe`) that proves our own
executable can serve as the Windows desktop shell. See `agents.md` for the
full project plan.

## Scope

Phase 0 only. No Qt, no KDE, no Plasma, no external dependencies:

* creates a top-level window covering the desktop work area
* processes the normal Windows message loop
* accepts keyboard (`ESC`/`Alt+F4` to quit) and mouse input
* exits cleanly
* logs startup information, errors and exit codes

## Layout

```
plasma-windows/
    CMakeLists.txt
    src/
        main.cpp
    README.md
```

## Requirements

* Windows 10 (target: 10 IoT Enterprise LTSC 2021, build 19044)
* Visual Studio 2022 Build Tools (MSVC v143)
* Windows SDK 10.0.19041
* CMake 3.21+
* Ninja

## Build

From a **Developer Command Prompt for VS 2022** (or PowerShell after running
`vcvars64.bat`):

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is produced at `build/shell.exe`.

Example with the VS Developer PowerShell helper:

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build"
```

Adjust `$vs` if Visual Studio is installed elsewhere (e.g.
`E:\Microsoft Visual Studio\...`).

The CMake cache pins `CMAKE_SYSTEM_VERSION=10.0.19041`; override it with
`-DCMAKE_SYSTEM_VERSION=10.0.xxxxx` if that SDK is not installed on your
machine.

## Run

```bat
build\shell.exe            :: normal run
build\shell.exe --debug    :: verbose debug logging
build\shell.exe --help     :: usage text
```

The window covers the primary monitor work area (dark background with
"Plasma Windows" test UI). Press `ESC` or `Alt+F4` to exit.

## Diagnostics

* Log file `shell.log` is written next to the executable, plus
  `OutputDebugString` output (visible in DebugView).
* When launched from `cmd.exe`, logs also appear in the console.
* On startup failure a message box shows the failing step and exit code.

| Exit code | Meaning                                  |
|-----------|------------------------------------------|
| 0         | clean shutdown (user quit)               |
| 1         | unexpected startup failure               |
| 2         | `RegisterClassExW` failed                |
| 3         | `CreateWindowExW` failed                 |
| 4         | `GetMessage` failed (message loop error) |

## Testing in the VM

Test only inside the Windows 10 LTSC 2021 VMware VM. Never change the
physical machine's shell.

1. Take a **VMware snapshot** before any test.
2. Copy `shell.exe` into the VM.
3. Open `cmd.exe` and run `build\shell.exe --debug` from the shell's folder.
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

Phase 0 is complete only when `shell.exe` runs as described above. Shell
replacement (Phase 0.5, switching `explorer.exe` <-> `shell.exe` via the
shell configuration) is a separate milestone and must not be attempted yet.

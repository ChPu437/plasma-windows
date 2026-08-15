# Konsole on Windows - build + status

Status: **Konsole v26.04.3 builds, installs and runs** on the dev machine
(window title shows the default shell: `...\powershell.exe - Konsole`).
Terminal interaction, Chinese input, window resize (column tracking),
copy/paste and multi-tab all verified working.

## Key fact: upstream already has Windows support

Konsole's master (and the v26.04.3 release) contains a native Windows
backend that does **not** use kpty:

- `src/Pty.h` has a `Q_OS_WIN` branch - on Windows `Pty` inherits
  `QObject` and wraps `IPtyProcess` from the vendored `src/ptyqt/`
  directory instead of `KPtyProcess`.
- `src/CMakeLists.txt`: `if(WIN32)` compiles `ptyqt/iptyprocess.cpp`
  and `ptyqt/conptyprocess.cpp` (a complete ConPTY backend:
  CreatePseudoConsole, STARTUPINFOEX/PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
  ResizePseudoConsole, read thread, exit notification via
  RegisterWaitForSingleObject, version check against
  CONPTY_MINIMAL_WINDOWS_VERSION); `if(NOT WIN32)` links KF6::Pty.
- kpty itself stays Unix-only in craft (`platforms = Unix`), so the
  craft konsole blueprint (which still lists kpty as a dependency)
  cannot build on Windows - hence the manual build below, same as
  Dolphin.

## Build (plain CMake, like Dolphin)

```bat
git clone --depth 1 --branch v26.04.3 https://github.com/KDE/konsole D:\_\konsole
cmake -S D:\_\konsole -B D:\_\konsole-build -G Ninja ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_INSTALL_PREFIX=D:\Projects\CraftRoot ^
    -DCMAKE_PREFIX_PATH=D:\Projects\CraftRoot ^
    -DBUILD_TESTING=OFF ^
    -DHAVE_LIBSSH=OFF
cmake --build D:\_\konsole-build
cmake --install D:\_\konsole-build
```

Built in one pass - no MSVC source fixes were needed. All dependencies
were already present in CraftRoot (KF6 Bookmarks/BookmarksWidgets/
GuiAddons/I18n/IconWidgets/KIOWidgets/NewStuff/Notifications/TextWidgets/
WindowSystem, Qt Multimedia/PrintSupport/Xml, ICU 78.1). IcoTool is
missing (icon tool) - harmless.

## What installs where

- `CraftRoot\bin\konsole.exe`, `konsoleapp.dll`, `konsoleprivate.dll`
- `CraftRoot\plugins\kf6\parts\konsolepart.dll` (KPart embedding -
  produced automatically; the loading path is unverified on Windows,
  Dolphin-embedding is a phase-2 item)
- `CraftRoot\bin\data\applications\org.kde.konsole.desktop` (Exec=konsole,
  Icon=utilities-terminal, X-KDE-Shortcuts=Ctrl+Alt+T)
- UI/colorscheme data is compiled into the binaries via `data.qrc` -
  no external data files needed
- Default profile picks up Windows' default shell automatically
  (PowerShell on this machine)

## Deployment to plasma-vm

Copied: konsole.exe + konsoleapp.dll + konsoleprivate.dll to
`plasma-vm\bin`, konsolepart.dll to `plasma-vm\plugins\kf6\parts`,
org.kde.konsole.desktop to `plasma-vm\bin\data\applications`. All
runtime DLL dependencies were already in the package (Qt6Multimedia,
Qt6PrintSupport, Qt6Xml, ICU, KF6 widgets libs).

## Notes / open items

- No source patches - upstream Windows support worked as-is.
- `patches/konsole/` stays empty for now; revisit if fixes appear.
- Chinese input inside the terminal depends on TextInputHost (same
  constraint as the rest of the shell - see docs/input-method.md).
- SSH sessions: `HAVE_LIBSSH=OFF`; SSH works via the system OpenSSH
  client over ConPTY in principle, verification is phase 2.

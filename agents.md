# Project: Plasma Shell for Windows

## 1. Project Goal

Build a Windows-native desktop shell that can eventually replace `explorer.exe` as the interactive desktop shell on Windows 10 IoT Enterprise LTSC 2021.

The long-term goal is **not** to port the entire KDE Plasma workspace to Windows.

The intended scope is:

* Use the existing Windows kernel, Win32, DWM, graphics stack, and Windows application model.
* Replace the Explorer desktop shell with a KDE Plasma-based shell.
* Port/use `plasmashell` and the minimum required KDE Frameworks components.
* Make KDE applications, especially Dolphin, usable on Windows.
* Keep Windows-native applications completely unchanged.
* Do NOT port KWin, Wayland, X11, systemd, or the entire KDE/Plasma infrastructure unless a concrete dependency proves necessary.

Conceptually:

```
Windows kernel
    |
  Winlogon
    |
  Userinit
    |
    v
Plasma Shell
    |
    +-- Plasma panel
    +-- Application launcher
    +-- Task manager
    +-- Desktop
    +-- Plasma widgets
    +-- KRunner (optional)
    |
    +-- KDE applications
          |
          +-- Dolphin
          +-- other KDE applications
```

Windows DWM remains the compositor and Windows remains responsible for native window rendering.

The project should treat Windows as the host operating system, not as a compatibility target for an entire Linux desktop stack.

---

## 2. Target Environment

Primary target:

* Windows 10 IoT Enterprise LTSC 2021
* x86-64
* Windows build 19044

Development environment:

* Windows host
* Visual Studio 2022 Build Tools
* MSVC v143
* Windows SDK 10.0.19041
* CMake
* Ninja
* Git
* VS Code / clangd
* KDE Craft will be introduced later

Runtime testing:

* VMware virtual machine
* Windows 10 IoT Enterprise LTSC 2021
* Development machine must NOT initially replace its own Explorer shell.

The VM should be considered disposable and should use VMware snapshots before shell replacement experiments.

The VM is currently not created, and this status should be updated after the user claims its creation.

The path to visual studio dev kit: E:\Microsoft Visual Studio

---

# 3. Architectural Principles

## 3.1 Do not replace Winlogon

Winlogon itself is not part of the project.

The shell should be launched through the normal Windows shell mechanism.

Conceptually:

```
Winlogon
    |
  Userinit
    |
    v
  shell.exe
```

The project should replace the configured shell rather than modifying Winlogon or implementing a new login manager.

## 3.2 Do not implement a compositor

Windows DWM remains responsible for composition.

The project should NOT attempt to implement:

* Wayland compositor
* X11 server
* replacement for DWM
* KWin-like rendering infrastructure

## 3.3 Windows-native APIs are preferred

When Plasma/KDE code needs functionality provided by Linux-specific infrastructure, prefer implementing a Windows backend using native APIs.

Examples:

* window enumeration -> Win32
* foreground window -> Win32
* window state -> Win32/DWM
* filesystem -> Windows filesystem APIs / KDE KIO backend
* known folders -> Windows Shell APIs
* notifications -> Windows notification APIs where appropriate

Do not introduce Linux compatibility layers merely to avoid writing a Windows backend.

---

# 4. Development Strategy

The project must proceed incrementally.

Do not attempt to port Plasma before the basic Windows shell mechanism is proven.

Milestones:

## Phase 0 — Pure Win32 Shell

Create a minimal native Win32 executable:

```
shell.exe
```

It should:

1. Start successfully on Windows 10 LTSC 2021.
2. Create a top-level window.
3. Cover the desktop/work area.
4. Process the normal Windows message loop.
5. Accept keyboard and mouse input.
6. Exit cleanly.
7. Produce useful diagnostics if startup fails.

This phase must NOT depend on:

* Qt
* KDE
* Plasma
* CMake packages from KDE
* DBus
* WSL
* Cygwin
* MSYS2
* Wine

The purpose is to prove that our own executable can serve as the Windows desktop shell.

---

## Phase 0.5 — Shell Replacement

After Phase 0 works as a normal application:

1. Create a safe mechanism to configure the current user's shell.

2. Support switching between:

   ```
   explorer.exe
   shell.exe
   ```

3. Provide a reliable recovery mechanism.

4. Test shell replacement only inside VMware.

5. Never make the physical development machine dependent on the experimental shell.

The shell-switching mechanism should support explicit rollback.

Do not modify system binaries.

Do not patch Winlogon.

---

## Phase 1 — Qt Shell

Replace the raw Win32 UI with a Qt-based shell while retaining the same shell lifecycle.

At this stage the shell should still be minimal.

The goal is to prove:

```
Windows shell
    |
    v
  Qt app
```

before introducing KDE dependencies.

---

## Phase 2 — KDE Frameworks

Introduce KDE Craft and the minimum required KDE Frameworks.

Likely dependencies include, but are not limited to:

* KConfig
* KCoreAddons
* KWindowSystem
* KIO
* KService
* KNotifications

Do NOT port unrelated KDE infrastructure.

Every dependency should have a concrete reason for being required by the target functionality.

---

## Phase 3 — Plasma Shell

Port/build `plasmashell` on Windows.

The desired functionality is:

* desktop
* wallpaper
* panel
* application launcher
* task manager
* widgets

Plasma should use Windows/DWM as the underlying window system.

The key compatibility layer is expected to involve `KWindowSystem` and related window-management abstractions.

Do not attempt to port KWin unless investigation proves that a specific Plasma feature fundamentally requires it.

---

## Phase 4 — Dolphin

Port/build Dolphin and the required KDE Frameworks/KIO functionality.

The initial filesystem target should be the native Windows filesystem.

At minimum:

* local drives
* directories
* files
* Desktop
* Documents
* Downloads
* Pictures
* standard Windows paths

Network and advanced KIO protocols are optional and should not block the core goal.

---

# 5. Current Task: Phase 0

The ONLY task at the beginning of the project is:

> Implement the smallest possible native Win32 executable that can later become the Windows desktop shell.

Do not start KDE/Qt/Plasma work yet.

## Required project structure

Use a simple CMake project:

```
plasma-windows/
    CMakeLists.txt
    src/
        main.cpp
    README.md
```

Architecture should remain intentionally minimal.

---

# 6. Phase 0 Functional Requirements

`shell.exe` must:

* be x86-64
* compile using MSVC v143
* target Windows 10
* link against the normal Win32 libraries
* create a native top-level window
* display a visible test UI
* cover the desktop/work area
* respond to normal Windows messages
* terminate cleanly

The first UI can be extremely simple.

For example:

```
+------------------------------------------+
|                                          |
|                                          |
|             Plasma Windows               |
|                                          |
|                                          |
+------------------------------------------+
```

Do not implement a panel, taskbar, launcher, or desktop widgets yet.

---

# 7. Diagnostics

The shell must be easy to debug.

At minimum provide:

* startup logging
* graceful error reporting
* exit/error code reporting
* optional debug logging controlled by a compile-time or runtime switch

Avoid dependencies on external logging frameworks at Phase 0.

Simple Win32 debugging facilities are sufficient.

---

# 8. Build Requirements

The project must build with:

```
MSVC v143
Windows SDK 10.0.19041
CMake
Ninja
```

The generated executable should be a normal Windows PE executable.

Do not introduce:

* MinGW
* MSYS2
* Cygwin
* WSL
* Wine
* cross-compilation toolchains

unless explicitly requested later.

---

# 9. Testing Requirements

The first test environment is the Windows 10 IoT Enterprise LTSC 2021 VMware VM.

Testing should initially be:

```
launch shell.exe manually
    |
    v
verify window
    |
    v
close shell.exe
    |
    v
repeat
```

Only after this succeeds should shell replacement be attempted.

Before changing the shell configuration:

1. Create a VMware snapshot.
2. Verify that `explorer.exe` can still be launched manually.
3. Have a recovery procedure ready.

---

# 10. Recovery Requirements

The experimental shell must never make the VM permanently unrecoverable.

The developer must know how to:

* launch Task Manager
* launch `cmd.exe`
* launch `powershell.exe`
* manually launch `explorer.exe`
* restore the previous shell configuration

If the shell crashes immediately after login, recovery must be possible without reinstalling Windows.

---

# 11. Agent Behavior

The coding agent should:

* make small, reviewable commits/changes
* avoid unnecessary dependencies
* explain architectural decisions
* verify builds after modifications
* avoid silently changing system configuration
* never modify Winlogon binaries
* never change the physical host's shell configuration
* never install large dependencies without explaining why
* keep the project buildable at every milestone

Any potentially destructive operation (for example: modifying the registry,
changing the shell configuration, installing software) must NOT be performed
on the development machine. Such operations are only allowed inside the test
VM. When such an operation needs to be done, the agent must stop and tell the
user to run it on the VM instead of executing it locally.

When uncertain whether a dependency or architectural change is necessary, prefer the simpler implementation and ask for confirmation.

The current milestone is complete only when `shell.exe` builds and runs successfully on the Windows 10 LTSC 2021 VMware VM.

Do not proceed to Qt, KDE, or Plasma automatically after completing Phase 0. Stop and report the result.

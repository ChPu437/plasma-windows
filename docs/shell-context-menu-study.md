# Shell context menu study - ManagedShell's IContextMenu integration

Status: research report (2026-08-13). Companion to
`docs/tray-host-cairo-study.md` (same sources in `research/`,
gitignored). Two questions answered: does Cairo help with (a) custom
window decorations - no; (b) custom shell context menus - yes, full
reference implementation in ManagedShell.

## 1. Window decorations: verdict - NOT helpful

Searched both repos for `GWL_WNDPROC`, `SetWindowLongPtr`,
`SetWindowsHookEx(WH_CALLWNDPROC)`, `WM_NCCALCSIZE`, `WM_NCPAINT`,
`DwmSetWindowAttribute` - no third-party window decoration code
exists. Cairo never touches other apps' title bars/frames.

What exists, for our other gaps:

* `CairoExplorerWindow.xaml.cs:21-23` - Aero Glass
  (`DwmExtendFrameIntoClientArea`) on their OWN file-manager window
  (equivalent of what Qt apps do natively with frameless windows).
* `ManagedShell.WindowsTasks/ApplicationWindow.cs:411-416` - `DWMWA_CLOAKED`
  for taskbar filtering (we already do this in our KWindowSystem
  backend).
* `cairoshell-master/.../CairoDesktop.Taskbar/DwmThumbnail.xaml.cs` -
  `DwmRegisterThumbnail`/`DwmUpdateThumbnailProperties` for live
  taskbar thumbnails - useful reference for our taskbar thumbnail gap.

Custom decorations for third-party Windows apps require global window
subclassing (WindowBlinds-style); that is out of scope for both Cairo
and our current roadmap. Breeze title bars only make sense for our own
Qt/KDE apps (frameless windows + Qt-side drawing). The DWM thumbnail
code above IS worth reading when we do taskbar previews.

## 2. Shell context menus: verdict - full reference implementation

ManagedShell's `src/ManagedShell.ShellFolders/` is a complete
IShellFolder/PIDL/IContextMenu wrapper (used by Cairo's desktop icons
and desktop background menus, and by RetroBar). Key files:

* `ShellContextMenu.cs` - abstract base: `NativeWindow` subclass whose
  `WndProc` forwards `WM_MENUSELECT`, `WM_INITMENUPOPUP`,
  `WM_MEASUREITEM`, `WM_DRAWITEM`, `WM_MENUCHAR` to `IContextMenu2` /
  `IContextMenu3` (`HandleMenuMsg`/`HandleMenuMsg2`) - REQUIRED for
  "Open With", "Send To" and owner-drawn extension menus to work.
  Also `GetCommandString` (verb lookups) and `InvokeCommand`
  (`CMINVOKECOMMANDINFOEX` with UNICODE, PTINVOKE, ASYNC, Ctrl/Shift
  modifier flags).
* `ShellItemContextMenu.cs` - the file/folder-item menu:
  1. `IShellFolder::GetUIObjectOf(parentFolder, pidls, IID_IContextMenu)`
  2. `CreatePopupMenu()`
  3. custom entries first (`AppendMenu`, preBuilder)
  4. `iContextMenu.QueryContextMenu(hmenu, numPrepended, CMD_FIRST,
     CMD_LAST, CMF_EXPLORE | CMF_ITEMMENU | [CMF_EXTENDEDVERBS] |
     [CMF_CANRENAME] | [CMF_DEFAULTONLY])` - third-party shell
     extensions fill the menu here
  5. custom entries after (postBuilder)
  6. `TrackPopupMenuEx(TPM_RETURNCMD)` blocking loop
  7. selection >= CMD_FIRST: `GetCommandString(GCS_VERB)`; if it is a
     custom entry route to the app delegate, else
     `InvokeCommand(cmd - CMD_FIRST)`
* `ShellFolderContextMenu.cs` - folder-background menu (desktop
  background, "New" submenu via `ShellNewMenuCommand`).
* `ShellItem.cs` / `ShellFile.cs` / `ShellFolder.cs` - PIDL-based
  shell namespace model (relative/absolute PIDLs, display names,
  icons via `IconHelper`, file-system detection).

Hybrid-menu pattern (how a shell adds its own items next to shell
extensions): `ShellItemContextMenu.cs:42-62` (`ConfigureMenuItems`) +
Cairo's builders (`DesktopIcons.xaml.cs:190-232`,
`Desktop.xaml.cs:690-760`) - custom UIDs start above
`CommonContextMenuItem.Max`; default item settable; Cairo's own
actions dispatched through a `Dictionary<uint,string>` of command
identifiers. Cairo desktop usage: `DesktopIcons.xaml.cs:234-253`
(icon menu), `Desktop.xaml.cs:191` (background menu).

## 3. Adaptation notes for plasma

* **Phase 4 / desktop FolderView**: to get "not worse than explorer"
  context menus (7-Zip, VS Code, Git, ...), implement a C++ port of
  this pattern: `SHGetDesktopFolder`/`SHParseDisplayName` -> folder
  PIDL -> `GetUIObjectOf` -> `QueryContextMenu` -> native
  `TrackPopupMenuEx` (or enumerate items into a QML menu and invoke by
  command ID - more work, loses owner-draw and live submenus; native
  HMENU is the pragmatic choice).
* COM threading: ManagedShell serializes all shell COM calls with a
  global lock (`IconHelper.ComLock` in `ShellItemContextMenu.cs:31`).
  In Qt we should run the menu on the GUI thread with the same lock or
  on an STA helper thread.
* `AllowDarkModeForWindow(handle, true)` before TrackPopupMenu for
  dark menus (`ShellItemContextMenu.cs:169-172`).
* Where it plugs in: libplasma FolderView / desktop containment
  context menus (QML `ContextMenu` would delegate to the native
  backend), and Dolphin later.
* The same ManagedShell `ShellFolders` also provides IShellFolder
  enumeration + icons - potential reference for a KIO Windows backend
  (This PC / Recycle Bin / shell: links), see `docs/explorer-parity.md`.

## 4. Key file index (research/ mirrors)

* `ManagedShell-master/src/ManagedShell.ShellFolders/ShellContextMenu.cs`
* `ManagedShell-master/src/ManagedShell.ShellFolders/ShellItemContextMenu.cs`
* `ManagedShell-master/src/ManagedShell.ShellFolders/ShellFolderContextMenu.cs`
* `ManagedShell-master/src/ManagedShell.ShellFolders/ShellItem.cs` /
  `ShellFolder.cs` / `ShellFile.cs` / `ShellNewMenuCommand.cs`
* `cairoshell-master/Cairo Desktop/CairoDesktop.DynamicDesktop/DesktopIcons.xaml.cs`
  (icon menu) and `Desktop.xaml.cs` (background menu)
* `cairoshell-master/Cairo Desktop/CairoDesktop.Taskbar/DwmThumbnail.xaml.cs`
  (taskbar thumbnails, for our taskbar gap)

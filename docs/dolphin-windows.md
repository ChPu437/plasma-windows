# Dolphin on Windows - build + status (Phase 4)

Status: **Dolphin 26.04.3 builds and launches** on the dev machine
(window title "Home - Dolphin", process responsive). Local filesystem
browsing via KIO's `file` protocol (`kio_file.dll` worker).

## Build

Dolphin is built manually with plain CMake (the craft blueprint exists
but its dependency list drags in kio-extras/ffmpegthumbs/qtcharts/
phonon which fail or are unnecessary on Windows):

```bat
:: unpack the craft tarball (download\archives\kde\applications\dolphin\dolphin-26.04.3.tar.xz)
cmake -S dolphin-26.04.3 -B build -G Ninja ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_INSTALL_PREFIX=D:\Projects\CraftRoot ^
    -DCMAKE_PREFIX_PATH=D:\Projects\CraftRoot ^
    -DBUILD_TESTING=OFF
cmake --build build --target install -j 12
```

All framework dependencies were already installed by craft; the only
missing bits that surfaced:

* `KF6UserFeedback` (optional, skipped - no telemetry)
* `KF6Baloo` (optional, skipped - no file indexing; Baloo has no
  Windows support)
* `kio-extras`, `ffmpegthumbs`, `qtcharts`, `phonon` (craft deps that
  are not required to compile/run Dolphin locally)

## Required patch: KIO

`patches/kio/0001-windows-export-all-symbols.patch`:
`CMakeLists.txt` sets `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON` on Windows.
MSVC does not export move special members of dllexport classes
(`KFileItem(KFileItem&&)`, `operator=(KFileItem&&)`), so Dolphin failed
to link with "undefined symbol: QList<KFileItem>::QList(QList&&)".
Member-level export is impossible (C2487: class already dllexport), so
the whole library exports all symbols.

After patching, rebuild KIO and reinstall it:
`cmake --build <kio build dir> --target install`.

## What works / open items

- [x] dolphin.exe launches, shows the Home window
- [ ] browse local drives/folders (verify with the user; KIO file
      worker present as `plugins\kf6\kio\kio_file.dll`)
- [ ] open files (text editor association etc.)
- [ ] Dolphin in the launcher (add a .desktop for dolphin.exe, or it
      can be pinned from the taskbar)
- [ ] kio-extras protocols (archive/sftp/smb) - out of scope for the
      core goal (local filesystem)
- [ ] Baloo search/tags - no Windows support

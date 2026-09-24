# StripTool

This repository builds two EPICS trend applications side by side:

- `StripTool` is the maintained legacy Motif application in `striptool/`.
- `qtstriptool` is the C++17/Qt replacement in `qtstriptool/`.

The Qt application is currently an **alpha**. It supports live Channel Access,
ten-curve controls, compatible `.stp` load/save, graph interaction,
annotations, Archiver Appliance history, text/CSV and image export, and Qt
printing. It is not yet the default replacement: operational archive,
cross-platform, parity, and soak validation remain open. Keep both executable
names distinct while those gates are open.

## Repository layout

- `striptool/` — Motif implementation; executable name `StripTool`.
- `qtstriptool/` — Qt implementation; executable name `qtstriptool`.
- `docs/legacy/` — historical manuals and release notes.
- `docs/` — compatibility baseline, parity matrix, and Qt documentation.
- `tests/fixtures/` — toolkit-independent `.stp` compatibility fixtures.

## Dependencies

Both applications require a built EPICS Base with Channel Access headers and
libraries. Set `EPICS_BASE=/path/to/base` if it is not found in a common
sibling or APS location. The build uses the host architecture reported by
EPICS Base.

Qt StripTool additionally requires a C++17 compiler and either Qt 5.15 or Qt 6
development files for Core, Gui, Widgets, PrintSupport, and Test. Unix builds
normally discover Qt with `pkg-config`; Qt 6 may also be discovered with
`qmake6`. Select a major version explicitly with `QT_VERSION=5` or
`QT_VERSION=6`.

Legacy StripTool additionally requires Motif and X11 development files,
including Xm, X11, Xmu, and Xpm. It is not built on Windows. Typical Linux
package names include `qtbase5-dev` (or the distribution's Qt 6 base
development package), `libmotif-dev`, `libx11-dev`, `libxmu-dev`, and
`libxpm-dev`; names vary by distribution. On macOS, install Qt and the EPICS
dependencies with the site's selected package manager and provide Motif/XQuartz
only if the legacy application is required. The Windows path expects Qt 6 for
MSVC, a compatible EPICS Base build, GNU Make, and the MSVC command-line tools;
set `QT_DIR`, `EPICS_BASE`, and `EPICS_HOST_ARCH` as needed.

## Build and test

Run from the repository root:

```sh
make                              # Build every available variant
make striptool                    # Build legacy Motif StripTool
make qtstriptool                  # Build Qt StripTool
make test-qtstriptool             # Core, config, UI, and performance tests
make test-qtstriptool-core
make test-qtstriptool-config
make test-qtstriptool-ui
make test-qtstriptool-performance
make test-qtstriptool-visual
make test-qtstriptool-ioc             # Self-contained IOC restart/reconnect test
make test-qtstriptool-ioc TEST_PV=some:readable:numeric:pv  # Also test a site PV
make test-qt-versions             # Test each locally available Qt major
make clean
make distclean
```

If Motif is unavailable, the default build reports that the legacy variant is
being skipped. A missing Qt or EPICS Base is an error for the Qt build. Build
products are placed in `bin/<OS>-<architecture>/`; Qt object trees are named
`qtstriptool/O.<OS>-<architecture>-qt<major>`.

On APS Linux hosts, the build automatically uses the RHEL 8-built GCC suite at
`/usr/local/oag/3rdParty/gcc-11.3.0` when its `gcc` and `g++` executables are
available. This matches the compiler used for the site EPICS libraries and
statically links the C++ runtime, following the MEDM build convention. Set
`CUSTOM_GCC_PATH=/another/gcc/prefix` to select another installation, or set it
to an empty value to use the compiler from `PATH`. Run `make distclean` before
switching compilers so objects from different C++ ABIs are not mixed. Use this
compiler from `rhel8-build shell` when producing a RHEL 8-compatible binary;
the compiler path by itself does not prevent a build run on RHEL 9 from taking
dependencies on RHEL 9 glibc symbols.

When checked out as `<extensions>/src/StripTool` in an EPICS extensions tree,
the normal builds also copy executables into the extensions host binary
directory. This is separate from the staged system packaging described below.

## Run

```sh
bin/$(uname -s)-$(uname -m)/StripTool example.stp
bin/$(uname -s)-$(uname -m)/qtstriptool example.stp
bin/$(uname -s)-$(uname -m)/qtstriptool -style adwaita example.stp
```

Qt StripTool defaults to the Fusion style for consistent, compact controls
across desktop environments. Qt consumes standard Qt options such as `-style`,
which can select another installed style; application-specific syntax
is `qtstriptool [--help] [--version] [configuration.stp]`. A bare configuration
name is searched in the current directory and then `STRIP_FILE_SEARCH_PATH`.

## Install and package Qt StripTool

On Unix-like systems, stage or install the Qt application and its desktop
metadata with:

```sh
make install-qt-package PREFIX=/usr/local
make install-qt-package DESTDIR=/tmp/qtstriptool-stage PREFIX=/usr
make package-qtstriptool PACKAGE_VERSION=0.1.0-alpha1
```

The install target writes under `PREFIX`: `bin/qtstriptool`, the desktop entry,
the scalable icon, AppStream metadata, and these guides. `DESTDIR` supports
distribution staging. The package target creates a relocatable staging tarball
under `O.package/`; it is not a native RPM, DEB, or signed application bundle
and does not bundle Qt runtime libraries.

For Windows, build the executable, copy it into an application directory, run
Qt's `windeployqt` on it, and include compatible EPICS `ca.dll` and `Com.dll`
plus their required runtimes. Test that directory on a clean host before
distribution. A macOS release should similarly use the selected Qt deployment
tool and the site's signing/notarization process; these bundles are not yet
release-verified.

Do not install a `StripTool` symlink to `qtstriptool` yet. See the user guide
for launch-script and desktop-entry migration with an explicit rollback path.

## Documentation

- [Qt user guide](docs/qtstriptool-user-guide.md)
- [Compatibility, known differences, and release progression](docs/qtstriptool-compatibility.md)
- [Appearance and visual review](docs/qtstriptool-appearance.md)
- [Performance and soak testing](docs/qtstriptool-performance.md)
- [Parity matrix](docs/QtParityMatrix.md)
- [Legacy compatibility baseline](docs/StripToolCompatibilityBaseline.md)

# Install and build Qt StripTool

Qt StripTool is built from this repository with GNU Make. It needs a C++17 compiler, Qt 5.15 or Qt 6 Widgets and PrintSupport development files, and a built EPICS Base with Channel Access. The legacy Motif program has separate dependencies and is not required to build `qtstriptool`.

## Build

Set `EPICS_BASE` if the build cannot find your EPICS Base tree, then run from the repository root:

```sh
make qtstriptool
```

The executable is copied to `bin/<OS>-<architecture>/qtstriptool`. The [repository README](https://github.com/rtsoliday/StripTool#dependencies) describes platform dependencies and the complete build and test commands.

## Run

```sh
bin/$(uname -s)-$(uname -m)/qtstriptool
bin/$(uname -s)-$(uname -m)/qtstriptool example.stp
```

Qt StripTool uses the Fusion style by default. Qt's standard `-style` option can select another installed style, for example `qtstriptool -style adwaita example.stp`.

## Install a Unix package tree

```sh
make install-qt-package PREFIX=/usr/local
make install-qt-package DESTDIR=/tmp/qtstriptool-stage PREFIX=/usr
make package-qtstriptool
```

The installation includes the executable, desktop entry, icon, metadata, and guides. `DESTDIR` stages the files for a distribution package. The tarball is a relocatable staging archive; it does not bundle Qt libraries. Windows and macOS deployment require their platform Qt deployment tools and site validation.

See [compatibility and cutover](../understand/compatibility) before replacing an existing StripTool launcher.

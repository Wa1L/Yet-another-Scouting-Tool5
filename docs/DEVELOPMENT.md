# Development

This guide covers building and running the project from source. For user-facing information, see
the [README](../README.md).

## Dependencies

### Common build requirements

- **C compiler**: A C99-capable GCC or Clang compiler. MinGW-w64 GCC is used on Windows.
- **Build system**: CMake 3.20 or newer
- **Package metadata**: `pkg-config` (provided by `pkgconf` on macOS and Arch Linux) so CMake can find `gtk4` and
  `gio-2.0`
- **GUI framework**: GTK4 development libraries
- **GLib tools**: `glib-compile-resources`, normally included with the GLib development package

### Linux

#### Debian/Ubuntu (and derivatives)

```bash
sudo apt-get install \
  build-essential \
  cmake \
  pkg-config \
  libgtk-4-dev \
  libglib2.0-dev \
  sysvinit-utils
```

`sysvinit-utils` provides `pidof`, which the Linux process backend uses to locate the Football Manager process.

#### Red Hat/CentOS/Fedora

```bash
sudo dnf install \
  gcc \
  cmake \
  pkgconf-pkg-config \
  gtk4-devel \
  glib2-devel \
  procps-ng
```

#### Arch Linux

```bash
sudo pacman -S --needed \
  base-devel \
  cmake \
  pkgconf \
  gtk4 \
  procps-ng
```

If `pidof` is not available, install the process-utilities package for your distribution. The exact package name may
differ between distributions.

### macOS (unofficial local builds only)

macOS is not an officially supported runtime platform. These instructions are provided only for contributors who want to
experiment with local builds. Install the Xcode Command Line Tools for Apple Clang, then install the build dependencies
with [Homebrew](https://brew.sh/):

```bash
xcode-select --install
brew install cmake pkgconf gtk4
```

The Homebrew `pkgconf` formula provides the `pkg-config` command. On Apple Silicon, Homebrew normally uses
`/opt/homebrew`; on Intel Macs it normally uses`/usr/local`. Make sure the relevant Homebrew `bin` directory is on
`PATH`before configuring CMake.

### Windows

#### MSYS2/MinGW64 (recommended)

1. Download and install [MSYS2](https://www.msys2.org/)
2. Open the **MinGW64** terminal and run:

```bash
pacman -S --needed \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-gtk4
```

Use the same MinGW64 environment for CMake, the compiler, `pkg-config`, and the GTK4 libraries. Do not mix MSYS,
MinGW64, and native Windows package environments. Other MinGW-w64 distributions can work if they provide matching GTK4,
GLib, and `pkg-config` installations.

## Building the project

Run these commands from the repository root after installing the dependencies.

### Single-config generators

```bash
# Configure (Release is the default for single-config generators)
cmake -S . -B build

# Build
cmake --build build --parallel

# Build Debug
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel

# Clean build artifacts
cmake --build build --target clean
```

### Multi-config generators

```bash
# Configure once
cmake -S . -B build

# Build Debug
cmake --build build --config Debug --parallel

# Clean Debug build artifacts
cmake --build build --config Debug --target clean
```

### Run and inspect the build

The executable filename is derived from CMake's `PROJECT_NAME`. Prefer the custom `run` target because it resolves the
correct path for single-config builds, multi-config builds, and application bundles produced by unofficial macOS builds:

```bash
# Single-config generators
cmake --build build --target run

# Multi-config generators
cmake --build build --config Debug --target run

# Show compiler and target information
cmake --build build --target info
```

## Runtime requirements for source builds

- GTK4 and GLib runtime libraries must be available to the application. The development packages normally install these
  automatically. To distribute a Windows build outside MSYS2, use the `package` target described in
  [Packaging a Windows build for distribution](#packaging-a-windows-build-for-distribution).
- A graphical desktop session is required on the officially supported platforms: X11 or Wayland on Linux, or a Windows
  desktop session on Windows. Unofficial macOS builds require the macOS window server.
- Football Manager 24 must be running with the target save loaded. The current platform backends look for a
  process/module associated with `fm.exe`.
- The application must have permission to inspect and modify the Football Manager process. Linux systems may restrict
  `/proc/<pid>/mem` or ptrace access; Windows may require the appropriate process rights. Unofficial macOS builds may
  require additional process-access or debugging permissions depending on system security settings.
- On Linux, the `pidof` command must be available because the process-discovery backend uses it.

## Development notes

- CMake selects the compiler from your environment or toolchain. Override it with `CC` or `-DCMAKE_C_COMPILER=...` when
  needed.
- `PROJECT_NAME` in `CMakeLists.txt` drives the application name passed to the code and the executable output name.
  `PROJECT_VERSION` drives the version shown in the footer.
- Platform-specific compile definitions are set automatically: `ARCH_LINUX` or `ARCH_WIN` for officially supported
  platforms. The code also defines `ARCH_MACOS` for unofficial local macOS builds.
- CMake compiles the UI layouts, CSS, icons, and flags into a generated GResource during the build. The generated files
  live in the build directory and should not be edited manually.
- The application icon is authored once in `assets/branding/yast.svg`. Every platform artefact — the PNG set, the
  Windows `yast.ico` and the macOS `yast.icns` — is generated from it by `assets/branding/generate.sh`, which needs
  `rsvg-convert` and `python3` (plus `iconutil` for the `.icns`, so the macOS artefact can only be refreshed on macOS).
  Re-run that script and commit the results whenever the master SVG changes.
- Each platform picks the icon up differently: Linux installs the hicolor theme and `.desktop` entry, Windows embeds
  `yast.ico` as a resource in the executable, and macOS reads `yast.icns` from the application bundle. Unofficial macOS
  builds are therefore always produced as a `YaST.app` bundle, in every configuration, because macOS resolves an
  application's icon from its bundle and falls back to the generic icon for a bare executable.
- On Windows, use a correctly configured MSYS2 MinGW64 environment for the whole build. The requirement is the matching
  toolchain and package paths, not a particular terminal application.
- GTK4 also requires the platform's normal graphics and font libraries. These are installed as transitive dependencies
  by the packages above.

## Packaging a Windows build for distribution

A MinGW build links GTK4 dynamically, so the bare `YaST.exe` only starts on a machine that already
has the MSYS2 prefix on `PATH`. Elsewhere it fails with errors such as
`libcairo-2.dll was not found`. The `package` target, available only on Windows, collects the whole
runtime closure into a self-contained directory and produces both a zip and an installer:

```bash
cmake --build build --target package
```

Building the installer needs NSIS 3. Install it with
`pacman -S --needed mingw-w64-x86_64-nsis`, or use the official build from
[nsis.sourceforge.io](https://nsis.sourceforge.io/) and put `makensis` on `PATH`. Without it the
target still produces the zip, and says so.

The artefacts are `build/YaST-<version>-windows-x64/` (the staging directory), `…-windows-x64.zip`
and `…-windows-x64-setup.exe`. The zip is unzipped anywhere and run in place; the installer writes
the same tree to `%LOCALAPPDATA%\Programs\YaST` and adds a Start Menu shortcut and an Apps &
Features entry.

### Bundle layout

```
bin\      YaST.exe and every DLL it needs
lib\      gdk-pixbuf loader modules and their cache
share\    GSettings schemas and the icon theme
```

This layout is required, not cosmetic. GLib, gdk-pixbuf and GTK are relocatable on Windows: each
finds its data by asking where its own DLL lives, through
`g_win32_get_package_installation_directory_of_module()`. That helper treats a `bin` directory as a
marker and walks up to its parent, so `lib\` and `share\` must stay siblings of `bin\`. Flattening
the tree, or moving the DLLs somewhere central like `System32`, makes GLib look for its schemas in
the wrong place and GTK aborts on startup. It is also why the DLLs cannot simply be tidied away
into a folder of their own.

The staging directory is the single source for both artefacts, so whatever is verified from the zip
is exactly what the installer ships.

### Keeping the bundle small

Copying the GTK runtime wholesale produces a bundle several times larger than the parts the
application can actually reach, so the script prunes each tree. The prunes are keyed off properties
of the application rather than hard-coded file lists, so they stay correct as GTK changes:

- **gdk-pixbuf loaders.** YaST decodes only the PNG flags and the two SVGs referenced from
  `styles.css`, so only those two loader modules ship. This is the largest saving, because each
  unused loader would otherwise pull its entire codec stack — libtiff, libwebp, libjxl, libavif,
  libheif and their dependencies — into the DLL closure. `loaders.cache` is filtered textually to
  match rather than regenerated, because regenerating it here would bake this machine's absolute
  paths into the bundle.
- **Adwaita icon theme.** The application draws its own iconography with Cairo paths and requests
  no icon by name, so the only icons reachable at runtime are the symbolic ones GTK's widgets ask
  for — the search entry's clear button, dropdown arrows, spin button steppers. Only Adwaita's
  `symbolic` tree ships; the full-colour and legacy raster trees, which are the bulk of the theme,
  are dropped. Adwaita cannot be dropped altogether: GTK4 embeds only its `image-missing` fallback,
  so without it those widgets render blank.
- **GSettings schemas.** Only the compiled blob is kept, never the `.xml` sources.

If you add an image format, an icon looked up by name, or a new dependency, re-check those
assumptions. The script prints the bundle's total size and its ten largest binaries on every run,
which makes an unexpected jump traceable to whatever caused it.

The installer compresses with solid LZMA, which suits a payload of structurally similar DLLs far
better than the default deflate, so `setup.exe` is typically well under half the size of the zip.

### Installer behaviour

`cmake/installer.nsi.in` installs per-user, into `%LOCALAPPDATA%\Programs`. That needs no elevation:
only one user runs the application and it never writes to its own directory, so Program Files would
mean a UAC prompt on an unsigned binary for nothing. The installer clears `bin\`, `lib\` and
`share\` before copying, so upgrading cannot leave a stale GTK DLL behind — one sitting next to the
executable would win every lookup. Uninstalling removes only those three directories and the
uninstaller, then removes the install directory itself non-recursively, so anything the user put
alongside the application survives.

The executable stays in `bin\` beside its DLLs, because Windows resolves a PE's implicit imports
from the executable's own directory and there is no way to redirect that. Shortcuts are created in
the Start Menu and in the install root, so browsing the installation shows the application rather
than the forty-odd DLLs it links against. Moving the DLLs to a shared location such as `System32`
is not an option: GLib finds its schemas, gdk-pixbuf loaders and icon theme relative to the
directory `libgtk-4-1.dll` itself lives in, so from `System32` it would search
`C:\Windows\share\glib-2.0\schemas` and GTK would abort at startup.

The script is a template: CMake expands its `@VAR@` placeholders into `build/installer.nsi`, and
`makensis` is then invoked with no options at all. Passing the values as `makensis /D` defines
instead would be fragile on three counts — the option prefix differs between the Windows and POSIX
makensis builds, values containing spaces depend on the invoking shell's quoting, and MSYS2
rewrites arguments that look like POSIX paths. Paths are injected in native backslash form, because
NSIS's `File` and `Icon` do not reliably accept the forward slashes CMake uses internally. If the
installer ever misbehaves, read the generated `build/installer.nsi` directly; it is the exact input
makensis saw.

Note that the resulting `setup.exe` is unsigned, so SmartScreen will warn on first run until the
download builds reputation. Code signing is the only real fix.

## Troubleshooting

**"pkg-config not found"**

- Install the platform package listed above (`pkg-config`, `pkgconf`, or `mingw-w64-x86_64-pkgconf`) and ensure its
  directory is on `PATH`.
- Verify the command is available:

  ```bash
  pkg-config --version
  ```

**"gtk/gtk.h: No such file or directory"**

- Install the GTK4 development package for your platform.
- Check that CMake's package environment can see GTK4:

  ```bash
  pkg-config --modversion gtk4
  pkg-config --cflags --libs gtk4
  ```
- For unofficial macOS builds, make sure Homebrew's `pkgconf` and `gtk4`prefixes are on `PATH`/`PKG_CONFIG_PATH`. On
  Windows, run these commands from the matching MSYS2 MinGW64 environment.

**"ld.exe: cannot find -lgtk-4" (Windows)**

- Use the MinGW64 environment consistently and install `mingw-w64-x86_64-gtk4` and `mingw-w64-x86_64-pkgconf`.
- Do not mix libraries discovered by MSYS2 with a different compiler or architecture.

**CMake reports an imported target contains a non-existent include or library path**

- A package manager upgrade may leave the CMake cache pointing at an older dependency directory. With CMake 3.24 or
  newer, reconfigure with a fresh cache:

  ```bash
  cmake --fresh -S . -B build
  ```

- If the problem persists, remove only the affected build directory and configure it again. Do not reuse a cache after
  changing Homebrew prefixes or switching MSYS2 environments.

**"`glib-compile-resources` not found"**

- Install the GLib development package and verify that the command is on `PATH`.
- Verify that `gio-2.0` is visible to `pkg-config`:

  ```bash
  pkg-config --modversion gio-2.0
  ```

**"Football Manager process not found" or memory access is denied**

- Start Football Manager 24 and load the save before starting the application.
- Confirm that the process/module is exposed under the expected `fm.exe` name.
- Check the platform-specific process permissions described in Runtime Requirements.

**The application cannot start because no display is available**

- Run it from an active Linux X11/Wayland or Windows desktop session. An unofficial macOS build requires a macOS desktop
  session. A headless shell alone is not sufficient.

**Build fails with compiler errors**

- Confirm that the selected compiler supports C99 and that CMake is using the intended compiler/toolchain.
- After changing compilers or package environments, configure a fresh build directory rather than reusing the previous
  cache.

### Automated release builds

The **Windows release** GitHub Actions workflow runs the same steps on a Windows runner. Publishing a GitHub release
attaches the installer and zip to it automatically; running the workflow by hand from the Actions tab keeps them as a
downloadable workflow artifact instead. The file names come from `PROJECT_VERSION` in `CMakeLists.txt`, not from the
release tag, so bump the version there before tagging a release.

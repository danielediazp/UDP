# UDP

## Setup

This project uses C++23 modules, which need a modern Clang and Ninja (Apple's
bundled clang and the default Makefiles generator don't support building
`.cppm` files). Steps below are for macOS.

1. Install [Homebrew](https://brew.sh) if you don't already have it.
2. Install the build tools:
   ```
   brew install llvm cmake ninja
   ```
3. (VS Code users) Install the **C/C++** and **CMake Tools** extensions from
   the Extensions panel.
4. Open this repository in VS Code (or from a terminal, `cd` into it) — the
   repository root is already wired up with a `CMakePresets.json` that points
   at the Homebrew LLVM compiler and the Ninja generator.
5. Configure and build:
   ```
   cmake --preset default
   cmake --build --preset default
   ```
6. Run it:
   ```
   build/udp
   ```

In VS Code, the same configure/build/run steps are available from the CMake
Tools status bar at the bottom of the window instead of the terminal.

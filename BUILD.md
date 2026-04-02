## Build Instructions

Pick the section that matches your platform or toolchain. In most cases the built binaries end up in a `build` or platform-specific output directory, and you can either copy them into your Quake III folder or use the provided `make install DESTDIR=<path_to_game_files>` flow where available.

### windows/msvc

Install Visual Studio Community Edition 2017 or later, then open and build the `fnquake3` solution:

`code/win32/msvc2017/fnquake3.sln`

The resulting executable is written to `code/win32/msvc2017/output`.

If you want the Vulkan backend, clean the solution, right-click the `fnquake3` project, open `Project Dependencies`, and select `renderervk` instead of `renderer`.

---

### windows/msys2

Install the build dependencies first:

`MSYS2 MSYS`

* pacman -Syu
* pacman -S make mingw-w64-x86_64-gcc mingw-w64-i686-gcc

Use `MSYS2 MINGW32` or `MSYS2 MINGW64` depending on your target system, then either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_game_files>`

---

### windows/mingw

All required build dependencies, including libraries and headers, are bundled in.

Build with either `make ARCH=x86` or `make ARCH=x86_64` depending on your target system, then either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_game_files>`

---

### generic/ubuntu linux/bsd

On a fresh Ubuntu-style install, you will likely need packages like these first:

* sudo apt install make gcc libcurl4-openssl-dev mesa-common-dev
* sudo apt install libxxf86dga-dev libxrandr-dev libxxf86vm-dev libasound-dev
* sudo apt install libsdl3-dev

Then build with: `make`

After that, either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_game_files>`

Current SDL backend dependency baseline: `SDL3 >= 3.2.0`

---

### Arch Linux

Use the generic Linux instructions above. This repository does not currently document an official Arch package name under the FnQuake3 branding.

---

### raspberry pi os

Install the build dependencies:

* apt install libsdl3-dev libxxf86dga-dev libcurl4-openssl-dev

Then build with: `make`

After that, either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_game_files>`

---

### macos

* install the official SDL3 framework to `/Library/Frameworks`
* run `brew install molten-vk`, or install the Vulkan SDK if you want to use the MoltenVK library

Then build with: `make`

Copy the resulting binaries from the `build` directory.

---

### ppc64le / ppc64 (PowerPC 64-bit)

Install the same build dependencies as the generic Linux section above, then build with:

`make`

The JIT compiler (`vm_powerpc.c`) supports optional ISA-level optimizations that are enabled automatically based on compiler target flags:

* **ISA 2.07 (POWER8)**: Uses direct-move instructions (`mtvsrwa`, `mfvsrwz`, `xscvdpsxws`) to eliminate memory round-trips in float/int conversions (`OP_CVIF`, `OP_CVFI`)
* **ISA 3.0 (POWER9)**: Uses hardware modulo instructions (`modsw`, `moduw`) to replace 3-instruction sequences for `OP_MODI` and `OP_MODU`

To enable these optimizations, pass the appropriate `-mcpu` flag:

`make CFLAGS='-mcpu=power8'` - enable ISA 2.07 optimizations

`make CFLAGS='-mcpu=power9'` - enable ISA 2.07 + ISA 3.0 optimizations

`make CFLAGS='-mcpu=native'` - auto-detect based on build machine (note: resulting binary may not be portable to older hardware)

Without an explicit `-mcpu`, those optimizations depend on the compiler and distro defaults. The JIT falls back cleanly to baseline instruction sequences when the target ISA level is not available.

---

Several Makefile options are available for Linux, MinGW, and macOS builds:

`BUILD_CLIENT=1` - build unified client/server executable, enabled by default

`BUILD_SERVER=1` - build dedicated server executable, enabled by default

`USE_SDL=0` - disable the SDL3 backend for video, audio, and input and use the legacy non-SDL Unix backend instead; SDL3 is enabled by default and enforced for macOS

`USE_VULKAN=1` - build vulkan modular renderer, enabled by default

`USE_OPENGL=1` - build opengl modular renderer, enabled by default

`USE_OPENGL2=0` - build opengl2 modular renderer, disabled by default

`USE_RENDERER_DLOPEN=1` - do not link a single renderer into the client binary; compile all enabled renderers as dynamic libraries and allow switching on the fly via the `\cl_renderer` cvar, enabled by default

`RENDERER_DEFAULT=opengl` - set the default value for `\cl_renderer`, or choose the renderer used for a static build when `USE_RENDERER_DLOPEN=0`; valid options are `opengl`, `opengl2`, `vulkan`

`USE_SYSTEM_JPEG=0` - use current system JPEG library, disabled by default

Example:

`make BUILD_SERVER=0 USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=vulkan` - build the client with a single static Vulkan renderer and skip the dedicated server binary

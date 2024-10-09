# SuperBLT
An open source Lua hook for Payday 2, designed and created for ease of use for both players and modders.

This is a unofficial continuation of the BLT modloader, with additional features aimed at allowing things
not possible in standard Lua, such as patching XML files that are loaded directly by the engine, or playing
3D sounds.

This is the developer repository, and should only be used if you know what you're doing (or running GNU+Linux - explained below). If you don't, visit the website at [SuperBLT.znix.xyz](https://superblt.znix.xyz/) to get an up-to-date drag-drop install.
The Lua component of the BLT which controls mod loading can be found in it's own repository, [payday2-superblt-lua](https://gitlab.com/znixian/payday2-superblt-lua).

## Download
Visit [The SuperBLT Site](https://superblt.znix.xyz/) to get the latest stable download for Windows.
Dribbleondo maintains a Linux download that has compiled loaders for Debian-based distro's and Arch-based distro's,
which [can be downloaded from here](https://modworkshop.net/mod/36557). If your distro or Linux OS cannot run the precompiled loader,
see below for building GNU+Linux binaries for yourself.

If you are on Linux, It is **strongly** advised that you **DO NOT** use the Flatpak version of Steam when running Payday 2 with SuperBLT enabled, as the Flatpak version of Steam imports various library dependencies that improves compatibility between OS's, while breaking the SuperBLT loader in the process. **You have been warned.**

## Documentation

Documentation for all SuperBLT functions can be found on the [SuperBLT Website](https://superblt.znix.xyz).

## Development

How to contribute to SuperBLT:

SuperBLT uses the CMake build system. This can generate a variety of build files,
including GNU Makefiles and MSVC Project/Solution files, from which the program can
then be built.

SuperBLT depends on two sets of libraries: those commonly available (such as OpenSSL
and ZLib) which you are expected to install via your package manager (GNU+Linux) or
by downloading and compiling (Windows).

It also has libraries that are very small projects, rarely available in package managers,
that come included as git submodules. These are automatically added as targets by CMake,
so you don't need to make any special efforts to use them.

### GNU+Linux

If you are using SELinux, you may require running either `sudo setsebool -P selinuxuser_execheap 1` or `sudo setenforce 0` before
doing anything. This allows the loader to adjust the memory of the PAYDAY 2 executable.

First, clone this repository, and pull all required projects and repositories into one folder (Note: You **NEED** to do this, otherwise you'll get runtime and compile errors):

```
git clone --recursive https://gitlab.com/znixian/payday2-superblt.git
```

Second, create and `cd` into a build directory:

```
mkdir build
cd build
```

Then run CMake:

```
cmake ..
```

Alternatively, if you want debug symbols for GDB, add it as an argument:

```
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

If the previous step told you that you're missing some libraries (which is quite likely), install them, and be
sure to install their development headers too. This can be done via your package manager.

These packages are normally not installed by default, such as: `openssl`, `libssl1.1`, `libcurl4-openssl-dev`, `libssh2-1-dev`, `libpng-dev`, `zlib-dev` (often called `zlib1g-dev` in debian distro's), and `libopenal-dev`. This is distribution and OS-dependant (these packages are named differently on arch-based systems, for example), and may require installing the non-dev-header packages too (`libpng-dev` and `libpng` for example). On Debian Distro's, the command to install all these dependancies looks like:

```
sudo apt-get install openssl libssl1.1 libssl-dev libcurl4 libcurl4-openssl-dev libopenal1 libopenal-data libopenal-dev zlib1g zlib1g-dev libpng-dev libssh2-1 libssh2-1-dev
```

Next, compile the loader. You can speed up the compilation process by replacing the number
"4" with the amount of threads your CPU has (generally it's twice the core count, so
for example: 2 cores will have 4 threads, 4 cores will have 8 threads, and so on):

```
make -j 4
```

Go into the build folder and copy the resulting `libsuperblt_loader.so` into the root directory
of PAYDAY 2's install folder (the same folder as the binary executable; `payday2_release`).

And finally, add the `LD_PRELOAD` enviornment variable to Steams' Launch Options:

```
env LD_PRELOAD="$LD_PRELOAD ./libsuperblt_loader.so" %command%
```

This environment variable will tell the game to look for the SuperBLT loader in PAYDAY 2's root directory when you run the game.

Be sure to install the basemod from [GitLab:znixian/payday2-superblt-lua](https://gitlab.com/znixian/payday2-superblt-lua),
as the automatic installer isn't currently implemented on GNU+Linux.

### Windows

First, clone this repository, and pull all required projects and repositories into one folder (Note: You **NEED** to do this, otherwise you'll get runtime and compile errors):

```
git clone --recursive https://gitlab.com/znixian/payday2-superblt.git
```

Next open it in your IDE of choice. In CLion, select `File -> Open` and the folder created
by git clone. For Visual Studio, select `File -> Open -> CMake` and select the top-level
`CMakeLists.txt` file.

Next set your IDE to build a 32-bit binary. In CLion first ensure you have a x86 toolchain
configured, in `File | Settings | Build, Execution, Deployment | Toolchains` - add a new
Visual Studio toolchain and set the architecture to `x86`. Ensure this toolchain is selected
in `File | Settings | Build, Execution, Deployment | CMake`, in your `Debug` configuration and
inside the toolchain box. Once CMake is done, select `WSOCK32` in the target box.

To enable multithreaded builds in CLion (which massively improves build times) you need to
download [JOM](https://wiki.qt.io/Jom). In the toolchain you added earlier, set the `Make`
field to the path of the `jom.exe` file you extracted earlier.

In Visual Studio, select the configuration box (at the top of the window, which may for
example say `x64-Debug`) and select `Manage Configurations...` if it isn't set to `x86-Debug` already.
Remove the existing configuration then add a new one, and select `x86-Debug`.
Select `Project->Generate Cache` and wait for it to run cmake - this may take some time.

At this point you can compile your project. In CLion, Click the little green hammer in the top-right
of CLion (the shortcut varies depending on platform). In Visual Studio, press F7. In either case this
will take some time as it compiles all of SuperBLT's dependencies, and finally SuperBLT itself.

Finally, you can make PAYDAY use your custom-built version of SBLT instead of having to copy the built
file to the PAYDAY directory each time you change something.
To do this, go to your `PAYDAY 2` directory and open PowerShell. Run:

```
cmd /c mklink WSOCK32.dll <path to SBLT>\out\build\x86-Debug\WSOCK32.dll
```

for Visual Studio, or:

```
cmd /c mklink WSOCK32.dll <path to SBLT>\cmake-build-debug\WSOCK32.dll
```

for CLion.

### Code Conventions
- Avoid `std::shared_ptr` and the likes unless you have a decent reason to use it. If you
need the reference counting go ahead, but please don't use it when a regular pointer would
work fine.
- Don't **ever** use CRLF.
- Please ensure there is a linefeed (`\n`) as the last byte of any files you create.
- Please use `git patch`, don't commit multiple unrelated or loosely related things in a
single commit. Likewise, please don't commit whitespace-only changes. `git blame` is a valuable
tool.
- Please run the source code though `clang-format` to ensure stuff like brace positions and whitespace
are consistent. Later, this will be put into a Continuous Integration task to mark offending
commits, along with testing stuff like compiling in GCC.
- Please ensure your code doesn't cause any compiler warnings (not counting libraries). This is
enforced for GCC, please watch your output if you're using Visual Studio.

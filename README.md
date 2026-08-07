# PAYDAY2-SuperBLT (64 Bit) ![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/diesel-modding/PAYDAY2-SuperBLT/create_build.yml)

An open-source Lua hook for PAYDAY 2, designed and created for ease of use for both players and modders.

This is an unofficial continuation of the BLT and SuperBLT mod loader for PAYDAY 2, with additional features aimed at allowing things
not possible in standard Lua, such as patching XML files that are loaded directly by the engine or playing
3D sounds.

This is the developer repository and should only be used if you know what you're doing. ~~If you don't, visit the website at [modworkshop.net: PAYDAY 2-SuperBLT](https://modworkshop.net/mod/49744) for an up-to-date drag-drop install.~~
The Lua component of the BLT, which controls mod loading, can be found in its own repository, [GitHub: diesel-modding/PAYDAY2-SuperBLT-Lua](https://github.com/diesel-modding/PAYDAY2-SuperBLT-Lua).

## Download
~~Visit ![modworkshop.net: PAYDAY 2-SuperBLT](https://modworkshop.net/mod/49744) to get the latest stable download.~~

Visit here to download the latest build: [Nightly.link: Nightly Release](https://nightly.link/diesel-modding/PAYDAY2-SuperBLT/workflows/create_build/master/Build%20Artifacts.zip)

You will need to also install the Base mod from: [GitHub: diesel-modding/PAYDAY2-SuperBLT-Lua](https://github.com/diesel-modding/PAYDAY2-SuperBLT-Lua) into your mods folder.

## Documentation

Documentation for SuperBLT can be found on the [SuperBLT Website](https://superblt.znix.xyz). (ignore everything related to XAudio! It has been deprecated.)

## Development

How to contribute to SuperBLT:

First, clone this repository and pull all required projects and repositories into one folder (Note: You **NEED** to do this, otherwise you'll get runtime and compile errors):

```
git clone --recursive https://github.com/diesel-modding/PAYDAY2-SuperBLT.git
```

if you cloned without `--recursive`, do this in the root of your repo:
```
git submodule update --init --recursive
```

You can use Visual Studio or the command line to generate the files and build SuperBLT.

For Visual Studio, select `File -> Open -> CMake` and select the top-level
`CMakeLists.txt` file.

In Visual Studio, select the configuration box (at the top of the window, which may, for
example, say `x64-Debug`) and select `x64-Debug` if it isn't already.
Select `Project->Generate Cache` and wait for it to run cmake - this may take some time.

You can now open the generated solution file in `/out/build/x64-Debug/SuperBLT.sln`

If you do not see the `out` folder, click "Show All Files" in the top bar of the Solution Explorer.

If you don't see the solution file, please ensure the configurations have `Visual Studio 16/17 Win64` selected as the cmake generator.

At this point, you can compile your project. In Visual Studio, press F7.
This will take some time as it compiles all of SuperBLT's dependencies and, finally, SuperBLT itself.

Finally, you can make PAYDAY 2 use your custom-built version of SBLT instead of having to copy the built
file to the PAYDAY 2 directory each time you change something.
Go to your `PAYDAY 2` directory and open PowerShell to do this. Run:

```
cmd /c mklink WSOCK32.dll <path to SBLT>\out\build\x64-Debug\WSOCK32.dll
```

For the command line, navigate to your SuperBLT folder using `cd`

Make a new directory named `build` using `mkdir build` and enter it using `cd`

Then enter the following commands:
`cmake .. -A x64 -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug`

`msbuild SuperBLT.sln /t:Build /p:Configuration=Debug`

You can symlink using the following in your `PAYDAY 2` directory. Run:

```
cmd /c mklink WSOCK32.dll <path to SBLT>\build\Debug\WSOCK32.dll
```

### Code Conventions
- Avoid `std::shared_ptr` and the likes unless you have a decent reason to use it. If you
need the reference counting, go ahead, but please don't use it when a regular pointer works fine.
- Don't **ever** use CRLF.
- Please ensure there is a linefeed (`\n`) as the last byte of any files you create.
- Please use `git patch.` Don't commit multiple unrelated or loosely related things in a
single commit. Likewise, please don't commit whitespace-only changes. `git blame` is a valuable
tool.
- Please run the source code using `clang-format` to ensure stuff like brace positions and whitespace
are consistent. Later, this will be put into a Continuous Integration task to mark offending
commits and test stuff like compiling in GCC.
- Please ensure your code doesn't cause any compiler warnings (not counting libraries). This is
enforced for GCC; please watch your output if you're using Visual Studio.

# SuperBLT DLL Changelog

This lists the changes between different versions of the SuperBLT DLL,
the changes for the basemod are listed in their own changelog.
Contributors other than maintainers are listed in parenthesis after specific changes.

## Version 3.4.1
- Added extra functionality to read language specific files (steam-test1)
- Added functionality to load custom soundbanks (HW12Dev)
- Fixed a potential stack corruption (HW12Dev)
- Improved wren error message clarity (vojin154)

## Version 3.4.0
- Updated to use absolute paths to fix issues with mod organizer (ZNix)

## Version 3.3.9

- Updated used libraries (test1)
- Added DB API for native plugins (Joppe)
- Added native plugin unloading (yupiel)

## Version 3.3.8

This is a hotfix for asset database loading

- Fixed loading assets from database (Cpone)

## Version 3.3.7

- Fixed signatures for U237 (Bo)
- Increased robustness of log function
- Increased error tolerance for XML parsing
- Fixed blt.parsexml reporting incorrect XML files on error

## Version 3.3.6

- Added a VR check for Wren
- Updated Wren to the latest version (test1)
- Added min and max audio distance for XAudio sources
- Fixed crash with VR version of the game

## Version 3.3.5

This is a hotfix for a missing function signature

- Updated OpenSSL, ZLib and OGG decoder (test1)
- Implemented SystemFS:delete_file (Der Muggemann)
- Disabled HRTF to improve audio quality (test1)

## Version 3.3.4

- Improved Linux specific code (Azer0s, whitequark)
- Improved XML parsing
- Updated Lua function signature (Cpone)

## Version 3.3.3

- Fixed AssetDB Idstring parsing
- Improved Linux specific code (CraftedCart)
- Updated to a less strict, custom version of mxml
- Reduced amount of http request logging

## Version 3.3.2

- Fixed crashes on computers with all.blb or any other files with names shorter than eight letters in the assets directory
- Fixed a compile issue on Linux (roberChen)

## Version 3.3.1 and below

These versions do not have nicely formatted changelogs, but you can see their Git commit history in GitLab if required.

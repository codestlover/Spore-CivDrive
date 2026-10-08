
CivDrive 1.0.1 is licensed under GPL-3.0-or-later.

Spore ModAPI SDK: Copyright Eric Mor and contributors; GPL-3.0-or-later.
CivDrive uses its headers and links SporeModAPI.lib; src/math/VMath.hpp follows the quaternion and matrix
conventions of the SDK's Math.cpp.
The full license and warranty disclaimer are in licenses/GPL-3.0.txt.
SDK source: https://github.com/Spore-Community/Spore-ModAPI

Microsoft Detours: Copyright Microsoft Corporation; MIT License.
The compiled DLL links Detours v4.0.1 (commit e4bfd6b) built from its sources, which cmake/setup.sh
fetches into deps/Detours; the Source.zip inside the release .sporemod includes them.
The full license is in licenses/Detours-MIT.md.
Source: https://github.com/microsoft/Detours

EASTL and EABase: Copyright (C) 2015 Electronic Arts Inc.; BSD 3-Clause License.
Their headers come with the Spore ModAPI SDK and are compiled into the DLL.
The full license is in licenses/EASTL-BSD.txt.
Source: https://github.com/electronicarts/EASTL

The scripts in cmake/ (setup.sh, layout.sh, format.sh and the toolchain file) are taken from Civ Challenge,
by the same author, GPL-3.0-or-later: https://github.com/codestlover/Spore-CivChallenge

Spore and Galactic Adventures are owned by Electronic Arts/Maxis. No game executable, assets, save data, or
decompiled game code are included in the mod. The minimap marker is cut at run time from the game's own icon
atlas, and the transition shader is compiled at run time by the game's own d3dx9.


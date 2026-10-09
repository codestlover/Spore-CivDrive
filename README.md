
# CivDrive

Third-person control of your own vehicles in the **Civilization stage** of *Spore Galactic Adventures*. Take any of
your land vehicles, ships or aircraft and drive it yourself: the camera sits behind it, WASD drives, the mouse aims
and fires. Everything the vehicle does still goes through the game's own rules, ranges and cursors.

Current version: **1.0.6**. Download `CivDrive.sporemod` from the [Releases](../../releases) page.

## Controls

| Action | How |
| --- | --- |
| Take a vehicle | **Double-click** one of your vehicles on the planet or its icon in the vehicle list, or select it and **double-tap Shift** |
| Drive, sail, fly | **W / S** forward and back, **A / D** turn (the arrow keys work too) |
| Turn the camera | Hold the **mouse wheel** or the **right mouse button** and move the mouse. Moving down lowers the camera behind the vehicle and turns it up to the sky (up to about 72°), so aircraft overhead can be seen and attacked from land and sea |
| Zoom | Mouse wheel |
| Aim | Hover a target: the game's own cursor appears (attack, spice claim or conversion, whichever the game would show) |
| Weapon range | A ring on the ground shows the vehicle's native weapon range. Green: the hovered target is in range; orange: drive closer. Aircraft targets get their ring at their own height |
| Attack with land and sea vehicles | **One left click** on the target. The vehicle opens fire once the target is within its native range |
| Attack with aircraft (lasers) | **Hold the left button** to fire and release it to stop. While holding you can move the aim to another target |
| Out of range | A click on a target out of range plays the game's refusal sound and gives no order |
| Stop the current attack | A short right click (without moving the mouse) |
| Claim a free spice source | The claim radius is read from the game's own claim behaviour (**24 m** from the source's centre) and drawn around the hovered source. Click inside it and the derrick starts at once; the vehicle stays where it is |
| Another nation's spice derrick | Military and religious vehicles attack or convert it from their native range. With an economic vehicle, drive inside the derrick's green ring, click it, then click **Bribe the Workers** in the native menu |
| Trade with or buy another nation's city | With an economic vehicle, click its **city hall**. The native menu offers only **Propose Trade Route / Trade With City** and **Buy City**, subject to the game's reachability, funds and trade progress rules. One click opens the menu for land, sea and air vehicles |
| Raid a tribe | Nothing to aim at: **drive into a tribe hut**. The game gets the raid order at that moment (let go of WASD on the hut and the game drives the last metres) |
| Capture a city | Destroy the city's buildings and turrets first (click them as usual). Then, with a military vehicle, hover the enemy city hall: the attack cursor appears. A click gives the game's own city attack order, the one the AI uses: the vehicle fires at the city hall from its native weapon range, and when the hall falls the city is captured, with the game's own cinematic. While buildings or turrets are left, a click on the city hall does nothing, as in normal play |
| Switch vehicles | **Double-click another of your vehicles** on the planet or in the list: the camera flies over and the control moves to it. A click on your own vehicle is never an attack |
| Back to the strategic view | **Double-tap Shift**. The view flies back to the strategic camera, which stays where it was |
| Game menu | **Esc** ends the control at once (the strategic camera jumps over the vehicle) and opens the game's menu |

On the **minimap** the game's green camera arrow stays where it is, and the vehicle you drive is marked with a
smaller **yellow copy** of it (the game's own icon, recoloured), pointing where the vehicle heads.

## What it does

- **Entering and leaving** is a smooth camera flight of about a second with a custom pixel shader
  (`shaders/transition_ps.hlsl`): a radial zoom blur, chromatic aberration, speed lines, a flash and a vignette. A
  faint vignette stays while you drive. The game's own d3dx9 compiles the shader the first time you enter.
- **Attacks are native.** A click goes to the same game function as a normal click with a selected vehicle, so
  attacks on vehicles, cities and turrets, spice claims, conversion and trade follow the game's rules. **The ranges
  are native too**: the weapon's own range check, the spice source's own claim radius, and the raid on arrival at
  the hut, as the game does it.
- **No auto-attack.** While you drive, the vehicle neither fires on its own nor takes orders from the AI, for every
  kind of vehicle: its stance (the red and blue buttons) has no effect, it does not return fire and does not shoot
  at enemies nearby. Any order or combat target the game gives it without your command is refused. It fires only
  on your click (aircraft only while the button is held).
- **Economic vehicles can trade and buy targets while you drive.** Click a foreign spice derrick to bribe its
  workers, or a foreign city hall for a menu with trade and purchase actions. The menu buttons work during piloting;
  clicking a menu never also attacks the object behind it. For economic vehicles, only spice targets change the
  range ring's color. A foreign derrick's ring is green inside and orange outside, using the same radius for drawing
  and checking range. Opening its bribe menu and issuing the bribe order both require being inside the green zone;
  driving away after opening the menu does not bypass that check. Cities, buildings and vehicles keep white rings
  because economic vehicles cannot attack. City trade and purchase still use the game's native conditions.
  Once you select an action, release WASD to let the vehicle complete its native order. After a city accepts a trade
  proposal, the vehicle automatically starts deliveries; choosing **Trade With City** a second time is not required. Pressing **WASD or any arrow key** cancels that
  vehicle's trade/proposal order and returns manual control immediately. Releasing the key does not resume
  deliveries; select **Trade With City** again when you want to restart them. The trade agreement stays in place.
- **Movement rules.** Land vehicles do not drive into cities (the walls and the gates and dock that stick out of
  them) or into the water (the nose of the vehicle is checked too, and long steps are checked in pieces). Ships stay
  in the water. At shores and walls the vehicle slides along the obstacle. Aircraft keep their height over the ground
  and the water. The speed is the vehicle's native speed.
- **It drives on the ground**: the vehicle tilts with the slope, its height is smoothed and a light suspension dips
  it when it speeds up or brakes and leans it in turns. Ships rock on the swell.
- **Sound, animation and shadows as in normal play.** The game gets the vehicle's real velocity, so wheels and
  tracks turn, the engine sounds and dust, wakes and contrails appear. The audio listeners follow the camera and the
  vehicle, so engines, weapons and city alarms nearby are heard. Shadows are computed around the vehicle, even when
  the strategic camera is far away.
- While you drive, the game does not move the vehicle itself (even the attack AI cannot drag it away), and after you
  leave it stays where you left it. The city pop-up is hidden, except over a city hall, where it shows the city's
  information as in normal play.
- **Cinematics and the city editor keep working.** When the game plays a cinematic (a city capture, for example),
  opens the city editor or switches to another camera, it gets the camera, the sound and the controls back for that
  time, and the vehicle waits. Afterwards the view flies back behind the vehicle and you carry on driving.
- Pausing the game stops your vehicle too. Minimising the game does not end the control.

## Installation

1. Install the [Spore ModAPI Launcher Kit](https://davoonline.com/sporemodder/rob55rod/ModAPI/Public/index.html)
   if you do not have it yet.
2. Open `CivDrive.sporemod` with **Spore ModAPI Easy Installer**, or copy `CivDrive.dll` into
   `Spore ModAPI Launcher Kit/mLibs/` by hand.
3. Start the game with **Spore ModAPI Launcher**. If the game was already running, restart it completely.

To remove the mod, use **Spore ModAPI Easy Uninstaller → CivDrive**, or delete `mLibs/CivDrive.dll`.

## Compatibility

The mod creates no files at all: no settings and no log. On start it checks the game's code byte for byte. It needs
the Galactic Adventures "march2017" code (`SporeApp_ModAPIFix.exe`, or the October 2024 Steam and GOG build, which
has the same code). If the code is different, or another mod changed the same places in an unexpected way, CivDrive
changes nothing and a double click on a vehicle simply does nothing.

Mods that hook the same game functions work alongside it, in any load order. For example,
[Civ Challenge](https://github.com/codestlover/Spore-CivChallenge) also hooks vehicle orders and the start of a spice
derrick: CivDrive puts its hook in front of the other mod's, the other mod does the same, and both run. Orders that
you give through the game's interface (the city capture button) are recognised even when another mod's hook sits in
front of CivDrive's. When another mod refuses a raid (Civ Challenge's *No land raids* rule), driving into a hut plays
one refusal, and CivDrive does not try that hut again until the vehicle has driven away.

## Building from source (Linux)

> **A note from the author:** I build and test this mod only on Linux. I personally have no idea how to build it on
> Windows, so there are no Windows build instructions here and I cannot help with a Windows build.
> To play the mod, you don't need to build anything: just use the `.sporemod` from Releases.

The mod is a 32-bit Windows DLL cross-compiled with clang-cl. Tested with:

- LLVM 21.1.8: `clang-cl`, `lld-link`, `llvm-lib`, `llvm-mt` and `llvm-rc` in `PATH` (on Debian and Ubuntu the last
  three come with the `llvm` package)
- CMake 4.3.4 (3.20 or newer is required) and `make`
- GNU `bash` 4.4 or newer, coreutils, findutils and grep (with `-P`), plus `git`, `curl`, `unzip` and `tar`
- clang-format 21, only for `cmake/format.sh`

Like every file of the project, each script starts with an empty line (see
[Formatting and the layout guard](#formatting-and-the-layout-guard)), so the scripts have no shebang line and are run
with `bash`.

### Build

```sh
git clone https://github.com/codestlover/Spore-CivDrive.git && cd Spore-CivDrive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-clangcl-i686.cmake \
    -DCIV_ACCEPT_MSVC_LICENSE=ON
cmake --build build
```

This produces `build/bin/CivDrive.dll`. The build is reproducible: with CMake 3.27 or newer and the pinned
dependencies it gives byte-identical binaries in any folder, so a build can be compared with the `CivDrive.dll`
inside the release `.sporemod`.

The transition shader is embedded into the DLL as text at build time and compiled by the game's own d3dx9 at run
time, so building needs no shader compiler.

### Dependencies

The first `cmake` run downloads everything the build needs into `deps/`, which git ignores, by running
`cmake/setup.sh`:

- `deps/Spore-ModAPI`: the [Spore ModAPI SDK](https://github.com/Spore-Community/Spore-ModAPI) at tag `v2.5.559`,
  checked by commit hash.
- `deps/sdk`: its headers with a few small fixes so that clang-cl accepts them.
- `deps/Detours`: [Microsoft Detours](https://github.com/microsoft/Detours) at tag `v4.0.1`, checked by commit hash.
- `deps/modapi`: `SporeModAPI.lib` from the `v2.5.559` release, checked by SHA-256.
- `deps/msvc-sdk`: the MSVC CRT 14.44 and Windows SDK 10.0.26100 for x86, downloaded with
  [xwin](https://github.com/Jake-Shadle/xwin) 0.10.0. If `xwin` 0.10.0 is not in `PATH`, the script downloads that
  release into `deps/.xwin` and checks it by SHA-256 (x86_64 and aarch64 Linux).

The MSVC CRT and Windows SDK come under the Microsoft license and are not redistributed.
`-DCIV_ACCEPT_MSVC_LICENSE=ON` means you accept that license; without it the first `cmake` run stops and explains
the choice. If you already have an xwin "splat" tree, pass `-DCIV_MSVC_SDK=/path/to/tree` instead (keep it outside
the repository). To keep the dependencies outside the repository, pass `-DCIV_DEPS=/some/dir` or set `CIV_DEPS` in
the environment before the first `cmake` run: an absolute path to a folder used only for these dependencies. The
same folder can be shared with [Civ Challenge](https://github.com/codestlover/Spore-CivChallenge), which prepares
the same dependencies with the same script. To change `CIV_DEPS` or `CIV_MSVC_SDK` later, configure a new build
folder. `deps/` needs about 1 GB; xwin's download cache is removed once the SDK is unpacked.

Later runs reuse `deps/`. `setup.sh` records its own checksum in `deps/.setup-stamp`, so when a new version of the
script arrives (for example after `git pull`), the next `cmake` run prepares the dependencies again. The script can
also be run by hand: `bash cmake/setup.sh --accept-msvc-license`. It also accepts `--xwin PATH`, `--xwin-cache PATH`
and `--msvc-sdk PATH`, and reads `CIV_DEPS` from the environment.

### Testing and releases

Releases are checked with an offline replica of the mod's own code checks against the game executable, a test of the
vector maths, and a Wine test that the game's caller of an order is found through another mod's hook in front of
CivDrive's. These tools and the packaging scripts work on the game's own code, which cannot be redistributed, so they
are not part of this repository, and the `.sporemod` is only published on the [Releases](../../releases) page.

## Project layout

| Path | Contents |
| --- | --- |
| `src/core` | Entry point and hooks, the fixed tuning values |
| `src/game` | Access to the game: functions through the ModAPI SDK's address table and raw addresses, each checked byte for byte; the caller detection |
| `src/pilot` | The control mode: input, driving, camera, attacks, spice claims, raids, the minimap marker |
| `src/render` | The transition layer with its shader, the range rings and the minimap marker |
| `src/math` | Vector maths |
| `shaders` | The transition pixel shader |
| `cmake` | The clang-cl cross toolchain and the scripts `setup.sh`, `layout.sh`, `format.sh` |

### Formatting and the layout guard

```sh
bash cmake/format.sh
```

This formats the C++ sources with clang-format 21 and fixes the framing of the files. To use a specific clang-format
binary, set `CLANG_FORMAT=/path/to/clang-format`.

The build also enforces the project layout with `cmake/layout.sh`. It checks every file of the project tree except
the build and dependency folders and editor and tool caches:

- Every text file starts with exactly one empty line and ends with exactly two.
- C++ headers use `.hpp`.
- `src/` keeps its files in subdirectories.

CMake runs this check at configure time and before every build. Any violation stops the build with a list of files.
`bash cmake/layout.sh --fix` fixes the framing. The first rule is also why the scripts have no shebang: `#!` only
works on the very first line of a file.

## Credits and license

CivDrive is licensed under **GPL-3.0-or-later** (see [`LICENSE`](LICENSE)).

- [Spore ModAPI SDK](https://github.com/Spore-Community/Spore-ModAPI) by Eric Mor and contributors, GPL-3.0-or-later.
- [Microsoft Detours](https://github.com/microsoft/Detours), MIT, fetched by `cmake/setup.sh`.
- The build scripts in `cmake/` come from [Civ Challenge](https://github.com/codestlover/Spore-CivChallenge).

Full notices are in [`NOTICE.md`](NOTICE.md) and [`licenses/`](licenses). Spore and Galactic Adventures belong to
Electronic Arts/Maxis. This repository and the release contain no game executables, game assets, save data or
decompiled game code. The minimap marker is cut at run time from the game's own icon atlas; no game image is
included.


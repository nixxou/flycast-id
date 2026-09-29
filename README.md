# flycast-id

Gives you the game id [Flycast](https://github.com/flyinghead/flycast) uses for a game, the one its per-game settings are saved under in `emu.cfg` (`[T-8120N]`, `[MARVEL VS CAPCOM2  JAPAN]`, `[  18WHEELER]`...).

For a Dreamcast disc that's the product number in the IP.BIN, easy enough. For arcade games it's the title in the cartridge boot header, and Flycast only knows it once it has loaded and decrypted the cartridge. So this tool does exactly that, with Flycast's own code: same rom table, same cartridge classes, same decryption, same disc readers. You get the id byte for byte, spaces included.

Handy if you write a frontend or a launcher and want to read or write a game's own Flycast config before the game has ever been started.

## Usage

```
flycast-id.exe <rom>
```

On success it prints one line and exits with 0:

```
id<TAB>MARVEL VS CAPCOM2  JAPAN
```

Anything else (unknown set, missing file, broken rom, not a Dreamcast disc, no id in the header) prints the reason on stderr, nothing on stdout, and exits with 1. It never prints an empty id. Leading spaces are part of some ids, keep them.

It reads the rom and whatever Flycast itself would read next to it, and writes nothing. No Flycast install, BIOS or `emu.cfg` needed. One exe, no DLL next to it.

## What it reads

| | |
|---|---|
| Arcade sets | `.zip`, `.7z`: Naomi, Naomi 2, Atomiswave, System SP (M1, M2, M4 and AW cartridges). The parent set is picked up next to a clone |
| Naomi GD-ROM | the `.zip` of the set, with its `<set>\<gdl-xxxx>.chd` next to it, same layout as MAME and Flycast |
| Legacy decrypted roms | `.bin`, `.dat`, `.lst` |
| Dreamcast discs | `.chd`, `.gdi`, `.cdi`, `.cue` |

Same as Flycast: no `.iso`, and a disc without a Dreamcast IP.BIN has no id.

Tested against a full MAME 0.288 set: every cartridge set Flycast knows gets its id, checked against what Flycast itself logs for a few of them (`mvsc2`, `18wheelr`, `ggisuka`). GD-ROM games go through the same code as Flycast but I had no image at hand to check one.

## Building

Windows, MSVC, CMake and Ninja (a Visual Studio 2022 developer prompt has them all).

```
git clone https://github.com/nixxou/flycast-id.git
cd flycast-id
git submodule update --init flycast
git -C flycast submodule update --init core/deps/libchdr
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`build\flycast-id.exe` is the tool. To build against another Flycast checkout, pass `-DFLYCAST_SOURCE_DIR=<path>`.

Flycast's sources are compiled as they are, nothing is copied in here. The only part taken out of a Flycast file is the base cartridge methods of `naomi_cart.cpp`, cut out between two markers when cmake runs. What the cartridge code would call in the rest of the emulator (scheduler, bus registers, interrupts...) is stubbed in `stubs.cpp`, none of it is reached while an id is read.

## License

GPL-2.0, since it's Flycast's code. See [LICENSE](LICENSE).

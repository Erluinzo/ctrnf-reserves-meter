# Reserves Meter (CTR Nitro-Fueled, OCTR PC port)

A game patch by Onur: shows your boost reserves as a bar and in seconds, in every boost bar style.
For the USA game, CUSA13795 version 1.21, on the [OCTR port](https://github.com/Ryu7w7/OCTR-NitroFueled-PC).

## What it does
**Reserves** are the boost time you have stored by chaining boosts. The game does not show them; this
patch does, under the lap counter at the top right of the race HUD:
- **The number:** seconds of reserves left, for example `12.4`.
- **The bar:** one layer for every 10 seconds, each new layer filling up over the full one below it.

  | Reserves | Bar | Frame |
  |---|---|---|
  | 0 to 10 s | green fills up | gray |
  | 10 to 20 s | red fills up over full green | red |
  | 20 to 30 s | blue-white fills up over full red | blue-white |
  | above 30 s | stays full blue-white (the number keeps counting) | blue-white |

It works with **every boost bar style** (Classic, Speedometer and Nitro Fueled), so you do not have to
use the CNK style. The meter hides in the pause menu.

## Install
In the OCTR launcher: Mods > Add files, choose `reserves-meter-1.0.0.zip` from the
[releases](https://github.com/Erluinzo/ctrnf-reserves-meter/releases), enable it, fully restart the game.

## Limits
- **One local player only.** In split screen the meter is not shown.
- **Offline only.** The launcher blocks online play while the mod is enabled.
- **Tested** in Time Trial and Single Race (all three boost bar styles, real boosts, pause menu).
  **Not tested yet:** Adventure, Battle, Ring Rally (the meter may cover part of the ring counter) and
  the end of a race (the meter should hide with the rest of the HUD).
- It cannot be used together with another patch that changes the same game code (for example another
  reserves meter); the launcher refuses the combination.

## How it works
The patch adds two small hooks to the game:
- after the game updates your kart's boosts, it works out the reserves the same way the game does
  (the time left on the running boost);
- at the start of each race HUD update, it adds copies of the game's own HUD pieces once (the lap
  counter text, the classic boost bar frame and the Nitro Fueled bar's fill pieces) to the top level of
  the HUD, outside every boost bar style, and then sets their text, width and colors.

No game files are changed. Notes on the game code: `notes/`.

## Credits
Idea from tomtotom's [CNK Drift Gauge + Reserves Meter](https://gamebanana.com/mods/729041). This patch is
written from scratch and uses none of its code or files.

## Building (developers)
Needs the OCTR checkout at `../CTRNF` (with `out/eboot.elf` from a build), gcc and binutils
(`tools/elf_look.py` also needs `pip install capstone`).

```sh
python3 build.py         # writes package/patches/ReservesMeter.xml (+ a switched-on copy in ../CTRNF/patches)
python3 tools/tune.py    # state of the meter in the running game (Linux); see its help for live changes
```

## License
MIT, see `LICENSE`.

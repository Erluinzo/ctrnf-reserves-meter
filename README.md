# Reserves Meter (CTR Nitro-Fueled, OCTR PC port)

A game patch by Onur: shows your boost reserves as a bar and in seconds, in every boost bar style.
For the game version 1.21, USA (CUSA13795) or EU (CUSA14876), on the [OCTR port](https://github.com/Ryu7w7/OCTR-NitroFueled-PC).

## What it does
**Reserves** are the boost time you have stored by chaining boosts. The game does not show them; this
patch does, under the lap counter at the top right of the race HUD:
- **The number:** the time left on your boost, in seconds, for example `12.4`. It counts down while
  you drive, and every boost you chain on top (drift, jump, boost pad, item) adds to it. At `0.0` your
  boost is over.
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
Download the ZIP for your game from the [releases](https://github.com/Erluinzo/ctrnf-reserves-meter/releases):
`reserves-meter-<version>.zip` for USA (CUSA13795), `reserves-meter-eu-<version>.zip` for EU (CUSA14876).
In the OCTR launcher: Mods > Add files, choose it, enable it, fully restart the game. To update, add the
new ZIP and remove the old one. Use only the ZIP for your game: with the other one enabled, the game does
not start ("executable patch targets a different game edition/version").

## Limits
- **One local player only.** In split screen the meter is not shown.
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
Needs the OCTR checkout at `../CTRNF` (with `out/eboot.elf` from a USA build, and the EU executable at
`orig/CUSA14876-eboot.elf`), gcc and binutils
(`tools/elf_look.py` also needs `pip install capstone`).

```sh
python3 build.py         # writes package/ (USA) and package-eu/ (EU) (+ switched-on copies in ../CTRNF/patches)
python3 tools/tune.py    # state of the meter in the running game (Linux); see its help for live changes
```

## License
MIT, see `LICENSE`.

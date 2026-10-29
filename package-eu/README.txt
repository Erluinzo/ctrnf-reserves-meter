Reserves Meter 1.1.0
Author: Onur

Shows your boost reserves in the race HUD: the boost time you have stored by
chaining boosts. You see it as a number of seconds ("12.4") and as a bar, under
the lap counter at the top right.

The number is the time left on your boost: how many seconds of extra speed you
still have. It counts down while you drive, and every boost you chain on top
(drift, jump, boost pad, item) adds to it. At 0.0 your boost is over.

It works with every boost bar style: Classic, Speedometer and Nitro Fueled.
You do not have to use the CNK style.

THE BAR
  0 to 10 s:   green fills up
  10 to 20 s:  red fills up over the full green bar
  20 to 30 s:  blue-white fills up over the full red bar
  above 30 s:  the bar stays full blue-white; the number keeps counting
The bar frame takes the color of the layer that is filling up.

LIMITS
- One local player only: in split screen the meter is not shown.
- It cannot be used together with another mod that changes the same game code
  (for example another reserves meter). The launcher tells you.

Idea from tomtotom's "CNK Drift Gauge + Reserves Meter". This mod is written
from scratch and uses none of its code or files.

GAME: Crash Team Racing Nitro-Fueled version 1.21, OCTR launcher with
code-signature support. There is one ZIP per game edition:
  reserves-meter-<version>.zip      USA (CUSA13795)
  reserves-meter-eu-<version>.zip   EU (CUSA14876), shown as "Reserves Meter (EU)"
Use only the one for your game: with the other one enabled, the game does not
start ("executable patch targets a different game edition/version"). The
launcher checks the game code before it applies the patch.

INSTALL: Mods > Add files, choose the ZIP for your game, enable it, fully
restart the game.
UPDATE: add the new ZIP and remove the old one.
UNINSTALL: disable or remove it.

CHANGES
1.1.0: EU edition (CUSA14876) supported, as a second ZIP. Allowed online (it
only changes what your own screen shows); 1.0.0 was marked offline only.

Source code (MIT license): https://github.com/Erluinzo/ctrnf-reserves-meter

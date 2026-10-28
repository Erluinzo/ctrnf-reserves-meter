#!/usr/bin/env python3
"""Reads or changes the reserves meter's state in the running game (Linux; /proc/PID/mem).

  tune.py                 prints the state
  tune.py text X Y        moves the seconds text (HUD space, 1920 x 1080)
  tune.py bar X Y         moves the bar
  tune.py full SECONDS    reserves for a full bar

The layout must match `struct state` in src/reserves.c.
"""
import struct, subprocess, sys

PROBE = 0xfcc430    # a game function whose first bytes are known: finds the image base
PROLOGUE = bytes.fromhex('554889e541574156415541545348')
STATE = 0x2beef80
# boosts, reserves, root, text, bar, fill, shown, hidden, layout_version, layout_changed,
# text_pos[3], bar_pos[3], full, hud, project
LAYOUT = struct.Struct('<Qf4xQQQQiiii3f3ff4xQQ')


def find_base(mem, pid):
    for line in open(f'/proc/{pid}/maps'):
        start = int(line.split('-')[0], 16)
        try:
            mem.seek(start + PROBE)
            if mem.read(len(PROLOGUE)) == PROLOGUE:
                return start
        except OSError:
            pass
    sys.exit('game image not found')


def main():
    pid = int(subprocess.check_output(['pgrep', '-x', 'sacredfire']).split()[0])
    with open(f'/proc/{pid}/mem', 'r+b') as mem:
        base = find_base(mem, pid)
        mem.seek(base + STATE)
        v = list(LAYOUT.unpack(mem.read(LAYOUT.size)))
        args = sys.argv[1:]
        if args:
            if args[0] in ('text', 'bar') and len(args) == 3:
                at = 10 if args[0] == 'text' else 13
                v[at], v[at + 1] = float(args[1]), float(args[2])
            elif args[0] == 'full' and len(args) == 2:
                v[16] = float(args[1])
            else:
                sys.exit(__doc__)
            v[9] = 1    # layout_changed: the game applies it on its next HUD update
            mem.seek(base + STATE)
            mem.write(LAYOUT.pack(*v))
    print(f'reserves {v[1]:.2f} s, shown {v[6]}, hidden {v[7]}; root {v[2]:#x} text {v[3]:#x} bar {v[4]:#x} fill {v[5]:#x}')
    print(f'hud {v[17]:#x} project {v[18]:#x}')
    print(f'text at ({v[10]:.0f}, {v[11]:.0f}), bar at ({v[13]:.0f}, {v[14]:.0f}), full bar = {v[16]:.1f} s, layout v{v[8]}')


if __name__ == '__main__':
    main()

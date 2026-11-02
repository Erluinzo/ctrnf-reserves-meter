#!/usr/bin/env python3
"""Builds the reserves meter patch, one mod package per game edition (the launcher needs each
package's XML to target one edition, and cannot pick between two ZIPs of one mod id), and a
switched-on test copy of each XML in the game's patches folder:
  package/patches/ReservesMeter.xml           USA CUSA13795 1.21
  package-eu/patches/ReservesMeter-EU.xml     EU CUSA14876 1.21, with mod.json and README.txt made from package/
  python3 build.py

Each edition's executable is only read: to check the original bytes at every write and to compute
the ExpectedSHA256 values the launcher checks before it applies the patch. The EU addresses were
found with ../CTRNF-Mods/tools/eu_map.py (same functions, moved by 0 to -0x40).
"""
import hashlib, json, re, shutil, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME_DIR = HERE.parent / 'CTRNF'
VERSION = '1.2.0'
EBOOT_BASE = 0x400000       # patch XML addresses are ELF addresses + 0x400000

CODE = 0x2339000            # unused zero padding at the end of the code segment (USA 0x2317810-0x233c078,
                            # EU 0x23177d0-0x233c038); before: ghost sharing (to 0x2337fff), Time Trial
                            # (to 0x23381f1)
STATE = 0x2beef80           # after the game's data (ends 0x2bee66d) and ghost sharing's state (to 0x2beef80)
STATE_END = 0x2bef000       # the page end
MAX_WRITE = 4096
# (target symbol, original bytes, new instruction, padding): each replaces whole instructions; the
# address of each is in the edition's 'hooks'
HOOKS = [
    ('hook_boost', 'c5fa104218e906000000', 'e9', 5),       # boost component, every frame
    ('hook_hud', '554889e54157', 'e9', 1),                 # racer HUD, every frame
]
# The owner tests each build at once: a copy goes into the game's patches folder, switched on.
EDITIONS = {
    'CUSA13795': dict(
        name='USA', elf=GAME_DIR / 'out/eboot.elf', xml=HERE / 'package/patches/ReservesMeter.xml',
        test_copy=GAME_DIR / 'patches/ReservesMeter.xml', code_end=0x233c078,
        hooks={'hook_boost': 0xe75240, 'hook_hud': 0xe5aa30},
        game={
            'game_boost_update': 0xe75250, 'game_hud_body': 0xe5aa36, 'game_maintained': 0xe2f050,
            'game_find_component': 0x3dbf90, 'game_spawn_placeable': 0x253c00, 'game_set_text': 0x268ef0,
            'game_set_position': 0x268f90, 'game_set_scale': 0x269120, 'game_set_pivot': 0x269190,
            'game_set_color': 0x269460,
            'game_set_opacity': 0x2694a0, 'game_set_hidden': 0x2694d0, 'game_handle_resolve': 0x149fca0,
            'game_local_players': 0xc21630, 'game_string': 0x1574080, 'game_string_release': 0x1518e30,
            # data
            'g_boost_meta': 0x2ac4018, 'g_players': 0x2ad1160, 'g_string_pool': 0x2af2600, 'rv': STATE, 'g_spectate_kart': 0x2beef78, 'g_race_replay': 0x2beea78,
            'g_composition_vtable': 0x2431b18,      # igGuiComposition objects start with this
        }),
    'CUSA14876': dict(
        name='EU', elf=GAME_DIR / 'orig/CUSA14876-eboot.elf', xml=HERE / 'package-eu/patches/ReservesMeter-EU.xml',
        test_copy=GAME_DIR / 'patches/ReservesMeter-EU.xml', code_end=0x233c038,
        hooks={'hook_boost': 0xe75230, 'hook_hud': 0xe5aa30},
        game={
            'game_boost_update': 0xe75240, 'game_hud_body': 0xe5aa36, 'game_maintained': 0xe2f050,
            'game_find_component': 0x3dbf90, 'game_spawn_placeable': 0x253c00, 'game_set_text': 0x268ef0,
            'game_set_position': 0x268f90, 'game_set_scale': 0x269120, 'game_set_pivot': 0x269190,
            'game_set_color': 0x269460,
            'game_set_opacity': 0x2694a0, 'game_set_hidden': 0x2694d0, 'game_handle_resolve': 0x149fc90,
            'game_local_players': 0xc21630, 'game_string': 0x1574070, 'game_string_release': 0x1518e20,
            # data
            'g_boost_meta': 0x2ac4018, 'g_players': 0x2ad1160, 'g_string_pool': 0x2af2600, 'rv': STATE, 'g_spectate_kart': 0x2beef78, 'g_race_replay': 0x2beea78,
            'g_composition_vtable': 0x2431b18,
        }),
}

CFLAGS = ['-c', '-O2', '-std=c11', '-fpie', '-ffreestanding', '-fno-stack-protector',
          '-fno-asynchronous-unwind-tables', '-fno-unwind-tables', '-fcf-protection=none', '-mno-red-zone',
          '-fvisibility=hidden', '-fno-tree-loop-distribute-patterns', '-Wall', '-Wextra', '-Werror']
# Only RIP-relative references: the game image is not loaded at a fixed address.
RELATIVE = {'R_X86_64_PC32', 'R_X86_64_PLT32'}

LINKER_SCRIPT = '''SECTIONS {
  . = %#x;
  .text : { *(.text .text.*) *(.rodata .rodata.*) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
'''


def read(elf, va, n):
    # The code segment starts at file offset 0x4000 with address 0.
    return elf[0x4000 + va:0x4000 + va + n]


def run(*command):
    subprocess.run([str(c) for c in command], check=True)


def build_code(tmp, game):
    objects = []
    for source in ('reserves.c', 'mem.c'):
        obj = tmp / (source + '.o')
        run('gcc', *CFLAGS, '-o', obj, HERE / 'src' / source)
        objects.append(obj)
    obj = tmp / 'hooks.o'
    run('as', '--64', '-o', obj, HERE / 'src/hooks.s')
    objects.append(obj)
    for obj in objects:
        relocs = subprocess.run(['readelf', '-rW', obj], capture_output=True, text=True, check=True).stdout
        kinds = set(re.findall(r'\bR_X86_64_\w+', relocs))
        if kinds - RELATIVE:
            sys.exit(f'{obj.name}: not RIP-relative: {sorted(kinds - RELATIVE)}')
    script = tmp / 'link.ld'
    script.write_text(LINKER_SCRIPT % CODE)
    linked = tmp / 'patch.elf'
    run('ld', '-T', script, '-o', linked, '-e', 'rv_hud_update',
        *[f'--defsym={name}={address:#x}' for name, address in game.items()], *objects)
    sections = subprocess.run(['readelf', '-SW', linked], capture_output=True, text=True, check=True).stdout
    # [Nr] Name Type Address Off Size ES Flg ...: only .text may take space in memory
    for name, size, flags in re.findall(r'\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+[0-9a-f]+\s+(\S*)', sections):
        if 'A' in flags and name != '.text' and int(size, 16):
            sys.exit(f'unexpected section {name}: the code segment cannot hold writable data')
    symbols = {}
    for line in subprocess.run(['nm', linked], capture_output=True, text=True, check=True).stdout.splitlines():
        address, _, name = line.split()
        symbols[name] = int(address, 16)
    binary = tmp / 'patch.bin'
    run('objcopy', '-O', 'binary', '-j', '.text', linked, binary)
    return binary.read_bytes(), symbols


def build(title, e):
    elf = e['elf'].read_bytes()
    with tempfile.TemporaryDirectory() as tmp:
        code, symbols = build_code(Path(tmp), e['game'])
    if CODE + len(code) > e['code_end']:
        sys.exit(f'code is {len(code)} bytes: does not fit before {e["code_end"]:#x}')
    if read(elf, CODE, len(code)) != bytes(len(code)):
        sys.exit(f'{e["elf"]}: the area at {CODE:#x} is not empty')
    data_end = 0x2a00000 + 0x1ee66d
    if STATE < data_end or STATE + 0x80 > STATE_END:
        sys.exit('state area is not in the free space after the game data')

    lines = []
    for target, original, opcode, padding in HOOKS:
        address = e['hooks'][target]
        original = bytes.fromhex(original)
        if read(elf, address, len(original)) != original:
            sys.exit(f'{e["elf"]}: the code at {address:#x} is not the expected {e["name"]} 1.21 code')
        rel = symbols[target] - (address + 5)
        new = bytes.fromhex(opcode) + rel.to_bytes(4, 'little', signed=True) + b'\x90' * padding
        assert len(new) == len(original), hex(address)
        # Signature: the replaced bytes, 8 bytes before them and at least 8 after, unique in the
        # executable (function starts look alike, so the part after may need to be longer)
        for after in range(8, 41, 4):
            signature = read(elf, address - 8, len(original) + 8 + after)
            if elf.count(signature) == 1:
                break
        else:
            sys.exit(f'signature at {address:#x} is not unique')
        lines.append((address, new, signature, 8))
    # The port's loader takes at most 4096 bytes per write (src/probe.c, "bad patch entry")
    for at in range(0, len(code), MAX_WRITE):
        lines.append((CODE + at, code[at:at + MAX_WRITE], None, 0))

    def line(va, value, signature, offset):
        sha = hashlib.sha256(read(elf, va, len(value))).hexdigest()
        extra = f' Signature="{signature.hex(" ")}" SignatureOffset="{offset:#x}"' if signature else ''
        return (f'      <Line Type="bytes" Address="{va + EBOOT_BASE:#010x}" Value="{value.hex()}"'
                f'{extra} ExpectedSHA256="{sha}" />')

    xml = f'''<?xml version="1.0" encoding="utf-8"?>
<!-- Built by build.py from src/: edit those, not this file.{" EU edition." if e["name"] == "EU" else ""}
     Reserves meter: shows your boost reserves as a bar and in seconds, in every boost bar style
     (Classic, Speedometer, Nitro Fueled), with copies of the game's own HUD pieces. One local
     player. The last lines are the new code ({len(code)} bytes). -->
<Patch>
  <TitleID><ID>{title}</ID></TitleID>
  <Metadata Title="Crash Team Racing Nitro-Fueled" Name="Reserves Meter"
            Note="Shows your boost reserves as a bar and in seconds, in every boost bar style."
            Author="Onur" PatchVer="{VERSION}" AppVer="01.21" AppElf="eboot.bin">
    <PatchList>
{chr(10).join(line(*l) for l in lines)}
    </PatchList>
  </Metadata>
</Patch>
'''
    e['xml'].parent.mkdir(parents=True, exist_ok=True)
    e['xml'].write_text(xml, encoding='utf-8')
    print(f'{e["xml"].relative_to(HERE)}: {len(HOOKS)} hooks, code {len(code)} bytes at {CODE:#x}')
    if e['test_copy'].parent.is_dir():
        e['test_copy'].write_text(xml.replace('AppElf="eboot.bin">', 'AppElf="eboot.bin" isEnabled="true">'),
                                  encoding='utf-8')
        print(f'test copy, switched on: {e["test_copy"]}')


def eu_package():
    """package-eu/ gets the USA package's README and manifest, with its own id and name"""
    manifest = json.loads((HERE / 'package/mod.json').read_text())
    assert manifest['version'] == VERSION, 'package/mod.json has another version'
    manifest.update(id=manifest['id'] + '-eu', name=manifest['name'] + ' (EU)',
                    patches=['patches/ReservesMeter-EU.xml'], files=[])
    (HERE / 'package-eu/mod.json').write_text(json.dumps(manifest, indent=2) + '\n')
    shutil.copy(HERE / 'package/README.txt', HERE / 'package-eu/README.txt')


def main():
    for title, e in EDITIONS.items():
        build(title, e)
    eu_package()


if __name__ == '__main__':
    main()

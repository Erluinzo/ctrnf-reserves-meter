# Boost reserves on the HUD: research (USA CUSA13795 1.21)

ELF addresses of `CTRNF/out/eboot.elf` (patch XML = + 0x400000). Read from code and assets only; nothing was run.
"Verified" = read in code or asset. "Guess" / "unverified" = not tested in game.
tomtotom's mod was not opened; everything here comes from the original executable and the original .pak files.
Field names come from the reflection tables (method in `CTRNF-Mods/notes/time-trial.md`).

## Answer (recommended, code only, no asset change)
1. Hook the boost component's per-frame update (0xe75240). After the game updates the boost list, compute the reserves of
   the watched kart and store one float (seconds). Same thread as the code that changes the list, so no race.
2. Hook the racer HUD component's per-frame update (0xe5aa30, entry). Once per HUD project, **clone** two existing
   placeables with the game's own spawn function 0x253c00 into the HUD root: the classic turbo meter background
   (with its fill as child) and the lap-counter text. Our clones have no VSC behaviors, so the style switch
   (Classic / Speedometer / Nitro Fueled, also the in-race toggle) never hides or moves them. Each frame set the text
   with 0x268ef0 ("12.4") and the fill width with 0x269120 (scale x).
3. No .igz is changed, so no conflict with widescreen layouts or other mods. Nothing else on the HUD is touched.

## 1. The reserves value (verified in code)
Reserves are not one field. They are the time left of the kart's active boost: when a new boost starts, the game moves the
time left of the running boost into the new one (or adds the new boost's time to the running one if that one is faster).

Classes (reflection):
- `CKartBoostComponent` (0x70, meta pointer variable `[0x2ac4018]`): +0x18 entity (owner, CComponent), **+0x28 `_kartBoosts`**
  (igObjectList: +0xc count, +0x20 item array), +0x30 `_pendingBoosts`, +0x38 `_currentBoostSpeed`, +0x54 `_currentBoostIntensity`,
  +0x60 `_vehicleComponent`, +0x68 `_damageComponent`.
- `CKartBoost` (0x50): +0x10 `_boostData`, **+0x18 `_elapsedTime`** (float s), +0x1c `_boostRatio`, +0x20 `_entity`,
  **+0x28 `_boostSustainDuration`** (float s, grows when boosts are chained), +0x2c `_maximumSpeed`, +0x30 `_isMaintained`,
  +0x31 `_isPermanent`, +0x34 `_maintainStopTime` (int, -1 = none), +0x38 `_boostStartTime` (int), +0x40 `_owner`, **+0x48 `_blendingOut`**.
- `CKartBaseBoostData` / `CKartBoostData`: +0xc `_speed`, **+0x1c `_boostInDuration`**, +0x20 `_boostSustainDuration`, +0x24 `_boostOutDuration`;
  CKartBoostData adds +0x28 `_maximumSpeed` ... +0x3b `_boostIsMaintainable`, +0x3c `_isABoostPad`, +0x3d `_canBeSuperBoost`, +0x3e `_isADriftBoost`.

Code:
- 0xe2e6b0 `add boost (component, boostData, ?)` (caller 0xe2dd10; `applyKartBoost_VSC` 0x112dd80 -> 0x1041110 -> 0xe2dd10; 9 more callers in kart code):
  picks `active` = the fastest boost in `_kartBoosts` with `_blendingOut == 0` (loop 0xe2e946/0xe2eae0; ties: the later one),
  creates the new CKartBoost (factory 0x1128700), init 0xe2ef40 (`sustain = data+0x20`, `maintainStop = -1`, `start = clock`), appends it, then
  - active faster than new (0xe2e98c): `active.sustain += new.data.in + new.data.sustain`;
  - else (0xe2e9ab): `new.sustain += max(0, maintained(active) + active.data.in + active.sustain - active.elapsed)`.
  No upper limit is applied here (none found).
- 0xe75240 per-frame callback `(component, rsi, rdx = context)`: `xmm0 = [rdx+0x18]` (frame time, s), `jmp 0xe75250`.
  0xe75250 `(component, xmm0 dt)`: for each boost 0xe75750 adds dt to `_elapsedTime` (0xe7575c) and sets `_boostRatio`; it sets
  `_blendingOut = 1` (0xe75822) after in + sustain; removes a boost when `elapsed > in + sustain + out + maintained` (0xe7533f-0xe75377).
- 0xe2f050 `float maintained(CKartBoost*)`: extra time while a maintainable boost (e.g. standing on a pad) is held:
  `(now or _maintainStopTime) - _boostStartTime` in 1/8192 s ticks (constant 0x1caa188 = 0.0001220703125) minus `data.in`, >= 0. Pure read.

So, in seconds (float):
```
reserves = max(0, active.data._boostInDuration + active._boostSustainDuration + maintained(active) - active._elapsedTime)
```
This is exactly the value the game carries into the next boost (0xe2e9ab). The fade-out part (`_boostOutDuration`) is not counted.
Typical data (`update2.pak LooseData/DrivingArchetypes.igz`): drift boosts in 0.1-0.2 s, sustain 0.067-0.4 s;
pads (`LooseData/CollisionMaterials.igz`, track files) sustain 0.27-1 s, maintainable. Max: none in the game; pick a full-scale value for the bar (guess: 3 s).

Getting the kart:
- Component lookup: `void *0x3dbf90(entity, meta, 0)` (searches `[[entity+0x18]+0x20]`, count +0xc). Boost component:
  `0x3dbf90(entity, [0x2ac4018], 0)` (as 0xe25a08 and 0xe74f56 do).
- The HUD component's entity `[hud+0x18]` is the kart: 0xe32185 puts it into `CGuiEventRacerSetSpeedometerVisible._entity`, and the
  Speedometer VSC passes that to `CKartNodes::getForwardSpeedValues_VSC` 0x112ebe0 -> 0x1041ce0, which looks up `CKartControllerComponent` on it.
- Local player: `CRacerHudComponent` exists only for real local players (HUD init 0xe65b20 asserts "only works on real players").
  AI, ghosts and online opponents have none. So the kart whose HUD runs is the local player's kart.
- Number of local players: `0xc21630([[0x2ad1160]+0x10])` (used by 0xe685c0 and 0xe31fb6). 1 = single screen.

## 2. HUD styles (verified in asset + code)
- Option text (`update2.pak ui/Octane_Options_Menu_Page_en.lng`): **"Preferred Boost Bar"** ("Select the default Boost Bar.") with
  **Classic / Speedometer / Nitro Fueled**. In race the "Toggle map / speedometer" button (hint: "Press Triangle to switch between three options while racing") cycles them.
- Enum `EOctaneBoostBarType` (names 0x26d8e50): 0 Classic, 1 Speedometer, 2 NitroFueled.
- Stored: `COctaneGlobalPlayerOptionsData._boostGaugeStyle` (+0x14, per player: `[[[0x2acde48]+0xdb0]+0x90+8*i]`) and
  `COctaneGlobalOptionsData._boostGaugeStyle` (+0x24). Current one: `CRacerHudComponent._boostBarIndex` (+0x8c).
- `CRacerHudComponent` (0x90): +0x18 entity, +0x28 `_racer` (handle), +0x38 **`_guiRacerHUDProject`** (handle), +0x40 EndRaceTimer, +0x48 countdown,
  ... +0x80 `_isInitialized`, +0x84 `_totalLap`, +0x88 `_finalLapTriggered`, +0x89 `_isRaceStarted`, +0x8a `_allowSpeedometerToggle`, +0x8c `_boostBarIndex`.
- HUD init 0xe65b20: project name from 0xe685c0: 1 player "Octane_Racer_HUD", 2 "Octane_Racer_HUD_2_Vertical", 3-4 "Octane_Racer_HUD_4";
  opened with 0x986820 into +0x38; then 0xe66d4f-0xe66d7c: `_boostBarIndex` = player option, `0xe32050(hud, index)`, `_isInitialized = 1`.
- 0xe32050 `(hud, index)`: new `CGuiEventRacerSetSpeedometerVisible` (factory 0x11989d0; +0x18 `_boostBarIndex`, +0x20 `_entity`, +0x28 `_isRaceStarted`),
  broadcast to the HUD project (`vtable+0xb0`). Toggle 0xe31f80: 1 player and race mode 1..6: `(index+1) % 3`, saved to the player option;
  split screen: only Classic <-> Nitro Fueled.
- How each style is made (`update2.pak ui/Octane_Racer_HUD.igz`, VSCs in `juicedomain_race.pak vsc/UI/...`): one project holds all of them; VSCs react to the event:
  - `Octane_NFBoostBar_Root` / `..._Nitro_Fueled_Boost_Bar`: opacity 1 only when index == 2, else 0 (NF bar with boost count).
  - `Octane_BoostBar_Root` (classic turbo meter, `ui/GuiSharedAssets/Octane_BoostBar.igz`): opacity per index (shown in Classic and Speedometer).
  - `Octane_Racer_HUD_Old_Boost_Bar_Positioning`: moves `Comp__Octane_BoostBar` to (1732,1008) for Classic, (1699,1008) for Speedometer.
  - `Octane_Racer_HUD_Speedometer`: needle from `getForwardSpeedValues_VSC`, opacity by index.
  - Same in every style: timer (`Comp__Timer`, '[Timer]', 'Lap Timer 1..7'), lap display (`Comp__Octane_RacerHud_LapDisplay`), position, Wumpa, power-up.
  No boost element is visible in all three styles, and no style shows reserves.
- Style switching only changes opacity/position of placeables that carry these VSCs. A placeable without VSC under the HUD root is never touched.

## 3. Ways to show bar + seconds
### A. Clone at run time (recommended; verified functions, untested together)
- `igGuiPlaceable *0x253c00(igGuiProject *project, igGuiPlaceable *source, igGuiPlaceable *parent)`: copies `source` (0x253d50, deep copy with
  children), adds it to `parent` (0x251fe0; parent 0 or of another project -> project root `+0x10`), links it last in the draw order, returns it
  (borrowed: the parent's child table holds the reference; 0 on failure). Used by the game: `spawnPlaceable_VSC` 0x26fa70, 0x9d3e80, 0x9d8640.
  Prefer `source = live->_sourcePlaceable` (+0x18, the template the live one came from), like the game spawns from templates; fall back to the live one.
- Templates (both exist in every 1-player HUD and in all styles, they just get hidden by opacity of their parents):
  - Text: placeable named **'[LapCounterText]'** (`_name` +0x28; `ui/GuiSharedAssets/Octane_RacerHud_LapDisplay.igz`, 140x45, no behaviors).
    Also in the split-screen HUDs. 1-player only alternatives: 'Lap Timer 1' (GeekSpeak, 160x40) or '[Timer]'.
  - Bar: placeable whose asset (`+0x38` handle -> igGuiSprite, name at asset +0x10) is **"[octane_ui_hud_boost_bg.tga]"**
    (`Octane_BoostBar.igz` placeable 0x3d0: 392x148, scale 0.824x0.737, no behaviors). Its only child is the fill
    "[octane_ui_hud_boost_fill.tga]" (0x520: 77.5x80, pivot (1,0), scale (0.95,0.35)). The game fills it by animating the fill's
    `_scale` from (0, 0.35, 1) to (2.77, 0.35, 1) (track `AnimationTag_BoostFill`, used by `common_Meter_Bar_Root_c`). So: `scale.x = 2.77 * ratio`.
- Tree walk: children = `[pl+0x48]` table (+0xc count, +0x20 items), as 0x24e510 does (composition contents are children at run time:
  the style VSCs inside compositions receive the project broadcast, which only walks `_children`).
- Setters (each writes the field and sets the dirty bit `byte [pl+0xd] |= 0x10`):
  | ELF | Call |
  |---|---|
  | 0x268ef0 | `set_DisplayText(pl, const char *text)` (makes its own string; compares first) |
  | 0x268f90 | `set_Position(pl, const float v[3])` (+0x60) |
  | 0x269120 | `set_Scale(pl, const float v[3])` (+0x84) |
  | 0x269190 | `set_Pivot(pl, const float v[3])` (+0x6c) |
  | 0x2691f0 | `set_Extent(pl, const float v[3])` (+0x90) |
  | 0x2693d0 | `set_TexCoordMax(pl, const float v[2])` (+0xb0) |
  | 0x269460 | `set_Color(pl, const uint8_t rgba[4])` (+0xc0, /255) |
  | 0x2694a0 | `set_Opacity(pl, float)` (xmm0, +0xe0) |
  | 0x2694d0 | `set_Hidden(pl, bool)` (bit 0x400 of +0xc) |
  (from the `igGuiPlaceable::get_/set_..._VSC` table built at 0x169f0eb-0x169f39f; pointers at 0x2807170, 0x2807240, ...)
- Handle read (copy of 0xe32076-0xe32112 / 0xe320b4-0xe320f2):
  ```c
  static void *handle_object(void **slot)
  {
      for (;;) {
          int32_t *h = *slot;
          if (!h) return 0;
          if (*h & 0x10000000) { game_handle_resolve(slot); h = *slot; if (!h) return 0; }   /* 0x149fca0 */
          void *o = *(void **)((char *)h + 0x28);
          if (!(*h & 0x20000000) || !o) return o;
          slot = (void **)((char *)o + 0x10);    /* the handle points to another handle */
      }
  }
  ```
  Project = `handle_object(&hud+0x38)`, root = `[project+0x10]` (`igGuiProject._rootPlaceable`).
- Our clones are children of the root, outside every style group, so they show in Classic, Speedometer and Nitro Fueled.
  Widescreen layouts scale the HUD in the shader, so the clones are scaled like the rest.

### B. Text-only bar (simplest fallback)
Clone only '[LapCounterText]' twice: one shows "12.4", one shows a bar of characters (e.g. 20 x '|'). Same calls, no sprite. Plain ASCII in GeekSpeak/Zoinks fonts should draw (unverified).

### C. Asset change (not recommended)
Add two placeables (text + sprite) under the root of `update2.pak ui/Octane_Racer_HUD.igz` (new objects, ROFS/RVTB/RHND fixups, children table grows).
The package format can deliver it (`archive_asset`: archive, asset, index, original/patched SHA-256, payload; `docs/mod-packaging.html`),
but it needs an IGZ editor that adds objects and handles, only works for this one HUD file (not split screen), and any other mod
that ships this file conflicts. Widescreen layouts do not change this file (they change `Octane_NFBoostBar.igz`).
Code would still be needed for the value.

## 4. Hooks (bytes verified; behavior untested)
| ELF | Original bytes | What | Live |
|---|---|---|---|
| 0xe75240 | `c5 fa 10 42 18 e9 06 00 00 00` (+6 x `90`) | boost component per-frame callback | rdi = CKartBoostComponent, rdx = context (dt at +0x18) |
| 0xe5aa30 | `55 48 89 e5 41 57` (then `41 56 41 55 41 54 53 50 8a 05 ..`) | `CRacerHudComponent` per-frame callback (registered at 0xe5924a by 0xe59210) | rdi = CRacerHudComponent |
- 0xe75240: only referenced by `lea` at 0xe74f36 (register) and 0xe7514d (unregister); nothing jumps inside. Its 16 bytes appear twice in the
  ELF (also 0xf79bd0); with the 8 bytes before (`90 x 8`, from 0xe75238) the signature is unique.
- 0xe5aa30: referenced only by `lea` at 0xe5924a and 0xe592c4; no branch into 0xe5aa31-0xe5aa43. First 20 bytes unique.
```asm
# 0xe75240: jmp rv_boost_hook; 5 x nop
rv_boost_hook:                    # entry rsp = 8 mod 16
    push rbx
    mov rbx, rdi                  # CKartBoostComponent
    vmovss xmm0, dword ptr [rdx + 0x18]
    call game_boost_update        # 0xe75250 (original work)
    mov rdi, rbx
    call rv_after_boost_update    # C
    pop rbx
    ret
# 0xe5aa30: jmp rv_hud_hook; nop
rv_hud_hook:
    SAVE
    call rv_hud_update            # rdi = CRacerHudComponent (unchanged)
    RESTORE
    push rbp
    mov rbp, rsp
    push r15
    jmp 0xe5aa36
```
```c
/* game: 0x3dbf90 get_component, [0x2ac4018] meta_kart_boost, 0xe2f050 boost_maintained, 0xc21630 local_players([[0x2ad1160]+0x10]),
   0x149fca0 handle_resolve, 0x253c00 spawn_placeable, 0x268ef0 set_text, 0x268f90 set_position, 0x269120 set_scale, 0x2694d0 set_hidden,
   0x1574080 / 0x1518e30 string intern / release (pool [0x2af2600]) */
void rv_after_boost_update(void *comp)                       /* boost thread: only here we read the list */
{
    if (AT(comp, 0x18, void *) != st.watch_entity) return;
    void *list = AT(comp, 0x28, void *), *best = 0;
    for (int i = 0; list && i < AT(list, 0xc, int); i++) {
        void *b = AT(list, 0x20, void **)[i];
        if (!b || AT(b, 0x48, uint8_t) || !AT(b, 0x10, void *)) continue;
        if (!best || AT(AT(b, 0x10, void *), 0xc, float) >= AT(AT(best, 0x10, void *), 0xc, float)) best = b;
    }
    float r = 0;
    if (best) r = AT(AT(best, 0x10, void *), 0x1c, float) + AT(best, 0x28, float) + game_boost_maintained(best) - AT(best, 0x18, float);
    st.reserves = r > 0 ? r : 0;
}
void rv_hud_update(void *hud)
{
    st.watch_entity = AT(hud, 0x18, void *);
    if (!AT(hud, 0x80, uint8_t) || game_local_players(AT(g_players, 0x10, void *)) != 1) return;   /* v1: single screen only */
    void *project = handle_object(&AT(hud, 0x38, void *)), *root = project ? AT(project, 0x10, void *) : 0;
    if (!root) return;
    if (root != st.root || !is_child(root, st.text) || !is_child(root, st.bar)) {     /* new race / restart: clone again */
        /* find '[LapCounterText]' and the "[octane_ui_hud_boost_bg.tga]" sprite placeable (DFS over +0x48);
           st.text = game_spawn_placeable(project, src_or_live(text), root); st.bar = ... (bg); st.fill = first child of st.bar;
           give each clone our own _name (+0x28) so is_child() can check pointer + name; set_position / set_scale / set_hidden(0) / set_opacity(1) */
    }
    /* each frame: if tenths changed: set_text(st.text, "12.4"); set_scale(st.fill, {2.77f * min(r / FULL, 1), 0.35f, 1}) */
}
```
Placement (guess, tune in game): free space at the left edge, e.g. text near (200, 560) and bar under it, 1920x1080 HUD space.
The bar clone is 392x148 at scale ~0.8; reduce the scale if it is too big.

## Risks and open points
- Threading: the boost list is read only inside the boost hook (same callback that changes it). The HUD hook runs in the HUD component
  callback, where the game itself opens projects (0x986820) and broadcasts GUI events (0xe32050), so GUI calls there match game practice.
  Cloning (0x253c00) is done by the game from GUI behavior code; calling it from the HUD callback is untested.
- Lifetime: clones belong to the HUD project and die with it. Never keep a reference; each frame check that they are still children of the
  current root (pointer compare, then our name). Restart, quit and next race make a new project: clone again.
- Templates must exist: '[LapCounterText]' (lap display) is missing in some modes (e.g. battle, Adventure hub uses `Octane_Adventure_HUD`);
  then show nothing. Template lookup by `_name` assumes run-time placeables keep the asset names (true for the dialog/ghost menus; unverified here).
- Race end, pause, countdown: clones do not follow the game's `HideOnRacerFinish`. Hide them when `CRacer._finished` (+0x8c; `_racer` handle at hud+0x28; field size unverified) is set.
- Split screen: one HUD component per local player; project sharing not checked. v1 = only when 1 local player.
- Online: display only, no gameplay write; the local kart is simulated locally. Still keep it offline-only until tested.
- Replays / ghosts: ghosts have no HUD component; no replay mode found.
- Max value: no game limit found; the bar needs a chosen full scale (text shows the real value).
- Free space: patch code 0x2339000-0x233c078, state 0x2beef80-0x2bef000 (CLAUDE.md); this plan needs well under that.

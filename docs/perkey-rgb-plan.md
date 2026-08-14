# Per-key RGB indicators for the PandaKB Corne (ZMK v0.3)

> **Status: implemented.** This is the design record as approved, kept for the
> reasoning and the verified facts about ZMK v0.3. The devicetree snippets below
> are from the design, not the final schema — the live configuration is
> `config/Corne.keymap`, described by the bindings in `dts/bindings/`.
> Known drift: `unbound-color` shipped as `unbound-value`, and the underglow LEDs
> follow the active layer's colour by default.

> Supersedes the shipped whole-strip feature (`src/underglow_indicators.c` + the
> `underglow_indicators` node in `config/Corne.keymap`), which is deleted in Slice 1.
>
> **Revision 2** — static (not held-triggered) highlighting + ZMK Studio disabled. This collapsed the
> original 4 slices into 2 and deleted the entire runtime-tracking and per-key-mask-sync design.

## Overview

Replace `CONFIG_ZMK_RGB_UNDERGLOW` with an in-repo renderer that owns all 27 WS2812 pixels on each
half and paints them from **compile-time tables generated out of the keymap devicetree**, plus
declarative color config. The renderer runs on both halves and is event-driven — no 20Hz tick.

The key insight from revision 2: with static highlighting and no ZMK Studio, the picture is a pure
function of the active layer index, and every input is knowable at build time. Both halves compile the
same tables from the same keymap (fact 3), so **the only thing that crosses the split is the active
layer index**.

### Goals

- **F1** Per-key color by active layer, with named **zones** overriding the layer's base color.
- **F2** Keys not explicitly bound on the highest active layer render dark — `&trans` and `&none`
  alike.
- **F3** Modifier keys are **always** lit in the modifier color on layers where they're bound.
- **F4** Layer-switch keys (`&mo`, `&lt`, `&to`, `&tog`) are lit in **their target layer's color**.
- **F5** Reimplement toggle and brightness up/down only.
- Blank on idle and synchronously before deep sleep; redraw only on state change.

### Non-goals

- No dynamic "held right now" feedback — highlighting is static per layer (confirmed).
- No animations/effects, no `&rgb_ug` compat shim, no gamma correction, no dongle wiring.
- No press-highlight in the shipped config (diagnostic only during Slice 1).
- No ZMK Studio support — Studio is being turned off (confirmed).

## Established facts

Verified against ZMK v0.3 (`/tmp/zmkchk/app`) and this repo:

1. Each half has one ws2812-spi strip, `chain-length = 27` (`Corne.dtsi:120`): 6 underglow + 21
   per-key. `chosen { zmk,underglow = &led_strip; }` at `Corne.dtsi:14`.
2. `rgb_underglow.c` rewrites every pixel every 50ms (`:67,:193,:294`), so per-key control requires
   `CONFIG_ZMK_RGB_UNDERGLOW=n` and our own renderer.
3. The keymap file is the `DTC_OVERLAY_FILE` for **both** builds
   (`app/keymap-module/modules/modules.cmake`) — keymap and config devicetree nodes are compiled into
   the peripheral firmware too.
4. `keymap.c` / `layer_state_changed.c` compile only on the central (`app/CMakeLists.txt:47`);
   `position_state_changed.c` and `activity_state_changed.c` are available on both halves.
5. Central→peripheral state must ride a `BEHAVIOR_LOCALITY_GLOBAL` behavior: `char behavior_dev[16]`
   (name relayed as a string, ≤15 chars) + `uint32 param1` + `uint32 param2`.
6. No central-side "peripheral connected" event exists in v0.3;
   `zmk_split_peripheral_status_changed` is raised only on the peripheral and `split/central.c` owns
   the single transport status-callback slot.
7. **Behavior roles are inspectable at compile time.** `&trans` = `zmk,behavior-transparent`,
   `&none` = `zmk,behavior-none`, `&kp` = `zmk,behavior-key-press`, `&mo` =
   `zmk,behavior-momentary-layer`, `&to`/`&tog` = `zmk,behavior-to-layer`/`-toggle-layer`.
   `&lt` and `&hm`/`&mt` are all `zmk,behavior-hold-tap`, distinguished by their own `bindings`:
   `&lt` is `<&mo>, <&kp>` (`app/dts/behaviors/layer_tap.dtsi`) while `&hm` is `<&kp>, <&kp>`.
   `DT_PHA_BY_IDX(layer, bindings, i, param1)` yields the target layer for `&mo`/`&lt` and the keycode
   for `&kp`/`&hm`.
8. `boards/shields/Corne/Corne_L.conf` and `Corne_R.conf` set `CONFIG_ZMK_RGB_UNDERGLOW=y` and
   `BRT_START=60`, contradicting `config/Corne.conf` (`BRT_START=0`). All three need reconciling.
9. **Unverified hypothesis** — LED↔position map from QMK's crkbd rev1 `g_led_config`; per-key LEDs
   snake from the inner thumb outward. Left half, LED→ZMK position:
   `6→38, 7→29, 8→17, 9→5, 10→4, 11→16, 12→28, 13→37, 14→36, 15→27, 16→15, 17→3, 18→2, 19→14,
   20→26, 21→25, 22→13, 23→1, 24→0, 25→12, 26→24`. Right half mirrored:
   `6→39, 7→30, 8→18, 9→6, 10→7, 11→19, 12→31, 13→40, 14→41, 15→32, 16→20, 17→8, 18→9, 19→21,
   20→33, 21→34, 22→22, 23→10, 24→11, 25→23, 26→35`. Slice 1 proves or refutes this.

## Slice 0 — Prior-art check (in progress)

GitHub search already surfaced several candidates worth reading before writing C:
`loss-and-quick/zmk-led-canvas` ("per-key and per-zone RGB lighting for ZMK, as a Zephyr module"),
`afiqzudinhadi/zmk-rgb-effects` ("per-key layer indicators"), `jagonmo/zmk-rgb-petra`,
`ssonparote/zmk-sofle-rgb`. Evaluate each for: ZMK v0.3 compatibility, how it owns the strip, how it
syncs across a split, and whether it supports layer-keyed per-key colors. Adopt if one fits; the
config schema and compile-time role tables here are the parts worth keeping either way.

Also grep the fetched west modules for `rgb_underglow` / `ZMK_RGB_UNDERGLOW` dependencies that would
break under `=n` — `zmk-nice-oled` is tracked at unpinned `main` and is the main suspect.

## Slice 1 — Own the strip; toggle/brightness; idle & sleep blanking

**Demoable:** each half lights the LED under the key being pressed, underglow LEDs show a static
color, everything blanks after 60s idle and before deep sleep, `&prgb` toggles and steps brightness on
both halves. Entirely local per half — no protocol yet — and it is the empirical test for fact 9.

**New:** `include/dt-bindings/zmk/perkey_rgb.h`, `include/dt-bindings/zmk/corne_positions.h`,
`src/perkey_rgb.c`, `src/behavior_perkey_rgb.c`, `dts/bindings/zmk,perkey-rgb-map.yaml`,
`dts/bindings/zmk,perkey-rgb.yaml`, `dts/bindings/behaviors/zmk,behavior-perkey-rgb.yaml`

**Modified:** `Kconfig`, `CMakeLists.txt`, `Corne.dtsi` (map node), `Corne_L.overlay` /
`Corne_R.overlay` (`side`), `config/Corne.conf`, `Corne_L.conf` / `Corne_R.conf`,
`config/Corne.keymap`, `build.yaml` (drop the `studio-rpc-usb-uart` snippet).

**Deleted:** `src/underglow_indicators.c`, `dts/bindings/zmk,underglow-indicators.yaml`, and all
`&rgb_ug` bindings on the SYS layer (replaced by `&prgb PRGB_BRD` / `PRGB_TOG` / `PRGB_BRI` at the
same three positions). Under `CONFIG_ZMK_RGB_UNDERGLOW=n` the old feature's `depends on` would
silently disable it, so it goes now rather than rotting.

**ZMK Studio off:** remove `CONFIG_ZMK_STUDIO*` from `config/Corne.conf` and `Corne_L.conf`, and drop
the snippet from `build.yaml`. Smaller firmware, and it guarantees the compile-time role tables can't
diverge from the running keymap.

### Hardware map (shield — both arrays present on both halves)

```dts
perkey_rgb_map: perkey_rgb_map {
    compatible = "zmk,perkey-rgb-map";
    /* One entry per strip LED, in strip order. PRGB_UG = not under a key.
     * HYPOTHESIS from QMK crkbd rev1 g_led_config - verify in Slice 1. */
    left-leds  = < PRGB_UG PRGB_UG PRGB_UG PRGB_UG PRGB_UG PRGB_UG
                   POS_TH3 POS_B6 POS_H6 POS_T6 POS_T5 POS_H5 POS_B5 POS_TH2
                   POS_TH1 POS_B4 POS_H4 POS_T4 POS_T3 POS_H3 POS_B3
                   POS_B2 POS_H2 POS_T2 POS_T1 POS_H1 POS_B1 >;
    right-leds = < ... mirrored ... >;
};
```

`Corne_L.overlay`: `&perkey_rgb_map { side = "left"; };` — `Corne_R.overlay`: `"right"`.
`corne_positions.h` names positions as the keymap reads: `POS_T1..T12`, `POS_H1..H12`, `POS_B1..B12`,
`POS_TH1..TH6`.

### Renderer

Precedence, highest wins: `modifier` > `layer-key` > `pressed` (diagnostic) > `unbound` > `zone` >
`layer base` > `default`. Then global brightness, the `BRT_MAX` ceiling, then on/off and blanking.

Emission is coalesced on the low-priority workqueue (10ms), hard rate-limited (30ms), and skipped when
the composed buffer `memcmp`s equal to the last sent. 27 LEDs × 24 bits at 4MHz ≈ **1.3ms of SPI per
frame**, so an uncapped per-keystroke redraw would be worse than stock's 20Hz tick. With static
highlighting and no press feedback, a normal typing session emits **zero** frames.

**Deep sleep:** on `zmk_activity_state_changed → SLEEP`, zero the buffer and call
`led_strip_update_rgb()` synchronously inside the listener. Stock defers to a workqueue and can lose
the race with `sys_poweroff()`, leaving LEDs lit through sleep.

**Settings:** own key `prgb/state`, persisting only `{version, brightness, on}`, debounced by
`CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE`.

**`&prgb` behavior:** `BEHAVIOR_LOCALITY_GLOBAL` with
`binding_convert_central_state_dependent_params` converting `TOG`→`ON`/`OFF` and `BRI`/`BRD`→
`BRT <absolute>`, exactly as `behavior_rgb_underglow.c` does. Only absolute values cross the split, so
the halves cannot drift and no custom message type is needed for lighting control.

**Acceptance:** both halves boot with OLEDs working; underglow LEDs show the configured color;
pressing any of the 42 keys lights exactly one per-key LED (**record which**); `PRGB_TOG` and
`PRGB_BRI/BRD` move both halves in lockstep; settings survive a power cycle; 60s idle blanks both
halves; deep sleep leaves them off, not frozen-lit.

## Slice 2 — Layer colors, zones, and static role highlighting

**Demoable:** NAV turns both halves green with the arrow cluster picked out; NUM shows the numpad
cluster distinctly; SYS (conditional NAV+SYM) shows red; homerow mods sit white on MAIN; thumb layer
keys glow in the color of the layer they reach; `&trans`/`&none` keys are dark on every layer.

**New:** `src/perkey_rgb_sync.c` (the `prgb_sync` behavior + codec, both halves),
`dts/bindings/zmk,perkey-rgb-zone.yaml`, `dts/bindings/behaviors/zmk,behavior-perkey-rgb-sync.yaml`
**Modified:** `zmk,perkey-rgb.yaml` (child-binding), `config/Corne.keymap`, `src/perkey_rgb.c`,
`CMakeLists.txt`, `Kconfig`

### Compile-time role table (the core of revision 2)

DT macros walk the keymap's layer children and each layer's `bindings` phandle-array, emitting per
(layer, position):

```c
enum prgb_role { PRGB_ROLE_NORMAL, PRGB_ROLE_UNBOUND, PRGB_ROLE_MOD, PRGB_ROLE_LAYER };
struct prgb_key_role { uint8_t role; uint8_t param; };   /* param = target layer for LAYER */
static const struct prgb_key_role key_roles[LAYERS][POSITIONS];
```

Classification, all at build time via fact 7:

| Binding | Role |
|---|---|
| `&trans`, `&none` | `UNBOUND` |
| `&kp <modifier>` | `MOD` |
| hold-tap whose hold binding is `&kp` with a modifier param (`&hm LCTRL A`) | `MOD` |
| `&mo N`, `&to N`, `&tog N` | `LAYER`, param = N |
| hold-tap whose hold binding is `&mo` (`&lt NUM RET`) | `LAYER`, param = N |
| anything else | `NORMAL` |

Chord mod-taps (`&mt LC(Z) Z`) carry the modifier in the `SELECT_MODS` top byte, so the usage id is
`Z`, not a modifier — they classify as `NORMAL`, which matches the confirmed decision.

Both halves generate this identically from the same keymap devicetree, so there is no central-only
code path and nothing per-key to sync.

### Config schema

Zones are declared once and referenced from any layer with a color, so a position set can be reused:

```dts
prgb_numpad: prgb_numpad {
    compatible = "zmk,perkey-rgb-zone";
    #zone-cells = <3>;
    positions = <POS_T8 POS_T9 POS_T10  POS_H8 POS_H9 POS_H10
                 POS_B8 POS_B9 POS_B10  POS_TH5>;
};

perkey_rgb {
    compatible = "zmk,perkey-rgb";
    default-color   = <  0   0  30>;
    unbound-color   = <  0   0   3>;
    modifier-color  = <  0   0 100>;
    underglow-color = <200 100  40>;

    main { layer = <MAIN>; color = <200 100 60>; };
    nav  { layer = <NAV>;  color = <120 100 50>; zones = <&prgb_arrows 200 100 100>; };
    sym  { layer = <SYM>;  color = < 20 100 50>; };
    sys  { layer = <SYS>;  color = <  0 100 50>; underglow-color = <0 100 60>; };
    fun  { layer = <FUN>;  color = <285 100 50>; };
    num  { layer = <NUM>;  color = < 55 100 40>; zones = <&prgb_numpad 200 100 90>; };
};
```

Named layer children with explicit `layer = <NAV>` replace today's positional array, which silently
mis-assigns every color if a layer is inserted. First matching zone in DT order wins. A `LAYER`-role
key renders in `layer_cfg[param].color` — the color of the layer it reaches.

### Split protocol

One behavior, `prgb_sync` (9 chars), `BEHAVIOR_LOCALITY_GLOBAL`. `zmk_behavior_invoke_binding()`
relays to peripherals and invokes locally, so both halves decode through the same path.

```
param1: [31:28] msg_type (1 = LAYER)   [27:26] target (0=L, 1=R, 2=BOTH)
        [25:21] layer_index            [20] active (central activity)
        [19:0]  reserved, must be 0
param2: reserved, must be 0
```

`msg_type == 0` is reserved so an all-zero relay is never valid. Receivers ignore reserved bits, drop
unknown types, and clamp `layer_index`. Traffic: **zero** relays while typing; one per layer change.

### Peripheral reconnect

Per fact 6 there's no central-side connect event, so: the peripheral subscribes to
`zmk_split_peripheral_status_changed` and renders a distinct "stale" look on disconnect; the central
polls `STRUCT_SECTION_FOREACH(zmk_split_transport_central, t)` → `get_status()` every second while
active and re-sends on the disconnected→connected edge. A local read of a couple of ints, no radio
cost.

**Acceptance:** NAV green on both halves within a frame, arrow zone distinct; NAV+SYM → SYS red on
both (conditional-layer path); NUM cluster distinct; homerow mods white on MAIN; the four thumb layer
keys each show their target layer's color; `&trans` keys dark on NAV/SYM/NUM/FUN and nothing dark on
MAIN; power-cycling the right half while holding NUM shows the stale look then catches up in ~1s; no
sync traffic while typing on the base layer.

## Risks

- **LED map hypothesis wrong (highest likelihood).** Pure devicetree data — a wrong permutation,
  mirror, or underglow placement is a one-array fix, no C change. The per-LED `PRGB_UG` sentinel also
  covers a non-contiguous or non-leading underglow block.
- **Kconfig merge order** across `config/Corne.conf` and the two shield `.conf`s (fact 8) —
  reconciled in Slice 1, plus an `#error` guard if `CONFIG_ZMK_RGB_UNDERGLOW` is set.
- **A west module depending on stock underglow** (`zmk-nice-oled`, unpinned) — Slice 0 checks.
- **Quiescent LED draw:** `EXT_POWER=n` stays, because on nice_nano_v2 that rail also feeds the OLED,
  so gating it would blank the display whenever the LEDs are off. Documented, not "fixed".
- **Peripheral idles independently** — pre-existing stock behavior; the `active` bit in the sync
  message covers the idle case while connected. Deep sleep stays per-half.
- **`split/peripheral.c` missing `break`** after INVOKE_BEHAVIOR logs "Unhandled command type" after
  correctly invoking the behavior. Pre-existing and harmless — don't chase it during Slice 2.

## Verification

**Build:** GitHub Actions builds `Corne_L nice_oled` and `Corne_R nice_oled` (no snippet after Studio
is removed) plus `settings_reset`, with zero new warnings.

**Confirming the LED mapping (Slice 1, definitive):**
1. Build with `pressed-color = <0 0 100>` and every other color at `b = 0`, so exactly one LED lights.
2. Press each of the 21 keys per half in a fixed order; record which physical LED lights.
3. Correct key → fact 9 confirmed. Consistent but wrong key → rewrite the two arrays, no C change.
   Nothing lights, or an underglow LED lights → `PRGB_UG` placement is wrong; set all 27 entries to
   `PRGB_UG`, confirm strip length, bisect for where the per-key LEDs begin.
4. Record the verified table as a dated comment in `Corne.dtsi`.

**Power check:** after Slice 2, confirm no periodic SPI activity while typing, dark after 60s idle,
dark after deep sleep.

## Decisions confirmed

- Static highlighting only — no held-modifier feedback.
- ZMK Studio disabled; keymap is edited as a file.
- `&trans` and `&none` both dim; modifier keys always white; layer keys in their target layer's color.
- Chord mod-taps are not highlighted.
- Toggle + brightness only; press-highlight is a diagnostic, shipped off.
- Zone colors live at the reference site so a position set can be reused at different colors.
- Colors keyed by layer index; `ext_power` gating stays off to keep the OLED alive.

## Open questions

None outstanding.

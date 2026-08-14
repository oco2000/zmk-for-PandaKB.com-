# Per-key RGB

This repo replaces ZMK's built-in RGB underglow with its own renderer. Each half owns its whole
27-LED strip and paints every key according to what that key does on the active layer.

It is built as part of this config repo, which doubles as a Zephyr module (`zephyr/module.yml`), and
runs against **stock `zmkfirmware/zmk` v0.3** — no forked ZMK.

## What you see

| | |
|---|---|
| Base colour | Every key takes the active layer's colour. |
| Zones | Named groups of keys (numpad, navigation pad, F-keys) get their own colour per layer. |
| Modifier keys | Any key that holds a bare modifier is white — homerow mods included. |
| Layer keys | A key that reaches another layer is painted in **that layer's** colour, so the thumbs advertise where they go. |
| Unbound keys | `&trans` and `&none` render as the layer colour, heavily darkened. |
| Underglow | The six non-key LEDs follow the active layer's colour. |

Highlighting is **static**: it depends on the active layer, not on what is currently held. Holding a
modifier changes nothing, because the key was already lit as a modifier.

Control keys live on the SYS layer: `&prgb PRGB_TOG` (on/off), `&prgb PRGB_BRI` / `&prgb PRGB_BRD`
(brightness). The LEDs start **off** and the on/off + brightness choice is persisted.

## How it works

The central idea: **the colour of a key is a pure function of the active layer**, and every input to
that function is known at build time. That is why almost nothing has to cross the split.

```
config/Corne.keymap ──┐
  colours, zones      ├─► compiled into BOTH halves (the keymap file is the
  keymap bindings   ──┘    devicetree overlay for the peripheral build too)
                                     │
                                     ▼
                        src/perkey_rgb_roles.c
                        derives {role, param} per (layer, key)
                                     │
      active layer index ────────────┤
      (the only thing synced)        ▼
                            src/perkey_rgb.c
                            composes 27 pixels, writes the strip
```

### Roles are derived from the keymap, not configured

`src/perkey_rgb_roles.c` walks the keymap's devicetree at compile time and records, for every layer
and every key position, the devicetree ordinal of the behavior bound there plus its first parameter.
At render time it classifies:

| Binding | Role |
|---|---|
| `&trans`, `&none` | `UNBOUND` |
| `&kp <modifier>` | `MOD` |
| hold-tap whose own hold binding is `&kp <modifier>` (`&hm LCTRL A`) | `MOD` |
| `&mo N`, `&to N`, `&tog N` | `LAYER`, param = N |
| hold-tap whose own hold binding is `&mo` (`&lt NUM RET`) | `LAYER`, param = N |
| anything else | `NORMAL` |

Behaviors are recognised by **devicetree ordinal collected per compatible**, never by name, so a
keymap that defines its own hold-taps is classified correctly without touching C. `&lt` and `&hm` are
both `zmk,behavior-hold-tap` and are told apart by what their own `bindings[0]` points at.

Chorded mod-taps like `&mt LC(Z) Z` carry the modifier in the keycode's top byte, so the usage id is
`Z` and they classify as `NORMAL` — deliberately, since holding one sends Ctrl+Z, not Ctrl.

**This is why `CONFIG_ZMK_STUDIO` must stay `n`.** These tables describe the keymap as flashed; a
keymap edited at runtime would no longer match them.

### The split

The colour tables are compiled into both firmwares, so the peripheral needs only one thing from the
central: which layer is active. That rides `prgbsync`, a `BEHAVIOR_LOCALITY_GLOBAL` behavior — the
one channel ZMK v0.3 gives an out-of-tree module for pushing state to peripherals.
`zmk_behavior_invoke_binding()` relays it and then runs it locally, so both halves decode through the
same path.

The central publishes on every layer change and on wake. There is no central-side "peripheral
connected" event in v0.3, so a peripheral that reconnects mid-layer keeps the previous colour until
the next layer change or wake — in practice, moments.

### Rendering

Event-driven, unlike stock ZMK's constant 20 Hz tick. Repaints are coalesced (10 ms), rate-limited
(30 ms) and skipped entirely when the composed frame is identical to the last one sent. Driving 27
LEDs costs roughly 1.3 ms of SPI, so the rate limit matters. With no `pressed-color` configured, an
ordinary typing session emits **zero** LED updates.

Colour precedence, highest first:

```
pressed (if configured) > modifier > layer key > unbound > zone > layer colour > default-color
```

On idle the strip blanks. On the way into deep sleep it blanks **synchronously inside the event
listener**, because ZMK powers devices down immediately afterwards and a deferred blank loses the
race, leaving the LEDs lit through sleep.

## Files

| Path | What |
|---|---|
| `config/Corne.keymap` | Colours and zones — the file you normally edit |
| `config/Corne.conf` | `CONFIG_ZMK_PERKEY_RGB*` settings |
| `boards/shields/Corne/Corne.dtsi` | LED-to-key map for both halves, `prgbsync` behavior node |
| `boards/shields/Corne/Corne_{L,R}.overlay` | `side` — which map this firmware renders |
| `src/perkey_rgb.c` | Renderer: pixel buffer, compose, brightness, on/off, settings, idle/sleep |
| `src/perkey_rgb_roles.c` | Compile-time keymap→role tables |
| `src/perkey_rgb_sync.c` | `prgbsync` behavior and the central-side publisher |
| `src/behavior_perkey_rgb.c` | `&prgb` — toggle and brightness |
| `include/dt-bindings/zmk/corne_positions.h` | `POS_*` names for key positions |
| `include/dt-bindings/zmk/perkey_rgb.h` | `PRGB_*` commands and the `PRGB_UG` sentinel |
| `dts/bindings/` | Devicetree binding schemas |

## Configuration

All colours are `<hue saturation value>` — hue 0-359, saturation and value 0-100. Value is scaled by
the runtime brightness and then by `CONFIG_ZMK_PERKEY_RGB_BRT_MAX` before reaching the LEDs.

Use an **HSV** picker (colorizer.org, or Google's "color picker"). Do not read the HSL values that
most pickers show alongside — HSL saturation is a different quantity.

### Zones

Declared once at the top level, coloured where they are referenced, so one position set can serve
several layers at different colours:

```dts
prgb_pad_r: prgb_pad_r {
    compatible = "zmk,perkey-rgb-zone";
    #zone-cells = <3>;
    positions = <POS_T8 POS_T9 POS_T10
                 POS_H8 POS_H9 POS_H10
                 POS_B8 POS_B9 POS_B10>;
};
```

The right hand's 3×3 block is the navigation pad on NAV, the numpad on NUM and F7-F9/F4-F6/F1-F3 on
FUN, so all three layers point at that one node.

### Layers

```dts
perkey_rgb {
    compatible = "zmk,perkey-rgb";

    default-color   = <200 100  70>;   // layer with no entry below
    unbound-value   = <6>;             // &trans/&none: layer hue, this value
    modifier-color  = <  0   0 100>;
    underglow-color = <200 100  50>;   // only when the layer has no entry
    // pressed-color = <0 0 100>;      // press feedback; omit to disable

    nav {
        layer = <NAV>;
        color = <120 100  70>;
        zones = <&prgb_pad_r 200 100 100>,
                <&prgb_pad_l 300 100 100>;
    };
};
```

Layers are matched by their `layer` property, not by child order, so inserting a layer cannot
silently shift every colour. The first zone naming a given position wins. A layer may set
`underglow-color` to stop the underglow following its base colour.

### Kconfig

| Symbol | Default | |
|---|---|---|
| `ZMK_PERKEY_RGB` | y | The feature. Requires `ZMK_RGB_UNDERGLOW=n` |
| `ZMK_PERKEY_RGB_BRT_MAX` | 60 | Hard ceiling on output, for the power budget |
| `ZMK_PERKEY_RGB_BRT_START` | 60 | Brightness before anything is saved |
| `ZMK_PERKEY_RGB_BRT_STEP` | 10 | Per press of `PRGB_BRI`/`PRGB_BRD` |
| `ZMK_PERKEY_RGB_ON_START` | n | Start lit? |
| `ZMK_PERKEY_RGB_AUTO_OFF_IDLE` | y | Blank when idle |
| `ZMK_PERKEY_RGB_COALESCE_MS` | 10 | Repaint coalescing window |
| `ZMK_PERKEY_RGB_MIN_INTERVAL_MS` | 30 | Minimum gap between strip writes |
| `ZMK_PERKEY_RGB_CHASE` | n | Diagnostic, see below |

## The LED map

`perkey_rgb_map` in `Corne.dtsi` lists, for each strip LED in order, the key position under it or
`PRGB_UG` for an LED that is not under a key. Both halves' arrays live there; the overlays pick one
with `side`.

Verified on hardware: 6 underglow LEDs, then the per-key LEDs **snake** column by column from the
inside out — the inner column runs bottom to top, the next top to bottom, and so on. The inner thumb
leads its column, the middle thumb trails the next, and the outer thumb leads the one after.
`chain-length = 27` is correct.

Key positions use `POS_<row><column>` with columns numbered 1-12 left to right across the whole
board: `POS_T1`-`POS_T12` top, `POS_H*` home, `POS_B*` bottom, `POS_TH1`-`POS_TH6` thumbs.

### Re-deriving it

Set `CONFIG_ZMK_PERKEY_RGB_CHASE=y`, flash, and watch: one LED walks the chain, 2 s per step, logging
its index. Note which physical key each step lands on. Steps where nothing lights mean
`chain-length` over-declares the strip. Read one half at a time — the halves run independent timers.

## Gotchas

**Relayed behavior names are limited to 8 characters.** They cross the split in a 9-byte field
including the terminator (`ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN`, `zmk/split/bluetooth/service.h`). A
longer name is truncated in transit, the peripheral never finds the behavior, and nothing is logged
on either side. This cost an afternoon; `src/perkey_rgb_sync.c` has a `BUILD_ASSERT` so it cannot
happen again.

**Turning off stock underglow removes dependencies it used to pull in.** ZMK defaults `CONFIG_SPI=y`
only inside `if ZMK_RGB_UNDERGLOW` (`app/Kconfig.defaults`), and only builds `src/workqueue.c` under
`ZMK_LOW_PRIORITY_WORK_QUEUE`. Our `Kconfig` selects both explicitly. Without `SPI` the WS2812 driver
is not compiled and the link fails with an undefined `__device_dts_ord_NNN`.

**Both halves must be flashed for any change.** Colours, zones and the LED map are all compiled into
each half. This differs from the whole-strip approach that preceded this, where the central could
dictate colour over the air.

**`ON_START` only applies until something is saved.** On/off and brightness are persisted to
settings, and the saved value wins on later boots. To actually get "off by default" on a keyboard
that has been toggled on, toggle it off once.

**GitHub Actions artifacts are always named `firmware.zip`.** It is easy to flash a stale download.
Check the run, and note that two runs can produce identically sized artifacts if only devicetree data
changed.

**Very low `unbound-value` gets coarse.** With `BRT_MAX=60`, a value of 6 puts the brightest channel
around 9/255, where integer rounding can skew the hue. If dark keys look off-colour rather than dim,
use 0.

## Extending it

- **New zone** — add a `zmk,perkey-rgb-zone` node, reference it from any layer's `zones` with a
  colour. No C changes.
- **New layer** — add a child to `perkey_rgb` with its `layer` index and colour.
- **Different LED order** — edit `left-leds`/`right-leds`. No C changes.
- **A new role** (e.g. "sticky key") — add the compatible's ordinals to a table in
  `perkey_rgb_roles.c`, extend `enum zmk_perkey_rgb_role`, and handle it in `key_color()` in
  `perkey_rgb.c`.
- **Something dynamic** (reacting to state rather than layer) — this needs a new message type in
  `perkey_rgb_sync.c` for the peripheral to learn about it, since only the layer index is synced
  today. Keep the 8-character name limit in mind.

## Verifying a change

There is no local toolchain; GitHub Actions builds on push. After flashing both halves:

1. MAIN is blue, homerow mods white, the four thumb layer keys show their destination colours.
2. NAV is green with the navigation pad blue on the right and magenta on the left.
3. NAV+SYM together gives red everywhere — this is the conditional SYS layer, and it exercises the
   split.
4. NUM lights the numpad block; FUN lights all of F1-F12.
5. `&trans` keys are dark on every layer; nothing is dark on MAIN.
6. `PRGB_TOG` and the brightness keys move both halves together.
7. Leave it 60 s: both halves blank. Type: they return.

## History

This replaced an earlier whole-strip feature that set one colour for the entire board per layer by
invoking ZMK's own `rgb_ug` behavior. That approach needed no peripheral reflash, but a single colour
cannot express zones or per-key roles.

`loss-and-quick/zmk-led-canvas` solves a similar problem well and is worth reading, but its
per-layer colours on a split peripheral require a **forked ZMK** (`zmk/layer_state.h`,
`active_layers_changed`) — precisely the piece the global-locality behavior replaces here. It also
expects per-key colours to be listed by hand, where this derives them from the keymap.

# scope-clock - Claude Code Configuration

## What this is

A toy / capability demo: a clock drawn on an analog oscilloscope in X-Y mode.
An STM32 streams `(x, y)` vector points to its dual 12-bit DAC by DMA (a static
frame that costs zero CPU once running), with a second DMA stream blanking the
beam on retrace. It renders an analog face, digital `HH:MM:SS`, or a WiFi
message; time comes from the on-chip RTC disciplined by SNTP via an ESP-01.
Minimal chip count: ~3 ICs (STM32 + ESP-01 + one dual op-amp) plus a Z-blank
transistor and a regulator.

## Run / build / test

```powershell
# Host preview (verified working here): rasterize + render every scene
.\run.cmd                       # -> tools\out\*.png, *.svg, contact_sheet.png
.\run.cmd --now                 # use the real clock
.\run.cmd --time 13:37:00 --message "HELLO" --show-retrace --dots

# Nucleo build: 8-bit PWM + simulate the actual RC output stage
.\run.cmd --target f401 --pwm
.\run.cmd --target f401 --pwm --r1 1000 --c1 4.7e-9 --r2 4700 --c2 1e-9

# Verify the C engine still matches the Python reference (needs any host gcc)
.\.venv\Scripts\python.exe tools\check_c_engine.py

# Regenerate the C font table after editing tools\vectorfont.py
.\.venv\Scripts\python.exe tools\gen_font.py

# STM32 firmware. PlatformIO lives in the project venv; it pulls its own
# arm-none-eabi-gcc (7.2.1) and libopencm3 into ~/.platformio on first run.
cd firmware\stm32
..\..\.venv\Scripts\pio.exe run                       # all four targets
..\..\.venv\Scripts\pio.exe run -e nucleo_g491re      # the board on the bench
..\..\.venv\Scripts\pio.exe run -e nucleo_g491re -t upload
```

Toolchain setup (already done): `.\.venv\Scripts\python.exe -m pip install
platformio`. A standalone Arm toolchain is **not** needed and winget's
`Arm.ArmGnuToolchain` refuses `--scope user` (exit 16, "no applicable
installer") because it is machine-scope only, which needs a UAC prompt.

Current sizes (all four build clean, zero warnings):

| Target | RAM | Flash |
|---|---|---|
| `nucleo_g491re` | 66284 / 98304 (67.4%) | 21948 / 524288 |
| `nucleo_g431kb` | 25316 / 32768 (77.3%) | 21796 / 131072 |
| `nucleo_f401re` | 21224 / 98304 (21.6%) | 22412 / 524288 |
| `disco_f407vg`  | 66284 / 131072 (50.6%) | 21724 / 1048576 |

First-time venv: `python -m venv .venv; .\.venv\Scripts\python -m pip install -r tools\requirements.txt`

## Architecture

| Area | Where | Notes |
|------|-------|-------|
| Font (source of truth) | `tools/vectorfont.py` | single-stroke vector glyphs |
| Host reference engine | `tools/vengine.py`, `scenes.py` | mirror the C firmware |
| Host preview / sim | `tools/preview.py` | scope-style PNG/SVG + point budget |
| Analog output model | `tools/analog.py` | loaded 2-pole RC, ripple vs lag |
| C-vs-Python check | `tools/check_c_engine.py`, `tools/hostcheck/` | compiles the firmware engine with host gcc and diffs it |
| Font codegen | `tools/gen_font.py` | → `firmware/stm32/src/font_vec.{c,h}` |
| STM32 firmware | `firmware/stm32/src/` | libopencm3; two board targets |
| ESP-01 WiFi | `firmware/esp01/esp01_wifi.ino` | SNTP + web form + UART |
| Electrical | `hardware/README.md` | both builds, BOM, scope hookup |
| Design | `docs/DESIGN.md` | the streamed-frame concept, protocol, verification |

The **streamed static frame** is the core idea: a sample-clock timer paces two
circular DMA streams that emit one beam sample per tick; `main.c` rebuilds a
frame only on the 1 Hz tick or a mode/message change and swaps it at a frame
boundary in the DMA transfer-complete ISR.

Four board targets share everything above the output stage. `board.h` dispatches
on a `-D` flag to `board_g491.h` / `board_g431.h` / `board_f401.h` /
`board_f407.h`, which supply the pin map,
budget, `ve_word_t` and `frame_pack()`; `display.h` is the common backend
interface, implemented by exactly one of:

| Target | Backend | Output stage |
|---|---|---|
| `nucleo_g491re` | `dac_dma_g4.c` | TIM6 → dual 12-bit DAC `DHR12RD` via DMAMUX; **the verified-working board**; 96 KB contiguous SRAM so the full 4096-pt budget fits with no linker hack |
| `nucleo_g431kb` | `dac_dma_g4.c` | TIM6 → dual 12-bit DAC `DHR12RD` via DMAMUX; **buffered outputs drive the scope directly, no op-amp** |
| `nucleo_f401re` | `pwm_dma.c` | TIM2 → TIM3 CCR1/CCR2 PWM pair + external RC (F401 has **no DAC**) |
| `disco_f407vg` | `dac_dma.c` | TIM6 → dual 12-bit DAC `DHR12RD` + GPIO `BSRR` Z-blank |

Board-specific scene/budget knobs live in the board header:
`VE_MAX_POINTS`, `VE_DEFAULT_STEP`, `VE_DEFAULT_MOVE_SETTLE`,
`SCENE_MINUTE_TICKS`, `BOARD_HAS_BUTTON`, `BOARD_HAS_ZBLANK`. Each has a
matching entry in `tools/vengine.py` `TARGETS` - **change both together**, the
checker enforces it.

## Invariants & "do not regress"

- **`tools/vectorfont.py` is the only place glyphs are defined.** After editing
  it, re-run `gen_font.py`; never hand-edit `font_vec.c`. **Why:** the C table
  is generated; edits there are silently overwritten.
- **The C engine mirrors the Python reference** (`vengine.py`/`scenes.py`).
  Change geometry in both, then run `tools/check_c_engine.py`. **Why:** the
  preview only predicts the hardware while they agree; the checker compiles the
  firmware engine with host gcc and diffs sample counts per target (1-2% drift
  is just float-vs-double at `ceil(dist/step)` boundaries).
- **Board-specific code belongs in `board_*.h` + a `display.h` backend.** The
  engine, scenes and font must stay board-agnostic. **Why:** that separation is
  the only reason the F401 port was a new header plus one .c file.
- **Every frame must stay ≤ `VE_MAX_POINTS`** or refresh drops below the floor
  (~49 Hz F407, ~43 Hz F401) and the image flickers. `preview.py` fails loudly
  (`OVER`) - keep it green.
- **`move_settle` is not decoration.** A slow output stage is still in transit
  when a stroke starts, so the beam paints a tail from the previous stroke
  ('H' renders as 'A'). **Why:** found in simulation on the F401 build; if you
  fit a slower RC, raise `VE_DEFAULT_MOVE_SETTLE` to match.
- **`pack_xy()`, `world_to_ccr()` and `ZBLANK_ACTIVE_HIGH` are hardware-truth**,
  tuned against the `MODE_TEST` pattern, not by rotating/mirroring the geometry.
- **The G431 needs `ld/stm32g431kb_32k.ld`; do not drop back to the generated
  script.** libopencm3 generates `ram = 22K` (SRAM1+SRAM2) and offers the 10 KB
  CCM only at 0x10000000. **Why:** the frames are ~24 KB, so the stock script
  overflows by 2788 bytes. On category 2 G4 parts the CCM is aliased at
  0x20005800 making one contiguous 32 KB block, and **DMA can reach CCM only
  via that alias** - so a `.ccmram` section at 0x10000000 would link fine and
  then fail silently at run time (RM0440 sec 2.4, AN4296).
- **DAC trigger-select encodings are INVERTED between F4 and G4.** F4: TSEL=0
  is TIM6_TRGO, 7 is software. G4: TSEL=0 is the internal clock, **7** is TIM6.
  The G4 also puts TEN1 at bit 1 (bit 2 on F4) and widens TSEL1 to bits 5:2.
  **Why:** the G4 backend was seeded from the F407's hand-rolled bit pattern, so
  `1<<2` landed inside TSEL1 and left the trigger off. Symptom on hardware:
  `DMA1_CNDTR` frozen at the frame length, `DHR12RD` never written, beam parked.
  Both files now build `DAC_CR_VALUE` from libopencm3 macros, which resolve
  per family. Verified on the G491: `DAC_CR` reads back `0x001F101F`.
- **Never hand-code a peripheral base address; it is not portable and it does
  not fail loudly.** `rtc.c` used to hard-code the F4's RCC at `0x4002_3800`.
  On the G4 (RCC at `0x4002_1000`) that is unmapped, so `rtc_init()` took a
  precise bus fault -> HardFault -> and since HardFault outranks SysTick the
  whole main loop stopped, while the DAC/DMA kept streaming the stale frame.
  Diagnosed live over SWD: `CFSR=0x8200`, `BFAR=0x40023840`.
- **Don't hand-roll peripheral base+offset macros when libopencm3 has them.**
  **Why:** doing so silently shadowed `DAC1_BASE`/`TIM6_BASE`, and hid a wrong
  `DAC_MCR` offset (it is 0x3C; 0x38 is `DAC_CCR`). Compiler warnings for
  redefinition were the only clue. Same class of bug: a board macro named
  `TIM2_ARR` shadowed libopencm3's register accessor - it is `SAMPLE_TIM_ARR`
  now. Keep the build warning-free so these stay visible.
- **`-specs=nano.specs` + `--gc-sections` are load-bearing on the G431**, not
  tidiness: libopencm3 compiles its whole USB/FDCAN stack into the archive.

## Status / known gaps

- Host tooling: **working, verified visually.** C engine compiles clean under
  host gcc for all targets and matches the Python reference.
- **The NUCLEO-G491RE build is VERIFIED RUNNING ON HARDWARE.** Confirmed live
  over SWD with OpenOCD (no scope needed): `DAC_CR=0x001F101F`, TIM6 `ARR=849`
  (exactly 200 kSa/s from the 170 MHz PLL), `DMA1_CNDTR` counting down,
  `DHR12RD` carrying correct geometry (a sample decoded to radius 0.90, the
  face ring), both frame buffers holding the 2101-point analog face, `CMAR`
  alternating between them (so the tear-free swap works), SysTick advancing
  ~1000/s, and `HFSR`/`CFSR` clean. `TZ=`/`T=`/`MODE=` over the COM13 VCP were
  parsed and took effect. **Not yet looked at on an actual oscilloscope.**
- All four targets compile and link clean for ARM (zero warnings) with
  PlatformIO + arm-none-eabi-gcc 7.2.1. The other three remain unflashed.
- **The F401 has no DAC and no TIM6/TIM7** (verified against RM0368: no such
  chapters). That is why the PWM build exists at all. Don't "restore" a DAC
  path for it.
- **G431 is SRAM-bound, not refresh-bound.** 32 KB total and 16 bytes/point
  means 1536 points max; the analog face already uses 94% of that. Adding
  geometry to that scene needs `check_c_engine.py` re-run. Refresh is 86 Hz.
- Request mappings confirmed against vendor docs: F401 TIM2_UP → DMA1
  Stream1/Stream7 Ch3 (RM0368 Table 28); G4 DMAMUX IDs DAC1_CH1=6, DAC1_CH2=7,
  TIM6_UP=8 (libopencm3 `g4/dmamux.h`, and now confirmed working on the G491).
  The **F407 mapping is still unverified.**
- **The hardware RTC peripheral is no longer used.** `rtc.c` keeps the same
  interface but carries the epoch on SysTick. **Why:** libopencm3 has no G4 RTC
  support at all (no `g4/rtc.h`), and the hand-written F4 version hard-faulted
  on the G4. SysTick is core-standard, identical on every target, and runs off
  HSI (~1%) rather than the LSI (~5%) this build actually used - simpler *and*
  more accurate. What is given up is VBAT hold-over, which this design already
  does without. Bonus: `rtc_localtime()` now returns a real sub-second `frac`,
  so a smooth sweeping second hand is finally possible.
- **Boot time is `RTC_DEFAULT_EPOCH` (rtc.h) = 10:10:00 UTC, Wed 26 Aug 2026**,
  and it runs from there until a `T=` replaces it. There is deliberately no
  "WAIT NTP" screen: the old `!rtc_synced()` override in `rebuild()` forced
  MODE_MESSAGE and so **swallowed every `MODE=` request until a `T=` arrived**,
  which made `MODE=TEST` useless for framing on a scope. Verified after reset:
  epoch reads 1787739000 and advances 3.00 s per 3 s.
- Still runtime-unverified: Z-blank polarity (needs a scope), the F401 PWM/RC
  output stage, and the whole F407 target.
- G4 DAC output buffers saturate ~0.2 V from each rail, so the outermost few
  percent of deflection is nonlinear. Cosmetic; frame it with the scope's
  V/div rather than distorting the geometry.
- Smooth second hand is now unblocked: `rtc_localtime()` returns a real
  sub-second `frac` off the millisecond timebase, but `scene_analog()` is still
  only redrawn on the 1 Hz tick, so raising the redraw rate is the remaining
  work. No VBAT coin cell (SNTP re-acquires after power loss by design).
- Not a git repo yet; `.gitignore` is in place for when it becomes one.

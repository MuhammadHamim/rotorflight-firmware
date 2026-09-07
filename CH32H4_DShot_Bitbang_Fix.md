# CH32H4 DShot Bitbang — Reference Port & Root-Cause Notes

This file documents how the working DShot implementation from the reference
`rotorflight` project (Temperslee's CH32H41x port) was ported into this
project, and **why** the previous DShot output was broken.

---

## 1. What was wrong

The previous code compiled the *generic* STM32F4-style bitbang files with
`CH32H4`/`CH32H41x` `#ifdef` patches bolted inside:

| File (before)                  | Problem                                                                 |
| ------------------------------ | ----------------------------------------------------------------------- |
| `drivers/dshot_bitbang.c`      | `bbUpdateComplete()` only reloaded the DMA for `STM32G4`                |
| `drivers/dshot_bitbang_stdperiph.c` | `dmaRegCache_t` lacked the `M1ADDR` register (CH32 DMA has 5 regs) |
| `drivers/dma_ch32h41x.c`       | `dmaMuxEnable()` was passing `index + 1` to `DMA_MuxChannelConfig()`    |

The reference project instead has **dedicated** CH32H41x bitbang files that
avoid all of these problems. Those files were ported here.

---

## 2. Root cause #1 — DMA counter never reloaded (the real killer)

The bitbang engine sends one DShot frame per motor-update by DMA-ing a buffer
of `BSHR` set/reset words to the GPIO port, paced by a TIM1 capture/compare
event.

`USE_DMA_REGISTER_CACHE` is enabled, so `bbSwitchToOutput()` re-arms the DMA
by restoring the saved register image (`bbLoadDMARegs()`), **including the
transfer counter `CNTR` = `portOutputCount`**.

In the old `dshot_bitbang.c`, `bbUpdateComplete()` only called
`bbSwitchToOutput()` inside an `#if defined(STM32G4)` block for the
non-telemetry path:

```c
} else {
#if defined(STM32G4)
    // Using circular mode resets the counter one short, so explicitly reload
    bbSwitchToOutput(bbPort);
#endif
}
```

On CH32H4 this code did **nothing**. After the first frame, the DMA channel's
`CNTR` was left at `0`, so `bbDMA_Cmd(ENABLE)` re-enabled a DMA channel with
zero transfers pending → **no further frames were ever clocked out**. The pin
just sat at its idle level.

The reference (and upstream Betaflight) call it unconditionally:

```c
} else {
    // Using circular mode resets the counter one short, so explicitly reload
    bbSwitchToOutput(bbPort);
}
```

This single difference is why only the very first frame (or nothing visible)
appeared on the scope.

---

## 3. Root cause #2 — `M1ADDR` missing from the DMA register cache

CH32H417 DMA channels have five key registers: `CFGR`, `CNTR`, `PADDR`,
`MADDR` and `M1ADDR`. The old `dmaRegCache_t` only cached four of them, so the
register image saved/restored by `bbSaveDMARegs()`/`bbLoadDMARegs()` was
incomplete. The ported version now caches all five.

---

## 4. DMAMUX — no programming needed for the bitbang pacer

The reference's bitbang path **does not call `dmaMuxEnable()` at all**.

Why it still works:

* WCH's `DMAMUX_DMAREQ_ID_*` enum is **1-based** (`TIM1_CH1 = 1`).
* `DMA_MuxChannelConfig()` writes `request - 1` into the 7-bit field.
* Therefore mux field `0` selects request ID `1` = **`TIM1_CH1`**.
* The DMAMUX registers reset to `0`, so **every DMA channel already listens
  to `TIM1_CH1` out of reset**.
* The bitbang pacer is always **TIM1_CH1** (it is the first free entry in
  `bbTimerHardware`, and `bbFindPacerTimer()` picks the first free one).

So the pacer timer's capture/compare DMA request reaches the DMA channel with
no software setup.

Two consequences:

1. The old code's `dmaMuxEnable(dmaIdentifier, bbPort->dmaChannel)` call was
   harmless but redundant (it wrote `0` into a field that was already `0`).
2. An earlier attempt to "fix" it with `DMA_MuxChannelConfig(index + 1, …)`
   was wrong. `DMA_MuxChannelConfig()` takes the **0-based** descriptor index
   (the WCH library shifts by `x * 8`), so `index` (not `index + 1`) is
   correct. That change was reverted.

> ⚠️ Note: relying on the reset default only works because the pacer is
> TIM1_CH1. If the pacer ever moves to TIM1_CH2/CH3/CH4, the mux would need to
> be programmed with the matching request ID (`2`, `3`, `4`).

---

## 5. Other differences that were aligned

* **`bbTimerHardware` order** — the reference lists `TIM1` before `TIM8`, so
  the pacer prefers TIM1. The old code listed `TIM8` first. With servos on
  TIM8 the conflict check skips it anyway, but matching the reference is
  safer.
* **GPIO mode at setup** — the reference configures the motor pin with
  `IOCFG_AF_PP` / `IO_CONFIG(DIR_OUT, GPIO_MODE_OUT_AF_PP, …)` in
  `bbGpioSetup()` and `dshotBitbangDevInit()`. `bbSwitchToOutput()` then
  reconfigures it to plain push-pull output at runtime. This is preserved.
* **DMA spec field name** — the reference stores the DMAMUX request in
  `dmaChannelSpec->dmaMuxId`; this project stores the same value in
  `dmaChannelSpec->channel`. The ported code uses `.channel`.
* **`DEF_TIM` arity** — the reference uses a 7-parameter `DEF_TIM` (with a
  separate TIM_UP DMA option). This project uses 6 parameters. The ported
  `bbTimerHardware[]` was adapted to 6 parameters; bitbang only needs the
  capture/compare DMA entry, so this is fine.

---

## 6. Files changed

| File                                                   | Change                                                          |
| ------------------------------------------------------ | --------------------------------------------------------------- |
| `src/main/drivers/dshot_bitbang_ch32h41x.c`            | **New** — ported dedicated bitbang core (unconditional reload)  |
| `src/main/drivers/dshot_bitbang_stdperiph_ch32h41x.c`  | **New** — ported stdperiph GPIO/DMA layer                       |
| `src/main/drivers/dshot_bitbang_impl.h`                | Added `M1ADDR` to `dmaRegCache_t` (CH32H4/H41x)                 |
| `make/mcu/CH32H4.mk`                                   | Build the dedicated files instead of the generic ones           |
| `src/main/drivers/dma_ch32h41x.c`                      | Reverted `DMA_MuxChannelConfig(index + 1, …)` back to `index`   |

Build command:

```sh
make TARGET=CH32H417
```

Output: `obj/rotorflight_4.6.0_CH32H417.hex`

---

## 7. Verifying on the bench

Flash the new hex, then probe the tail-motor pin (PB11) with DC coupling and
the probe ground clip on the board GND:

* **DShot300**: ~3.33 µs per bit period, ~53 µs frame, 3.3 V logic swing.
* **DShot600**: ~1.67 µs per bit period, ~26.7 µs frame.

Frames repeat at the motor update rate (a few kHz). If you see frames only
"once at boot", it is the exact symptom of root cause #1 (counter not
reloaded) and means the ported file was not actually compiled in — check
`make/mcu/CH32H4.mk`.

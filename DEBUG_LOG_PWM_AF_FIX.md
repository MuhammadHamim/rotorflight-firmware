# Debug Log: Fixing Motor/Servo PWM Outputs on CH32H417

This document records the full debugging journey for getting the ESC, tail motor, and servo PWM outputs working on the custom CH32H417 flight controller. It is written as a learning reference — what the symptoms were, what each wrong turn taught, and how the final fix works.

---

## 1. The Hardware

From the FC schematic, the output header is wired as:

| Net                    | MCU Pin               | Function             |
| ---------------------- | --------------------- | -------------------- |
| S1                     | PE3                   | Servo 1              |
| S2                     | PE4                   | Servo 2              |
| S3                     | PE5                   | Servo 3              |
| ESC                    | PE6                   | Main motor (PWM ESC) |
| TAIL                   | PB11                  | Tail motor           |
| RPM / TLM / AUX / SBUS | PA2 / PA3 / PB6 / PB7 | UART2 / UART1        |

The original `target.c` mapped PE3–PE6 all to **TIM8_CH1–CH4** (servos + ESC on one timer) and PB11 to **TIM2_CH4**.

---

## 2. Timeline of the Bug

### Stage 1 — "Servos work, motors show nothing"

**Symptom (oscilloscope):**

- S1–S3 (PE3–PE5): clean 50 Hz PWM ✔
- ESC (PE6): flat, roughly DC ✗
- Tail (PB11): flat ✗

**State of the code:** the CH32H4 `DEF_TIM` macro in `src/main/drivers/timer_def.h` hardcoded the GPIO alternate function to `0` for _every_ timer channel:

```c
#define DEF_TIM(tim, chan, pin, flags, out, dmaopt) {  \
    tim,                                               \
    TIMER_GET_IO_TAG(pin),                             \
    ...
    0 /* alternateFunction: CH32H4 uses pin remapping, not AF */  \
```

**Diagnosis path:**

1. `resource` and `timer show` in the CLI proved the pins and timers _were_ allocated — so this was not a configuration problem.
2. Reading the driver code (`pwm_output.c`, `servos.c`) showed servos and motors use the _same_ `IOConfigGPIOAF(io, IOCFG_AF_PP, timerHardware->alternateFunction)` call. Whatever was wrong affected the pin-level configuration.
3. The WCH EVT examples showed timer outputs being configured with `GPIO_PinAFConfig(..., GPIO_AFx)` — proving CH32H4 **does** use the GPIO alternate-function selector (the comment in the macro was wrong).

**Wrong turn #1:** I first assumed the AF is a property of the _timer_ (like STM32F4, where TIM8 is always AF3), so I created a per-timer table: TIM8 → AF3, TIM2 → AF1, TIM11 → AF2. This built and flashed.

### Stage 2 — "Tail works, servos died"

**Symptom:** Tail (PB11) now showed PWM ✔, but S1–S3 stopped ✗.

**What this proved:** AF1 for TIM2 on PB11 was correct, but **AF3 for TIM8 on the PE pins was wrong**. The evidence from the two builds was now extremely valuable:

| Build                          | PE3–PE5 (TIM8) | PB11 (TIM2) |
| ------------------------------ | -------------- | ----------- |
| AF0 everywhere                 | ✔ worked       | ✗ no signal |
| Per-timer (TIM8=AF3, TIM2=AF1) | ✗ no signal    | ✔ worked    |

Deduction: TIM2_CH4 on PB11 = AF1, and TIM8 on PE3–PE5 = **AF0** — i.e. **the AF on CH32H417 is per-pin, not per-timer**. The same timer channel can live on different AFs depending on which pin it comes out of (TIM8_CH1 is AF3 on PC6 but AF0 on PE3).

### Stage 3 — Reading the datasheet (the definitive answer)

The user provided `CH32H417DS0.PDF` (datasheet) and `CH32H417RM.PDF` (reference manual). The RM's GPIO chapter only documents the AF register mechanics — the actual per-pin AF assignments are in the **datasheet**, Table 2-2-4 _"TIM Pin functions"_ (extracted with a small Python/pymupdf script, page 68):

```
TIM1_CH1    PE9(AF1),  PA8(AF1)
TIM8_CH1    PC6(AF3),  PE3(AF0)     <-- AF3 only on the PC pins!
TIM8_CH2    PC7(AF3),  PE4(AF0)
TIM8_CH3    PC8(AF3),  PE5(AF0)
TIM8_CH4    PC9(AF3),  PE6(AF0)
TIM2_CH4    PA3(AF1),  PB11(AF1)
TIM11_CH1   PD3(AF2),  PF6(AF13), PE0(AF13)
TIM12_CH4   PF13(AF13), PE6(AF3), PE15(AF2)   <-- note: PE6 again!
```

This confirmed every observation and revealed the pin is multi-functional: **PE6 can be TIM8_CH4 (AF0), TIM4_CH4 (AF2), TIM9_CH4 (AF4) or TIM12_CH4 (AF3)**.

### Stage 4 — The hidden second bug (why the ESC looked like DC)

With the correct AF0 on TIM8, the ESC _still_ didn't behave. The reason was **timer sharing**, not the AF:

- `fc/init.c` calls `motorInit()` (line 564) _before_ `servoInit()` (line 736).
- Both drivers call `pwmOutConfig()` → `configTimeBase()`, which sets the timer's prescaler (PSC) and period (ARR). **Whoever initializes last wins.**
- The motor driver computed its CCR scale for ~250 Hz (PSC=22, ≈15.9 MHz counter clock). Then `servoInit()` reconfigured TIM8 for the 50 Hz servo rate (1 MHz counter, ARR=19999).
- Result: the ESC's "1 ms idle pulse" value (CCR ≈ 15909) was rendered on the servo timebase → **~15.9 ms high / 4 ms low at 50 Hz** — an ~80% duty cycle waveform that looks like a DC level on a scope.

This explained the very first symptom exactly: _"it shows some dc values"_.

**Fix for this one:** the datasheet shows PE6 has TIM12_CH4 on **AF3** — a completely separate timer. Moving the ESC from TIM8_CH4 to TIM12_CH4 gives the motor its own timebase and removes the conflict entirely.

---

## 3. The Final Fix

### 3.1 Per-pin AF table (`src/main/drivers/timer_def.h`)

The CH32H4 section of `DEF_TIM` now looks up the AF from a per-pin table (same mechanism the STM32F7 port uses), instead of hardcoding `0`:

```c
#define DEF_TIM(tim, chan, pin, flags, out, dmaopt) {   \
    tim,                                                \
    TIMER_GET_IO_TAG(pin),                              \
    DEF_TIM_CHANNEL(CH_##chan),                         \
    flags,                                              \
    (DEF_TIM_OUTPUT(CH_##chan) | out),                  \
    DEF_TIM_AF(TCH_##tim##_##chan, pin)                 \
    ...
```

with the lookup macros:

```c
#define DEF_TIM_AF(timch, pin) CONCAT(DEF_TIM_AF__, DEF_TIM_AF_GET(timch, pin))
#define DEF_TIM_AF__D(af_n) GPIO_AF##af_n
```

and ~130 table entries covering every pin in `fullTimerHardware` (which compiles because `USE_TIMER_MGMT` is enabled on CH32H4), sourced from datasheet Table 2-2-4. A few examples:

```c
/* TIM8: AF3 on PC6-PC9, AF0 on PE3-PE6 */
#define DEF_TIM_AF__PE3__TCH_TIM8_CH1  D(0)
#define DEF_TIM_AF__PC6__TCH_TIM8_CH1  D(3)
#define DEF_TIM_AF__PE6__TCH_TIM8_CH4  D(0)
#define DEF_TIM_AF__PC9__TCH_TIM8_CH4  D(3)

/* TIM2 */
#define DEF_TIM_AF__PB11__TCH_TIM2_CH4 D(1)

/* TIM12: PE6 as an alternative ESC-capable channel */
#define DEF_TIM_AF__PE6__TCH_TIM12_CH4 D(3)
```

The `NONE` entries (used by the DShot bitbang pacer timers in `dshot_bitbang.c`) map to `D(0)` — bitbang drives the pin as plain GPIO so the AF value is irrelevant, but the macro must still resolve.

### 3.2 ESC moved to its own timer (`src/main/target/CH32H417/target.c`)

```c
const timerHardware_t timerHardware[USABLE_TIMER_CHANNEL_COUNT] = {
    DEF_TIM(TIM8,  CH1, PE3,  TIM_USE_SERVO, 0, 0),  // S1
    DEF_TIM(TIM8,  CH2, PE4,  TIM_USE_SERVO, 0, 1),  // S2
    DEF_TIM(TIM8,  CH3, PE5,  TIM_USE_SERVO, 0, 2),  // S3
    DEF_TIM(TIM12, CH4, PE6,  TIM_USE_MOTOR, 0, 6),  // Main Motor (ESC)
    DEF_TIM(TIM2,  CH4, PB11, TIM_USE_MOTOR, 0, 4),  // Tail Motor
    DEF_TIM(TIM11, CH1, PD3,  TIM_USE_LED,   0, 5),  // LED strip
};
```

Final pin/AF/timer map (all values verified by preprocessing the compiled `timerHardware` array):

| Pin  | Function  | Timer     | Channel | AF   | Datasheet   |
| ---- | --------- | --------- | ------- | ---- | ----------- |
| PE3  | S1 servo  | TIM8      | CH1     | 0x00 | PE3(AF0) ✔  |
| PE4  | S2 servo  | TIM8      | CH2     | 0x00 | PE4(AF0) ✔  |
| PE5  | S3 servo  | TIM8      | CH3     | 0x00 | PE5(AF0) ✔  |
| PE6  | ESC       | **TIM12** | CH4     | 0x03 | PE6(AF3) ✔  |
| PB11 | Tail      | TIM2      | CH4     | 0x01 | PB11(AF1) ✔ |
| PD3  | LED strip | TIM11     | CH1     | 0x02 | PD3(AF2) ✔  |

Now every output is on its own timer: TIM8 = servos only, TIM12 = ESC only, TIM2 = tail only. Servo init can no longer corrupt motor timing.

---

## 4. How the GPIO AF Mechanism Works (the background)

CH32H417 GPIOs are multiplexed like STM32F4/F7:

1. The pin's mode is set to **alternate-function push-pull** (`GPIO_Mode_AF_PP`).
2. A 4-bit **alternate function number (AF0–AF15)** is written to the `AFIO->GPIOx_AFLR/AFHR` registers via `GPIO_PinAFConfig()`.
3. Each AF number maps the pin to a different peripheral. The mapping is fixed in silicon and documented **per pin** in the datasheet.

In the firmware, the flow is:

```
target.c:  DEF_TIM(TIM12, CH4, PE6, TIM_USE_MOTOR, 0, 6)
                 |
                 v  (compile time: AF lookup)
timer_def.h:  DEF_TIM_AF__PE6__TCH_TIM12_CH4  ->  D(3)  ->  GPIO_AF3
                 |
                 v  (runtime)
motorPwmDevInit()  ->  IOConfigGPIOAF(io, IOCFG_AF_PP, GPIO_AF3)
                 ->  GPIO_PinAFConfig(GPIOE, pin 6, 3)   // PE6 -> TIM12_CH4
                 ->  pwmOutConfig(TIM12, ...)            // PSC/ARR/CCR + start
```

If the AF number written doesn't match the peripheral the driver then programs, the timer runs and the CCR updates — but **the pin is physically wired to a different peripheral**, so nothing (or the wrong signal) appears.

---

## 5. Lessons Learned

1. **On WCH CH32H4 parts, the alternate function is a property of the (pin, peripheral) pair — not of the peripheral.** Never assume "TIM8 = AF3 everywhere". Always check the datasheet's per-pin function table. (On many STM32s the AF _is_ consistent per timer, which makes this trap easy to fall into when porting.)

2. **Empirical A/B evidence is a powerful diagnosis tool.** The pair of builds (AF0: servos ✔, tail ✗ / AF3: servos ✗, tail ✔) uniquely determined the correct AFs _before_ the datasheet was even opened. When a signal appears/disappears with exactly one variable changed, trust what it tells you.

3. **RTFM at the right level.** The reference manual (RM) documents register mechanics but defers pin functions to the datasheet (DS). For pin muxing questions, the datasheet's "Pin Definition / Pin Functions" tables are the ground truth. The WCH EVT example code is helpful but only shows _one_ pin option per peripheral (PC6 for TIM8_CH1), which caused the wrong initial assumption.

4. **Watch out for shared timers.** When two drivers call `configTimeBase()` on the same hardware timer, the last initializer silently reprograms the rate for everyone. Symptom: outputs "work" but with wildly wrong pulse widths (a 1 ms pulse becoming 16 ms looks like DC on a scope). Prefer giving each output function its own timer, or ensure all users of a timer agree on the timebase.

5. **Init order matters.** `motorInit()` and `servoInit()` both configure timer hardware; the order in `fc/init.c` decides who wins. When adding a new output, check whether its timer is already claimed by an earlier-initialized driver.

6. **`resource` / `timer show` in the CLI are your friends.** They immediately distinguish "the pin isn't configured" from "the pin is configured but muxed wrong" — the first points at config/code, the second at the AF value.

7. **A flat DC reading on a scope is ambiguous.** It can mean: pin muxed to nothing, CCR stuck at 0 (driver not writing), CCR ≥ ARR (constant high), or a valid-but-huge duty cycle. Check the actual level (0 V vs 3.3 V) and vary the throttle to disambiguate.

---

## 6. Files Changed

| File                                | Change                                                                                                        |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| `src/main/drivers/timer_def.h`      | CH32H4 `DEF_TIM` macro now uses per-pin AF lookup; added complete per-pin AF table from datasheet Table 2-2-4 |
| `src/main/target/CH32H417/target.c` | ESC moved from TIM8_CH4 (shared with servos) to TIM12_CH4 (dedicated timer); comments updated                 |

Build: `make TARGET=CH32H417` → `obj/rotorflight_4.6.0_CH32H417.hex`

## 7. Verified Results

| Output      | Expected signal                                           | Status    |
| ----------- | --------------------------------------------------------- | --------- |
| S1 (PE3)    | 50 Hz servo PWM, ~1.5 ms                                  | ✔ working |
| S2 (PE4)    | 50 Hz servo PWM, ~1.5 ms                                  | ✔ working |
| S3 (PE5)    | 50 Hz servo PWM, ~1.5 ms                                  | ✔ working |
| ESC (PE6)   | 250 Hz PWM, ~1 ms idle pulse                              | ✔ working |
| Tail (PB11) | Motor PWM (DShot requires the mixed-protocol driver work) | ✔ working |

**Note on DShot:** the Configurator's _Throttle Protocol_ is global for all motors. Running standard PWM on the ESC while the tail uses DShot requires the per-motor protocol support described in `MOTOR_SERVO_CONFIGURATION.md`.

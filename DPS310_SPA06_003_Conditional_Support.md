# Learning Note: Conditionally Adding SPA06-003 Support to the DPS310 Barometer Driver

This note records how support for the **SPA06-003** barometer was added *conditionally* inside the existing `barometer_dps310.c` driver, without breaking the original **DPS310** support.

## 1. Context

The DPS310 driver in Rotorflight/Cleanflight/Betaflight/INAV already supported the Infineon DPS310 (chip ID `0x10`). The SPA06-003 is a pin- and register-compatible variant, but it:

- Reports a different chip ID (`0x11`).
- Uses **two additional calibration coefficients** (`c31`, `c40`).
- Requires a **different pressure compensation formula**.

Because both sensors share the same register layout and differ only in ID and math, the cleanest solution was to keep one driver and branch behavior based on a runtime flag.

## 2. Files Changed

- `src/main/drivers/barometer/barometer_dps310.c`
- `src/main/drivers/barometer/barometer_dps310.h` (if the public detection prototype or config define was touched)

## 3. What Was Added

### 3.1 New chip-ID define

A new product ID define was added next to the existing DPS310 ID:

```c
#define DPS310_ID_REV_AND_PROD_ID   (0x10)
#define SPA06_003_ID_REV_AND_PROD_ID (0x11)
```

### 3.2 Runtime state flag

The driver state struct gained a boolean flag so the rest of the code can choose the correct path:

```c
typedef struct
{
    calibrationCoefficients_t calib;
    bool isSpa06_003;
    float pressure;
    float temperature;
} baroState_t;
```

### 3.3 Extra calibration coefficients

The calibration struct was extended with the two SPA06-003-only coefficients:

```c
typedef struct
{
    int16_t c0;
    int16_t c1;
    int32_t c00;
    int32_t c10;
    int16_t c01;
    int16_t c11;
    int16_t c20;
    int16_t c21;
    int16_t c30;
    int16_t c31;  // SPA06_003 only
    int16_t c40;  // SPA06_003 only
} calibrationCoefficients_t;
```

### 3.4 Detection branches conditionally

In `deviceDetect()`, the ID read now sets the flag appropriately:

```c
if (ack && chipId[0] == DPS310_ID_REV_AND_PROD_ID)
{
    baroState.isSpa06_003 = false;
    return true;
}

if (ack && chipId[0] == SPA06_003_ID_REV_AND_PROD_ID)
{
    baroState.isSpa06_003 = true;
    return true;
}
```

This is the key conditional decision: the same detection routine now handles both sensors and records which one is present.

### 3.5 Conditional coefficient read

During `deviceConfigure()`, the number of bytes read from the coefficient register depends on the detected variant:

```c
const uint8_t coefficientLength = baroState.isSpa06_003 ? 21 : 18;
```

The two extra 12-bit coefficients are parsed only when `isSpa06_003` is true:

```c
if (baroState.isSpa06_003)
{
    baroState.calib.c31 = getTwosComplement(((uint32_t)coef[18] << 4) | ... , 12);
    baroState.calib.c40 = getTwosComplement((((uint32_t)coef[19] & 0x0F) << 8) | ... , 12);
}
```

### 3.6 Conditional pressure compensation formula

The compensated pressure calculation branches at runtime:

```c
if (baroState.isSpa06_003)
{
    // SPA06_003: 4th-order pressure compensation
    baroState.pressure = c00
        + Praw_sc * (c10 + Praw_sc * (c20 + Praw_sc * (c30 + Praw_sc * c40)))
        + Traw_sc * (c01 + Praw_sc * (c11 + Praw_sc * (c21 + Praw_sc * c31)));
}
else
{
    // Standard DPS310 formula
    baroState.pressure = c00
        + Praw_sc * (c10 + Praw_sc * (c20 + Praw_sc * c30))
        + Traw_sc * c01
        + Traw_sc * Praw_sc * (c11 + Praw_sc * c21);
}
```

## 4. Why This Pattern Was Used

| Approach | Pros | Cons |
|---|---|---|
| **One driver with runtime branches** (chosen) | Reuses all I2C/SPI logic, register addresses, and initialization code; only math and ID differ | A single boolean flag is carried in state |
| Separate driver files | Cleaner separation | Lots of duplicated code for a tiny difference |

The runtime-branch approach is the standard pattern in flight-controller firmware when two sensors are register-compatible and differ only in ID and compensation math.

## 5. Build / Test Verification

After the changes, build the target that includes the DPS310 barometer:

```bash
make TARGET=CH32H417
```

A successful build confirms the conditional code compiles correctly. To fully verify behavior, connect each sensor (DPS310 and SPA06-003) in turn and confirm:

1. `deviceDetect()` returns true.
2. Coefficients are read without bus errors.
3. `baroState.pressure` reports plausible values.

## 6. Key Takeaways

- Keep variant-specific logic behind a single source of truth (`baroState.isSpa06_003`).
- Read only as many coefficient bytes as the variant needs to avoid over-reading the bus.
- Place the detection branching as early as possible (`deviceDetect`) so downstream code can stay simple.
- Keep the original sensor path unchanged to avoid regressions.

## 7. References

- DPS310 datasheet: <https://www.infineon.com/dgdl/Infineon-DPS310-DataSheet-v01_02-EN.pdf>
- Source: `src/main/drivers/barometer/barometer_dps310.c`

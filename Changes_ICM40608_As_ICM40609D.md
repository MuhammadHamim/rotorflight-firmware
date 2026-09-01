# ICM40608 as ICM40609D Integration Notes

## Goal

Use the existing ICM40609D driver to drive the ICM40608 gyro/accel on the CH32H417 target, because the ICM40609D driver is more complete and handles both chips.

## Changed files

### 1. `src/main/target/CH32H417/target.h`

Added two feature defines:

```c
#define USE_ACCGYRO_ICM40609D
#define USE_ICM40608_AS_ICM40609D
```

### 2. `src/main/target/CH32H417/target.mk`

Added the ICM40609D source file to the target build list:

```make
drivers/accgyro/accgyro_spi_icm40609.c \
```

### 3. `src/main/drivers/accgyro/accgyro_mpu.c`

- Included `accgyro_spi_icm40609.h` when `USE_ICM40608_AS_ICM40609D` is defined.
- SPI gyro detection table now uses `icm40609SpiDetect` instead of `icm40608SpiDetect` when the macro is defined.

### 4. `src/main/drivers/accgyro/accgyro_spi_icm40608.c`

- Included `accgyro_spi_icm40609.h` when the macro is defined.
- `icm40608SpiGyroDetect()` now sets `gyro->initFn = icm40609GyroInit` and `gyro->readFn = mpuGyroReadSPI`.
- `icm40608SpiAccDetect()` now sets `acc->initFn = icm40609AccInit` and `acc->readFn = mpuAccReadSPI`.

### 5. `src/main/drivers/accgyro/accgyro_spi_icm40609.c`

- Added missing `ICM40609_WHO_AM_I_CONST 0x3B`.
- When `USE_ICM40608_AS_ICM40609D` is defined:
  - `icm40609SpiDetect()` reports the detected sensor as `ICM_40608_SPI` so it uses the existing ICM40608 gyro/acc detection path.
  - `icm40609SpiAccDetect()` / `icm40609SpiGyroDetect()` check for `ICM_40608_SPI`.
  - Only the ICM40608 AAF profile table is compiled; the ICM40609D table is excluded to avoid unused-variable warnings.

## Build status

`make TARGET=CH32H417` builds successfully with the macro enabled.

## Files still required

- `src/main/drivers/accgyro/accgyro_spi_icm40608.c` is still needed because `gyro_init.c` calls `icm40608SpiGyroDetect()` and the acc init calls `icm40608SpiAccDetect()` for the `ICM_40608_SPI` sensor type. It now acts as a thin wrapper that forwards to the ICM40609D driver.
- `src/main/drivers/accgyro/accgyro_spi_icm40608.h` is still included by `accgyro_mpu.c` and `accgyro_spi_icm40608.c`.

## To fully delete the old ICM40608 files (optional cleanup)

If you want to remove `accgyro_spi_icm40608.c/.h` later, you must:

1. Add `GYRO_ICM40609` to `gyroHardware_e` in `src/main/drivers/accgyro/accgyro.h`.
2. Add `ICM_40609_SPI` to `mpuSensor_e` in `src/main/drivers/accgyro/accgyro_mpu.h`.
3. Add a `GYRO_ICM40609` case to `gyroDetect()` in `src/main/sensors/gyro_init.c` that calls `icm40609SpiGyroDetect()`.
4. Do the same for the accelerometer detect path.
5. Remove the `USE_GYRO_SPI_ICM40608` / `USE_ACC_SPI_ICM40608` defines from `target.h`.
6. Remove `accgyro_spi_icm40608.c` from `target.mk`.

Keeping the wrapper files is the smaller, safer change for now.

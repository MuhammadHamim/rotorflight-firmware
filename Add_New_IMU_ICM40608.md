# To add new IMU ICM40608

- Crete a new file in the folder `src/main/drivers/accgyro/` called `accgyro_spi_icm40608.c`
- Create a new file in the folder `src/main/drivers/accgyro/` called `accgyro_spi_icm40608.h`

### This file talks to the Chip's registers and implements the functions to read and write to the chip. Follow the other IMU driver files as a reference.

# List the new IMU Gyro in the accgyro.h file in the enum `gyro_type_e` and the new IMU Accel in the accel.h file in the enum `accel_type_e`.

- `accgyro.h` add `GYRO_ICM40608` to the enum `gyro_type_e`
- `accel.h` add `ACC_ICM40608` to the enum `accel_type_e`
- add the new IMU's WHO_AM_I constant to the `accgyro_mpu.h` file. The constant is `#define ICM40608_WHO_AM_I_CONST (0x39)`. From datasheet you'll find the WHO_AM_I register address we should use the reset value here, since I found that the 42688p also using the reset value in ICM42688P_WHO_AM_I_CONST. The WHO_AM_I register address is 75h(0x75) and the reset value is 0x39.
- Add ICM_40608_SPI to mpuSensor_e in `accgyro_mpu.h` file.
- Add defined(USE_GYRO_SPI_ICM40608) to the GYRO_USES_SPI list (line ~27) for consistency with the other SPI gyros (it's currently a mostly-unused macro, but keep it correct).

- also we need to include the 'drivers/accgyro/accgyro_spi_icm40608.h' header file in the `accgyro_mpu.c` file.

- add

```c
#ifdef USE_GYRO_SPI_ICM40608
    icm40608SpiDetect,
#endif
```

in the `accgyro_mpu.c` file, add to `gyroSpiDetectFnTable[]`

## `gyro_init.c`

- include IMU header file `#include "drivers/accgyro/accgyro_spi_icm40608.h"` in the `gyro_init.c` file.
- add a detection case in gyroDetect():

```c
#ifdef USE_GYRO_SPI_ICM40608
    case GYRO_ICM40608:
        if(icm40608SpiGyroDetect(dev)){
            gyroHardware = GYRO_ICM40608;
            break;
        }
        FALLTHROUGH;
#endif
```

- In gyroInitSensor()'s overflow-protection switch, add case GYRO_ICM40608:. The ICM-206xx family is subject to the overflow/inversion bug, so put it in the gyroHasOverflowProtection = false; group (or leave it out — the default: already gives false, which is the safe choice for a new gyro).
- Add || defined(USE_GYRO_SPI_ICM40608) to the #if lists in both gyroDetectSensor() and gyroPreInitSensor() so mpuDetect() actually runs for it.

## `acceleration_init.c`

- include IMU header file `#include "drivers/accgyro/accgyro_spi_icm40608.h"` in the `acceleration_init.c` file.
- add a case in accDetect():

```c
#ifdef USE_ACC_SPI_ICM40608
    case ACC_ICM40608:
        if (icm40608SpiAccDetect(dev)) {
            accHardware = ACC_ICM40608;
            break;
        }
        FALLTHROUGH;
#endif
```

## [`gyro_sync.c`](./src/main/drivers/accgyro/gyro_sync.c)

- Add a case in gyroSetSampleRate() for ICM_40608_SPI. If it behaves like the ICM-20689, mirror its rates (8 kHz on H7, otherwise 1 kHz): [haven't added yet]

```c
case ICM_40608_SPI:
#if defined(STM32H7)
    gyroRateKHz = GYRO_RATE_8_kHz;
    gyroSampleRateHz = 8000;
    accSampleRateHz = 1000;
#else
    gyroRateKHz = GYRO_RATE_1_kHz;
    gyroSampleRateHz = 1000;
    accSampleRateHz = 1000;
#endif
    break;
```

## `settings.c`

- add "ICM40608" to the lookupTableAccHardware[] and lookupTableGyroHardware[] arrays in `settings.c` file.

```c
const char * const lookupTableAccHardware[] = { ... "ICM42688P", "ICM40608", "BMI160", ... };
const char * const lookupTableGyroHardware[] = { ... "ICM42688P", "ICM40608", "BMI160", ... };
```

⚠️ These tables are indexed by the enum values, so the order must exactly match [gyroHardware_e](./src/main/drivers/accgyro/accgyro.h) / [accelerationSensor_e](./src/main/pg/accel.h).

## `common_post.h`

- add `defined(USE_GYRO_SPI_ICM40608)` to the #if list for USE_SPI_GYRO, so that the SPI bus is initialized for it.

## In target board CH32H417 add ICM40608 Gyro and Acc

## add the `drivers/accgyro/accgyro_spi_icm40608.c` and `drivers/accgyro/accgyro_spi_icm40608.h` files to the `target.mk` file to TARGET_SRC:

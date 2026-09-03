# Motor and Servo Configuration Guide for CH32H417 Flight Controller

## Hardware Pin Assignment (from schematic)

Based on your flight controller schematic:

| Function             | Pin  | Timer Channel | Protocol       |
| -------------------- | ---- | ------------- | -------------- |
| **Servo 1 (S1)**     | PE3  | TIM8_CH1      | PWM (50-498Hz) |
| **Servo 2 (S2)**     | PE4  | TIM8_CH2      | PWM (50-498Hz) |
| **Servo 3 (S3)**     | PE5  | TIM8_CH3      | PWM (50-498Hz) |
| **Main Motor (ESC)** | PE6  | TIM8_CH4      | PWM            |
| **Tail Motor**       | PB11 | TIM2_CH4      | DShot          |
| **LED Strip**        | PD3  | TIM11_CH1     | WS2812         |

## Firmware Configuration Changes

### 1. Timer Hardware Configuration (`target.c`)

The timer hardware array has been updated to reflect your schematic:

```c
const timerHardware_t timerHardware[USABLE_TIMER_CHANNEL_COUNT] = {
    DEF_TIM(TIM8, CH1, PE3, TIM_USE_SERVO, 0, 0),  // S1 (Servo 1)
    DEF_TIM(TIM8, CH2, PE4, TIM_USE_SERVO, 0, 1),  // S2 (Servo 2)
    DEF_TIM(TIM8, CH3, PE5, TIM_USE_SERVO, 0, 2),  // S3 (Servo 3)
    DEF_TIM(TIM8, CH4, PE6, TIM_USE_MOTOR, 0, 3),  // Main Motor (ESC - PWM)
    DEF_TIM(TIM2, CH4, PB11, TIM_USE_MOTOR, 0, 4), // Tail Motor (DShot)
    DEF_TIM(TIM11, CH1, PD3, TIM_USE_LED, 0, 5),   // LED_STRIP (WS2812)
};
```

### 2. Target Configuration (`target.h`)

The following settings are already configured in `target.h`:

- `USE_SERVOS` - Enabled for servo support
- `USE_DSHOT_BITBANG` - Enabled for DShot protocol on tail motor
- `USABLE_TIMER_CHANNEL_COUNT 6` - Total timer channels used

## CLI Configuration Commands

After flashing the firmware, use these CLI commands to configure your motors and servos:

### Motor Configuration

```bash
# Set main motor (motor 1) to PWM protocol
set motor_pwm_protocol = PWM_ONESHOT125

# Set motor control modes:
# Motor 1 (main ESC) = PWM mode (0)
# Motor 2 (tail) = DShot mode (1)
set motor_control_mode = 0,1

# Set motor PWM rate for main motor (50-498 Hz)
set motor_pwm_rate = 50

# Enable DShot bitbang for tail motor
set dshot_bitbang = ON

# Save configuration
save
```

### Servo Configuration

```bash
# Configure servo parameters for S1, S2, S3
# Format: servo <index> <mid> <min> <max> <rneg> <rpos> <rate> <speed> <flags>

# Servo 1 (S1) - typical helicopter servo settings
servo 0 1500 -500 500 100 100 50 0 0

# Servo 2 (S2)
servo 1 1500 -500 500 100 100 50 0 0

# Servo 3 (S3)
servo 2 1500 -500 500 100 100 50 0 0

# Set servo PWM rate (typically 50Hz for standard servos, up to 333Hz for digital)
set servo_pwm_rate = 50

# Save configuration
save
```

### Mixer Configuration (for Helicopter)

For a typical helicopter setup with swashplate:

```bash
# Set mixer to helicopter mode
set mixer_mode = HELI_120_CCPM

# Configure servo geometry for swashplate mixing
# This depends on your specific helicopter model
servo_geometry 0 0 0    # S1 - cyclic pitch
servo_geometry 1 120 0  # S2 - cyclic roll (120° offset)
servo_geometry 2 240 0  # S3 - cyclic pitch (240° offset)

# Save configuration
save
```

## Verification Steps

1. **Check motor/servo assignment:**

   ```bash
   status
   ```

   This will show which timers are assigned to motors and servos.

2. **Test motors:**

   ```bash
   motor 1 1100  # Test main motor at low throttle
   motor 2 1100  # Test tail motor at low throttle
   motor 1 1000  # Stop main motor
   motor 2 1000  # Stop tail motor
   ```

3. **Test servos:**

   ```bash
   servo 0 1500  # Center servo 1
   servo 1 1500  # Center servo 2
   servo 2 1500  # Center servo 3
   ```

4. **Verify DShot on tail motor:**
   ```bash
   get dshot_bitbang
   get motor_control_mode
   ```

## Important Notes

### Mixed Protocol Support

- **Main Motor (PE6)**: Uses traditional PWM protocol (PWM_ONESHOT125, MULTISHOT, etc.)
- **Tail Motor (PB11)**: Uses DShot bitbang protocol
- The firmware supports per-motor protocol selection via `motor_control_mode` array

### DMA Channel Allocation

- Motors/Servos: DMA channels 0-4 (DMA1_CH1-5)
- LED Strip: DMA channel 5 (DMA1_CH7)
- No conflicts between motor/servo and LED strip DMA

### Timer Allocation

- **TIM8**: Shared by servos (CH1-3) and main motor (CH4)
- **TIM2**: Dedicated to tail motor (CH4)
- **TIM11**: Dedicated to LED strip (CH1)

### Servo Update Rates

- Standard analog servos: 50Hz
- Digital servos: 50-333Hz
- High-speed digital servos: up to 560Hz

### Motor Protocols

- **PWM protocols**: PWM, ONESHOT125, ONESHOT42, MULTISHOT
- **DShot protocols**: DSHOT150, DSHOT300, DSHOT600, DSHOT1200

## Troubleshooting

### Motors not responding

1. Check `motor_pwm_protocol` setting
2. Verify `motor_control_mode` for each motor
3. Ensure ESC is calibrated and armed

### Servos not moving

1. Check `servo_pwm_rate` setting
2. Verify servo parameters with `servo` command
3. Check mixer configuration

### DShot tail motor issues

1. Verify `dshot_bitbang = ON`
2. Check ESC supports DShot protocol
3. Try lower DShot speed (DSHOT150 or DSHOT300)

### Timer conflicts

- If you experience issues, check `status` command output
- Ensure no timer channel is assigned to multiple functions

## Building and Flashing

After making these changes:

```bash
# Build firmware for CH32H417 target
make TARGET=CH32H417

# Flash using DFU or your preferred method
make TARGET=CH32H417 flash
```

## Additional Resources

- Rotorflight documentation: https://github.com/rotorflight
- CLI reference: Use `help` command in CLI for all available commands
- Mixer documentation: See `docs/Mixer.md` in the firmware repository

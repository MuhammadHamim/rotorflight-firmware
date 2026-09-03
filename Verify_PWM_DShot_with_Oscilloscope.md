# Verifying PWM and DShot Outputs with an Oscilloscope

This guide explains how to confirm that motor/servo PWM and DShot signals are generated correctly on the **CH32H417** flight controller after flashing Rotorflight firmware.

---

## 1. What You Need

| Item | Purpose |
|------|---------|
| Oscilloscope | At least 2 channels, 10 MHz bandwidth or more is sufficient |
| Logic analyzer (optional) | Useful for decoding DShot packets |
| Multimeter | Verify ground and 3.3 V / 5 V reference |
| Jumper wires / probe hooks | Connect to FC motor/servo pads without shorting nearby pins |
| USB cable / battery | Power the flight controller and ESCs |

---

## 2. Signal Reference for CH32H417

From `src/main/target/CH32H417/target.c`:

| Function | Pin | Timer / Channel | Protocol |
|----------|-----|-----------------|----------|
| Servo S1 | PE3 | TIM8_CH1 | PWM servo |
| Servo S2 | PE4 | TIM8_CH2 | PWM servo |
| Servo S3 | PE5 | TIM8_CH3 | PWM servo |
| Main Motor (ESC) | PE6 | TIM8_CH4 | PWM |
| Tail Motor | PB11 | TIM2_CH4 | DShot |
| LED Strip | PD3 | TIM11_CH1 | WS2812 (not covered here) |

> **Tip:** PWM and DShot share the same physical layer — a digital pulse train on a single wire. The difference is the pulse width / bit encoding.

---

## 3. Pre-Flight Software Setup

1. **Flash the firmware** for `TARGET=CH32H417`:
   ```bash
   make TARGET=CH32H417
   ```
2. **Connect the FC to Rotorflight Configurator** (or your preferred config tool).
3. **Remove propellers** and keep motors unarmed if possible.
4. **Configure output protocols**:
   - Main Motor → PWM (default for TIM8_CH4).
   - Tail Motor → DShot (e.g. DShot150/300/600).
5. **Power the FC with a battery** so the ESC BEC or regulator provides servo/motor rail voltage.
6. **Use the Motors tab** in Configurator to spin each motor individually, or use the Servos tab to move servos.

---

## 4. Oscilloscope Settings

| Setting | Recommended Value |
|---------|-------------------|
| Coupling | DC |
| Probe | 10:1 or 1:1, whichever gives a clean 3.3 V signal |
| Time base | 500 µs/div for PWM, 20 µs/div for DShot |
| Voltage scale | 1 V/div or 2 V/div |
| Trigger | Rising edge, auto or normal, level ~1.5 V |

---

## 5. Connecting the Probes

1. Connect the **oscilloscope ground clip** to a **GND pad** on the FC (a large ground plane pad or the battery negative tab).
2. Connect the **signal probe** to the pad you want to test:
   - PWM Main Motor → **PE6**
   - DShot Tail Motor → **PB11**
   - Servos → **PE3, PE4, PE5**
3. Keep probe leads short to reduce ringing and noise.

> **Warning:** Never let the probe ground clip touch a motor power pad. This can short the BEC/ESC.

---

## 6. Checking PWM Output

### Expected Signal

- Frequency: **50 Hz** (20 ms period) for servos and ESC PWM.
- Pulse width: **1000 µs to 2000 µs** (1 ms to 2 ms).
- Idle throttle is usually **1000 µs**.
- Mid-stick servo position is **1500 µs**.

### What to Look For on the Scope

1. A repeating pulse every **20 ms**.
2. Pulse width changes when you move the throttle stick or servo slider.
3. High level near **3.3 V** (or 5 V if the pad is 5 V tolerant / buffered).
4. Clean rising and falling edges without excessive ringing.

### Quick Checks

| Symptom | Likely Cause |
|---------|--------------|
| No signal | Output not enabled, wrong timer DMA option, or firmware not flashed correctly |
| Flat 0 V | Pin configured as input, or motor/servo output disabled |
| Flat 3.3 V | Timer stuck high, wrong timer channel, or target mismatch |
| Wrong pulse width | ESC protocol / servo midpoint / endpoint settings |
| Jittery pulse | Electrical noise, loose ground, or DMA conflict |

---

## 7. Checking DShot Output

### Expected Signal

- DShot uses a **single-wire digital serial protocol** at 3.3 V logic.
- Each packet is a 16-bit frame sent at the configured DShot bitrate (DShot150, DShot300, DShot600, etc.).
- The line is normally **low** between packets.

### Bit Encoding

- A `1` bit: high ~75% of the bit period, low ~25%.
- A `0` bit: high ~37.5% of the bit period, low ~62.5%.

Example bit periods:

| DShot Version | Bit Period | `1` high time | `0` high time |
|---------------|-----------|---------------|---------------|
| DShot150 | 6.67 µs | 5.0 µs | 2.5 µs |
| DShot300 | 3.33 µs | 2.5 µs | 1.25 µs |
| DShot600 | 1.67 µs | 1.25 µs | 0.625 µs |

### What to Look For on the Scope

1. Bursts of narrow pulses when you raise the motor throttle.
2. Pulses are roughly 3.3 V high, 0 V low.
3. Pulse widths match the DShot bitrate you configured.
4. At least one packet repeats rapidly while the motor channel is active.

### Using a Logic Analyzer (Optional)

If you have a logic analyzer:

1. Set the threshold to **1.65 V**.
2. Capture PB11 at the same bitrate configured in Configurator.
3. Use PulseView or Saleae DShot decoder to verify the throttle command value changes when you move the slider.

### Quick Checks

| Symptom | Likely Cause |
|---------|--------------|
| No pulses | DShot not selected, motor protocol mismatch, or no DMA assigned |
| Random pulses but ESC does not arm | DShot bitrate mismatch, signal ground missing, or ESC not DShot capable |
| Correct pulses but motor does not spin | ESC needs low-throttle arming, protocol setting on ESC is wrong |

---

## 8. Step-by-Step Measurement Workflow

1. **Power on the FC** with USB and/or battery.
2. **Open Configurator** and connect.
3. **Go to the Motors tab**.
4. **Enable motor control** if required (usually a switch).
5. **Set Master slider to 0%** and verify PWM output is 1000 µs / DShot shows idle packets.
6. **Raise the slider to ~10%** and confirm:
   - PWM pulse width increases.
   - DShot packet rate / values change.
7. **Move the Servo sliders** and verify PE3, PE4, PE5 show 50 Hz pulses that change width.
8. **Repeat for each output** you want to validate.

---

## 9. Common Pitfalls

- **Ground reference:** Always use the FC ground, not the oscilloscope earth ground if using mains-powered bench equipment, to avoid ground loops.
- **Probe loading:** A long ground lead can distort fast DShot edges. Use a spring ground tip if available.
- **DMA conflicts:** In `target.c`, each motor/servo output uses a distinct DMA option (`0-4`). If two outputs share a DMA stream, one may not work.
- **Pin mapping mismatch:** Confirm the physical pad matches the pin name in `target.c`. Schematic labels may differ.

---

## 10. Summary Table

| Output | Signal Type | Frequency | Typical Pulse / Packet |
|--------|-------------|-----------|------------------------|
| Servo S1-S3 | PWM | 50 Hz | 1000–2000 µs |
| Main Motor | PWM | 50 Hz | 1000–2000 µs |
| Tail Motor | DShot | Packet rate depends on bitrate | 16-bit packets |

---

## 11. Related Files

- `src/main/target/CH32H417/target.c` — timer and DMA assignments
- `MOTOR_SERVO_CONFIGURATION.md` — how motor/servo protocols are configured in firmware
- `docs/Controls.md` — transmitter setup and channel mapping

If the signals look correct on the scope but the ESC or servo still does not respond, review the protocol settings and wiring next.

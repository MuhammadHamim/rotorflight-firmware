# Fix USB Enumeration Failure After Flashing Rotorflight Firmware to CH32H417

After flashing the rotorflight firmware to the FC via DFU, the device disappears from the host (laptop) — the USB CDC VCP never enumerates. This analysis identifies multiple root causes and proposes fixes.

## Root Cause Analysis

### Issue 1 (CRITICAL): `.noncacheable` Section Missing from Linker Script

The CherryUSB middleware declares USB DMA buffers in a `.noncacheable` section via `USB_NOCACHE_RAM_SECTION` ([usb_config.h:43](file:///f:/BetaFlight/rotorflight/lib/main/CH32H41x/middlewares/board/usb_config.h#L43)):

```c
#define USB_NOCACHE_RAM_SECTION __attribute__((section(".noncacheable")))
```

These are the critical USB endpoint buffers in [cdc_vcp_ch32h41x.c:71-75](file:///f:/BetaFlight/rotorflight/lib/main/CH32H41x/middlewares/board/cdc_vcp_ch32h41x.c#L71-L75):
```c
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t usb_drivers_buffer[CDC_MAX_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t read_buffer[RX_BUFFER_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t write_buffer[TX_BUFFER_SIZE];
```

And the USB core state in [usbd_core.c:40](file:///f:/BetaFlight/rotorflight/lib/main/CH32H41x/middlewares/core/usbd_core.c#L40):
```c
USB_NOCACHE_RAM_SECTION struct usbd_core_priv { ... }
```

**The linker script [ch32_flash_h41x_split.ld](file:///f:/BetaFlight/rotorflight/src/link/ch32_flash_h41x_split.ld) has NO `.noncacheable` section**, causing these buffers to become orphan sections. The linker places them at an unspecified location — likely in `DTCM` (which is the default `RAM` region). 

On CH32H417, the USBHS DMA engine may not be able to access DTCM properly, or the buffers may land at addresses that violate alignment/caching requirements. This causes USB hardware to fail silently — the device never responds to host enumeration packets.

> [!CAUTION]
> This is the **most likely primary cause** of the USB failure. Betaflight works because its firmware image size and linker behavior happen to place these orphan sections at a SRAM-accessible address, but this is fragile. The rotorflight firmware layout may differ enough to break it.

### Issue 2 (HIGH): `systemInit()` Disables USBHS Clock Early

In [system_ch32h41x.c:200-204](file:///f:/BetaFlight/rotorflight/src/main/drivers/system_ch32h41x.c#L200-L204):
```c
RCC_HBPeriphClockCmd(
    RCC_HBPeriph_USBHS |
    RCC_HBPeriph_OTG_FS|
    0, DISABLE
);
```

Then `enableGPIOPowerUsageAndNoiseReductions()` re-enables it at [line 86](file:///f:/BetaFlight/rotorflight/src/main/drivers/system_ch32h41x.c#L86). This is fine — the disable/re-enable is a clean reset pattern. But there's a concern: if the OTG_FS peripheral gets re-enabled without proper configuration, it could interfere with USBHS mode. This is **identical to betaflight** though, so it's likely not the primary cause.

### Issue 3 (MEDIUM): Rotorflight's Lazy USB Init vs Betaflight's Early Init

**Betaflight** calls `usbVcpInit()` from [main.c:99](file:///f:/BetaFlight/betaflight/src/main/main.c#L99) right after `initPhase2()` — **before** `initPhase3()` which opens serial ports. This means USB hardware is initialized early.

**Rotorflight** does NOT call `usbVcpInit()` anywhere in its main init. Instead, `usbVcpInit()` is only called lazily from inside [usbVcpOpen()](file:///f:/BetaFlight/rotorflight/src/main/drivers/serial_usb_vcp_ch32h4.c#L460-L470), which is triggered when `mspSerialInit()` opens the VCP serial port at [init.c:919](file:///f:/BetaFlight/rotorflight/src/main/fc/init.c#L919).

While this should eventually work (mspSerialInit is called later in init), the lazy init means that if any earlier init code hangs or crashes (e.g., gyro detection on missing hardware), USB will **never** be initialized and the device will appear dead.

### Issue 4 (MEDIUM): String Descriptors Say "Betaflight" Instead of "Rotorflight"

In [cdc_vcp_ch32h41x.c:31-36](file:///f:/BetaFlight/rotorflight/lib/main/CH32H41x/middlewares/board/cdc_vcp_ch32h41x.c#L31-L36):
```c
static const char *string_descriptors[] = {
    (const char[]){0x09, 0x04}, /* Langid */
    "Betaflight",               /* Manufacturer */
    "Betaflight CH32H415",      /* Product */
    "2025123456",               /* Serial Number */
    ...
};
```

This won't prevent enumeration but means the Rotorflight configurator might not recognize the device (the configurator may filter by product string).

## Proposed Changes

### Linker Script — Add `.noncacheable` Section

#### [MODIFY] [ch32_flash_h41x_split.ld](file:///f:/BetaFlight/rotorflight/src/link/ch32_flash_h41x_split.ld)

Add a `.noncacheable` section placed in **SRAM** (not DTCM) so that USB DMA buffers are accessible by the USBHS peripheral:

```diff
+    /* USB DMA buffers — must be in SRAM (DMA-accessible), not DTCM */
+    .noncacheable (NOLOAD) :
+    {
+        . = ALIGN(16);
+        *(.noncacheable)
+        *(.noncacheable.*)
+        . = ALIGN(16);
+    } >SRAM
```

This must be placed before the `.data` section in the linker script. The SRAM region (`0x20126000`, 359K) is DMA-accessible and appropriate for USB buffers.

---

### Linker Script — Ensure SRAM Is Available for Noncacheable

#### [MODIFY] [ch32h41x_v5f.ld](file:///f:/BetaFlight/rotorflight/src/link/ch32h41x_v5f.ld)

No changes needed — SRAM region is already defined and has 359K available. The `.noncacheable` section (< 4KB of USB buffers) will consume a negligible portion.

---

### USB Descriptor Strings — Update for Rotorflight

#### [MODIFY] [cdc_vcp_ch32h41x.c](file:///f:/BetaFlight/rotorflight/lib/main/CH32H41x/middlewares/board/cdc_vcp_ch32h41x.c)

Update string descriptors from "Betaflight" to "Rotorflight" so the configurator can find the device:

```diff
 static const char *string_descriptors[] = {
     (const char[]){0x09, 0x04}, /* Langid */
-    "Betaflight",               /* Manufacturer */
-    "Betaflight CH32H415",      /* Product */
+    "Rotorflight",              /* Manufacturer */
+    "Rotorflight CH32H417",     /* Product */
     "2025123456",               /* Serial Number */
     "WCH HS Virtual COM Port",  /* interface */
 };
```

---

### (Optional but Recommended) Add Early USB Init

#### [MODIFY] [init.c](file:///f:/BetaFlight/rotorflight/src/main/fc/init.c)

Consider adding an explicit `usbVcpInit()` call early in the `init()` function (before gyro/sensor init), so USB is available even if later init stages hang. This matches betaflight's pattern:

```diff
+#ifdef USE_VCP
+    // Initialize USB early so VCP is available for debugging
+    // even if later init stages (gyro, etc.) hang
+    extern void usbVcpInit(void);
+    usbVcpInit();
+#endif
```

> [!IMPORTANT]
> If we add early USB init, we must also update `usbVcpOpen()` to avoid double-initialization. The existing `isUsbInitialized` guard in `usbVcpOpen()` handles this correctly.

But wait — `usbVcpOpen()` uses a static `isUsbInitialized` flag. If we call `usbVcpInit()` externally, the flag won't be set and `usbVcpOpen()` will call it again. We need to make the guard global or set it from the early call.

## Open Questions

> [!IMPORTANT]
> **Q1: Does betaflight actually work on this exact FC board?** You mentioned "the WCH engineer has already done the porting for CH32H417 in betaflight" — has the same physical FC board successfully run betaflight firmware with working USB? If betaflight also fails, the issue may be hardware/schematic related (e.g., missing USB pull-up resistor).

> [!IMPORTANT]
> **Q2: FC schematic USB circuit** — I couldn't view the PDF schematic directly. Could you confirm:
> - Are USB D+/D- connected to **PB8/PB9** (USBHS)?
> - Is there a 1.5K pull-up resistor on D+ (PB8)?
> - Is there a USB VBUS detect circuit? If so, what pin?
> - Are PB8/PB9 shared with any other function on this board (SWJ debug)?

> [!NOTE]
> **Q3: DFU version** — The DFU hex filename is `CH32H415_DFU_master-CH32H415REU6_V3F.hex`. This is for CH32H**415**, but your target is CH32H**417**. Are these the same die / compatible? The DFU flash descriptor says `0x08016000` which matches the rotorflight linker, so the flash layout is correct. But please confirm this DFU is intended for your exact chip package.

## Verification Plan

### Build Verification
1. Run `make TARGET=CH32H417` and verify it compiles successfully
2. Check the `.map` file to confirm `.noncacheable` section is placed in SRAM region (`0x2012xxxx`)
3. Verify USB buffers (`usb_drivers_buffer`, `read_buffer`, `write_buffer`, `usbd_core_priv`) appear in the SRAM address range

### Manual Verification
1. Flash the DFU bootloader
2. Build and flash the updated rotorflight firmware
3. Verify the FC enumerates as a USB CDC device on the laptop
4. Open Rotorflight Configurator and verify connection
5. Test MSP communication (CLI access, configuration read/write)

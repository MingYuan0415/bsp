**English** | [简体中文](README_ZH.md)

# MicroTech BSP

This repository is the ESP-IDF board support component for MicroTech and
currently supports only **Waveshare ESP32-S3-Touch-AMOLED-1.8**. It provides a
unified HAL for the 368 x 448 SH8601 QSPI AMOLED, FT5x06 touch, HOME/POWER
buttons, TCA9554 expander, PCF85063 RTC, AXP2101 PMU, QMI8658C IMU,
ES8311/NS4150B audio and removable SD storage.

## Directory Layout

- `include/bsp_hal.h`: public HAL defining the lifecycle, capability bits,
  display port and the screen/input/RTC/power/IMU/audio/SD operation tables.
- `include/bsp_audio.h`: board-level control API for the ES8311/NS4150B audio
  path.
- `src/`: board-agnostic transactional HAL registry.
- `waveshare/esp32-s3-touch-amoled-1.8/`: board pins, driver wrappers and
  resource init/rollback implementation.
- `tests/host/`: display, PMU, RTC, IMU, audio and SDSPI focused tests that
  need no hardware.
- `XPowersLib/src/`: pinned snapshot of the third-party PMU library.

## Integration and Configuration

The component requires ESP-IDF 5.0 or later. Add this directory to
`EXTRA_COMPONENT_DIRS` in the consuming project; this project uses:

```cmake
set(EXTRA_COMPONENT_DIRS layers/bsp)
```

`idf_component.yml` resolves `esp_io_expander`, `esp_lcd_sh8601`,
`esp_lcd_touch_ft5x06` and `esp_codec_dev`, and the consuming project must also
provide the local `mt_log` component. The selected board enables
`IO_EXPANDER_ENABLE_GPIO_API_WRAPPER` so SDSPI can map EXIO7 to a virtual CS
GPIO. Configure and build:

```sh
idf.py set-target esp32s3
idf.py menuconfig  # MicroTech Project Config -> Development Board
idf.py build
```

The only current board type is the default entry and is selectable only under
`IDF_TARGET_ESP32S3`.

Audio wiring and calibration are board-level facts: fixed I2S0,
GPIO16/9/45/8/10, PA GPIO46 and 36 dB microphone gain (a 6 dB step of the
ES8311). At 30 dB a voice memo peaked around -33 dBFS, well below the same
device's synthesized playback; 42 dB closed the gap but also lifted the analog
interference that scales with that gain and left no headroom for the start-up
transient, so the board uses 36 dB and the recorder supplies the remaining
level digitally. The PCM format, volume, mute and target PA state are passed
in by the upper-layer runtime product configuration. After
`board_audio_init()` completes the PA stays off; `bsp_audio_start()` returns
`ESP_ERR_INVALID_STATE` when `bsp_audio_configure()` has not been called first.

Display transport configuration is a board-level profile and is not overridden
through Kconfig. The current Waveshare backend is fixed at 368 x 448 RGB565,
40 MHz QSPI, a 10-line logical transfer, a 44-line physical DMA limit and queue
depth 2, with PSRAM Direct DMA and TE sync disabled. 40 MHz is an empirical
project value, not a rate guaranteed by SH8601A preliminary Table 14; the
existing 80 MHz comparison showed insufficient benefit. Under ESP-IDF 6.1,
enabling non-TE Direct DMA has been observed to cause a blue line at the top and
GUI freeze, so no production or experimental entry point is kept.

A new board type must provide its own private display profile and HAL backend,
and restrict the board option with the corresponding `IDF_TARGET`. Component
dependencies may only be selected per target or by manifest rule;
`REQUIRES`/`PRIV_REQUIRES` must not be changed through `CONFIG_BOARD_TYPE_*`. An
unregistered target must stop configuration and must not fall back to this
board's backend.

## Hardware Adaptation

| Function | Current wiring and behavior | HAL/API |
| --- | --- | --- |
| QMI8658C | I2C address `0x6b`; EXIO6 is INT1, polled in task context; requests 100 Hz, +/-4 g, +/-64 dps by default | `BSP_CAPABILITY_IMU`, `bsp_hal_get_imu()`; `configure`, `read`, `set_enabled`, `get_interrupt_level` |
| ES8311/NS4150B | ES8311 is configured over the shared I2C and the I2S link is full-duplex; NS4150B is powered from 3.3 V, the 150 kOhm input resistor corresponds to 6.02 dB gain, and GPIO46 controls the PA. The PA pin is owned only by the BSP: the ES8311 configuration uses `pa_pin` of -1, otherwise the codec would re-run `gpio_config()` on the same pin during `ES_PA_SETUP` | `BSP_CAPABILITY_AUDIO`, `bsp_hal_get_audio()` and `bsp_audio_*`; supports configuration, start/stop, read/write, volume, mute and PA |
| SD | SDSPI uses SPI3, MOSI/MISO/CLK are GPIO1/3/2, EXIO7 acts as the active-low CS through the GPIO wrapper, default 20 MHz | `BSP_CAPABILITY_SD`, `bsp_hal_get_sd()`; mount/unmount and mount-state query |
| AXP2101 | EXIO5 is the active-low IRQ; polling reads and clears the latched status | `BSP_CAPABILITY_POWER`, `bsp_hal_get_power()`; `get_info` and `poll_irq` |
| PCF85063 | EXIO3 is the active-low RTC_INT; supports repeating calendar alarms, pending query and clear | `BSP_CAPABILITY_RTC`, `bsp_hal_get_rtc()`; `alarm_configure/disable/get_status/clear/poll_interrupt` |
| AMOLED TE | The panel TE is wired to GPIO13, but no valid edges have been detected on hardware so far, so the board profile keeps it disabled. With TE off the BSP limits a single SPI DMA chunk to 10 lines (7,360 B) and the transfer queue to depth 2, which caps the internal DMA bounce buffer at 14,720 B; PSRAM direct DMA is not enabled without TE. Draw-buffer sizing and the render strip policy belong to the display adapter consumer | `bsp_display_port_t.transport` exports QSPI, 40 MHz, 4 data lines, 16 bpp, transfer and DMA line counts; `te` exports only the sync state, GPIO and edge |

The default AXP2101 profile matches the schematic: DCDC1/2/3/4 at
3.3/0.9/1.2/1.8 V with DCDC5 off; ALDO1/2/3/4 at 3.3/3.3/3.0/1.8 V; BLDO1/2 at
1.2/2.8 V; CPUSLDO at 1.2 V; DLDO1/2 off. Charge parameters are 50 mA
precharge, 200 mA constant current, 25 mA termination and a 4.1 V target, with
battery detection, Gauge, main-battery charging and battery/VBUS/power-key/
charge-status IRQs enabled.

SD `/sdcard`, at most 5 files and a 16 KiB allocation unit are passed in by the
root runtime product configuration. Normal init/start never requests a format;
only an explicit, generation-matched recovery initiated by the upper layer may
rebuild the partition and format after `FR_NO_FILESYSTEM` / `FR_INT_ERR`.
Recovery on a healthy mount does not erase files. The BSP owns the card, SDSPI
device, diskio, FatFs, VFS, SPI bus and virtual CS itself and does not call the
SDK's combined sdcard mount/unmount/format helpers. Active formatting uses
`f_mount(NULL)` → `f_mkfs()` → `f_mount()` → capacity re-check; a recovery
rebuild additionally calls `f_fdisk()` first. Formatting is reported successful
only when the remount succeeds and the capacity is readable and non-zero.
Failure clears mounted immediately; every failure stage retains the actual
resource markers, and `board_sdspi_unmount()` retries from the incomplete stage
without releasing completed stages twice. The raw VFS route is removed first,
and on failure the full FATFS context is retained; the SDK FATFS context is then
destroyed (its internal duplicate unregister returning INVALID_STATE means the
route is already removed), preventing the VFS callback from retaining a
dangling pointer or double-freeing.

## HAL Usage Constraints

Call `bsp_init()` first, then query capabilities through
`bsp_get_capabilities()`. Display, touch and input are required for successful
initialization; a failed RTC, PMU, IMU or audio initialization does not prevent
the BSP from becoming ready, and the SD capability means the wiring and
expander are available, so consumers must still handle a missing card or a
failed mount. Check the matching capability bit and `is_available()` before
using optional features, and call `bsp_deinit()` only after every consumer has
stopped.

`bsp_display_get_port()` and `bsp_hal_get_*()` return borrowed objects; the
display port is held exclusively by the BSP lifecycle owner (`app_runtime`)
through `bsp_display_acquire()` (only one holder at a time), and other
components consume the port under that lease and must not acquire it
themselves. On shutdown, use `bsp_display_release_and_deinit()` to release the
lease and shut down the BSP atomically, so no new acquisition can be inserted
between the release and `bsp_deinit()`; if any consumer still holds the lease,
`bsp_deinit()` returns `ESP_ERR_INVALID_STATE` and logs the holder name. The
other operation tables offer no lifecycle lease: they must not be retained or
used before initialization completes, after deinit begins, or after deinit
completes.

The screen operations implement only the board-level phase and do not
synchronize upper-layer consumers. Before calling `suspend()`, stop the
touch/display consumers, pause the render worker, and drain SPI/DMA through the
panel IO fence. On resume, call `resume_prepare()` first, then resume rendering,
force a full refresh and complete the fence, and finally call `resume_commit()`
to show the panel and restore touch. Input task sleep is paired as
`prepare_sleep -> complete_sleep`.

EXIO3/5/6 are not wired directly to ESP32 GPIOs and can only be polled through
the TCA9554 in task context; they cannot be registered as RTC GPIO wake
sources. Touch wake is not implemented and GPIO21 is not in the wake
descriptor; the only light-sleep wake source today is the HOME button on GPIO0,
active low.

## Host Tests

Run the BSP suite from the project root:

```sh
cmake -S layers/bsp/tests/host -B /tmp/bsp-host -G Ninja
cmake --build /tmp/bsp-host
ctest --test-dir /tmp/bsp-host --output-on-failure
```

The suite covers the fixed display transport profile, display state/sleep, the
AXP2101 callback and default profile, QMI8658C conversion and EXIO6 polling,
PCF85063 alarms and EXIO3, ES8311 format/lifecycle, and SDSPI virtual CS with
failure-cleanup retry. Its own tests do not exercise the display adapter of a
consuming component; that adapter's suite is responsible for the TE-off partial
path and the transport descriptor mapping it consumes.

Host tests do not replace hardware verification. Display requires checking the
GPIO13 TE pulse, full-screen/partial refresh and repeated screen-off/wake; IMU
requires checking rest values, axes, ODR and INT1; audio requires checking
record/playback, sample format, volume/mute and PA; SD requires checking card
insertion, read/write, unmount and failure retry; RTC/PMU/low power requires
checking alarm/IRQ clearing, battery/VBUS telemetry, charge parameters and
GPIO0 active-low wake.

## Third-Party Code

Do not modify `XPowersLib/src/`, ESP-IDF or managed components directly;
prefer upgrading the dependency and keep board-level corrections in this
repository's wrappers. The XPowersLib version, upstream commit and license
boundary are in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). This
repository is MIT licensed.

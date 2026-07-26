# MicroTech BSP

本仓库是 MicroTech 的 ESP-IDF 板级支持组件，目前仅支持 **Waveshare ESP32-S3-Touch-AMOLED-1.8**。它为 368 x 448 SH8601 QSPI AMOLED、FT5x06 触摸、HOME/POWER 按键、TCA9554 扩展器、PCF85063 RTC、AXP2101 PMU、QMI8658C IMU、ES8311/NS4150B 音频和可移除 SD 存储提供统一 HAL。

## 目录结构

- `include/bsp_hal.h`：公共 HAL，定义生命周期、能力位、显示端口及 screen/input/RTC/power/IMU/audio/SD 操作表。
- `include/bsp_audio.h`：ES8311/NS4150B 音频路径的板级控制 API。
- `src/`：与具体开发板解耦的事务式 HAL 注册表。
- `waveshare/esp32-s3-touch-amoled-1.8/`：板级引脚、驱动封装和资源初始化/回滚实现。
- `tests/host/`：不依赖真机的显示、PMU、RTC、IMU、音频和 SDSPI 聚焦测试。
- `XPowersLib/src/`：固定版本的第三方 PMU 库快照。

## 集成与配置

组件要求 ESP-IDF 5.0 及以上。在调用工程中将本目录加入 `EXTRA_COMPONENT_DIRS`；本项目使用：

```cmake
set(EXTRA_COMPONENT_DIRS layers/bsp)
```

`idf_component.yml` 会解析 `esp_io_expander`、`esp_lcd_sh8601`、`esp_lcd_touch_ft5x06` 和 `esp_codec_dev`，调用工程还须提供本地 `mt_log` 组件。所选板型会启用 `IO_EXPANDER_ENABLE_GPIO_API_WRAPPER`，供 SDSPI 将 EXIO7 映射为虚拟 CS GPIO。配置并构建：

```sh
idf.py set-target esp32s3
idf.py menuconfig  # MicroTech Project Config -> Development Board
idf.py build
```

当前唯一板型为默认项，且仅在 `IDF_TARGET_ESP32S3` 下可选。

`MicroTech Project Config -> Board audio` 可配置 I2S 端口、MCLK/BCLK/LRCK/DOUT/DIN、NS4150B PA GPIO、PCM 格式、MCLK 倍频、麦克风输入增益和默认音量。当前默认值为 I2S0，GPIO16/9/45/8/10，PA GPIO46，16 kHz、16-bit、双声道、384x MCLK、麦克风增益 30 dB、音量 60，开始流传输时默认开启 PA。

`MicroTech Project Config -> Board display` 将 LCD SPI 安全默认固定为 40 MHz；80 MHz 仅用于显式、受控的 A/B 测试，当前未经上板验证。ESP32-S3 的默认 80 MHz APB SPI 时钟不能精确分频出 60 MHz，因此不提供 60 MHz 档位。`BSP_DISPLAY_SPI_MAX_TRANSFER_LINES` 默认 10 行，`BSP_DISPLAY_NON_TE_PSRAM_DMA_DIRECT` 默认关闭，`BSP_DISPLAY_TE_SYNC` 也保持默认关闭。当前板卡在 ESP-IDF 6.0.2 下启用非 TE direct/10 后已观察到顶部蓝线与 GUI 冻结，Direct 开关只保留为问题复现入口，禁止生产启用。Host 中的 Direct 与 80 MHz 测试只验证配置透传，不代表硬件可用。

## 硬件适配

| 功能 | 当前接线与行为 | HAL/API |
| --- | --- | --- |
| QMI8658C | I2C 地址 `0x6b`；EXIO6 为 INT1，任务上下文轮询；默认请求 100 Hz、+/-4 g、+/-64 dps | `BSP_CAPABILITY_IMU`、`bsp_hal_get_imu()`；`configure`、`read`、`set_enabled`、`get_interrupt_level` |
| ES8311/NS4150B | ES8311 通过共享 I2C 配置，I2S 全双工传输；NS4150B 由 3.3 V 供电，150 kOhm 输入电阻对应 6.02 dB 增益，GPIO46 控制 PA | `BSP_CAPABILITY_AUDIO`、`bsp_hal_get_audio()` 和 `bsp_audio_*`；支持配置、start/stop、read/write、音量、静音和 PA |
| SD | SDSPI 使用 SPI3，MOSI/MISO/CLK 为 GPIO1/3/2，EXIO7 经 GPIO wrapper 作为低有效 CS，默认 20 MHz | `BSP_CAPABILITY_SD`、`bsp_hal_get_sd()`；mount/unmount 和挂载状态查询 |
| AXP2101 | EXIO5 为低有效 IRQ；轮询时读取并清除 latched status | `BSP_CAPABILITY_POWER`、`bsp_hal_get_power()`；`get_info` 和 `poll_irq` |
| PCF85063 | EXIO3 为低有效 RTC_INT；支持重复日历 alarm、pending 查询和清除 | `BSP_CAPABILITY_RTC`、`bsp_hal_get_rtc()`；`alarm_configure/disable/get_status/clear/poll_interrupt` |
| AMOLED TE | 面板 TE 连接 GPIO13，但当前真机未检测到有效边沿；`BSP_DISPLAY_TE_SYNC` 默认关闭。App Manager 使用两块 60 行 PSRAM 绘制条带（共 88,320 B），BSP 默认将单次 SPI DMA 分块限制为 10 行，并以深度 2 队列将内部 DMA bounce 峰值限制为 14,720 B；非 TE 默认不启用 PSRAM 直 DMA | 显式启用配置后，`bsp_display_port_t.te` 导出 GPIO 上升沿、所选 SPI 频率（默认 40 MHz）、4 data lines 和 16 bpp 参数，App Manager 使用 `ESP_LV_ADAPTER_TEAR_AVOID_MODE_TE_SYNC` |

AXP2101 默认 profile 与原理图一致：DCDC1/2/3/4 为 3.3/0.9/1.2/1.8 V，DCDC5 关闭；ALDO1/2/3/4 为 3.3/3.3/3.0/1.8 V；BLDO1/2 为 1.2/2.8 V；CPUSLDO 为 1.2 V；DLDO1/2 关闭。充电参数为预充 50 mA、恒流 200 mA、终止 25 mA、目标 4.1 V，并启用电池检测、Gauge、主电池充电以及电池/VBUS/电源键/充电状态 IRQ。

SD 默认挂载点是 `/sdcard`，最多打开 5 个文件，FAT allocation unit 为 16 KiB。`format_if_mount_failed` 默认是 `false`，挂载失败不会自动格式化；只有显式启用该配置才允许破坏性恢复。挂载或卸载的 SPI bus/GPIO wrapper 清理失败时保留 partial handle，后续 `board_sdspi_unmount()` 可重试回滚。

## HAL 使用约束

先调用 `bsp_init()`，再通过 `bsp_get_capabilities()` 判断能力。显示、触摸和输入是初始化成功的必需能力；RTC、PMU、IMU 和音频初始化失败时不会阻止 BSP 就绪，SD 能力表示接线和 expander 可用，消费者仍须处理无卡或挂载失败。使用可选功能前应检查对应能力位和 `is_available()`，完成所有消费者停机后再调用 `bsp_deinit()`。

`bsp_display_get_port()` 和 `bsp_hal_get_*()` 返回借用对象，不提供生命周期租约；初始化未完成、反初始化开始后或反初始化完成后不得保留或使用。

screen 操作只实现板级阶段，不负责同步上层消费者。调用 `suspend()` 前必须停止触摸/显示消费者、暂停渲染 worker，并通过 panel IO 栅栏排空 SPI/DMA。恢复时先调用 `resume_prepare()`，再恢复渲染、强制完整刷新并完成栅栏，最后调用 `resume_commit()` 显示面板和恢复触摸。输入任务休眠按 `prepare_sleep -> complete_sleep` 配对。

EXIO3/5/6 没有直接连接 ESP32 GPIO，只能通过 TCA9554 在任务上下文轮询，不能注册为 RTC GPIO 唤醒源。触摸唤醒尚未实现，GPIO21 不在 wake descriptor 中；当前轻睡眠唤醒源只有 HOME 键 GPIO0，低电平有效。

## 宿主测试

从工程根目录运行 BSP 套件：

```sh
cmake -S layers/bsp/tests/host -B /tmp/bsp-host -G Ninja
cmake --build /tmp/bsp-host
ctest --test-dir /tmp/bsp-host --output-on-failure
```

该套件覆盖显示状态/休眠、AXP2101 callback 与默认 profile、QMI8658C 转换和 EXIO6 轮询、PCF85063 alarm 与 EXIO3、ES8311 格式/生命周期，以及 SDSPI 虚拟 CS 和失败清理重试。App Manager adapter 测试还会校验 TE-off partial 路径和显式启用后的 TE sync 参数映射：

```sh
cmake -S layers/app_manager/app_core/tests/host -B /tmp/app-manager-host -G Ninja
cmake --build /tmp/app-manager-host
ctest --test-dir /tmp/app-manager-host --output-on-failure
```

宿主测试不替代硬件验证。显示需检查 GPIO13 TE 脉冲、全屏/局部刷新和重复熄屏/唤醒；IMU 需检查静止值、轴向、ODR 与 INT1；音频需检查录放、采样格式、音量/静音和 PA；SD 需检查插卡、读写、卸载和失败重试；RTC/PMU/低功耗需检查 alarm/IRQ 清除、电池/VBUS 遥测、充电参数及 GPIO0 低电平唤醒。

## 第三方代码

不要直接修改 `XPowersLib/src/`、ESP-IDF 或 managed components；优先升级依赖，板级修正放在本仓库 wrapper 中。XPowersLib 的版本、上游提交和许可边界见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。本仓库采用 MIT 许可证。

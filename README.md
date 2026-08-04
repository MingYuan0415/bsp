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

音频接线和校准属于板级事实：固定使用 I2S0、GPIO16/9/45/8/10、PA GPIO46 和
30 dB 麦克风增益。PCM 格式、音量、mute 和目标 PA 状态由上层运行时产品配置传入。
`board_audio_init()` 完成后 PA 保持关闭；未先调用 `bsp_audio_configure()` 时
`bsp_audio_start()` 返回 `ESP_ERR_INVALID_STATE`。

显示传输配置属于板级 profile，不通过 Kconfig 覆盖。当前 Waveshare 后端固定为
368 x 448 RGB565、40 MHz QSPI、10 行逻辑传输、44 行物理 DMA 上限、queue depth 2，
并关闭 PSRAM Direct DMA 和 TE 同步。40 MHz 是项目经验值，并非 SH8601A preliminary
Table 14 保证的频率；既有 80 MHz 对照收益不足。ESP-IDF 6.0.2 下启用非 TE Direct
DMA 已观察到顶部蓝线和 GUI 冻结，因此不再保留生产或实验配置入口。

新增板型必须提供独立的私有 display profile 和 HAL backend，并以对应 `IDF_TARGET`
限制板型选项。组件依赖只能按 target 或 manifest rule 选择，不能通过
`CONFIG_BOARD_TYPE_*` 改变 `REQUIRES`/`PRIV_REQUIRES`。未注册 target 必须停止配置，
不得回退到本板后端。

## 硬件适配

| 功能 | 当前接线与行为 | HAL/API |
| --- | --- | --- |
| QMI8658C | I2C 地址 `0x6b`；EXIO6 为 INT1，任务上下文轮询；默认请求 100 Hz、+/-4 g、+/-64 dps | `BSP_CAPABILITY_IMU`、`bsp_hal_get_imu()`；`configure`、`read`、`set_enabled`、`get_interrupt_level` |
| ES8311/NS4150B | ES8311 通过共享 I2C 配置，I2S 全双工传输；NS4150B 由 3.3 V 供电，150 kOhm 输入电阻对应 6.02 dB 增益，GPIO46 控制 PA | `BSP_CAPABILITY_AUDIO`、`bsp_hal_get_audio()` 和 `bsp_audio_*`；支持配置、start/stop、read/write、音量、静音和 PA |
| SD | SDSPI 使用 SPI3，MOSI/MISO/CLK 为 GPIO1/3/2，EXIO7 经 GPIO wrapper 作为低有效 CS，默认 20 MHz | `BSP_CAPABILITY_SD`、`bsp_hal_get_sd()`；mount/unmount 和挂载状态查询 |
| AXP2101 | EXIO5 为低有效 IRQ；轮询时读取并清除 latched status | `BSP_CAPABILITY_POWER`、`bsp_hal_get_power()`；`get_info` 和 `poll_irq` |
| PCF85063 | EXIO3 为低有效 RTC_INT；支持重复日历 alarm、pending 查询和清除 | `BSP_CAPABILITY_RTC`、`bsp_hal_get_rtc()`；`alarm_configure/disable/get_status/clear/poll_interrupt` |
| AMOLED TE | 面板 TE 连接 GPIO13，但当前真机未检测到有效边沿，板级 profile 固定关闭。App Manager 使用两块 60 行 PSRAM 绘制条带（共 88,320 B），BSP 将单次 SPI DMA 分块限制为 10 行，并以深度 2 队列将内部 DMA bounce 峰值限制为 14,720 B；非 TE 不启用 PSRAM 直 DMA | `bsp_display_port_t.transport` 导出 QSPI、40 MHz、4 data lines、16 bpp、传输和 DMA 行数；`te` 仅导出同步状态、GPIO 与边沿 |

AXP2101 默认 profile 与原理图一致：DCDC1/2/3/4 为 3.3/0.9/1.2/1.8 V，DCDC5 关闭；ALDO1/2/3/4 为 3.3/3.3/3.0/1.8 V；BLDO1/2 为 1.2/2.8 V；CPUSLDO 为 1.2 V；DLDO1/2 关闭。充电参数为预充 50 mA、恒流 200 mA、终止 25 mA、目标 4.1 V，并启用电池检测、Gauge、主电池充电以及电池/VBUS/电源键/充电状态 IRQ。

SD 的 `/sdcard`、最多 5 个文件和 16 KiB allocation unit 由根运行时产品配置传入。
普通 init/start 永不请求格式化；只有上层显式调用
`sd_storage_service_recover_and_mount()` 时 BSP mount adapter 才收到格式化恢复模式。
挂载或卸载的 SPI bus/GPIO wrapper 清理失败时保留 partial handle，后续
`board_sdspi_unmount()` 可重试回滚。

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

该套件覆盖固定显示 transport profile、显示状态/休眠、AXP2101 callback 与默认
profile、QMI8658C 转换和 EXIO6 轮询、PCF85063 alarm 与 EXIO3、ES8311
格式/生命周期，以及 SDSPI 虚拟 CS 和失败清理重试。App Manager adapter 测试还会
校验 TE-off partial 路径和 transport descriptor 映射：

```sh
cmake -S layers/app_manager/app_core/tests/host -B /tmp/app-manager-host -G Ninja
cmake --build /tmp/app-manager-host
ctest --test-dir /tmp/app-manager-host --output-on-failure
```

宿主测试不替代硬件验证。显示需检查 GPIO13 TE 脉冲、全屏/局部刷新和重复熄屏/唤醒；IMU 需检查静止值、轴向、ODR 与 INT1；音频需检查录放、采样格式、音量/静音和 PA；SD 需检查插卡、读写、卸载和失败重试；RTC/PMU/低功耗需检查 alarm/IRQ 清除、电池/VBUS 遥测、充电参数及 GPIO0 低电平唤醒。

## 第三方代码

不要直接修改 `XPowersLib/src/`、ESP-IDF 或 managed components；优先升级依赖，板级修正放在本仓库 wrapper 中。XPowersLib 的版本、上游提交和许可边界见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。本仓库采用 MIT 许可证。

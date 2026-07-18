# MicroTech BSP

本仓库是 MicroTech 的 ESP-IDF 板级支持组件，目前仅支持 **Waveshare ESP32-S3-Touch-AMOLED-1.8**。它为 368 x 448 SH8601 QSPI AMOLED、FT5x06 触摸、HOME/POWER 按键、TCA9554 扩展器、PCF85063 RTC 和 AXP2101 PMU 提供统一 HAL。

## 目录结构

- `include/bsp_hal.h`：唯一公开接口，定义生命周期、能力位、显示端口及 screen/input/RTC/power 操作表。
- `src/`：与具体开发板解耦的事务式 HAL 注册表。
- `waveshare/esp32-s3-touch-amoled-1.8/`：板级引脚、驱动封装和资源初始化/回滚实现。
- `tests/host/`：不依赖真机的显示休眠状态与 XPowersLib 回调契约测试。
- `XPowersLib/src/`：固定版本的第三方 PMU 库快照。

## 集成与配置

组件要求 ESP-IDF 5.0 及以上。在调用工程中将本目录加入 `EXTRA_COMPONENT_DIRS`；本项目使用：

```cmake
set(EXTRA_COMPONENT_DIRS layers/bsp)
```

`idf_component.yml` 会解析 `esp_io_expander`、`esp_lcd_sh8601` 和 `esp_lcd_touch_ft5x06`，调用工程还须提供本地 `mt_log` 组件。配置并构建：

```sh
idf.py set-target esp32s3
idf.py menuconfig  # MicroTech Project Config -> Development Board
idf.py build
```

当前唯一板型为默认项，且仅在 `IDF_TARGET_ESP32S3` 下可选。

## HAL 使用约束

先调用 `bsp_init()`，再通过 `bsp_get_capabilities()` 判断能力。显示、触摸和输入是初始化成功的必需能力；RTC 与 PMU 初始化失败时不会阻止 BSP 就绪，应检查对应能力位和 `is_available()`。完成所有消费者停机后再调用 `bsp_deinit()`。

`bsp_display_get_port()` 和 `bsp_hal_get_*()` 返回借用对象，不提供生命周期租约；初始化未完成、反初始化开始后或反初始化完成后不得保留或使用。

screen 操作只实现板级阶段，不负责同步上层消费者。调用 `suspend()` 前必须停止触摸/显示消费者、暂停渲染 worker，并通过 panel IO 栅栏排空 SPI/DMA。恢复时先调用 `resume_prepare()`，再恢复渲染、强制完整刷新并完成栅栏，最后调用 `resume_commit()` 显示面板和恢复触摸。输入任务休眠按 `prepare_sleep -> complete_sleep` 配对。

## 宿主测试

```sh
cmake -S tests/host -B /tmp/bsp-host -G Ninja
cmake --build /tmp/bsp-host
ctest --test-dir /tmp/bsp-host --output-on-failure
```

宿主测试仅覆盖休眠完成状态判定和 AXP2101 回调契约，不替代硬件验证。显示或电源改动需在真机检查初始化/反初始化、首帧显示、亮度、触摸与按键、重复熄屏/唤醒；RTC、PMU 或低功耗改动还需检查时间读写、电池/VBUS 遥测及 GPIO0 低电平唤醒。

## 第三方代码

不要直接修改 `XPowersLib/src/`、ESP-IDF 或 managed components；优先升级依赖，板级修正放在本仓库 wrapper 中。XPowersLib 的版本、上游提交和许可边界见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。本仓库采用 MIT 许可证。

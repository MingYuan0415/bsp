# 第三方组件说明

`XPowersLib/` 是从 [Lewis He 的 XPowersLib](https://github.com/lewisxhe/XPowersLib) vendored 的源码快照，采用 MIT 许可证。上游版本声明为 0.3.4，固定于提交 `d6997586e68f65afd51baa775903df930db39821`（2026-07-01）。本目录保留该提交的 `src/` 和 `LICENSE`，未包含示例、数据手册和 CI 文件，也未对上游源码施加语义补丁；提交时行尾按本仓库规则归一化为 LF。

除版本升级或明确记录的上游补丁外，不在该目录修改代码。板级适配、I2C 回调、错误映射和资源生命周期均位于 `waveshare/esp32-s3-touch-amoled-1.8/mt_axp2101.cpp`。

MIT 许可证全文见 `XPowersLib/LICENSE`；分发此快照时应同时保留其源文件中的版权与许可声明。

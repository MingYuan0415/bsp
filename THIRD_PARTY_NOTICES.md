# 第三方组件说明

`XPowersLib/src/` 是从 [Lewis He 的 XPowersLib](https://github.com/lewisxhe/XPowersLib) vendored 的 0.3.4 接口快照，采用 MIT 许可证。该快照保留仓库固定的旧 `.tpp` 布局，以匹配当前 ESP-IDF wrapper；上游提交 `d6997586e68f65afd51baa775903df930db39821` 的新 `.hpp` 布局尚未直接替换，因为它要求额外的 ESP-IDF 版本配置并改变了集成接口。

除版本升级或明确记录的上游补丁外，不在该目录修改代码。板级适配、I2C 回调、错误映射和资源生命周期均位于 `waveshare/esp32-s3-touch-amoled-1.8/mt_axp2101.cpp`。

MIT 许可证全文见上游项目；分发此快照时应同时保留其源文件中的版权与许可声明。

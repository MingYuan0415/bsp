set(MT_BOARD_ID "waveshare_esp32_s3_touch_amoled_1_8")
set(MT_BOARD_TARGET "esp32s3")
set(MT_BOARD_KCONFIG_SYMBOL
    "CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_8")
set(MT_BOARD_VENDOR "waveshare")
set(MT_BOARD_NAME "esp32-s3-touch-amoled-1.8")
set(MT_BOARD_DISPLAY_PROFILE "board_display_profile.h")
set(MT_BOARD_HAL_CAPABILITIES
    display touch input rtc power imu audio sd
)
set(MT_BOARD_SOURCES
    board_init.c
    board_display.c
    board_input.c
    board_audio.c
    board_audio_format.c
    board_sdspi.c
    board_i2c_panel_io.c
    board_tca9554.c
    board_imu.c
    board_imu_bridge.c
    board_power.c
    board_rtc.c
    mt_axp2101.cpp
    mt_pcf85063.c
)
set(MT_BOARD_PUBLIC_REQUIRES
    esp_lcd
    esp_lcd_touch
)
set(MT_BOARD_PRIVATE_REQUIRES
    mt_log
    espressif__esp_io_expander
    espressif__esp_lcd_sh8601
    esp_lcd_touch_ft5x06
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_i2s
    esp_driver_spi
    esp_driver_sdspi
    freertos
    esp_timer
    fatfs
    sdmmc
    esp_codec_dev
)

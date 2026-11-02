set(MT_BOARD_ID "waveshare_esp32_p4_wifi6_touch_lcd_3_5")
set(MT_BOARD_TARGET "esp32p4")
set(MT_BOARD_KCONFIG_SYMBOL
    "CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_3_5")
set(MT_BOARD_VENDOR "waveshare")
set(MT_BOARD_NAME "esp32-p4-wifi6-touch-lcd-3.5")
set(MT_BOARD_DISPLAY_PROFILE "board_display_profile.h")
set(MT_BOARD_HAL_CAPABILITIES
    display touch input power audio sd
)
set(MT_BOARD_SOURCES
    board_init.c
    board_display.c
    board_input.c
    board_power.c
    board_audio.c
    board_sdmmc.c
)
set(MT_BOARD_PUBLIC_REQUIRES
    esp_lcd
    esp_lcd_touch
)
set(MT_BOARD_PRIVATE_REQUIRES
    mt_log
    espressif__esp_lcd_st7796
    esp_lcd_touch_ft5x06
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_i2s
    esp_driver_spi
    esp_driver_ledc
    esp_codec_dev
    fatfs
    vfs
    sdmmc
    esp_driver_sdmmc
    freertos
    esp_timer
)

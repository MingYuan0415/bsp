set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(XPOWERSLIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/XPowersLib)

function(mt_select_board_from_sdkconfig out_vendor out_name)
    if(NOT EXISTS "${SDKCONFIG}")
        return()
    endif()

    file(STRINGS "${SDKCONFIG}" selected_board_configs REGEX "^CONFIG_BOARD_TYPE_.*=y$")

    foreach(selected_board IN LISTS selected_board_configs)
        if(selected_board STREQUAL "CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_8=y")
            set(${out_vendor} "waveshare" PARENT_SCOPE)
            set(${out_name} "esp32-s3-touch-amoled-1.8" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    if(selected_board_configs)
        message(FATAL_ERROR "Unsupported MicroTech board selection: ${selected_board_configs}")
    endif()
endfunction()

if(CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_8)
    set(BOARD_VENDOR "waveshare")
    set(BOARD_NAME "esp32-s3-touch-amoled-1.8")
endif()

if(NOT BOARD_VENDOR OR NOT BOARD_NAME)
    mt_select_board_from_sdkconfig(BOARD_VENDOR BOARD_NAME)
endif()

if(NOT BOARD_VENDOR OR NOT BOARD_NAME)
    set(BOARD_VENDOR "waveshare")
    set(BOARD_NAME "esp32-s3-touch-amoled-1.8")
endif()

set(BOARD_PATH ${BOARD_ROOT}/${BOARD_VENDOR}/${BOARD_NAME})
set(BOARD_SOURCES
    "src/bsp_hal.c"
    "${BOARD_PATH}/board_init.c"
    "${BOARD_PATH}/board_display.c"
    "${BOARD_PATH}/board_input.c"
    "${BOARD_PATH}/board_audio.c"
    "${BOARD_PATH}/board_audio_format.c"
    "${BOARD_PATH}/board_sdspi.c"
    "${BOARD_PATH}/board_i2c_panel_io.c"
    "${BOARD_PATH}/board_tca9554.c"
    "${BOARD_PATH}/board_imu.c"
    "${BOARD_PATH}/board_imu_bridge.c"
    "${BOARD_PATH}/board_power.c"
    "${BOARD_PATH}/board_rtc.c"
    "${BOARD_PATH}/mt_axp2101.cpp"
    "${BOARD_PATH}/mt_pcf85063.c"
    "${XPOWERSLIB_ROOT}/src/XPowersLibInterface.cpp"
)

idf_component_register(
    SRCS ${BOARD_SOURCES}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        ${BOARD_PATH}
    REQUIRES
        esp_lcd
        esp_lcd_touch
    PRIV_REQUIRES
        mt_log
        espressif__esp_io_expander
        espressif__esp_lcd_sh8601
        esp_lcd_touch_ft5x06
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_spi
        esp_driver_sdspi
        esp_timer
        fatfs
        sdmmc
        esp_codec_dev
)

target_include_directories(${COMPONENT_LIB} SYSTEM PRIVATE
    "${XPOWERSLIB_ROOT}/src"
    "${XPOWERSLIB_ROOT}/src/REG"
)

cmake_minimum_required(VERSION 3.16)
project(bsp_host_tests LANGUAGES C CXX)

enable_testing()

add_executable(xpowers_behavior_test
    xpowers_behavior_test.cpp
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_power.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/mt_axp2101.cpp
    ../../XPowersLib/src/XPowersLibInterface.cpp
)

target_include_directories(xpowers_behavior_test PRIVATE
    imu_fakes
    fakes
    ../../include
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_include_directories(xpowers_behavior_test SYSTEM PRIVATE
    ../../XPowersLib/src
)

target_compile_features(xpowers_behavior_test PRIVATE cxx_std_17)
target_compile_options(xpowers_behavior_test PRIVATE
    -Wall
    -Wextra
)

add_test(NAME xpowers_behavior_test COMMAND xpowers_behavior_test)

add_executable(board_display_state_test
    board_display_state_test.c
)

target_include_directories(board_display_state_test PRIVATE
    fakes
    ../../include
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(board_display_state_test PRIVATE c_std_11)
target_compile_options(board_display_state_test PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME board_display_state_test COMMAND board_display_state_test)

add_executable(board_display_suspend_test
    board_display_suspend_test.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_display.c
)

target_include_directories(board_display_suspend_test PRIVATE
    fakes
    ../../include
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(board_display_suspend_test PRIVATE c_std_11)
target_compile_options(board_display_suspend_test PRIVATE
    -Wall
    -Wextra
    -Werror
    -ffunction-sections
    -fdata-sections
)
target_link_options(board_display_suspend_test PRIVATE
    -Wl,--gc-sections
)

add_test(NAME board_display_suspend_test COMMAND board_display_suspend_test)

add_executable(board_display_te_off_test
    board_display_suspend_test.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_display.c
)

target_include_directories(board_display_te_off_test PRIVATE
    fakes
    ../../include
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(board_display_te_off_test PRIVATE c_std_11)
target_compile_definitions(board_display_te_off_test PRIVATE
    BSP_TEST_TE_SYNC_UNDEFINED=1
)
target_compile_options(board_display_te_off_test PRIVATE
    -Wall
    -Wextra
    -Werror
    -ffunction-sections
    -fdata-sections
)
target_link_options(board_display_te_off_test PRIVATE
    -Wl,--gc-sections
)

add_test(NAME board_display_te_off_test COMMAND board_display_te_off_test)

add_executable(board_imu_test
    board_imu_test.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_imu.c
)

target_include_directories(board_imu_test PRIVATE
    imu_fakes
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(board_imu_test PRIVATE c_std_11)
target_compile_options(board_imu_test PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_link_libraries(board_imu_test PRIVATE m)

add_test(NAME board_imu_test COMMAND board_imu_test)

add_executable(board_imu_bridge_test
    board_imu_bridge_test.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_imu_bridge.c
)

target_include_directories(board_imu_bridge_test PRIVATE
    imu_fakes
    fakes
    ../../include
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(board_imu_bridge_test PRIVATE c_std_11)
target_compile_options(board_imu_bridge_test PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_link_libraries(board_imu_bridge_test PRIVATE m)

add_test(NAME board_imu_bridge_test COMMAND board_imu_bridge_test)

add_executable(mt_pcf85063_alarm_test
    mt_pcf85063_alarm_test.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/mt_pcf85063.c
)

target_include_directories(mt_pcf85063_alarm_test PRIVATE
    rtc_fakes
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(mt_pcf85063_alarm_test PRIVATE c_std_11)
target_compile_options(mt_pcf85063_alarm_test PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME mt_pcf85063_alarm_test COMMAND mt_pcf85063_alarm_test)

add_executable(board_rtc_alarm_test
    board_rtc_alarm_test.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_rtc.c
)

target_include_directories(board_rtc_alarm_test PRIVATE
    fakes
    ../../include
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(board_rtc_alarm_test PRIVATE c_std_11)
target_compile_options(board_rtc_alarm_test PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME board_rtc_alarm_test COMMAND board_rtc_alarm_test)

add_executable(board_audio_format_test
    board_audio_format_test.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_audio_format.c
)

target_include_directories(board_audio_format_test PRIVATE
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(board_audio_format_test PRIVATE c_std_11)
target_compile_options(board_audio_format_test PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME board_audio_format_test COMMAND board_audio_format_test)

add_executable(board_sdspi_test
    board_sdspi_test.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_sdspi.c
)

target_include_directories(board_sdspi_test PRIVATE
    sdspi_fakes
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(board_sdspi_test PRIVATE c_std_11)
target_compile_definitions(board_sdspi_test PRIVATE
    CONFIG_IO_EXPANDER_ENABLE_GPIO_API_WRAPPER=1
)
target_compile_options(board_sdspi_test PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME board_sdspi_test COMMAND board_sdspi_test)

add_executable(board_audio_lifecycle_test
    board_audio_lifecycle_test.c
    audio_fakes/board_audio_fakes.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_audio.c
    ../../waveshare/esp32-s3-touch-amoled-1.8/board_audio_format.c
)

target_include_directories(board_audio_lifecycle_test PRIVATE
    audio_fakes/include
    ../../waveshare/esp32-s3-touch-amoled-1.8
)

target_compile_features(board_audio_lifecycle_test PRIVATE c_std_11)
target_compile_options(board_audio_lifecycle_test PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME board_audio_lifecycle_test COMMAND board_audio_lifecycle_test)

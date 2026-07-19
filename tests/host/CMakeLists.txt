cmake_minimum_required(VERSION 3.16)
project(bsp_host_tests LANGUAGES C CXX)

enable_testing()

add_executable(xpowers_behavior_test
    xpowers_behavior_test.cpp
    ../../XPowersLib/src/XPowersLibInterface.cpp
)

target_include_directories(xpowers_behavior_test PRIVATE
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

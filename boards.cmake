include_guard(GLOBAL)

function(_mt_board_manifest target requested_board out_var out_target_var out_id_var)
    set(board_id "${requested_board}")
    if(board_id STREQUAL "")
        if(target STREQUAL "esp32s3")
            set(board_id "waveshare_esp32_s3_touch_amoled_1_8")
        elseif(target STREQUAL "esp32p4")
            set(board_id "waveshare_esp32_p4_wifi6_touch_lcd_3_5")
        else()
            message(FATAL_ERROR
                "No MicroTech board backend is registered for IDF_TARGET=${target}")
        endif()
    endif()

    if(board_id STREQUAL "waveshare_esp32_s3_touch_amoled_1_8")
        set(manifest
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/waveshare/esp32-s3-touch-amoled-1.8/board.cmake")
        set(board_target "esp32s3")
    elseif(board_id STREQUAL "waveshare_esp32_p4_wifi6_touch_lcd_3_5")
        set(manifest
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/waveshare/esp32-p4-wifi6-touch-lcd-3.5/board.cmake")
        set(board_target "esp32p4")
    else()
        message(FATAL_ERROR "Unknown MicroTech board '${board_id}'")
    endif()
    if(NOT target STREQUAL "" AND NOT target STREQUAL board_target)
        message(FATAL_ERROR
            "Board ${board_id} requires IDF_TARGET=${board_target}, got ${target}")
    endif()
    set(${out_var} "${manifest}" PARENT_SCOPE)
    set(${out_target_var} "${board_target}" PARENT_SCOPE)
    set(${out_id_var} "${board_id}" PARENT_SCOPE)
endfunction()

function(mt_resolve_board target)
    set(requested_board "")
    if(ARGC GREATER 1)
        set(requested_board "${ARGV1}")
    endif()
    _mt_board_manifest("${target}" "${requested_board}" board_manifest
        board_target board_id)

    unset(MT_BOARD_ID)
    unset(MT_BOARD_TARGET)
    unset(MT_BOARD_KCONFIG_SYMBOL)
    unset(MT_BOARD_VENDOR)
    unset(MT_BOARD_NAME)
    unset(MT_BOARD_DEVICE_PANEL)
    unset(MT_BOARD_DEVICE_PLATFORM)
    unset(MT_BOARD_SYSTEM_PM_SUPPORTED)
    unset(MT_BOARD_HOSTED_RADIO)
    unset(MT_BOARD_RADIO_SUPPORTED)
    unset(MT_BOARD_DISPLAY_PROFILE)
    unset(MT_BOARD_HAL_CAPABILITIES)
    unset(MT_BOARD_SOURCES)
    unset(MT_BOARD_PUBLIC_REQUIRES)
    unset(MT_BOARD_PRIVATE_REQUIRES)
    unset(MT_BOARD_DEFAULT_PRODUCT)
    unset(MT_BOARD_DOCUMENTS)
    include("${board_manifest}")
    set(required_fields
        MT_BOARD_ID
        MT_BOARD_TARGET
        MT_BOARD_KCONFIG_SYMBOL
        MT_BOARD_VENDOR
        MT_BOARD_NAME
        MT_BOARD_DEVICE_PANEL
        MT_BOARD_DEVICE_PLATFORM
        MT_BOARD_SYSTEM_PM_SUPPORTED
        MT_BOARD_HOSTED_RADIO
        MT_BOARD_RADIO_SUPPORTED
        MT_BOARD_DISPLAY_PROFILE
        MT_BOARD_HAL_CAPABILITIES
        MT_BOARD_SOURCES
        MT_BOARD_PUBLIC_REQUIRES
        MT_BOARD_PRIVATE_REQUIRES
        MT_BOARD_DEFAULT_PRODUCT
        MT_BOARD_DOCUMENTS
    )
    foreach(field IN LISTS required_fields)
        if(NOT DEFINED ${field} OR "${${field}}" STREQUAL "")
            message(FATAL_ERROR
                "MicroTech board manifest ${board_manifest} lacks ${field}")
        endif()
        set(${field} "${${field}}" PARENT_SCOPE)
    endforeach()
    if(NOT MT_BOARD_ID STREQUAL board_id)
        message(FATAL_ERROR
            "Board manifest ${board_manifest} declares MT_BOARD_ID=${MT_BOARD_ID}, expected ${board_id}")
    endif()
    if(NOT MT_BOARD_TARGET STREQUAL board_target)
        message(FATAL_ERROR
            "Board manifest ${board_manifest} declares MT_BOARD_TARGET=${MT_BOARD_TARGET}, expected ${board_target}")
    endif()

    set(capability_defines "")
    foreach(capability IN LISTS MT_BOARD_HAL_CAPABILITIES)
        string(TOUPPER "${capability}" capability_upper)
        list(APPEND capability_defines "MT_BOARD_HAS_${capability_upper}=1")
    endforeach()
    set(MT_BOARD_CAPABILITY_DEFINES "${capability_defines}" PARENT_SCOPE)
    set(MT_BOARD_MANIFEST "${board_manifest}" PARENT_SCOPE)
endfunction()

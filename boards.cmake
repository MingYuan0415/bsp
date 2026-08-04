include_guard(GLOBAL)

function(mt_resolve_board target)
    if(target STREQUAL "esp32s3")
        set(board_manifest
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/waveshare/esp32-s3-touch-amoled-1.8/board.cmake")
    else()
        message(FATAL_ERROR
            "No MicroTech board backend is registered for IDF_TARGET=${target}")
    endif()

    include("${board_manifest}")
    set(required_fields
        MT_BOARD_ID
        MT_BOARD_TARGET
        MT_BOARD_KCONFIG_SYMBOL
        MT_BOARD_VENDOR
        MT_BOARD_NAME
        MT_BOARD_DISPLAY_PROFILE
        MT_BOARD_HAL_CAPABILITIES
        MT_BOARD_SOURCES
        MT_BOARD_PUBLIC_REQUIRES
        MT_BOARD_PRIVATE_REQUIRES
    )
    foreach(field IN LISTS required_fields)
        if(NOT DEFINED ${field} OR "${${field}}" STREQUAL "")
            message(FATAL_ERROR
                "MicroTech board manifest ${board_manifest} lacks ${field}")
        endif()
        set(${field} "${${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

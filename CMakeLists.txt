set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(XPOWERSLIB_ROOT ${CMAKE_CURRENT_LIST_DIR}/XPowersLib)

include(${BOARD_ROOT}/boards.cmake)
mt_resolve_board("${IDF_TARGET}")

set(BOARD_PATH ${BOARD_ROOT}/${MT_BOARD_VENDOR}/${MT_BOARD_NAME})
if(NOT EXISTS "${BOARD_PATH}/${MT_BOARD_DISPLAY_PROFILE}")
    message(FATAL_ERROR
        "Board ${MT_BOARD_ID} lacks display profile ${MT_BOARD_DISPLAY_PROFILE}")
endif()
set(BOARD_SOURCES
    "src/bsp_hal.c"
    "${XPOWERSLIB_ROOT}/src/XPowersLibInterface.cpp"
)
foreach(board_source IN LISTS MT_BOARD_SOURCES)
    list(APPEND BOARD_SOURCES "${BOARD_PATH}/${board_source}")
endforeach()

idf_component_register(
    SRCS ${BOARD_SOURCES}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        ${BOARD_PATH}
    REQUIRES ${MT_BOARD_PUBLIC_REQUIRES}
    PRIV_REQUIRES ${MT_BOARD_PRIVATE_REQUIRES}
)

target_include_directories(${COMPONENT_LIB} SYSTEM PRIVATE
    "${XPOWERSLIB_ROOT}/src"
    "${XPOWERSLIB_ROOT}/src/REG"
)

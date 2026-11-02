#ifndef __BOARD_DISPLAY_PROFILE_H__
#define __BOARD_DISPLAY_PROFILE_H__

/*
 * Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5 panel transport profile.
 *
 * The 3.5-inch IPS panel is driven by an ST7796 over 4-wire SPI. 80 MHz is the
 * vendor reference clock; the SPI panel IO splits each LVGL strip into
 * BOARD_LCD_SPI_MAX_TRANSFER_LINES rows so a bounce buffer stays small. TE is
 * not wired and PSRAM Direct DMA is enabled only after hardware validation.
 */
#define BOARD_LCD_HOR_RES                   (320U)
#define BOARD_LCD_VER_RES                   (480U)
#define BOARD_LCD_SPI_CLOCK_HZ              (80000000U)
#define BOARD_LCD_SPI_DATA_LINES            (1U)
#define BOARD_LCD_BITS_PER_PIXEL            (16U)
#define BOARD_LCD_SPI_MAX_TRANSFER_LINES    (20U)
#define BOARD_LCD_DMA_MAX_FULL_LINES        (20U)
#define BOARD_LCD_SPI_TRANS_QUEUE_DEPTH     (2U)
#define BOARD_LCD_PSRAM_DMA_DIRECT          (0)
#define BOARD_LCD_TE_SYNC                   (0)

#if BOARD_LCD_HOR_RES == 0U || BOARD_LCD_VER_RES == 0U
    #error "Display resolution must be nonzero"
#endif
#if BOARD_LCD_SPI_CLOCK_HZ == 0U || BOARD_LCD_SPI_DATA_LINES != 1U
    #error "The ESP32-P4 panel requires a nonzero SPI clock and one data line"
#endif
#if BOARD_LCD_BITS_PER_PIXEL != 16U
    #error "The ESP32-P4 panel requires RGB565"
#endif
#if BOARD_LCD_SPI_MAX_TRANSFER_LINES == 0U || \
    BOARD_LCD_SPI_MAX_TRANSFER_LINES > BOARD_LCD_VER_RES
    #error "Display transfer rows must fit the panel"
#endif
#if BOARD_LCD_DMA_MAX_FULL_LINES == 0U || \
    BOARD_LCD_DMA_MAX_FULL_LINES > BOARD_LCD_VER_RES || \
    BOARD_LCD_SPI_MAX_TRANSFER_LINES > BOARD_LCD_DMA_MAX_FULL_LINES
    #error "Display DMA row bounds are invalid"
#endif
#if BOARD_LCD_SPI_TRANS_QUEUE_DEPTH == 0U || \
    BOARD_LCD_SPI_TRANS_QUEUE_DEPTH > 10U
    #error "Display transaction queue depth is invalid"
#endif
#if BOARD_LCD_PSRAM_DMA_DIRECT != 0 && BOARD_LCD_PSRAM_DMA_DIRECT != 1
    #error "Direct DMA policy must be boolean"
#endif
#if BOARD_LCD_TE_SYNC != 0 && BOARD_LCD_TE_SYNC != 1
    #error "TE policy must be boolean"
#endif

#endif /* __BOARD_DISPLAY_PROFILE_H__ */

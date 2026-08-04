#ifndef __BOARD_DISPLAY_PROFILE_H__
#define __BOARD_DISPLAY_PROFILE_H__

#define BOARD_LCD_HOR_RES                   (368U)
#define BOARD_LCD_VER_RES                   (448U)
#define BOARD_LCD_SPI_CLOCK_HZ              (40000000U)
#define BOARD_LCD_SPI_DATA_LINES            (4U)
#define BOARD_LCD_BITS_PER_PIXEL            (16U)
#define BOARD_LCD_SPI_MAX_TRANSFER_LINES    (10U)
#define BOARD_LCD_DMA_MAX_FULL_LINES        (44U)
#define BOARD_LCD_SPI_TRANS_QUEUE_DEPTH     (2U)
#define BOARD_LCD_PSRAM_DMA_DIRECT          (0)
#define BOARD_LCD_TE_SYNC                   (0)

#if BOARD_LCD_HOR_RES == 0U || BOARD_LCD_VER_RES == 0U
    #error "Display resolution must be nonzero"
#endif
#if BOARD_LCD_SPI_CLOCK_HZ == 0U || BOARD_LCD_SPI_DATA_LINES != 4U
    #error "Waveshare display requires a nonzero QSPI clock and four data lines"
#endif
#if BOARD_LCD_BITS_PER_PIXEL != 16U
    #error "Waveshare display requires RGB565"
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

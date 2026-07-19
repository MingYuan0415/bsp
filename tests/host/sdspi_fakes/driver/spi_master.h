#ifndef __SDSPI_FAKE_SPI_MASTER_H__
#define __SDSPI_FAKE_SPI_MASTER_H__

#include "esp_err.h"

typedef int spi_host_device_t;
typedef int spi_dma_chan_t;

typedef struct spi_bus_config
{
    int mosi_io_num;
    int miso_io_num;
    int sclk_io_num;
    int quadwp_io_num;
    int quadhd_io_num;
    int max_transfer_sz;
} spi_bus_config_t;

#define SPI2_HOST       2
#define SPI3_HOST       3
#define SPI_DMA_CH_AUTO 3

esp_err_t spi_bus_initialize(spi_host_device_t host,
                             const spi_bus_config_t *bus_config,
                             spi_dma_chan_t dma_channel);
esp_err_t spi_bus_free(spi_host_device_t host);

#endif /* __SDSPI_FAKE_SPI_MASTER_H__ */

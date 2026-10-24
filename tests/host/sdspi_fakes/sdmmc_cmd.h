#ifndef __SDSPI_FAKE_SDMMC_CMD_H__
#define __SDSPI_FAKE_SDMMC_CMD_H__

#include <stdint.h>
#include "driver/sdspi_host.h"
#include "esp_err.h"

typedef struct sdmmc_card
{
    sdmmc_host_t host;
    struct
    {
        uint32_t sector_size;
    } csd;
} sdmmc_card_t;

esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card);

#endif /* __SDSPI_FAKE_SDMMC_CMD_H__ */

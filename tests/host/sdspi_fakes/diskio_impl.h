#ifndef __SDSPI_FAKE_DISKIO_IMPL_H__
#define __SDSPI_FAKE_DISKIO_IMPL_H__
#include "esp_err.h"
#include "ff.h"
esp_err_t ff_diskio_get_drive(BYTE *drive);
void ff_diskio_unregister(BYTE drive);
#endif

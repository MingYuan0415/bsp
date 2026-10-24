#ifndef __SDSPI_FAKE_FF_H__
#define __SDSPI_FAKE_FF_H__
#include <stdint.h>
typedef uint8_t BYTE;
typedef unsigned int UINT;
typedef uint32_t DWORD;
typedef uint32_t LBA_t;
typedef struct
{
    int unused;
} FATFS;
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NO_FILESYSTEM } FRESULT;
typedef struct
{
    BYTE fmt;
    BYTE n_fat;
    UINT align;
    UINT n_root;
    DWORD au_size;
} MKFS_PARM;
#define FM_ANY 7U
FRESULT f_mount(FATFS *fs, const char *drive, BYTE immediate);
FRESULT f_mkfs(const char *drive, const MKFS_PARM *options, void *work, UINT size);
FRESULT f_fdisk(BYTE drive, const LBA_t partitions[], void *work);
#endif

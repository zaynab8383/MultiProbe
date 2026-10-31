#ifndef IMAGE_STORE_H
#define IMAGE_STORE_H

#include "main.h"
#include <stdint.h>

typedef enum
{
    IMG_OK_ALREADY    = 0,
    IMG_OK_PROGRAMMED = 1,
    IMG_ERR_FLASH     = 2,
    IMG_ERR_PROGRAM   = 3,
    IMG_ERR_VERIFY    = 4
} ImageStatus;

ImageStatus Image_EnsureInFlash(void);
uint32_t Image_Crc32(uint32_t crc, const uint8_t *data, uint32_t len);

#endif
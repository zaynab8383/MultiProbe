#ifndef LOGO_IMAGE_RLE_H
#define LOGO_IMAGE_RLE_H

#include <stdint.h>

typedef struct {
    uint16_t count;   /* number of consecutive pixels */
    uint16_t color;   /* RGB565 */
} LogoRun;

#define LOGO_RLE_RUN_COUNT   2081U
#define LOGO_IMAGE_WIDTH     240U
#define LOGO_IMAGE_HEIGHT    240U
#define LOGO_IMAGE_CRC32     0x4299426EUL

extern const LogoRun logo_rle[LOGO_RLE_RUN_COUNT];

#endif

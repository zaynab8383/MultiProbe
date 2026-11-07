#include "image_store.h"
#include "w25q64.h"
#include "logo_image_rle.h"

#define PAGE_SIZE      256U
#define SECTOR_SIZE    4096U

static const uint32_t crc_tab[16] =
{
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

uint32_t Image_Crc32(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;
    while (len--)
    {
        crc ^= *data++;
        crc = (crc >> 4) ^ crc_tab[crc & 0x0FU];
        crc = (crc >> 4) ^ crc_tab[crc & 0x0FU];
    }
    return ~crc;
}

static HAL_StatusTypeDef flash_image_crc(uint32_t *crc_out)
{
    static uint8_t buf[PAGE_SIZE];
    uint32_t crc = 0U;

    for (uint32_t off = 0U; off < LCD_IMAGE_BYTES; off += PAGE_SIZE)
    {
        if (W25Q64_Read(LCD_IMAGE_FLASH_ADDR + off, buf, PAGE_SIZE) != HAL_OK)
            return HAL_ERROR;
        crc = Image_Crc32(crc, buf, PAGE_SIZE);
    }
    *crc_out = crc;
    return HAL_OK;
}

static HAL_StatusTypeDef program_image_from_rle(void)
{
    static uint8_t page[PAGE_SIZE];
    uint32_t fill = 0U;
    uint32_t addr = LCD_IMAGE_FLASH_ADDR;
    uint32_t total_pixels = 0U;

    for (uint32_t r = 0U; r < LOGO_RLE_RUN_COUNT; r++)
    {
        const uint16_t color = logo_rle[r].color;
        const uint8_t  hi    = (uint8_t)(color >> 8);
        const uint8_t  lo    = (uint8_t)color;

        for (uint32_t n = logo_rle[r].count; n > 0U; n--)
        {
            if (total_pixels >= (LCD_IMAGE_WIDTH * LCD_IMAGE_HEIGHT))
                return HAL_ERROR;

            page[fill++] = hi;
            page[fill++] = lo;
            total_pixels++;

            if (fill == PAGE_SIZE)
            {
                if ((addr & (SECTOR_SIZE - 1U)) == 0U)
                {
                    if (W25Q64_SectorErase(addr) != HAL_OK)
                        return HAL_ERROR;
                }
                if (W25Q64_PageProgram(addr, page, PAGE_SIZE) != HAL_OK)
                    return HAL_ERROR;

                addr += PAGE_SIZE;
                fill  = 0U;
            }
        }
    }

    if (total_pixels != (LCD_IMAGE_WIDTH * LCD_IMAGE_HEIGHT) || fill != 0U)
        return HAL_ERROR;

    return HAL_OK;
}

ImageStatus Image_EnsureInFlash(void)
{
    uint32_t crc;

    if (W25Q64_Init() != HAL_OK)
        return IMG_ERR_FLASH;

    if (flash_image_crc(&crc) == HAL_OK && crc == LOGO_IMAGE_CRC32)
        return IMG_OK_ALREADY;

    if (program_image_from_rle() != HAL_OK)
        return IMG_ERR_PROGRAM;

    if (flash_image_crc(&crc) != HAL_OK || crc != LOGO_IMAGE_CRC32)
        return IMG_ERR_VERIFY;

    return IMG_OK_PROGRAMMED;
}

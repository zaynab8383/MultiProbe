
#include "main.h"
#include "lcd.h"
#include "logo_image_rle.h"
#include "w25q64.h"

/*
 * ST7789VW, 240x240, 8-bit 8080 parallel via FMC Bank1 (NE1).
 *
 * Hardware (from CubeMX + MULTIPROBE schematic):
 *   D0..D7 = FMC_D0..D7
 *   WR     = FMC_NWE (PD5)
 *   RD     = FMC_NOE (PD4)
 *   CS     = FMC_NE1 (PC7) + GPIO PA15 (LCD_CS)
 *   DC/RS  = FMC_A19 (PE3) + GPIO PE13 (LCD_DC)
 *   RESET  = PE12 (LCD_RES)
 *
 * Command address A19=0 : 0x60000000
 * Data address    A19=1 : 0x60080000  (bit 19 of 8-bit bus)
 */

#define LCD_IMAGE_CHUNK_PIXELS  256U   /* larger chunk = fewer OSPI transactions */

#define LCD_CMD_ADDR   0x60000000UL
#define LCD_DATA_ADDR  0x60080000UL

extern SRAM_HandleTypeDef hsram1;

/* ------------------------------------------------------------------ */
/* Low-level helpers                                                   */
/* ------------------------------------------------------------------ */

static inline void LCD_Select(void)
{
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
}

static inline void LCD_DC_Command(void)
{
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
}

static inline void LCD_DC_Data(void)
{
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
}

static inline void FMC_Write8(uint32_t address, uint8_t value)
{
    *(__IO uint8_t *)address = value;
    __DSB();
}

static void LCD_WriteCommand(uint8_t command)
{
    LCD_DC_Command();
    FMC_Write8(LCD_CMD_ADDR, command);
}

static void LCD_WriteData8(uint8_t data)
{
    LCD_DC_Data();
    FMC_Write8(LCD_DATA_ADDR, data);
}

static void LCD_WriteData16(uint16_t data)
{
    LCD_WriteData8((uint8_t)(data >> 8));
    LCD_WriteData8((uint8_t)data);
}

static void LCD_BusSetup(void)
{
    FMC_NORSRAM_TimingTypeDef timing = {0};

    /* H7 FMC write FIFO packs consecutive 8-bit writes and breaks LCD protocol. */
    hsram1.Init.WriteFifo = FMC_WRITE_FIFO_DISABLE;

    /* Conservative but reliable 8080 timing (can be tightened later). */
    timing.AddressSetupTime      = 5;
    timing.AddressHoldTime       = 5;
    timing.DataSetupTime         = 30;
    timing.BusTurnAroundDuration = 5;
    timing.CLKDivision           = 2;
    timing.DataLatency           = 2;
    timing.AccessMode            = FMC_ACCESS_MODE_A;

    if (HAL_SRAM_Init(&hsram1, &timing, NULL) != HAL_OK)
    {
        Error_Handler();
    }
}

static void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    LCD_WriteCommand(0x2AU); /* CASET */
    LCD_WriteData16(x0);
    LCD_WriteData16(x1);

    LCD_WriteCommand(0x2BU); /* RASET */
    LCD_WriteData16(y0);
    LCD_WriteData16(y1);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void LCD_Init(void)
{
    LCD_BusSetup();

    LCD_Select();

    /* Hardware reset */
    HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(150);

    LCD_WriteCommand(0x01); /* SWRESET */
    HAL_Delay(150);

    LCD_WriteCommand(0x11); /* SLPOUT */
    HAL_Delay(120);

    LCD_WriteCommand(0x36); /* MADCTL */
    LCD_WriteData8(0x00);   /* RGB, no rotation */

    LCD_WriteCommand(0x3A); /* COLMOD: 16-bit RGB565 */
    LCD_WriteData8(0x55);

    LCD_WriteCommand(0xB2); /* Porch control */
    LCD_WriteData8(0x0C);
    LCD_WriteData8(0x0C);
    LCD_WriteData8(0x00);
    LCD_WriteData8(0x33);
    LCD_WriteData8(0x33);

    LCD_WriteCommand(0xB7); /* Gate control */
    LCD_WriteData8(0x35);

    LCD_WriteCommand(0xBB); /* VCOM */
    LCD_WriteData8(0x19);

    LCD_WriteCommand(0xC0); /* LCM control */
    LCD_WriteData8(0x2C);

    LCD_WriteCommand(0xC2); /* VDV/VRH enable */
    LCD_WriteData8(0x01);

    LCD_WriteCommand(0xC3); /* VRH */
    LCD_WriteData8(0x12);

    LCD_WriteCommand(0xC4); /* VDV */
    LCD_WriteData8(0x20);

    LCD_WriteCommand(0xC6); /* Frame rate */
    LCD_WriteData8(0x0F);

    LCD_WriteCommand(0xD0); /* Power control */
    LCD_WriteData8(0xA4);
    LCD_WriteData8(0xA1);

    /* Positive gamma */
    LCD_WriteCommand(0xE0);
    LCD_WriteData8(0xD0);
    LCD_WriteData8(0x04);
    LCD_WriteData8(0x0D);
    LCD_WriteData8(0x11);
    LCD_WriteData8(0x13);
    LCD_WriteData8(0x2B);
    LCD_WriteData8(0x3F);
    LCD_WriteData8(0x54);
    LCD_WriteData8(0x4C);
    LCD_WriteData8(0x18);
    LCD_WriteData8(0x0D);
    LCD_WriteData8(0x0B);
    LCD_WriteData8(0x1F);
    LCD_WriteData8(0x23);

    /* Negative gamma */
    LCD_WriteCommand(0xE1);
    LCD_WriteData8(0xD0);
    LCD_WriteData8(0x04);
    LCD_WriteData8(0x0C);
    LCD_WriteData8(0x11);
    LCD_WriteData8(0x13);
    LCD_WriteData8(0x2C);
    LCD_WriteData8(0x3F);
    LCD_WriteData8(0x44);
    LCD_WriteData8(0x51);
    LCD_WriteData8(0x2F);
    LCD_WriteData8(0x1F);
    LCD_WriteData8(0x1F);
    LCD_WriteData8(0x20);
    LCD_WriteData8(0x23);

    LCD_WriteCommand(0x21); /* INVON — required for most IPS ST7789 panels */
    LCD_WriteCommand(0x13); /* NORON */
    HAL_Delay(10);

    LCD_WriteCommand(0x29); /* DISPON */
    HAL_Delay(20);

    /* CS stays low; FMC_NE1 still generates CS strobes per access. */
}

void LCD_FillColor(uint16_t rgb565)
{
    const uint8_t high = (uint8_t)(rgb565 >> 8);
    const uint8_t low  = (uint8_t)rgb565;
    uint32_t i;

    LCD_Select();
    LCD_SetWindow(0U, 0U, LCD_WIDTH - 1U, LCD_HEIGHT - 1U);
    LCD_WriteCommand(0x2C); /* RAMWR */

    LCD_DC_Data();
    for (i = 0U; i < ((uint32_t)LCD_WIDTH * LCD_HEIGHT); i++)
    {
        FMC_Write8(LCD_DATA_ADDR, high);
        FMC_Write8(LCD_DATA_ADDR, low);
    }
}

void LCD_DrawImage(const uint16_t *image)
{
    uint32_t i;

    if (image == NULL)
        return;

    LCD_Select();
    LCD_SetWindow(0U, 0U, LCD_WIDTH - 1U, LCD_HEIGHT - 1U);
    LCD_WriteCommand(0x2C); /* RAMWR */
    LCD_DC_Data();

    for (i = 0U; i < ((uint32_t)LCD_WIDTH * LCD_HEIGHT); i++)
    {
        uint16_t pixel = image[i];
        FMC_Write8(LCD_DATA_ADDR, (uint8_t)(pixel >> 8));
        FMC_Write8(LCD_DATA_ADDR, (uint8_t)pixel);
    }
}

void LCD_DrawLogo(void)
{
    /* Only useful when logo_image_rle.h contains the full RLE data */
    LCD_Select();
    LCD_SetWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    LCD_WriteCommand(0x2C);
    LCD_DC_Data();

    for (uint32_t i = 0; i < LOGO_RLE_RUN_COUNT; i++)
    {
        uint16_t count = logo_rle[i].count;
        uint16_t color = logo_rle[i].color;
        uint8_t high = (uint8_t)(color >> 8);
        uint8_t low  = (uint8_t)color;

        for (uint16_t j = 0; j < count; j++)
        {
            FMC_Write8(LCD_DATA_ADDR, high);
            FMC_Write8(LCD_DATA_ADDR, low);
        }
    }
}

HAL_StatusTypeDef LCD_DrawImageFromFlash(void)
{
    /* Static buffer so it does not eat stack. 256 pixels = 512 bytes. */
    static uint8_t imageChunk[LCD_IMAGE_CHUNK_PIXELS * 2U];

    uint32_t pixelIndex = 0U;
    uint32_t remainingPixels = (uint32_t)LCD_WIDTH * (uint32_t)LCD_HEIGHT;

    LCD_Select();
    LCD_SetWindow(0U, 0U, LCD_WIDTH - 1U, LCD_HEIGHT - 1U);
    LCD_WriteCommand(0x2CU); /* RAMWR */
    LCD_DC_Data();

    while (remainingPixels > 0U)
    {
        uint32_t chunkPixels = remainingPixels;
        if (chunkPixels > LCD_IMAGE_CHUNK_PIXELS)
            chunkPixels = LCD_IMAGE_CHUNK_PIXELS;

        uint32_t chunkBytes = chunkPixels * 2U;

        if (W25Q64_Read(LCD_IMAGE_FLASH_ADDR + (pixelIndex * 2U),
                        imageChunk,
                        chunkBytes) != HAL_OK)
        {
            return HAL_ERROR;
        }

        /* Stream the raw RGB565 bytes (big-endian as stored) to LCD */
        for (uint32_t i = 0U; i < chunkBytes; i++)
        {
            FMC_Write8(LCD_DATA_ADDR, imageChunk[i]);
        }

        pixelIndex += chunkPixels;
        remainingPixels -= chunkPixels;
    }

    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/* RAM image cache                                                     */
/* The 115200-byte logo is read from the W25Q64 ONCE into this buffer  */
/* (lives in internal AXI SRAM, .bss). Every frame is then drawn from  */
/* RAM, which removes the ~28 ms OSPI read from each frame.            */
/* ------------------------------------------------------------------ */

#define LCD_RAM_LOAD_CHUNK  16384U

static uint8_t s_imageRam[LCD_IMAGE_BYTES] __attribute__((aligned(4)));
static uint8_t s_imageRamValid = 0U;

HAL_StatusTypeDef LCD_LoadImageToRam(void)
{
    s_imageRamValid = 0U;

    for (uint32_t off = 0U; off < LCD_IMAGE_BYTES; off += LCD_RAM_LOAD_CHUNK)
    {
        uint32_t len = LCD_IMAGE_BYTES - off;
        if (len > LCD_RAM_LOAD_CHUNK)
            len = LCD_RAM_LOAD_CHUNK;

        if (W25Q64_Read(LCD_IMAGE_FLASH_ADDR + off, &s_imageRam[off], len) != HAL_OK)
            return HAL_ERROR;
    }

    s_imageRamValid = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef LCD_DrawImageFromRam(void)
{
    if (!s_imageRamValid)
        return HAL_ERROR;

    LCD_Select();
    LCD_SetWindow(0U, 0U, LCD_WIDTH - 1U, LCD_HEIGHT - 1U);
    LCD_WriteCommand(0x2CU); /* RAMWR */
    LCD_DC_Data();

    /* Bytes are already stored big-endian (hi, lo) as the LCD expects. */
    const uint8_t *p = s_imageRam;
    for (uint32_t i = 0U; i < LCD_IMAGE_BYTES; i++)
    {
        FMC_Write8(LCD_DATA_ADDR, p[i]);
    }

    return HAL_OK;
}

/* Draw a w x h RGB565 (big-endian bytes) bitmap, each pixel enlarged scale x scale,
 * centered on the screen. Used by the video player (e.g. 120x120 with scale 2 = fullscreen). */
void LCD_DrawBitmap(const uint8_t *data, uint16_t w, uint16_t h, uint8_t scale)
{
    static uint8_t row[LCD_WIDTH * 2U];

    if (data == NULL || w == 0U || h == 0U || scale == 0U)
        return;

    const uint32_t dw = (uint32_t)w * scale;
    const uint32_t dh = (uint32_t)h * scale;
    if (dw > LCD_WIDTH || dh > LCD_HEIGHT)
        return;

    const uint16_t x0 = (uint16_t)((LCD_WIDTH  - dw) / 2U);
    const uint16_t y0 = (uint16_t)((LCD_HEIGHT - dh) / 2U);

    LCD_Select();
    LCD_SetWindow(x0, y0, (uint16_t)(x0 + dw - 1U), (uint16_t)(y0 + dh - 1U));
    LCD_WriteCommand(0x2CU); /* RAMWR */
    LCD_DC_Data();

    if (scale == 1U)
    {
        for (uint32_t i = 0U; i < ((uint32_t)w * h * 2U); i++)
            FMC_Write8(LCD_DATA_ADDR, data[i]);
        return;
    }

    for (uint32_t y = 0U; y < h; y++)
    {
        const uint8_t *src = &data[(uint32_t)y * w * 2U];
        uint32_t j = 0U;

        /* build one enlarged row ... */
        for (uint32_t x = 0U; x < w; x++)
        {
            const uint8_t hi = src[2U * x];
            const uint8_t lo = src[2U * x + 1U];
            for (uint8_t k = 0U; k < scale; k++)
            {
                row[j++] = hi;
                row[j++] = lo;
            }
        }

        /* ... and send it 'scale' times */
        for (uint8_t k = 0U; k < scale; k++)
        {
            for (uint32_t i = 0U; i < j; i++)
                FMC_Write8(LCD_DATA_ADDR, row[i]);
        }
    }
}

void LCD_DrawTestPattern(void)
{
    /* 4 colored quadrants + white border – useful when flash image is missing */
    LCD_Select();
    LCD_SetWindow(0U, 0U, LCD_WIDTH - 1U, LCD_HEIGHT - 1U);
    LCD_WriteCommand(0x2C);
    LCD_DC_Data();

    for (uint16_t y = 0; y < LCD_HEIGHT; y++)
    {
        for (uint16_t x = 0; x < LCD_WIDTH; x++)
        {
            uint16_t color;
            if (x < 2 || x > 237 || y < 2 || y > 237)
                color = LCD_COLOR_WHITE;
            else if (x < 120 && y < 120)
                color = LCD_COLOR_RED;
            else if (x >= 120 && y < 120)
                color = LCD_COLOR_GREEN;
            else if (x < 120 && y >= 120)
                color = LCD_COLOR_BLUE;
            else
                color = LCD_COLOR_YELLOW;

            FMC_Write8(LCD_DATA_ADDR, (uint8_t)(color >> 8));
            FMC_Write8(LCD_DATA_ADDR, (uint8_t)color);
        }
    }
}

void LCD_InitAndShowImage(void)
{
    LCD_Init();

    /* Prefer image stored in external flash.
     * If read fails (empty flash / wrong address / OSPI issue) fall back to test pattern.
     */
    if (LCD_DrawImageFromFlash() != HAL_OK)
    {
        LCD_DrawTestPattern();
    }
}
/* ------------------------------------------------------------------ */
/* Status bar helpers: fill a rectangle, draw text (small built-in font) */
/* ------------------------------------------------------------------ */

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if ((uint32_t)x + w > LCD_WIDTH)  w = (uint16_t)(LCD_WIDTH  - x);
    if ((uint32_t)y + h > LCD_HEIGHT) h = (uint16_t)(LCD_HEIGHT - y);

    const uint8_t hi = (uint8_t)(color >> 8);
    const uint8_t lo = (uint8_t)color;

    LCD_Select();
    LCD_SetWindow(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    LCD_WriteCommand(0x2C); /* RAMWR */
    LCD_DC_Data();

    uint32_t n = (uint32_t)w * h;
    for (uint32_t i = 0U; i < n; i++)
    {
        FMC_Write8(LCD_DATA_ADDR, hi);
        FMC_Write8(LCD_DATA_ADDR, lo);
    }
}

/* 7x10 ASCII font (32..126), 1 bit per pixel, bit 6 = leftmost column.
 * Generated from DejaVu Sans Mono (free licence) rendered at 10 px. */
#define FONT_W 7U
#define FONT_H 10U

static const uint8_t s_font[95][FONT_H] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
    {0x00,0x08,0x08,0x08,0x08,0x08,0x00,0x08,0x00,0x00}, /* ! */
    {0x00,0x14,0x14,0x14,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x00,0x14,0x14,0x3E,0x28,0x7C,0x28,0x28,0x00,0x00}, /* # */
    {0x00,0x08,0x1E,0x28,0x38,0x0E,0x0A,0x3C,0x08,0x00}, /* $ */
    {0x00,0x70,0x50,0x74,0x18,0x2E,0x0A,0x0E,0x00,0x00}, /* % */
    {0x00,0x1C,0x10,0x18,0x2A,0x26,0x24,0x1A,0x00,0x00}, /* & */
    {0x00,0x08,0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x08,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x08,0x00}, /* ( */
    {0x10,0x10,0x08,0x08,0x08,0x08,0x08,0x10,0x10,0x00}, /* ) */
    {0x00,0x2A,0x1C,0x1C,0x2A,0x00,0x00,0x00,0x00,0x00}, /* * */
    {0x00,0x00,0x08,0x08,0x3E,0x08,0x08,0x00,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x10}, /* , */
    {0x00,0x00,0x00,0x00,0x00,0x1C,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00}, /* . */
    {0x00,0x02,0x04,0x04,0x08,0x08,0x10,0x10,0x20,0x00}, /* / */
    {0x00,0x1C,0x22,0x22,0x2A,0x22,0x22,0x1C,0x00,0x00}, /* 0 */
    {0x00,0x38,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00}, /* 1 */
    {0x00,0x1C,0x22,0x02,0x06,0x0C,0x10,0x3E,0x00,0x00}, /* 2 */
    {0x00,0x1C,0x22,0x02,0x1C,0x02,0x22,0x1C,0x00,0x00}, /* 3 */
    {0x00,0x04,0x0C,0x14,0x34,0x3E,0x04,0x04,0x00,0x00}, /* 4 */
    {0x00,0x3C,0x20,0x3C,0x02,0x02,0x02,0x3C,0x00,0x00}, /* 5 */
    {0x00,0x1E,0x30,0x20,0x3C,0x22,0x22,0x1C,0x00,0x00}, /* 6 */
    {0x00,0x3E,0x06,0x04,0x04,0x08,0x08,0x10,0x00,0x00}, /* 7 */
    {0x00,0x1C,0x22,0x22,0x1C,0x22,0x22,0x1C,0x00,0x00}, /* 8 */
    {0x00,0x1C,0x22,0x22,0x1E,0x02,0x06,0x3C,0x00,0x00}, /* 9 */
    {0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x10,0x00,0x00}, /* : */
    {0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x10,0x10,0x10}, /* ; */
    {0x00,0x00,0x02,0x1C,0x20,0x1C,0x02,0x00,0x00,0x00}, /* < */
    {0x00,0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00,0x00}, /* = */
    {0x00,0x00,0x20,0x1C,0x02,0x1C,0x20,0x00,0x00,0x00}, /* > */
    {0x00,0x3C,0x04,0x08,0x10,0x10,0x00,0x10,0x00,0x00}, /* ? */
    {0x00,0x1C,0x12,0x2E,0x2A,0x2A,0x2A,0x2E,0x10,0x0C}, /* @ */
    {0x00,0x08,0x08,0x14,0x14,0x1C,0x22,0x22,0x00,0x00}, /* A */
    {0x00,0x3C,0x22,0x22,0x3C,0x22,0x22,0x3C,0x00,0x00}, /* B */
    {0x00,0x1E,0x32,0x20,0x20,0x20,0x32,0x1E,0x00,0x00}, /* C */
    {0x00,0x3C,0x26,0x22,0x22,0x22,0x26,0x3C,0x00,0x00}, /* D */
    {0x00,0x3E,0x20,0x20,0x3E,0x20,0x20,0x3E,0x00,0x00}, /* E */
    {0x00,0x3E,0x20,0x20,0x3E,0x20,0x20,0x20,0x00,0x00}, /* F */
    {0x00,0x1C,0x32,0x20,0x26,0x22,0x32,0x1E,0x00,0x00}, /* G */
    {0x00,0x22,0x22,0x22,0x3E,0x22,0x22,0x22,0x00,0x00}, /* H */
    {0x00,0x3E,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00}, /* I */
    {0x00,0x1C,0x04,0x04,0x04,0x04,0x24,0x18,0x00,0x00}, /* J */
    {0x00,0x22,0x24,0x28,0x30,0x28,0x24,0x22,0x00,0x00}, /* K */
    {0x00,0x20,0x20,0x20,0x20,0x20,0x20,0x3E,0x00,0x00}, /* L */
    {0x00,0x22,0x36,0x36,0x2A,0x22,0x22,0x22,0x00,0x00}, /* M */
    {0x00,0x22,0x32,0x32,0x2A,0x26,0x26,0x22,0x00,0x00}, /* N */
    {0x00,0x1C,0x22,0x22,0x22,0x22,0x22,0x1C,0x00,0x00}, /* O */
    {0x00,0x3C,0x22,0x22,0x3C,0x20,0x20,0x20,0x00,0x00}, /* P */
    {0x00,0x1C,0x22,0x22,0x22,0x22,0x22,0x1C,0x06,0x00}, /* Q */
    {0x00,0x3C,0x22,0x22,0x3C,0x26,0x22,0x21,0x00,0x00}, /* R */
    {0x00,0x1C,0x22,0x20,0x1C,0x02,0x22,0x1C,0x00,0x00}, /* S */
    {0x00,0x3E,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00}, /* T */
    {0x00,0x22,0x22,0x22,0x22,0x22,0x22,0x1C,0x00,0x00}, /* U */
    {0x00,0x22,0x22,0x14,0x14,0x14,0x08,0x08,0x00,0x00}, /* V */
    {0x00,0x42,0x5A,0x5A,0x3C,0x24,0x24,0x24,0x00,0x00}, /* W */
    {0x00,0x22,0x14,0x14,0x08,0x14,0x14,0x22,0x00,0x00}, /* X */
    {0x00,0x22,0x14,0x14,0x08,0x08,0x08,0x08,0x00,0x00}, /* Y */
    {0x00,0x3E,0x04,0x04,0x08,0x10,0x10,0x3E,0x00,0x00}, /* Z */
    {0x18,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x18,0x00}, /* [ */
    {0x00,0x20,0x10,0x10,0x08,0x08,0x04,0x04,0x02,0x00}, /* backslash */
    {0x18,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x18,0x00}, /* ] */
    {0x00,0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E}, /* _ */
    {0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x00,0x3C,0x02,0x1E,0x22,0x3E,0x00,0x00}, /* a */
    {0x20,0x20,0x20,0x3C,0x22,0x22,0x22,0x3C,0x00,0x00}, /* b */
    {0x00,0x00,0x00,0x1C,0x20,0x20,0x20,0x1C,0x00,0x00}, /* c */
    {0x02,0x02,0x02,0x1E,0x22,0x22,0x22,0x1E,0x00,0x00}, /* d */
    {0x00,0x00,0x00,0x1C,0x22,0x3E,0x20,0x1E,0x00,0x00}, /* e */
    {0x0C,0x10,0x10,0x3C,0x10,0x10,0x10,0x10,0x00,0x00}, /* f */
    {0x00,0x00,0x00,0x1E,0x22,0x22,0x22,0x1E,0x02,0x1C}, /* g */
    {0x20,0x20,0x20,0x2C,0x32,0x22,0x22,0x22,0x00,0x00}, /* h */
    {0x08,0x00,0x00,0x18,0x08,0x08,0x08,0x3E,0x00,0x00}, /* i */
    {0x08,0x00,0x00,0x38,0x08,0x08,0x08,0x08,0x08,0x30}, /* j */
    {0x20,0x20,0x20,0x24,0x28,0x38,0x24,0x22,0x00,0x00}, /* k */
    {0x70,0x10,0x10,0x10,0x10,0x10,0x10,0x0C,0x00,0x00}, /* l */
    {0x00,0x00,0x00,0x3E,0x2A,0x2A,0x2A,0x2A,0x00,0x00}, /* m */
    {0x00,0x00,0x00,0x2C,0x32,0x22,0x22,0x22,0x00,0x00}, /* n */
    {0x00,0x00,0x00,0x1C,0x22,0x22,0x22,0x1C,0x00,0x00}, /* o */
    {0x00,0x00,0x00,0x3C,0x22,0x22,0x22,0x3C,0x20,0x20}, /* p */
    {0x00,0x00,0x00,0x1E,0x22,0x22,0x22,0x1E,0x02,0x02}, /* q */
    {0x00,0x00,0x00,0x1E,0x12,0x10,0x10,0x10,0x00,0x00}, /* r */
    {0x00,0x00,0x00,0x1E,0x20,0x1E,0x02,0x3C,0x00,0x00}, /* s */
    {0x00,0x10,0x10,0x3C,0x10,0x10,0x10,0x1C,0x00,0x00}, /* t */
    {0x00,0x00,0x00,0x22,0x22,0x22,0x22,0x1E,0x00,0x00}, /* u */
    {0x00,0x00,0x00,0x22,0x14,0x14,0x14,0x08,0x00,0x00}, /* v */
    {0x00,0x00,0x00,0x22,0x2A,0x14,0x14,0x14,0x00,0x00}, /* w */
    {0x00,0x00,0x00,0x36,0x14,0x08,0x14,0x36,0x00,0x00}, /* x */
    {0x00,0x00,0x00,0x22,0x14,0x14,0x08,0x08,0x08,0x30}, /* y */
    {0x00,0x00,0x00,0x3E,0x04,0x08,0x10,0x3E,0x00,0x00}, /* z */
    {0x0C,0x08,0x08,0x08,0x30,0x08,0x08,0x08,0x0C,0x00}, /* { */
    {0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08}, /* | */
    {0x18,0x08,0x08,0x08,0x06,0x08,0x08,0x08,0x18,0x00}, /* } */
    {0x00,0x00,0x00,0x00,0x38,0x06,0x00,0x00,0x00,0x00}, /* ~ */
};

static int8_t FontIndex(char c)
{
    if (c >= 32 && c <= 126) return (int8_t)(c - 32);
    return 0;                                   /* unknown -> space */
}

void LCD_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    const uint8_t *glyph = s_font[FontIndex(c)];
    uint16_t w = (uint16_t)(FONT_W * scale);
    uint16_t h = (uint16_t)(FONT_H * scale);
    if ((uint32_t)x + w > LCD_WIDTH || (uint32_t)y + h > LCD_HEIGHT) return;

    LCD_Select();
    LCD_SetWindow(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    LCD_WriteCommand(0x2C);
    LCD_DC_Data();

    for (uint8_t ry = 0U; ry < FONT_H; ry++)
    {
        uint8_t row = glyph[ry];
        for (uint8_t sy = 0U; sy < scale; sy++)
        {
            for (uint8_t rx = 0U; rx < FONT_W; rx++)
            {
                uint8_t bit = (uint8_t)((row >> (FONT_W - 1U - rx)) & 1U);
                uint16_t color = bit ? fg : bg;
                uint8_t hi = (uint8_t)(color >> 8);
                uint8_t lo = (uint8_t)color;
                for (uint8_t sx = 0U; sx < scale; sx++)
                {
                    FMC_Write8(LCD_DATA_ADDR, hi);
                    FMC_Write8(LCD_DATA_ADDR, lo);
                }
            }
        }
    }
}

void LCD_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t cx = x;
    uint16_t step = (uint16_t)(FONT_W * scale);
    while (*s)
    {
        LCD_DrawChar(cx, y, *s, fg, bg, scale);
        cx = (uint16_t)(cx + step);
        s++;
    }
}

/* 1 bit/pixel bitmap (MSB first, rows padded to whole bytes). 1 = fg, 0 = bg. */
void LCD_DrawBitmap1bpp(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const uint8_t *bits, uint16_t fg, uint16_t bg)
{
    if (w == 0U || h == 0U) return;
    if ((uint32_t)x + w > LCD_WIDTH || (uint32_t)y + h > LCD_HEIGHT) return;

    uint16_t rowBytes = (uint16_t)((w + 7U) / 8U);

    LCD_Select();
    LCD_SetWindow(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    LCD_WriteCommand(0x2C);
    LCD_DC_Data();

    const uint8_t fh = (uint8_t)(fg >> 8), fl = (uint8_t)fg;
    const uint8_t bh = (uint8_t)(bg >> 8), bl = (uint8_t)bg;

    for (uint16_t ry = 0U; ry < h; ry++)
    {
        const uint8_t *row = &bits[(uint32_t)ry * rowBytes];
        for (uint16_t rx = 0U; rx < w; rx++)
        {
            if ((row[rx >> 3] >> (7U - (rx & 7U))) & 1U)
            {
                FMC_Write8(LCD_DATA_ADDR, fh);
                FMC_Write8(LCD_DATA_ADDR, fl);
            }
            else
            {
                FMC_Write8(LCD_DATA_ADDR, bh);
                FMC_Write8(LCD_DATA_ADDR, bl);
            }
        }
    }
}

void LCD_MPU_Config(void)
{
    MPU_Region_InitTypeDef mpu = {0};

    HAL_MPU_Disable();

    /*
     * 0x60000000 is Normal/cacheable by default on Cortex-M7.
     * Speculative reads would strobe LCD RD and corrupt GRAM.
     * Mark the whole FMC Bank1 region as Device, non-cacheable.
     */
    mpu.Enable           = MPU_REGION_ENABLE;
    mpu.Number           = MPU_REGION_NUMBER0;
    mpu.BaseAddress      = 0x60000000UL;
    mpu.Size             = MPU_REGION_SIZE_64MB;
    mpu.SubRegionDisable = 0x00;
    mpu.TypeExtField     = MPU_TEX_LEVEL0;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpu.IsShareable      = MPU_ACCESS_SHAREABLE;
    mpu.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsBufferable     = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

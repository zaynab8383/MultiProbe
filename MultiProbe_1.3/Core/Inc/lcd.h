
#ifndef INC_LCD_H
#define INC_LCD_H

#include "main.h"
#include <stdint.h>

/* RGB565 colors */
#define LCD_COLOR_RED    0xF800U
#define LCD_COLOR_GREEN  0x07E0U
#define LCD_COLOR_BLUE   0x001FU
#define LCD_COLOR_BLACK  0x0000U
#define LCD_COLOR_WHITE  0xFFFFU
#define LCD_COLOR_YELLOW 0xFFE0U
#define LCD_COLOR_CYAN   0x07FFU
#define LCD_COLOR_MAGENTA 0xF81FU

#define LCD_WIDTH   240U
#define LCD_HEIGHT  240U

void LCD_MPU_Config(void);
void LCD_Init(void);
void LCD_FillColor(uint16_t rgb565);
void LCD_InitAndShowImage(void);

/* Draw full-screen RGB565 image from external W25Q64 flash (address LCD_IMAGE_FLASH_ADDR) */
HAL_StatusTypeDef LCD_DrawImageFromFlash(void);

/* Read the logo from W25Q64 into an internal RAM buffer (call once, 115200 bytes) */
HAL_StatusTypeDef LCD_LoadImageToRam(void);

/* Draw the cached logo from RAM (no flash access). Returns HAL_ERROR if not loaded. */
HAL_StatusTypeDef LCD_DrawImageFromRam(void);

/* Draw a w x h RGB565 bitmap (big-endian bytes), every pixel enlarged scale x scale,
 * centered on the screen (w*scale and h*scale must be <= 240). */
void LCD_DrawBitmap(const uint8_t *data, uint16_t w, uint16_t h, uint8_t scale);

/* Draw full-screen RGB565 image from RAM buffer (must be 240*240 uint16_t) */
void LCD_DrawImage(const uint16_t *image);

/* Draw RLE-compressed logo if data is complete (see logo_image_rle.h) */
void LCD_DrawLogo(void);

/* Simple test pattern for debugging */
void LCD_DrawTestPattern(void);
/* Fill a rectangle with a solid color (used for the status bar / menu areas) */
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* Draw one character using the built-in 3x5 font (digits, '.', 'V', '-', space) */
void LCD_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);

/* Draw a string using the built-in font */
void LCD_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);
/* Draw a 1 bit/pixel bitmap (MSB first, 1 = fg, 0 = bg) */
void LCD_DrawBitmap1bpp(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const uint8_t *bits, uint16_t fg, uint16_t bg);

/* Draw an RLE4 image: byte = ((run-1)<<4) | palette_index, row-major, w*h pixels. */
void LCD_DrawRLE4(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  const uint8_t *rle, uint16_t len, const uint16_t *pal);

#endif /* INC_LCD_H */

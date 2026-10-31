
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

/* Draw full-screen RGB565 image from RAM buffer (must be 240*240 uint16_t) */
void LCD_DrawImage(const uint16_t *image);

/* Draw RLE-compressed logo if data is complete (see logo_image_rle.h) */
void LCD_DrawLogo(void);

/* Simple test pattern for debugging */
void LCD_DrawTestPattern(void);

#endif /* INC_LCD_H */

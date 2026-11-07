#ifndef W25Q64_H
#define W25Q64_H

#include "main.h"
#include <stdint.h>

#define W25Q64_CAPACITY_BYTES  (8U * 1024U * 1024U)

#define LCD_IMAGE_WIDTH        240U
#define LCD_IMAGE_HEIGHT       240U
#define LCD_IMAGE_BYTES        (LCD_IMAGE_WIDTH * LCD_IMAGE_HEIGHT * 2U)  /* 115200 */

#define LCD_IMAGE_FLASH_ADDR   0x00100000U

HAL_StatusTypeDef W25Q64_Init(void);

HAL_StatusTypeDef W25Q64_Read(uint32_t address, uint8_t *buffer, uint32_t length);
HAL_StatusTypeDef W25Q64_ReadID(uint8_t *id_buf3);

HAL_StatusTypeDef W25Q64_WriteEnable(void);
HAL_StatusTypeDef W25Q64_WaitBusy(uint32_t timeout_ms);
HAL_StatusTypeDef W25Q64_SectorErase(uint32_t address);
HAL_StatusTypeDef W25Q64_PageProgram(uint32_t address, const uint8_t *data, uint32_t length);
HAL_StatusTypeDef W25Q64_Write(uint32_t address, const uint8_t *data, uint32_t length);
HAL_StatusTypeDef W25Q64_WriteImage(const uint8_t *image_data);

#endif

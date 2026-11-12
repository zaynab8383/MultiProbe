#include "w25q64.h"
#include "qspi_ll.h"
#include <stddef.h>
#include <string.h>

/*
 * W25Q64 (IC1) access on top of the register-level driver in qspi_ll.h.
 * The flash is normally in OCTOSPI MEMORY-MAPPED mode (0x90000000) so the CPU can read constant data
 * straight from it. Erase/program briefly leave that mode and re-enter it afterwards.
 * Do not call these from interrupt handlers.
 */

static uint8_t s_mapped = 0U;

static HAL_StatusTypeDef begin_indirect(void)
{
    s_mapped = 0U;
    return qll_hw_init() ? HAL_OK : HAL_ERROR;
}

static HAL_StatusTypeDef end_indirect(void)
{
    if (!qll_enter_mmap()) return HAL_ERROR;
    s_mapped = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef W25Q64_ReadID(uint8_t *id_buf3)
{
    if (id_buf3 == NULL) return HAL_ERROR;
    if (begin_indirect() != HAL_OK) return HAL_ERROR;
    int ok = qll_cmd(0x9FU, -1, 0, id_buf3, 3U);
    if (end_indirect() != HAL_OK) return HAL_ERROR;
    return ok ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef W25Q64_Init(void)
{
    if (begin_indirect() != HAL_OK) return HAL_ERROR;
    int ok = qll_ensure_qe();
    uint8_t id[3] = {0};
    if (ok) ok = qll_cmd(0x9FU, -1, 0, id, 3U);
    if (end_indirect() != HAL_OK) return HAL_ERROR;
    if (!ok) return HAL_ERROR;

    /* 17h = W25Q64JV (8MB), 16h = W25Q32JV (4MB) */
    if (id[0] != 0xEFU || id[1] != 0x40U || (id[2] != 0x17U && id[2] != 0x16U))
        return HAL_ERROR;
    return HAL_OK;
}

HAL_StatusTypeDef W25Q64_Read(uint32_t address, uint8_t *buffer, uint32_t length)
{
    if (buffer == NULL || length == 0U ||
        address >= W25Q64_CAPACITY_BYTES ||
        length > (W25Q64_CAPACITY_BYTES - address))
        return HAL_ERROR;

    if (!s_mapped && end_indirect() != HAL_OK)
        return HAL_ERROR;

    memcpy(buffer, (const void *)(QLL_MAP_BASE + address), length);
    return HAL_OK;
}

HAL_StatusTypeDef W25Q64_WriteEnable(void)
{
    if (begin_indirect() != HAL_OK) return HAL_ERROR;
    int ok = qll_write_enable();
    if (end_indirect() != HAL_OK) return HAL_ERROR;
    return ok ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef W25Q64_WaitBusy(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (begin_indirect() != HAL_OK) return HAL_ERROR;
    int ok = qll_wait_ready(QLL_SPIN_LIMIT);
    if (end_indirect() != HAL_OK) return HAL_ERROR;
    return ok ? HAL_OK : HAL_TIMEOUT;
}

HAL_StatusTypeDef W25Q64_SectorErase(uint32_t address)
{
    if (address >= W25Q64_CAPACITY_BYTES) return HAL_ERROR;
    if (begin_indirect() != HAL_OK) return HAL_ERROR;
    int ok = qll_write_enable()
          && qll_cmd(0x20U, (int32_t)address, 0, 0, 0U)
          && qll_wait_ready(QLL_SPIN_LIMIT);
    if (end_indirect() != HAL_OK) return HAL_ERROR;
    return ok ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef W25Q64_PageProgram(uint32_t address, const uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0U || length > 256U ||
        address >= W25Q64_CAPACITY_BYTES)
        return HAL_ERROR;
    if (begin_indirect() != HAL_OK) return HAL_ERROR;
    int ok = qll_write_enable()
          && qll_cmd(0x02U, (int32_t)address, data, 0, length)
          && qll_wait_ready(QLL_SPIN_LIMIT);
    if (end_indirect() != HAL_OK) return HAL_ERROR;
    return ok ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef W25Q64_Write(uint32_t address, const uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0U)
        return HAL_ERROR;

    uint32_t offset = 0;

    while (offset < length)
    {
        uint32_t page_addr = address + offset;
        uint32_t page_off  = page_addr & 0xFFU;
        uint32_t chunk     = 256U - page_off;

        if (chunk > (length - offset))
            chunk = length - offset;

        /* Erase sector if we are at the beginning of a sector */
        if ((page_addr & 0xFFFU) == 0U)
        {
            if (W25Q64_SectorErase(page_addr) != HAL_OK)
                return HAL_ERROR;
        }

        if (W25Q64_PageProgram(page_addr, data + offset, chunk) != HAL_OK)
            return HAL_ERROR;

        offset += chunk;
    }
    return HAL_OK;
}

HAL_StatusTypeDef W25Q64_WriteImage(const uint8_t *image_data)
{
    if (image_data == NULL)
        return HAL_ERROR;

    return W25Q64_Write(LCD_IMAGE_FLASH_ADDR, image_data, LCD_IMAGE_BYTES);
}

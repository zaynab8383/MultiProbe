//#include "w25q64.h"
//#include <stddef.h>
//#include <string.h>
//
//extern OSPI_HandleTypeDef hospi1;
//
///* ------------------------------------------------------------------ */
///* Low-level helpers                                                   */
///* ------------------------------------------------------------------ */
//
//static HAL_StatusTypeDef OSPI_SendCommand(uint8_t instruction,
//                                          uint32_t address,
//                                          uint32_t address_mode,
//                                          uint32_t data_mode,
//                                          uint32_t nb_data,
//                                          const uint8_t *tx_data)
//{
//    OSPI_RegularCmdTypeDef cmd = {0};
//
//    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
//    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
//    cmd.Instruction        = instruction;
//    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
//    cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
//    cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
//
//    cmd.Address            = address;
//    cmd.AddressMode        = address_mode;
//    cmd.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
//    cmd.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
//
//    cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
//    cmd.DataMode           = data_mode;
//    cmd.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
//    cmd.DummyCycles        = 0U;
//    cmd.NbData             = nb_data;
//    cmd.DQSMode            = HAL_OSPI_DQS_DISABLE;
//    cmd.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
//
//    if (HAL_OSPI_Command(&hospi1, &cmd, 1000U) != HAL_OK)
//        return HAL_ERROR;
//
//    if (nb_data > 0U && tx_data != NULL)
//    {
//        if (HAL_OSPI_Transmit(&hospi1, (uint8_t *)tx_data, 1000U) != HAL_OK)
//            return HAL_ERROR;
//    }
//
//    return HAL_OK;
//}
//
///* ------------------------------------------------------------------ */
///* Public functions                                                    */
///* ------------------------------------------------------------------ */
//
//HAL_StatusTypeDef W25Q64_Read(uint32_t address, uint8_t *buffer, uint32_t length)
//{
//    if (buffer == NULL || length == 0U ||
//        address >= W25Q64_CAPACITY_BYTES ||
//        length > (W25Q64_CAPACITY_BYTES - address))
//        return HAL_ERROR;
//
//    OSPI_RegularCmdTypeDef cmd = {0};
//
//    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
//    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
//    cmd.Instruction        = 0x03U;                 /* Read Data */
//    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
//    cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
//    cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
//
//    cmd.Address            = address;
//    cmd.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
//    cmd.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
//    cmd.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
//
//    cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
//    cmd.DataMode           = HAL_OSPI_DATA_1_LINE;
//    cmd.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
//    cmd.DummyCycles        = 0U;
//    cmd.NbData             = length;
//    cmd.DQSMode            = HAL_OSPI_DQS_DISABLE;
//    cmd.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
//
//    if (HAL_OSPI_Command(&hospi1, &cmd, 1000U) != HAL_OK)
//        return HAL_ERROR;
//
//    return HAL_OSPI_Receive(&hospi1, buffer, 1000U);
//}
//
//HAL_StatusTypeDef W25Q64_ReadID(uint8_t *id_buf3)
//{
//    if (id_buf3 == NULL)
//        return HAL_ERROR;
//
//    OSPI_RegularCmdTypeDef cmd = {0};
//
//    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
//    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
//    cmd.Instruction        = 0x9FU;                 /* JEDEC ID */
//    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
//    cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
//    cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
//
//    cmd.AddressMode        = HAL_OSPI_ADDRESS_NONE;
//    cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
//    cmd.DataMode           = HAL_OSPI_DATA_1_LINE;
//    cmd.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
//    cmd.DummyCycles        = 0U;
//    cmd.NbData             = 3U;
//    cmd.DQSMode            = HAL_OSPI_DQS_DISABLE;
//    cmd.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
//
//    if (HAL_OSPI_Command(&hospi1, &cmd, 1000U) != HAL_OK)
//        return HAL_ERROR;
//
//    return HAL_OSPI_Receive(&hospi1, id_buf3, 1000U);
//}
//
//HAL_StatusTypeDef W25Q64_WriteEnable(void)
//{
//    return OSPI_SendCommand(0x06U, 0, HAL_OSPI_ADDRESS_NONE, HAL_OSPI_DATA_NONE, 0, NULL);
//}
//
//HAL_StatusTypeDef W25Q64_WaitBusy(uint32_t timeout_ms)
//{
//    uint32_t start = HAL_GetTick();
//    uint8_t status = 0x01U;
//
//    while (status & 0x01U)   /* WIP bit */
//    {
//        if ((HAL_GetTick() - start) > timeout_ms)
//            return HAL_TIMEOUT;
//
//        OSPI_RegularCmdTypeDef cmd = {0};
//        cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
//        cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
//        cmd.Instruction        = 0x05U;             /* Read Status Register-1 */
//        cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
//        cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
//        cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
//        cmd.AddressMode        = HAL_OSPI_ADDRESS_NONE;
//        cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
//        cmd.DataMode           = HAL_OSPI_DATA_1_LINE;
//        cmd.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
//        cmd.DummyCycles        = 0U;
//        cmd.NbData             = 1U;
//        cmd.DQSMode            = HAL_OSPI_DQS_DISABLE;
//        cmd.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
//
//        if (HAL_OSPI_Command(&hospi1, &cmd, 100U) != HAL_OK)
//            return HAL_ERROR;
//
//        if (HAL_OSPI_Receive(&hospi1, &status, 100U) != HAL_OK)
//            return HAL_ERROR;
//    }
//    return HAL_OK;
//}
//
//HAL_StatusTypeDef W25Q64_SectorErase(uint32_t address)
//{
//    address &= ~0xFFFU;   // align to 4KB
//
//    if (W25Q64_WriteEnable() != HAL_OK)
//        return HAL_ERROR;
//
//    if (OSPI_SendCommand(0x20U, address, HAL_OSPI_ADDRESS_1_LINE, HAL_OSPI_DATA_NONE, 0, NULL) != HAL_OK)
//        return HAL_ERROR;
//
//    return W25Q64_WaitBusy(500);   // sector erase ~45ms typical, timeout 500ms
//}
//
//HAL_StatusTypeDef W25Q64_PageProgram(uint32_t address, const uint8_t *data, uint32_t length)
//{
//    if (data == NULL || length == 0U || length > 256U)
//        return HAL_ERROR;
//
//    if (W25Q64_WriteEnable() != HAL_OK)
//        return HAL_ERROR;
//
//    if (OSPI_SendCommand(0x02U, address, HAL_OSPI_ADDRESS_1_LINE, HAL_OSPI_DATA_1_LINE, length, data) != HAL_OK)
//        return HAL_ERROR;
//
//    return W25Q64_WaitBusy(10);    // page program ~0.7ms
//}
//
//HAL_StatusTypeDef W25Q64_Write(uint32_t address, const uint8_t *data, uint32_t length)
//{
//    if (data == NULL || length == 0U)
//        return HAL_ERROR;
//
//    uint32_t offset = 0;
//
//    while (offset < length)
//    {
//        uint32_t page_addr = address + offset;
//        uint32_t page_off  = page_addr & 0xFFU;
//        uint32_t chunk     = 256U - page_off;
//
//        if (chunk > (length - offset))
//            chunk = length - offset;
//
//        // Erase sector if we are at the beginning of a sector
//        if ((page_addr & 0xFFFU) == 0U)
//        {
//            if (W25Q64_SectorErase(page_addr) != HAL_OK)
//                return HAL_ERROR;
//        }
//
//        if (W25Q64_PageProgram(page_addr, data + offset, chunk) != HAL_OK)
//            return HAL_ERROR;
//
//        offset += chunk;
//    }
//    return HAL_OK;
//}
//
//HAL_StatusTypeDef W25Q64_WriteImage(const uint8_t *image_data)
//{
//    if (image_data == NULL)
//        return HAL_ERROR;
//
//    return W25Q64_Write(LCD_IMAGE_FLASH_ADDR, image_data, LCD_IMAGE_BYTES);
//}
#include "w25q64.h"
#include <stddef.h>
#include <string.h>

extern OSPI_HandleTypeDef hospi1;

/* ------------------------------------------------------------------ */
/* Low-level helpers                                                   */
/* ------------------------------------------------------------------ */

static HAL_StatusTypeDef OSPI_SendCommand(uint8_t instruction,
                                          uint32_t address,
                                          uint32_t address_mode,
                                          uint32_t data_mode,
                                          uint32_t nb_data,
                                          const uint8_t *tx_data)
{
    OSPI_RegularCmdTypeDef cmd = {0};

    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = instruction;
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;

    cmd.Address            = address;
    cmd.AddressMode        = address_mode;
    cmd.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
    cmd.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;

    cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode           = data_mode;
    cmd.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
    cmd.DummyCycles        = 0U;
    cmd.NbData             = nb_data;
    cmd.DQSMode            = HAL_OSPI_DQS_DISABLE;
    cmd.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

    if (HAL_OSPI_Command(&hospi1, &cmd, 1000U) != HAL_OK)
        return HAL_ERROR;

    if (nb_data > 0U && tx_data != NULL)
    {
        if (HAL_OSPI_Transmit(&hospi1, (uint8_t *)tx_data, 1000U) != HAL_OK)
            return HAL_ERROR;
    }

    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/* Public functions                                                    */
/* ------------------------------------------------------------------ */

/* Settings that suit ONE W25Q64JV on OCTOSPI1 (IO0..IO3 on port 1 low):
 *  - DualQuad must be OFF (only one flash is wired, CubeMX "dual_quad_mode" would garble data)
 *  - Read Data (0x03) allows max 50 MHz -> kernel 280 MHz / (7+1) = 35 MHz
 *  - /CS deselect time for program/erase must be >= 50 ns -> 4 cycles @ 35 MHz = 114 ns
 */
#define W25Q64_OSPI_PRESCALER   7U
#define W25Q64_OSPI_CS_HIGH     4U
#define W25Q64_OSPI_DEVSIZE     23U

HAL_StatusTypeDef W25Q64_Init(void)
{
    if (hospi1.Init.DualQuad          != HAL_OSPI_DUALQUAD_DISABLE ||
        hospi1.Init.ClockPrescaler    != W25Q64_OSPI_PRESCALER     ||
        hospi1.Init.ChipSelectHighTime!= W25Q64_OSPI_CS_HIGH       ||
        hospi1.Init.DeviceSize        != W25Q64_OSPI_DEVSIZE)
    {
        OSPIM_CfgTypeDef ospim = {0};

        hospi1.Init.DualQuad           = HAL_OSPI_DUALQUAD_DISABLE;
        hospi1.Init.ClockPrescaler     = W25Q64_OSPI_PRESCALER;
        hospi1.Init.ChipSelectHighTime = W25Q64_OSPI_CS_HIGH;
        hospi1.Init.DeviceSize         = W25Q64_OSPI_DEVSIZE;

        if (HAL_OSPI_DeInit(&hospi1) != HAL_OK)
            return HAL_ERROR;
        if (HAL_OSPI_Init(&hospi1) != HAL_OK)
            return HAL_ERROR;

        ospim.ClkPort   = 1;
        ospim.NCSPort   = 1;
        ospim.IOLowPort = HAL_OSPIM_IOPORT_1_LOW;
        if (HAL_OSPIM_Config(&hospi1, &ospim, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
            return HAL_ERROR;
    }

    HAL_Delay(5);

    uint8_t id[3] = {0};
    if (W25Q64_ReadID(id) != HAL_OK)
        return HAL_ERROR;

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

    OSPI_RegularCmdTypeDef cmd = {0};

    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = 0x03U;                 /* Read Data */
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;

    cmd.Address            = address;
    cmd.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
    cmd.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
    cmd.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;

    cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode           = HAL_OSPI_DATA_1_LINE;
    cmd.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
    cmd.DummyCycles        = 0U;
    cmd.NbData             = length;
    cmd.DQSMode            = HAL_OSPI_DQS_DISABLE;
    cmd.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

    if (HAL_OSPI_Command(&hospi1, &cmd, 1000U) != HAL_OK)
        return HAL_ERROR;

    return HAL_OSPI_Receive(&hospi1, buffer, 1000U);
}

HAL_StatusTypeDef W25Q64_ReadID(uint8_t *id_buf3)
{
    if (id_buf3 == NULL)
        return HAL_ERROR;

    OSPI_RegularCmdTypeDef cmd = {0};

    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = 0x9FU;                 /* JEDEC ID */
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;

    cmd.AddressMode        = HAL_OSPI_ADDRESS_NONE;
    cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode           = HAL_OSPI_DATA_1_LINE;
    cmd.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
    cmd.DummyCycles        = 0U;
    cmd.NbData             = 3U;
    cmd.DQSMode            = HAL_OSPI_DQS_DISABLE;
    cmd.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

    if (HAL_OSPI_Command(&hospi1, &cmd, 1000U) != HAL_OK)
        return HAL_ERROR;

    return HAL_OSPI_Receive(&hospi1, id_buf3, 1000U);
}

HAL_StatusTypeDef W25Q64_WriteEnable(void)
{
    return OSPI_SendCommand(0x06U, 0, HAL_OSPI_ADDRESS_NONE, HAL_OSPI_DATA_NONE, 0, NULL);
}

HAL_StatusTypeDef W25Q64_WaitBusy(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t status = 0x01U;

    while (status & 0x01U)   /* WIP bit */
    {
        if ((HAL_GetTick() - start) > timeout_ms)
            return HAL_TIMEOUT;

        OSPI_RegularCmdTypeDef cmd = {0};
        cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
        cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
        cmd.Instruction        = 0x05U;             /* Read Status Register-1 */
        cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
        cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
        cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
        cmd.AddressMode        = HAL_OSPI_ADDRESS_NONE;
        cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
        cmd.DataMode           = HAL_OSPI_DATA_1_LINE;
        cmd.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
        cmd.DummyCycles        = 0U;
        cmd.NbData             = 1U;
        cmd.DQSMode            = HAL_OSPI_DQS_DISABLE;
        cmd.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

        if (HAL_OSPI_Command(&hospi1, &cmd, 100U) != HAL_OK)
            return HAL_ERROR;

        if (HAL_OSPI_Receive(&hospi1, &status, 100U) != HAL_OK)
            return HAL_ERROR;
    }
    return HAL_OK;
}

HAL_StatusTypeDef W25Q64_SectorErase(uint32_t address)
{
    address &= ~0xFFFU;   // align to 4KB

    if (W25Q64_WriteEnable() != HAL_OK)
        return HAL_ERROR;

    if (OSPI_SendCommand(0x20U, address, HAL_OSPI_ADDRESS_1_LINE, HAL_OSPI_DATA_NONE, 0, NULL) != HAL_OK)
        return HAL_ERROR;

    return W25Q64_WaitBusy(500);   // sector erase ~45ms typical, timeout 500ms
}

HAL_StatusTypeDef W25Q64_PageProgram(uint32_t address, const uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0U || length > 256U ||
        ((address & 0xFFU) + length) > 256U ||
        address >= W25Q64_CAPACITY_BYTES)
        return HAL_ERROR;

    if (W25Q64_WriteEnable() != HAL_OK)
        return HAL_ERROR;

    if (OSPI_SendCommand(0x02U, address, HAL_OSPI_ADDRESS_1_LINE, HAL_OSPI_DATA_1_LINE, length, data) != HAL_OK)
        return HAL_ERROR;

    return W25Q64_WaitBusy(20);    // page program 0.4-3 ms
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

        // Erase sector if we are at the beginning of a sector
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

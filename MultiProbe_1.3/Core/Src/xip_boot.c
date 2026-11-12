/*
 * xip_boot.c - maps the external W25Q64 (IC1) into the address space at 0x90000000 (OCTOSPI memory-mapped,
 * Quad I/O). Constant data placed in the ".extdata" section (pages_assets.c, logo_image_rle.c) lives there
 * and is read directly by the CPU. Code stays in internal flash.
 * Call once from main() before any of that data is used. Pure register code (no HAL).
 */
#include "qspi_ll.h"

int XIP_Init(void)
{
    if (!qll_hw_init())    return 0;
    if (!qll_ensure_qe())  return 0;
    if (!qll_enter_mmap()) return 0;
    return 1;
}

#include "w25q64.h"
#include <string.h>

/* Linker symbols (STM32H7B0VBTX_FLASH.ld) */
extern char _extdata_lma, _extdata_vma, _extdata_size, _extmagic;

/*
 * Copies the constant data that the debugger staged in RAM into the W25Q64 (once).
 * Returns 0 = QSPI already up to date / nothing staged, 1 = programmed + verified, -1 = failed.
 * When the board is powered on without a debugger the staging RAM holds garbage (magic missing),
 * so nothing happens and the data already stored in the QSPI flash is used.
 */
int ExtData_Sync(void)
{
    const volatile uint32_t *mg = (const volatile uint32_t *)&_extmagic;
    if (mg[0] != 0x4D505153UL || mg[1] != 0xA5C3965AUL)
        return 0;

    const uint8_t *src  = (const uint8_t *)&_extdata_lma;
    const uint8_t *qspi = (const uint8_t *)&_extdata_vma;
    uint32_t size = (uint32_t)(uintptr_t)&_extdata_size;
    uint32_t off  = (uint32_t)(uintptr_t)&_extdata_vma - QLL_MAP_BASE;

    if (size == 0U) return 0;
    if (memcmp(src, qspi, size) == 0) return 0;

    if (W25Q64_Init() != HAL_OK) return -1;
    if (W25Q64_Write(off, src, size) != HAL_OK) return -1;
    return (memcmp(src, qspi, size) == 0) ? 1 : -1;
}

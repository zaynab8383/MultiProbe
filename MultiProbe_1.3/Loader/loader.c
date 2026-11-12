/*
 * External loader for the W25Q64JV (IC1) on OCTOSPI1 - STM32CubeProgrammer / STM32CubeIDE format.
 * Build with Loader/Makefile -> MultiProbe_W25Q64.stldr  (an ELF renamed to .stldr).
 * Runs from RAM at 0x24000004 right after reset, so clocks are the HSI defaults (no HAL, no SysTick).
 */
#include "qspi_ll.h"

#define NOR_FLASH 3   /* memory-mapped NOR at 0x90000000 (ST app note uses NOR_FLASH for OCTOSPI NOR) */

/* Diagnostics at a fixed RAM address (read after a failed run with:  -c port=SWD mode=HOTPLUG -r32 0x24001000 0x40) */
__attribute__((section(".diag"), used)) volatile uint32_t g_diag[16];
#define DG(i, v) (g_diag[(i)] = (uint32_t)(v))

struct DeviceSectors { uint32_t SectorNum; uint32_t SectorSize; };
struct StorageInfo
{
    char     DeviceName[100];
    uint16_t DeviceType;
    uint32_t DeviceStartAddress;
    uint32_t DeviceSize;
    uint32_t PageSize;
    uint8_t  EraseValue;
    struct DeviceSectors EraseSectors[10];
};

__attribute__((section(".Dev_info"), used))
const struct StorageInfo StorageInfo =
{
    "MultiProbe_W25Q64",
    NOR_FLASH,
    QLL_MAP_BASE,
    QLL_SIZE_BYTES,
    0x100U,                              /* page */
    0xFFU,                               /* erased value */
    { { 128U, 0x10000U }, { 0U, 0U } }   /* 128 x 64 KB blocks */
};

static uint32_t off_of(uint32_t a) { return (a >= QLL_MAP_BASE) ? (a - QLL_MAP_BASE) : a; }

int Init(void)
{
    uint8_t id[3] = {0, 0, 0}, sr1 = 0xEE, sr2 = 0xEE;
    DG(0, 0xA0);                                   /* Init entered */
    if (!qll_hw_init())   { DG(0, 0xE1); return 0; }
    DG(0, 0xA1);
    if (!qll_cmd(0x9FU, -1, 0, id, 3U)) { DG(0, 0xE2); DG(8, OCTOSPI1->SR); return 0; }
    DG(1, (id[0] << 16) | (id[1] << 8) | id[2]);   /* expect 0xEF4017 */
    (void)qll_cmd(0x05U, -1, 0, &sr1, 1U);
    (void)qll_cmd(0x35U, -1, 0, &sr2, 1U);
    DG(2, sr1); DG(3, sr2);
    DG(0, 0xA2);
    if (!qll_ensure_qe()) { DG(0, 0xE3); (void)qll_cmd(0x05U, -1, 0, &sr1, 1U); (void)qll_cmd(0x35U, -1, 0, &sr2, 1U); DG(4, sr1); DG(5, sr2); return 0; }
    DG(0, 0xA3);
    if (!qll_enter_mmap()) { DG(0, 0xE4); return 0; }
    DG(0, 0xA4);
    return 1;
}

int Write(uint32_t Address, uint32_t Size, uint8_t *buffer)
{
    uint32_t a = off_of(Address);
    DG(6, 0xB0); DG(7, a);
    if (!qll_hw_init()) { DG(6, 0xB1); return 0; }
    while (Size)
    {
        uint32_t n = 256U - (a & 0xFFU);
        if (n > Size) n = Size;
        if (!qll_write_enable()) { DG(6, 0xB2); DG(9, OCTOSPI1->SR); return 0; }
        if (!qll_cmd(0x02U, (int32_t)a, buffer, 0, n)) { DG(6, 0xB3); DG(9, OCTOSPI1->SR); DG(10, OCTOSPI1->CR); return 0; }
        if (!qll_wait_ready(QLL_SPIN_LIMIT)) { DG(6, 0xB4); return 0; }
        a += n; buffer += n; Size -= n;
    }
    return qll_enter_mmap();
}

int SectorErase(uint32_t EraseStartAddress, uint32_t EraseEndAddress)
{
    uint32_t a = off_of(EraseStartAddress) & ~0xFFFFUL;
    uint32_t e = off_of(EraseEndAddress);
    DG(11, 0xC0); DG(12, a); DG(13, e);
    if (!qll_hw_init()) { DG(11, 0xC1); return 0; }
    for (; a <= e; a += 0x10000UL)
    {
        if (!qll_write_enable()) { DG(11, 0xC2); return 0; }
        if (!qll_cmd(0xD8U, (int32_t)a, 0, 0, 0U)) { DG(11, 0xC3); DG(14, OCTOSPI1->SR); return 0; }     /* 64 KB block erase */
        if (!qll_wait_ready(QLL_SPIN_LIMIT)) { DG(11, 0xC4); return 0; }
    }
    DG(11, 0xC5);
    return qll_enter_mmap();
}

int MassErase(void)
{
    if (!qll_hw_init()) return 0;
    if (!qll_write_enable()) return 0;
    if (!qll_cmd(0xC7U, -1, 0, 0, 0U)) return 0;
    if (!qll_wait_ready(QLL_SPIN_LIMIT)) return 0;
    return qll_enter_mmap();
}

uint32_t CheckSum(uint32_t StartAddress, uint32_t Size, uint32_t InitVal)
{
    const uint8_t *p = (const uint8_t *)StartAddress;
    uint32_t s = InitVal;
    if (!qll_hw_init() || !qll_enter_mmap()) return 0;
    while (Size--) s += *p++;
    return s;
}

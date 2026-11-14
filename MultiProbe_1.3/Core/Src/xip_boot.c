/*
 * xip_boot.c - boot stage that runs from INTERNAL flash (see linker script .boot_text), called at the end of
 * SystemInit(), i.e. before .data is copied and before any code of the external flash is executed.
 *
 *   1. maps the W25Q64 (IC1) at 0x90000000 (OCTOSPI1 memory-mapped, quad)
 *   2. if the debugger has staged a new firmware image in RAM (magic present): encrypts it with AES-CTR using the
 *      per-device key and programs it into the external flash (only the 4 KB sectors that differ)
 *   3. enables OTFDEC1 region 1 over the image, so the CPU sees the decrypted code/data
 *   4. enables the I-cache (external flash is slower than internal)
 *
 * No libc, no HAL, no SysTick, no global variables (nothing is initialised yet).
 */
#include "qspi_ll.h"
#include "aes_boot.h"
#include "otf_crypto.h"
#include "otf_config.h"

#define BOOT_FN __attribute__((optimize("O1,no-tree-loop-distribute-patterns")))

/* linker script symbols */
extern char _extmagic, _extstage_base, _extimg_size;

#define SECTOR_BYTES   4096UL
#define OTF_REGION     0U              /* OTFDEC region 1 */

#if OTF_ENABLE
static const uint8_t k_secret[16] = OTF_SECRET_BYTES;
#endif

/* Erase one 4 KB sector and program it from buf[4096]; returns 1 on success. Leaves the flash memory-mapped. */
BOOT_FN static int sector_program(uint32_t off, const uint8_t *buf)
{
    int ok = qll_hw_init() && qll_write_enable()
          && qll_cmd(0x20U, (int32_t)off, 0, 0, 0U) && qll_wait_ready(QLL_SPIN_LIMIT);
    for (uint32_t p = 0; ok && p < SECTOR_BYTES; p += 256U)
        ok = qll_write_enable() && qll_cmd(0x02U, (int32_t)(off + p), buf + p, 0, 256U)
          && qll_wait_ready(QLL_SPIN_LIMIT);
    (void)qll_enter_mmap();
    return ok;
}

BOOT_FN static int sector_equal(uint32_t off, const uint8_t *buf)
{
    const volatile uint8_t *f = (const volatile uint8_t *)(QLL_MAP_BASE + off);
    for (uint32_t i = 0; i < SECTOR_BYTES; i++)
        if (f[i] != buf[i]) return 0;
    return 1;
}

/* Programs the staged image (encrypted if OTF_ENABLE). Returns 0 = nothing to do / ok, -1 = failed. */
BOOT_FN static int image_sync(const AES128_Ctx *ctx, const uint32_t nonce[2], uint32_t image_bytes)
{
    const volatile uint32_t *mg = (const volatile uint32_t *)&_extmagic;
    if (mg[0] != 0x4D505153UL || mg[1] != 0xA5C3965AUL)
        return 0;                                           /* no debugger-staged image -> use what is in flash */

    if ((image_bytes == 0U) || (image_bytes > (1024UL * 1024UL)))
        return -1;

    const volatile uint8_t *stage = (const volatile uint8_t *)&_extstage_base;
    uint32_t size = (image_bytes + SECTOR_BYTES - 1U) & ~(SECTOR_BYTES - 1U);
    uint8_t buf[SECTOR_BYTES];
    uint8_t ks[16];
    int result = 0;

    for (uint32_t off = 0; off < size; off += SECTOR_BYTES)
    {
        for (uint32_t b = 0; b < SECTOR_BYTES; b += 16U)
        {
#if OTF_ENABLE
            OTF_Keystream(ctx, nonce, OTF_FW_VERSION, OTF_REGION, QLL_MAP_BASE + off + b, ks);
#else
            for (uint32_t i = 0; i < 16U; i++) ks[i] = 0U;
            (void)ctx; (void)nonce;
#endif
            for (uint32_t i = 0; i < 16U; i++)
            {
                uint32_t image_pos = off + b + i;
                /* Never copy uninitialised SRAM after the final linker section into external flash. */
                uint8_t plain = (image_pos < image_bytes) ? stage[image_pos] : 0xFFU;
                buf[b + i] = (uint8_t)(plain ^ ks[i]);
            }
        }
        if (!sector_equal(off, buf))
        {
            if (!sector_program(off, buf) || !sector_equal(off, buf))
            {
                result = -1;
                break;
            }
        }
    }

    if (result == 0)
    {   /* done: wipe the staged plaintext (and the magic) so it does not stay in RAM */
        volatile uint8_t *w = (volatile uint8_t *)&_extstage_base;
        for (uint32_t i = 0; i < size; i++) w[i] = 0U;
        volatile uint32_t *m = (volatile uint32_t *)&_extmagic;
        m[0] = 0U; m[1] = 0U;
    }
    return result;
}

#if OTF_ENABLE
/*
 * OTFDEC computes a hardware CRC while KEYR0..KEYR3 are written.
 * Keep this check in the pre-C-runtime boot code so a bad key load is
 * detected before execution is transferred to the external image.
 * Algorithm copied from ST's STM32H7 HAL OTFDEC driver.
 */
BOOT_FN static uint32_t otf_key_crc(const uint32_t key_words[4])
{
    const uint8_t poly = 0x07U;
    const uint32_t strobe[4] = {0xAA55AA55UL, 0x00000003UL, 0x00000018UL, 0x000000C0UL};
    uint8_t crc = 0U;

    for (uint32_t j = 0U; j < 4U; j++)
    {
        uint32_t keyval = key_words[j];

        if (j == 0U)
            keyval ^= strobe[0];
        else
            keyval ^= (strobe[j] << 24) | ((uint32_t)crc << 16)
                    | (strobe[j] << 8) | (uint32_t)crc;

        crc = 0U;
        for (uint32_t i = 0U; i < 32U; i++)
        {
            uint32_t bit = (((uint32_t)crc >> 7)
                          ^ ((keyval >> (31U - i)) & 0x0FU)) & 1U;
            crc = (uint8_t)(crc << 1);
            if (bit != 0U)
                crc ^= poly;
        }
        crc ^= 0x55U;
    }

    return (uint32_t)crc;
}

BOOT_FN static int otf_enable(const uint8_t key[16], const uint32_t nonce[2], uint32_t image_bytes)
{
    OTFDEC_Region_TypeDef *r = OTFDEC1_REGION1;
    uint32_t key_words[4];

    if ((image_bytes == 0U) || ((image_bytes & 0xFFFU) != 0U))
        return 0;

    RCC->AHB3ENR |= RCC_AHB3ENR_OTFDEC1EN;
    (void)RCC->AHB3ENR;

    /* MODE = 10: decrypt every read (instruction and data) in this region. */
    r->REG_CONFIGR = (r->REG_CONFIGR & ~OTFDEC_REG_CONFIGR_MODE)
                   | OTFDEC_REG_CONFIGR_MODE_1;

    /*
     * AES key mapping for standard OTFDEC:
     * KEYR0 = AES key word 0 (last 4 bytes), ... KEYR3 = word 3 (first 4 bytes).
     * The hardware requires writes in the strict order KEYR0, KEYR1, KEYR2, KEYR3.
     */
    key_words[0] = ((uint32_t)key[12] << 24) | ((uint32_t)key[13] << 16)
                 | ((uint32_t)key[14] << 8) | (uint32_t)key[15];
    key_words[1] = ((uint32_t)key[8] << 24) | ((uint32_t)key[9] << 16)
                 | ((uint32_t)key[10] << 8) | (uint32_t)key[11];
    key_words[2] = ((uint32_t)key[4] << 24) | ((uint32_t)key[5] << 16)
                 | ((uint32_t)key[6] << 8) | (uint32_t)key[7];
    key_words[3] = ((uint32_t)key[0] << 24) | ((uint32_t)key[1] << 16)
                 | ((uint32_t)key[2] << 8) | (uint32_t)key[3];

    r->REG_KEYR0 = key_words[0];
    __DSB(); __ISB();
    r->REG_KEYR1 = key_words[1];
    __DSB(); __ISB();
    r->REG_KEYR2 = key_words[2];
    __DSB(); __ISB();
    r->REG_KEYR3 = key_words[3];
    __DSB(); __ISB();

    /* Do not lock a key that the peripheral failed to load correctly. */
    if ((((r->REG_CONFIGR & OTFDEC_REG_CONFIGR_KEYCRC)
          >> OTFDEC_REG_CONFIGR_KEYCRC_Pos) & 0xFFU) != otf_key_crc(key_words))
        return 0;

    r->REG_CONFIGR |= OTFDEC_REG_CONFIGR_KEYLOCK;

    r->REG_START_ADDR = QLL_MAP_BASE;
    r->REG_END_ADDR   = QLL_MAP_BASE + image_bytes - 1U; /* end address on 4-KB boundary */
    r->REG_NONCER0    = nonce[0];
    r->REG_NONCER1    = nonce[1];

    r->REG_CONFIGR = (r->REG_CONFIGR & ~OTFDEC_REG_CONFIGR_VERSION)
                   | ((uint32_t)OTF_FW_VERSION << OTFDEC_REG_CONFIGR_VERSION_Pos);

    __DSB(); __ISB();
    r->REG_CONFIGR |= OTFDEC_REG_CONFIGR_REG_EN;
    __DSB(); __ISB();

    r->REG_CONFIGR |= OTFDEC_REG_CONFIGR_CONFIGLOCK;
    __DSB(); __ISB();

    return ((r->REG_CONFIGR & OTFDEC_REG_CONFIGR_REG_EN) != 0U);
}

#endif

BOOT_FN void Boot_ExternalInit(void)
{
    AES128_Ctx ctx;
    uint8_t key[16];
    uint32_t nonce[2];
    uint32_t image_bytes = (uint32_t)(uintptr_t)&_extimg_size;
    uint32_t size = (image_bytes + SECTOR_BYTES - 1U) & ~(SECTOR_BYTES - 1U);

    for (uint32_t i = 0; i < 16U; i++) key[i] = 0U;
    nonce[0] = 0U; nonce[1] = 0U;

    if (!qll_hw_init() || !qll_ensure_qe() || !qll_enter_mmap())
        for (;;) { }                                        /* flash missing / wiring problem */

#if OTF_ENABLE
    {
        const volatile uint32_t *u = (const volatile uint32_t *)UID_BASE;
        uint32_t uid[3];
        uid[0] = u[0]; uid[1] = u[1]; uid[2] = u[2];
        OTF_Derive(uid, k_secret, key, nonce);
        AES128_Init(&ctx, key);
    }
#endif

    if (image_sync(&ctx, nonce, image_bytes) != 0)
        for (;;) { }                                        /* programming the external flash failed */

#if OTF_ENABLE
    if (!otf_enable(key, nonce, size))
        for (;;) { }                                        /* OTFDEC key CRC / region setup failed */
#else
    (void)size; (void)key;
#endif

    __DSB();
    __ISB();
    SCB_InvalidateICache();
    SCB_EnableICache();
}

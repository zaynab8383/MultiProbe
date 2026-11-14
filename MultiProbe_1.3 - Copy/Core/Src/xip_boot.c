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
BOOT_FN static int image_sync(const AES128_Ctx *ctx, const uint32_t nonce[2])
{
    const volatile uint32_t *mg = (const volatile uint32_t *)&_extmagic;
    if (mg[0] != 0x4D505153UL || mg[1] != 0xA5C3965AUL)
        return 0;                                           /* no debugger-staged image -> use what is in flash */

    const volatile uint8_t *stage = (const volatile uint8_t *)&_extstage_base;
    uint32_t size = ((uint32_t)(uintptr_t)&_extimg_size + SECTOR_BYTES - 1U) & ~(SECTOR_BYTES - 1U);
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
                buf[b + i] = (uint8_t)(stage[off + b + i] ^ ks[i]);
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
BOOT_FN static void otf_enable(const uint8_t key[16], const uint32_t nonce[2], uint32_t image_bytes)
{
    OTFDEC_Region_TypeDef *r = OTFDEC1_REGION1;

    RCC->AHB3ENR |= RCC_AHB3ENR_OTFDEC1EN;
    (void)RCC->AHB3ENR;

    /* MODE = 10: every read (instruction or data) of the region is decrypted. Must be set BEFORE the key. */
    r->REG_CONFIGR = (r->REG_CONFIGR & ~OTFDEC_REG_CONFIGR_MODE) | OTFDEC_REG_CONFIGR_MODE_1;

    /* key: KEYR3 = key[0..3] ... KEYR0 = key[12..15]; written 0,1,2,3 (RM0455 41.4.x) */
    r->REG_KEYR0 = ((uint32_t)key[12] << 24) | ((uint32_t)key[13] << 16) | ((uint32_t)key[14] << 8) | key[15];
    r->REG_KEYR1 = ((uint32_t)key[8]  << 24) | ((uint32_t)key[9]  << 16) | ((uint32_t)key[10] << 8) | key[11];
    r->REG_KEYR2 = ((uint32_t)key[4]  << 24) | ((uint32_t)key[5]  << 16) | ((uint32_t)key[6]  << 8) | key[7];
    r->REG_KEYR3 = ((uint32_t)key[0]  << 24) | ((uint32_t)key[1]  << 16) | ((uint32_t)key[2]  << 8) | key[3];
    r->REG_CONFIGR |= OTFDEC_REG_CONFIGR_KEYLOCK;           /* keys can no longer be written until reset */

    r->REG_START_ADDR = QLL_MAP_BASE;
    r->REG_END_ADDR   = QLL_MAP_BASE + image_bytes - 1U;     /* low 12 bits are all ones: 4 KB granularity */
    r->REG_NONCER0    = nonce[0];
    r->REG_NONCER1    = nonce[1];
    r->REG_CONFIGR    = (r->REG_CONFIGR & ~OTFDEC_REG_CONFIGR_VERSION)
                      | ((uint32_t)OTF_FW_VERSION << OTFDEC_REG_CONFIGR_VERSION_Pos);
    r->REG_CONFIGR   |= OTFDEC_REG_CONFIGR_REG_EN;
    r->REG_CONFIGR   |= OTFDEC_REG_CONFIGR_CONFIGLOCK;      /* configuration frozen until the next reset */
}
#endif

BOOT_FN void Boot_ExternalInit(void)
{
    AES128_Ctx ctx;
    uint8_t key[16];
    uint32_t nonce[2];
    uint32_t size = ((uint32_t)(uintptr_t)&_extimg_size + SECTOR_BYTES - 1U) & ~(SECTOR_BYTES - 1U);

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

    if (image_sync(&ctx, nonce) != 0)
        for (;;) { }                                        /* programming the external flash failed */

#if OTF_ENABLE
    otf_enable(key, nonce, size);
#else
    (void)size; (void)key;
#endif

    SCB_EnableICache();
}

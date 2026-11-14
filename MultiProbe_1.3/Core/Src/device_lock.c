#include "device_lock.h"

/* STM32H7B0: آدرس Unique Device ID. این برخلاف خیلی از خط‌های دیگه‌ی H7 (که 0x1FF1E800 هستن) است؛
 * توی CMSIS همین پروژه (Drivers/CMSIS/.../stm32h7b0xx.h -> UID_BASE) چک شده و 0x08FFF800 است. */
#define DEVLOCK_UID_BASE   0x08FFF800UL

/* === رمز خصوصی پروژه ===
 * این سه عدد را به هر مقدار ۳۲ بیتی دلخواه (فقط خودت می‌دونی) عوض کن، قبل از provision کردن اولین
 * برد. بعد از عوض کردن، دیگه دست نزن -- تغییرش یعنی fingerprint همه‌ی بردهای قبلی عوض می‌شه. */
#define DEVLOCK_SECRET0    0x4D50524Bu   /* TODO: عوض کن */
#define DEVLOCK_SECRET1    0x3B1A7F05u   /* TODO: عوض کن */
#define DEVLOCK_SECRET2    0x1B873593u   /* TODO: عوض کن */

/* اثرانگشت مورد انتظار همین یک برد مشخص.
 * اولین بار: به همین صورت (FFFFFFFF) بذار و فلش کن -> روی LCD «fingerprint» این چیپ چاپ می‌شه ->
 * همون 4 عدد هگز رو اینجا جایگزین کن -> دوباره بیلد/فلش کن. از این به بعد این .bin فقط رو همین
 * چیپ بالا میاد. */
#define DEVLOCK_EXPECTED0  0xCFA8C232u
#define DEVLOCK_EXPECTED1  0xEA733601u
#define DEVLOCK_EXPECTED2  0x675B99E1u
#define DEVLOCK_EXPECTED3  0x4A98E64Du

/* یک finalizer 32 بیتی ساده (avalanche خوب، نه یک الگوریتم رمزنگاری استاندارد).
 * هدف اینجا مخفی کردن ریاضی یا مقاومت در برابر تحلیل رمزی نیست؛ فقط اینکه از روی UID ساده
 * قابل حدس نباشه و یک تغییر کوچیک در UID کل خروجی رو عوض کنه. */
static uint32_t devlock_mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

void DeviceLock_ReadUid(uint32_t uid[3])
{
    const volatile uint32_t *u = (const volatile uint32_t *)DEVLOCK_UID_BASE;
    uid[0] = u[0];
    uid[1] = u[1];
    uid[2] = u[2];
}

static void devlock_derive(const uint32_t uid[3], uint32_t domain, uint32_t out[4])
{
    for (uint8_t lane = 0U; lane < 4U; lane++)
    {
        uint32_t h = DEVLOCK_SECRET0 ^ (domain + lane * 0x9E3779B9u);
        h = devlock_mix32(h ^ uid[0]);
        h = devlock_mix32(h ^ (uid[1] + DEVLOCK_SECRET1));
        h = devlock_mix32(h ^ (uid[2] + DEVLOCK_SECRET2));
        h = devlock_mix32(h ^ (lane * 0x85EBCA6Bu));
        out[lane] = h;
    }
}

void DeviceLock_Fingerprint(const uint32_t uid[3], uint32_t fp[4])
{
    devlock_derive(uid, 0x4C4F434Bu /* "LOCK" */, fp);
}

void DeviceLock_DeriveKey(const uint32_t uid[3], uint32_t domain, uint32_t key[4])
{
    devlock_derive(uid, domain, key);
}

DevLock_Result_t DeviceLock_Check(uint32_t fp_out[4])
{
    uint32_t uid[3];
    DeviceLock_ReadUid(uid);

    uint32_t fp[4];
    DeviceLock_Fingerprint(uid, fp);
    if (fp_out != 0)
    {
        fp_out[0] = fp[0]; fp_out[1] = fp[1]; fp_out[2] = fp[2]; fp_out[3] = fp[3];
    }

    const uint32_t expected[4] = { DEVLOCK_EXPECTED0, DEVLOCK_EXPECTED1, DEVLOCK_EXPECTED2, DEVLOCK_EXPECTED3 };

    uint8_t allFF = 1U;
    for (uint8_t i = 0U; i < 4U; i++) { if (expected[i] != 0xFFFFFFFFu) allFF = 0U; }
    if (allFF) return DEVLOCK_UNPROVISIONED;

    for (uint8_t i = 0U; i < 4U; i++) { if (fp[i] != expected[i]) return DEVLOCK_MISMATCH; }
    return DEVLOCK_OK;
}

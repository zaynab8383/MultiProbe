#ifndef DEVICE_LOCK_H
#define DEVICE_LOCK_H

#include <stdint.h>

/*
 * قفل ضد-کپی بر پایه‌ی UID (96 بیتی) میکروکنترلر.
 *
 * ایده: یک "اثرانگشت" 128 بیتی از UID این چیپ + یک رمز ثابت توی سورس می‌سازیم.
 * این اثرانگشت با یک عدد از پیش نوشته‌شده در فلش (EXPECTED_FP) مقایسه می‌شه.
 *   - برابر بود -> برنامه عادی اجرا می‌شه.
 *   - EXPECTED_FP هنوز خالی/FFFFFFFF بود (تازه کامپایل شده، هنوز provision نشده) ->
 *     روی LCD چاپ می‌شه و برنامه متوقف می‌مونه تا همون عدد رو توی سورس بذاری و دوباره فلش کنی.
 *   - برابر نبود (یعنی همین .bin روی یه برد/چیپ دیگه زده شده) -> قفل: برنامه اصلی اجرا نمی‌شه.
 *
 * !! این تابع به‌تنهایی محافظت کامل نیست !! اگه کسی بتونه فلش داخلی رو از روی SWD/JTAG
 * دامپ کنه، هم EXPECTED_FP رو می‌بینه هم DEVICE_LOCK_SECRET رو، و می‌تونه چک رو دور بزنه یا
 * برای چیپ خودش از نو حساب کنه. برای اینکه این قفل واقعاً معنی داشته باشه حتماً باید همراه
 * با فعال کردن Read-Out Protection (RDP Level 1 یا 2) روی آپشن‌بایت‌ها باشه (از
 * STM32CubeProgrammer، تب Option Bytes؛ کد لازم نداره). بدون RDP، این فقط جلوی کپی "تنبلانه"
 * رو می‌گیره نه یک کپی‌بردار مصمم رو.
 */

/* نتیجه‌ی بررسی قفل */
typedef enum {
    DEVLOCK_OK = 0,        /* UID مطابقت داره، ادامه بده */
    DEVLOCK_UNPROVISIONED, /* هنوز EXPECTED_FP ست نشده؛ fingerprint چاپ شد، منتظر provision */
    DEVLOCK_MISMATCH       /* UID مطابقت نداره -> کپی روی چیپ دیگه */
} DevLock_Result_t;

/* UID خام این چیپ (96 بیت) را می‌خواند. */
void DeviceLock_ReadUid(uint32_t uid[3]);

/* اثرانگشت 128 بیتی (نه رمزنگاری قوی، فقط برای bind کردن به UID) را از UID + رمز ثابت می‌سازد. */
void DeviceLock_Fingerprint(const uint32_t uid[3], uint32_t fp[4]);

/* همون مکانیزم مخلوط‌سازی ولی با یک رشته‌ی "domain" متفاوت -> برای تولید کلید AES-128 جدا از
 * fingerprint ضد-کپی (مثلاً برای OTFDEC روی دیتای QSPI)، تا اگه یکی لو رفت اون یکی عوض نشه. */
void DeviceLock_DeriveKey(const uint32_t uid[3], uint32_t domain, uint32_t key[4]);

/* بررسی اصلی: EXPECTED_FP کامپایل‌شده را با fingerprint این چیپ مقایسه می‌کند.
 * fp_out (اختیاری، می‌تونه NULL باشه) همیشه با fingerprint محاسبه‌شده پر می‌شه -- فقط توی حالت
 * UNPROVISIONED لازمش داری تا رو LCD نشونش بدی و توی سورس کپی کنی. */
DevLock_Result_t DeviceLock_Check(uint32_t fp_out[4]);

#endif

#include "app.h"
#include <math.h>
#include "ui.h"
#include "lcd.h"
#include "leds.h"
#include "adc_probe.h"
#include "probe_hw.h"
#include "board_map.h"
#include "statusbar.h"
#include "video_player.h"
#include "keypad.h"
#include "menu.h"
#include "carousel.h"
#include "pages_assets.h"
#include <stdio.h>
#include <string.h>

extern ADC_HandleTypeDef hadc1;

#define TICK_DUE(last, ms)  ((HAL_GetTick() - (last)) >= (ms))

static void beep_ms(uint32_t ms)
{
    HAL_GPIO_WritePin(Buzzer_GPIO_Port, Buzzer_Pin, GPIO_PIN_SET);
    HAL_Delay(ms);
    HAL_GPIO_WritePin(Buzzer_GPIO_Port, Buzzer_Pin, GPIO_PIN_RESET);
}

/* ================================================================== */
/*  Voltmeter + signal shape                                           */
/*   بالا: ولتاژ DC + Hz و Duty ، وسط: شکل موج ، پایین: L / A / H       */
/* ================================================================== */
#define VM_BOX_X      4U
#define VM_BOX_Y      98U
#define VM_BOX_W      232U
#define VM_BOX_H      70U
#define VM_IX         (VM_BOX_X + 1U)
#define VM_IY         (VM_BOX_Y + 1U)
#define VM_IW         (VM_BOX_W - 2U)          /* 230 ستون */
#define VM_IH         (VM_BOX_H - 2U)          /* 68 پیکسل */
#define VM_N          2048U                    /* تعداد نمونه‌ی هر فریم */
#define VM_TRIG_ZONE  512U                     /* ماشه توی این بخش اول دنبال لبه‌ی بالارونده می‌گرده */
#define VM_SHOW       (VM_N - VM_TRIG_ZONE)    /* نمونه‌هایی که رسم می‌شن */
#define VM_BG         0x0006U                  /* آبی خیلی تیره */
#define VM_GRID       0x31A6U
#define VM_TRACE      0xFFE0U                  /* خط زرد مثل ویدیو */
#define VM_SIG_SPAN_V 0.8f                     /* اختلاف Hi-Lo بیشتر از این = سیگنال */
/* --- بالای فایل، کنار بقیه‌ی static های اهم‌متر --- */
#define OHM_FILT_N 5U
static float   s_ohmHist[OHM_FILT_N];
static uint8_t s_ohmHistCount  = 0U;
static uint8_t s_ohmHistPos    = 0U;
static uint8_t s_ohmWasOpen    = 1U;
static uint8_t s_ohmLastRange  = 0xFFU;

static float ohm_median(const float *a, uint8_t n)
{
    float tmp[OHM_FILT_N];
    for (uint8_t i = 0U; i < n; i++) tmp[i] = a[i];
    for (uint8_t i = 1U; i < n; i++)
    {
        float key = tmp[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && tmp[j] > key) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = key;
    }
    return tmp[n / 2U];
}
static uint16_t s_vmBuf[VM_N];

/* کلاک واقعی CPU (اندازه‌گیری مستقل با HSI) ؛ اگه اندازه‌گیری نشد یا غیرمنطقی بود همون SystemCoreClock */
static uint32_t s_cpuRealHz = 0U;
static uint32_t cpu_hz(void) { return s_cpuRealHz ? s_cpuRealHz : SystemCoreClock; }


static float vm_counts_to_vin(uint32_t c)
{
    return Adc_PinToVin((float)c * 3.3f / 65535.0f);
}

static float vm_pick_scale(float vmax)
{
    static const float k[] = { 5.0f, 10.0f, 15.0f, 20.0f, 30.0f, 45.0f };
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(k) / sizeof(k[0])); i++)
        if (vmax * 1.05f <= k[i]) return k[i];
    return 45.0f;
}

static uint16_t vm_y(uint32_t counts, float vtop)
{
    float f = vm_counts_to_vin(counts) / vtop;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return (uint16_t)(VM_IY + (VM_IH - 1U) - (uint16_t)(f * (float)(VM_IH - 1U)));
}

/* پس‌زمینه‌ی [ya, yb) از ستون x، با خط‌چین شبکه */
static void vm_col_bg(uint16_t x, uint16_t ya, uint16_t yb)
{
    for (uint16_t g = 1U; g < 4U; g++)
    {
        uint16_t gy = (uint16_t)(VM_IY + (VM_IH * g) / 4U);
        if (gy >= ya && gy < yb)
        {
            if (gy > ya) LCD_FillRect(x, ya, 1U, (uint16_t)(gy - ya), VM_BG);
            LCD_FillRect(x, gy, 1U, 1U, (x & 3U) ? VM_BG : VM_GRID);
            ya = (uint16_t)(gy + 1U);
        }
    }
    if (ya < yb) LCD_FillRect(x, ya, 1U, (uint16_t)(yb - ya), VM_BG);
}

static void vm_draw_wave(uint32_t start, float vtop, uint32_t nShow)
{
    const uint16_t yMin = VM_IY, yMax = (uint16_t)(VM_IY + VM_IH - 1U);
    uint16_t prevY = 0xFFFFU;

    for (uint16_t cx = 0U; cx < VM_IW; cx++)
    {
        uint32_t a = start + ((uint32_t)cx * nShow) / VM_IW;
        uint32_t b = start + ((uint32_t)(cx + 1U) * nShow) / VM_IW;
        if (b <= a) b = a + 1U;
        if (b > VM_N) b = VM_N;
        if (a >= VM_N) a = VM_N - 1U;

        uint16_t lo = 65535U, hi = 0U;
        for (uint32_t i = a; i < b; i++)
        {
            if (s_vmBuf[i] < lo) lo = s_vmBuf[i];
            if (s_vmBuf[i] > hi) hi = s_vmBuf[i];
        }
        uint16_t yTop = vm_y(hi, vtop);          /* ولتاژ بیشتر = y کمتر */
        uint16_t yBot = vm_y(lo, vtop);
        if (prevY != 0xFFFFU)                    /* لبه‌ی عمودی بین دو ستون وصل بشه */
        {
            if (prevY < yTop) yTop = prevY;
            if (prevY > yBot) yBot = prevY;
        }
        if (yBot <= yTop)                        /* خط حداقل ۲ پیکسل ضخامت */
        {
            if (yBot < yMax) yBot++; else yTop--;
        }
        prevY = vm_y(s_vmBuf[b - 1U], vtop);

        uint16_t x = (uint16_t)(VM_IX + cx);
        vm_col_bg(x, yMin, yTop);
        LCD_FillRect(x, yTop, 1U, (uint16_t)(yBot - yTop + 1U), VM_TRACE);
        vm_col_bg(x, (uint16_t)(yBot + 1U), (uint16_t)(yMax + 1U));
    }
}

/* فرکانس و duty از لبه‌های بالارونده (با هیسترزیس ۴۰٪/۶۰٪) */
static uint8_t vm_analyze(uint16_t mn, uint16_t mx, uint32_t cyc, float *hz, float *duty, uint8_t *tooFast)
{
    *tooFast = 0U;
    uint32_t hi = (uint32_t)mn + ((uint32_t)(mx - mn) * 6U) / 10U;
    uint32_t lo = (uint32_t)mn + ((uint32_t)(mx - mn) * 4U) / 10U;
    uint8_t  high = (s_vmBuf[0] >= hi) ? 1U : 0U;
    uint32_t rises = 0U, first = 0U, last = 0U, hcnt = 0U, hlast = 0U;

    for (uint32_t i = 1U; i < VM_N; i++)
    {
        uint32_t v = s_vmBuf[i];
        if (!high && v >= hi)
        {
            high = 1U;
            rises++;
            if (rises == 1U) { first = i; hcnt = 0U; }
            else             { last = i;  hlast = hcnt; }
        }
        else if (high && v <= lo)
        {
            high = 0U;
        }
        if (rises >= 1U && high) hcnt++;
    }
    if (rises < 2U || last <= first) return 0U;

    float dt = ((float)cyc / (float)cpu_hz()) / (float)VM_N;
    float period_samples = (float)(last - first) / (float)(rises - 1U);
    if (period_samples < 4.0f) { *tooFast = 1U; return 0U; }   /* خیلی سریع: قابل اعتماد نیست */
    *hz   = 1.0f / (period_samples * dt);
    *duty = 100.0f * (float)hlast / (float)(last - first);
    return 1U;
}

static void vm_fmt_time(char *dst, size_t n, float s)
{
    char t[12];
    if (s < 1.0e-3f)      { UI_FmtFixed(t, sizeof t, s * 1.0e6f, 0U); snprintf(dst, n, "%sus", t); }
    else if (s < 1.0f)    { UI_FmtFixed(t, sizeof t, s * 1.0e3f, 1U); snprintf(dst, n, "%sms", t); }
    else                  { UI_FmtFixed(t, sizeof t, s, 1U);          snprintf(dst, n, "%ss",  t); }
}

/* حالت AUTO: بازه‌ی زمانی خودکار از روی فرکانس سیگنال تنظیم می‌شه تا توی زوم ۱x
 * حدود VM_AUTO_PERIODS پریود دیده بشه (دیگه موج مربعی/PWM شبیه سوزن نمی‌شه). */
#define VM_AUTO_PERIODS  5.0f
#define VM_AUTO_DEF_US   10U
#define VM_AUTO_MAX_US   200U

static void App_Voltmeter(void)
{
    UI_Begin("VOLTMETER");
    UI_Hint("OK zero(2x rst) L/R zoom U/D rate");
    LCD_FillRect(VM_BOX_X, VM_BOX_Y, VM_BOX_W, VM_BOX_H, UI_Theme()->accent);
    LCD_FillRect(VM_IX, VM_IY, VM_IW, VM_IH, VM_BG);

    /* بالا/پایین = فاصله‌ی نمونه‌ها (باد ریت). tb = 0 -> AUTO ، tb = 1.. -> k_tbUs[tb-1] (µs اضافه) */
    static const uint16_t k_tbUs[] = { 0U, 2U, 5U, 10U, 20U, 50U, 100U, 200U };
    const uint8_t TB_N = (uint8_t)(sizeof(k_tbUs) / sizeof(k_tbUs[0]));
    uint8_t  tb        = 0U;                 /* پیش‌فرض: AUTO */
    uint32_t autoGapUs = VM_AUTO_DEF_US;

    /* چپ/راست = زوم افقی: چپ = جمع (پریود بیشتر توی صفحه) ، راست = باز (جزئیات بیشتر) */
    static const uint8_t k_zoom[] = { 1U, 2U, 4U, 8U, 16U };
    const uint8_t ZN = (uint8_t)(sizeof(k_zoom) / sizeof(k_zoom[0]));
    uint8_t zi = 1U;                         /* پیش‌فرض ۲x، تا هر دو طرف جا داشته باشه */

    char msg[24] = "";
    uint32_t msgUntil = 0U;
    uint32_t lastOk = 0U - 5000U;
    uint32_t last = 0U - 1000U;
    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_SELECT)
        {
            if ((HAL_GetTick() - lastOk) < 800U)          /* دو بار پشت‌سرهم = ریست صفر */
            {
                Adc_ResetZero();
                snprintf(msg, sizeof msg, "Zero reset");
            }
            else                                           /* پروب‌ها باید روی GND باشن */
            {
                snprintf(msg, sizeof msg, "%s", Adc_CalibrateZero() ? "Zero set" : "Not zero!");
            }
            lastOk = HAL_GetTick();
            msgUntil = HAL_GetTick() + 1500U;
            last = 0U - 1000U;
        }
        if (in == MENU_INPUT_LEFT  && zi > 0U)        { zi--; last = 0U - 1000U; }
        if (in == MENU_INPUT_RIGHT && zi + 1U < ZN)   { zi++; last = 0U - 1000U; }
        /* بالا = بازه‌ی زمانی بزرگ‌تر (نمونه‌ها کندتر) ، پایین = کوچک‌تر (سریع‌تر) */
        if (in == MENU_INPUT_UP   && tb < TB_N) { tb++; last = 0U - 1000U; }
        if (in == MENU_INPUT_DOWN && tb > 0U)   { tb--; last = 0U - 1000U; }
        if (!TICK_DUE(last, 200U)) continue;
        last = HAL_GetTick();

        char t[16], b[32];

        /* --- ولتاژ DC (میانگین کند و دقیق) --- */
        float vin = Adc_GetVin();
        LEDs_UpdateVoltage(vin);
        UI_FmtFixed(t, sizeof t, vin, 2U);
        snprintf(b, sizeof b, "%sV", t);
        {
            char pad[8];
            snprintf(pad, sizeof pad, "%-6s", b);
            LCD_DrawString(4U, 54U, pad, UI_OK, UI_BG, 3U);
        }

        /* --- نمونه‌برداری سریع از PC4 --- */
        uint32_t gapUs = (tb == 0U) ? autoGapUs : (uint32_t)k_tbUs[tb - 1U];
        uint32_t gap = (SystemCoreClock / 1000000U) * gapUs;
        uint32_t cyc = Adc_CapturePC4(s_vmBuf, VM_N, gap);
        if (cyc != 0U)
        {
            uint32_t mn = 65535U, mx = 0U, sum = 0U;
            for (uint32_t i = 0U; i < VM_N; i++)
            {
                uint32_t v = s_vmBuf[i];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
                sum += v;
            }
            float vLo  = vm_counts_to_vin(mn);
            float vHi  = vm_counts_to_vin(mx);
            float vAvg = vm_counts_to_vin(sum / VM_N);
            uint8_t sig = ((vHi - vLo) > VM_SIG_SPAN_V) ? 1U : 0U;

            /* ماشه: اولین لبه‌ی بالارونده‌ی وسط سطح */
            uint32_t start = 0U;
            if (sig)
            {
                uint32_t mid = (mn + mx) / 2U;
                for (uint32_t i = 1U; i < VM_TRIG_ZONE; i++)
                {
                    if (s_vmBuf[i - 1U] < mid && s_vmBuf[i] >= mid) { start = i; break; }
                }
            }

            float vtop = vm_pick_scale(vHi);
            uint32_t nShow = VM_SHOW / (uint32_t)k_zoom[zi];       /* تعداد نمونه‌ی قابل‌مشاهده بعد از زوم */
            vm_draw_wave(start, vtop, nShow);
            LED_Blue_Set(sig);

            /* --- Hz / Duty (بالا سمت راست) --- */
            float hz = 0.0f, duty = 0.0f;
            uint8_t tooFast = 0U;
            uint8_t fok = sig ? vm_analyze((uint16_t)mn, (uint16_t)mx, cyc, &hz, &duty, &tooFast) : 0U;

            /* --- AUTO: فاصله‌ی نمونه‌ها رو برای فریم بعدی تنظیم کن --- */
            if (tb == 0U)
            {
                if (!sig)
                {
                    autoGapUs = VM_AUTO_DEF_US;
                }
                else if (fok)
                {
                    float dtS   = ((float)cyc / (float)SystemCoreClock) / (float)VM_N;   /* فاصله‌ی واقعی نمونه‌ها */
                    float baseS = dtS - (float)gapUs * 1.0e-6f;                           /* بدون فاصله‌ی اضافه */
                    if (baseS < 1.0e-7f) baseS = 1.0e-7f;
                    float wantS = VM_AUTO_PERIODS / (hz * (float)VM_SHOW);
                    float g = (wantS - baseS) * 1.0e6f;
                    if (g < 0.0f) g = 0.0f;
                    if (g > (float)VM_AUTO_MAX_US) g = (float)VM_AUTO_MAX_US;
                    uint32_t gn   = (uint32_t)(g + 0.5f);
                    uint32_t diff = (gn > autoGapUs) ? (gn - autoGapUs) : (autoGapUs - gn);
                    if (diff * 4U > autoGapUs + 2U) autoGapUs = gn;                      /* فقط اگه >۲۵٪ فرق کرد */
                }
                else if (tooFast)
                {
                    autoGapUs = 0U;                                                       /* سریع‌ترین نمونه‌برداری */
                }
                else                                                                      /* لبه‌ی کافی نبود: سیگنال کند */
                {
                    autoGapUs = autoGapUs * 2U + 10U;
                    if (autoGapUs > VM_AUTO_MAX_US) autoGapUs = VM_AUTO_MAX_US;
                }
            }

            char f7[10], d7[10];
            if (fok)
            {
                if (hz < 1000.0f)       { UI_FmtFixed(t, sizeof t, hz, 0U);            snprintf(b, sizeof b, "%sHz", t); }
                else if (hz < 1.0e6f)   { UI_FmtFixed(t, sizeof t, hz / 1000.0f, 1U);  snprintf(b, sizeof b, "%skHz", t); }
                else                    { UI_FmtFixed(t, sizeof t, hz / 1.0e6f, 1U);   snprintf(b, sizeof b, "%sMHz", t); }
                snprintf(f7, sizeof f7, "%-7s", b);
                UI_FmtFixed(t, sizeof t, duty, 0U);
                snprintf(b, sizeof b, "D%s%%", t);
                snprintf(d7, sizeof d7, "%-7s", b);
            }
            else
            {
                snprintf(f7, sizeof f7, "%-7s", "--Hz");
                snprintf(d7, sizeof d7, "%-7s", "D--%");
            }
            LCD_DrawString(136U, 52U, f7, fok ? UI_CYAN : UI_DIM, UI_BG, 2U);
            LCD_DrawString(136U, 74U, d7, fok ? UI_ORANGE : UI_DIM, UI_BG, 2U);

            /* --- L / A / H (زیر نمودار) --- */
            UI_FmtFixed(t, sizeof t, vLo, 1U);   snprintf(b, sizeof b, "L%s", t);  snprintf(f7, sizeof f7, "%-5s", b);
            LCD_DrawString(4U,   172U, f7, UI_CYAN,   UI_BG, 2U);
            UI_FmtFixed(t, sizeof t, vAvg, 1U);  snprintf(b, sizeof b, "A%s", t);  snprintf(f7, sizeof f7, "%-5s", b);
            LCD_DrawString(82U,  172U, f7, UI_WHITE,  UI_BG, 2U);
            UI_FmtFixed(t, sizeof t, vHi, 1U);   snprintf(b, sizeof b, "H%s", t);  snprintf(f7, sizeof f7, "%-5s", b);
            LCD_DrawString(160U, 172U, f7, UI_ORANGE, UI_BG, 2U);

            /* --- خط اطلاعات ریز: PC4 ، صفر ، مقیاس ، پهنای زمانیِ نمودار --- */
            char s[12];
            UI_FmtFixed(t, sizeof t, Adc_GetPC4(), 3U);
            snprintf(b, sizeof b, "PC4 %s", t);   snprintf(s, sizeof s, "%-10s", b);
            LCD_DrawString(4U, 196U, s, UI_CYAN, UI_BG, 1U);
            UI_FmtFixed(t, sizeof t, Adc_GetZero(), 3U);
            snprintf(b, sizeof b, "Z %s", t);     snprintf(s, sizeof s, "%-8s", b);
            LCD_DrawString(78U, 196U, s, UI_ORANGE, UI_BG, 1U);
            UI_FmtFixed(t, sizeof t, vtop, 0U);
            snprintf(b, sizeof b, "0-%sV", t);    snprintf(s, sizeof s, "%-6s", b);
            LCD_DrawString(138U, 196U, s, UI_DIM, UI_BG, 1U);
            vm_fmt_time(b, sizeof b, ((float)cyc / (float)SystemCoreClock) / (float)VM_N * (float)nShow);
            snprintf(s, sizeof s, "%-8s", b);
            LCD_DrawString(184U, 196U, s, UI_MAGENTA, UI_BG, 1U);
        }

        /* پیام موقت (Zero set ...) یا وضعیت بازه‌ی زمانی و زوم */
        if (HAL_GetTick() < msgUntil)
        {
            UI_Text(4U, 207U, 2U, UI_TITLE, UI_BG, msg);
        }
        else
        {
            char info[24];
            if (tb == 0U) snprintf(info, sizeof info, "TB:AUTO Z:x%u", (unsigned)k_zoom[zi]);
            else          snprintf(info, sizeof info, "TB:%uus Z:x%u", (unsigned)k_tbUs[tb - 1U], (unsigned)k_zoom[zi]);
            UI_Text(4U, 207U, 2U, UI_DIM, UI_BG, info);
        }
    }
    LED_Blue_Set(0);
    LEDs_AllOff();
}

/* ================================================================== */
/*  Ohmmeter                                                           */
/* ================================================================== */
static const OhmRange_t k_ohmRanges[OHM_RANGES] = OHM_RANGE_TABLE;

/* مقاومت سری مسیر Probe2 -> GND (R35 + بافر)؛ از هر اندازه‌گیری کم می‌شه */
static float s_ohmSeries = OHM_SERIES_DEFAULT;

/* نشتی نود Probe1 (uA) به‌صورت تابعی از ولتاژ نود: درون‌یابی خطی روی جدول board_map.h */
typedef struct { float v; float ua; } OhmLeak_t;
static const OhmLeak_t k_ohmLeak[OHM_LEAK_POINTS] = OHM_LEAK_TABLE;
static float s_ohmLeakUA = 0.0f;                   /* آخرین نشتی اعمال‌شده (برای نمایش) */

static float ohm_leak_ua(float v)
{
    if (v <= k_ohmLeak[0].v) return k_ohmLeak[0].ua;
    for (uint8_t i = 1U; i < OHM_LEAK_POINTS; i++)
    {
        if (v <= k_ohmLeak[i].v)
        {
            float f = (v - k_ohmLeak[i - 1U].v) / (k_ohmLeak[i].v - k_ohmLeak[i - 1U].v);
            return k_ohmLeak[i - 1U].ua + f * (k_ohmLeak[i].ua - k_ohmLeak[i - 1U].ua);
        }
    }
    /* بالاتر از آخرین نقطه: با شیب آخر ادامه بده */
    const OhmLeak_t *p = &k_ohmLeak[OHM_LEAK_POINTS - 2U], *q = &k_ohmLeak[OHM_LEAK_POINTS - 1U];
    return q->ua + (v - q->v) * (q->ua - p->ua) / (q->v - p->v);
}

/* Rx (قبل از کم‌کردن مقاومت سری) برحسب اهم؛ منفی = OPEN. vpin = ولتاژ واقعی Probe1.
 * ⚠ PB1 (ADC1_IN5) از طریق C46 خازنی کوپل شده و DC رو نمی‌بینه (فقط برای سیگنال AC) ؛
 *   پس ولتاژ DC پروب از PB0 (قبل از OPAMP) خونده می‌شه. */
static float ohm_raw(uint8_t r, float *vpin)
{
    Probe_OhmSelect(r);                             /* Probe2 = 0V ، پول‌آپ 3.3V با Rref روی Probe1 */
    HAL_Delay(15);                                   /* صبر برای نشستن سوئیچ‌ها */
    float v = Adc_GetProbe1Dc();
    if (v < 0.0f) v = 0.0f;
    *vpin = v;
    if (v >= OHM_VREF * OHM_OPEN_FRACTION) return -1.0f;
    const float itot = (OHM_VREF - v) / k_ohmRanges[r].rref;      /* کل جریان از Rref */
    const float ileak = ohm_leak_ua(v) * 1.0e-6f;                 /* نشتی زنر در این ولتاژ نود */
    s_ohmLeakUA = ileak * 1.0e6f;
    float irx = itot - ileak;                                    /* جریان واقعی توی Rx(+مقاومت سری) */
    if (irx <= 0.03f * itot || irx <= 1.0e-9f) return -1.0f;     /* عملاً باز / غیرقابل‌اعتماد */
    float rx = v / irx;
    return (rx > OHM_MAX_OHM) ? -1.0f : rx;
}

/* Rx برحسب اهم؛ منفی = OPEN */
static float ohm_measure(uint8_t r, float *vpin)
{
    float rx = ohm_raw(r, vpin);
    if (rx < 0.0f) return rx;
    rx -= s_ohmSeries;
    return (rx < 0.0f) ? 0.0f : rx;
}

static void fmt_ohm(char *dst, size_t n, float r)
{
    char t[16];
    if (r < 0.0f)            { snprintf(dst, n, "OPEN"); }
    else if (r < 1000.0f)    { UI_FmtFixed(t, sizeof t, r, 1);           snprintf(dst, n, "%s ohm", t); }
    else if (r < 1.0e6f)     { UI_FmtFixed(t, sizeof t, r / 1000.0f, 2); snprintf(dst, n, "%s%s k", (r > OHM_RELIABLE_MAX) ? "~" : "", t); }
    else                     { UI_FmtFixed(t, sizeof t, r / 1.0e6f, 2);  snprintf(dst, n, "%s M", t); }
}

static void App_Ohmmeter(void)
{
    LEDs_AllOff();
    UI_Begin("OHMMETER");
    UI_Hint("L/R range OK zero(short) BACK");

    uint8_t mode = 0U;                              /* 0 = AUTO ، 1.. = رنج ثابت */
    uint32_t last = 0U - 1000U;
    char zmsg[20] = "";
    uint32_t zmsgUntil = 0U;
    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_SELECT)                /* پروب‌ها رو بهم بزن: مقاومت سری رو اندازه می‌گیره */
        {
            float vz, rz = ohm_raw(0U, &vz);        /* رنج 1k */
            if (rz >= 0.0f && rz < 600.0f)
            {
                s_ohmSeries = rz;
                snprintf(zmsg, sizeof zmsg, "Zero set");
            }
            else
            {
                snprintf(zmsg, sizeof zmsg, "Short probes!");
            }
            zmsgUntil = HAL_GetTick() + 1500U;
        }
        if (in == MENU_INPUT_RIGHT) mode = (uint8_t)((mode + 1U) % (OHM_RANGES + 1U));
        if (in == MENU_INPUT_LEFT)  mode = (uint8_t)((mode + OHM_RANGES) % (OHM_RANGES + 1U));
        if (in != MENU_INPUT_NONE) last = 0U - 1000U;
        if (!TICK_DUE(last, 300U)) continue;
        last = HAL_GetTick();

        uint8_t r = (mode == 0U) ? ((s_ohmLastRange < OHM_RANGES) ? s_ohmLastRange : (uint8_t)(OHM_RANGES - 1U)) : (uint8_t)(mode - 1U);
        float vp, rx = ohm_measure(r, &vp);

        if (mode == 0U)
        {
            /* AUTO: از رنج بالا (۱۰k) شروع می‌شه (ولتاژ نود کمتر = نشتی زنر کمتر = دقت بیشتر) و فقط برای
             * مقاومت‌های کوچیک می‌ره رنج ۱k. هیسترزیس: وقتی روی ۱k هستیم تا ۱٫۲×آستانه همون‌جا می‌مونیم. */
            const float thr = (s_ohmLastRange == 0U) ? OHM_AUTO_LOW_OHM * 1.2f : OHM_AUTO_LOW_OHM;
            if (r > 0U && rx >= 0.0f && rx < thr) { r = 0U; rx = ohm_measure(r, &vp); }
            else if (r == 0U && (rx < 0.0f || rx >= thr)) { r = (uint8_t)(OHM_RANGES - 1U); rx = ohm_measure(r, &vp); }
            uint8_t isOpenNow = (rx < 0.0f);
            if (isOpenNow != s_ohmWasOpen || r != s_ohmLastRange)
            {
                s_ohmHistCount = 0U;
                s_ohmHistPos   = 0U;
                s_ohmWasOpen   = isOpenNow;
                s_ohmLastRange = r;
            }
            if (!isOpenNow)
            {
                s_ohmHist[s_ohmHistPos] = rx;
                s_ohmHistPos = (uint8_t)((s_ohmHistPos + 1U) % OHM_FILT_N);
                if (s_ohmHistCount < OHM_FILT_N) s_ohmHistCount++;
                rx = ohm_median(s_ohmHist, s_ohmHistCount);
            }
        }

        char b[32], t[16];
        fmt_ohm(b, sizeof b, rx);
        UI_Text(4U, 56U, 3U, (rx < 0.0f) ? UI_BAD : UI_OK, UI_BG, b);

        snprintf(b, sizeof b, "Range %s", (mode == 0U) ? "AUTO" : "FIXED");
        UI_Row(3U, UI_FG, UI_BG, b);
        snprintf(b, sizeof b, "%s", k_ohmRanges[r].name);
        UI_Row(4U, UI_FG, UI_BG, b);
        UI_FmtFixed(t, sizeof t, vp, 3);
        snprintf(b, sizeof b, "P1 %s V", t);
        UI_Row(5U, UI_CYAN, UI_BG, b);
        UI_FmtFixed(t, sizeof t, Adc_ReadPinVolts(&hadc1, ADC_CHANNEL_3), 3);   /* PA6 = Probe2 ، باید ~0 باشه */
        snprintf(b, sizeof b, "P2 pin %s V", t);
        UI_Row(6U, UI_ORANGE, UI_BG, b);
        { char tl[12]; UI_FmtFixed(tl, sizeof tl, s_ohmLeakUA, 1);
          snprintf(b, sizeof b, "Leak %s uA", tl); }                 /* نشتی زنر که از جریان کم شده (برای کالیبراسیون جدول) */
        UI_Row(2U, UI_DIM, UI_BG, b);
        if (HAL_GetTick() < zmsgUntil)
        {
            UI_Row(7U, UI_TITLE, UI_BG, zmsg);
        }
        else
        {
            UI_FmtFixed(t, sizeof t, s_ohmSeries, 0);
            snprintf(b, sizeof b, "Offset %s ohm", t);
            UI_Row(7U, UI_DIM, UI_BG, b);
        }
    }
    Probe_OhmOff();
}

/* ================================================================== */
/*  PWM generate / read                                                */
/* ================================================================== */
static const uint32_t k_freqs[] = { 10U, 50U, 100U, 500U, 1000U, 2000U, 5000U, 10000U, 20000U };
#define FREQ_COUNT  (sizeof(k_freqs) / sizeof(k_freqs[0]))

static void App_Pwm(void)
{
    LEDs_AllOff();
    UI_Begin("PWM GEN/READ");
    UI_Hint("U/D sel L/R chg OK run");

    ProbeId_t out  = PROBE_1;
    uint8_t   fi   = 4U;                 /* 1 kHz */
    uint8_t   duty = 50U;
    ReadSrc_t rd   = RD_DEFAULT_FOR_PROBE2;
    uint8_t   run  = 0U;
    uint8_t   sel  = 0U;
    uint8_t   redraw = 1U;
    uint32_t  last = 0U - 1000U;

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;

        int8_t d = 0;
        if (in == MENU_INPUT_UP)   sel = (sel == 0U) ? 4U : (uint8_t)(sel - 1U);
        if (in == MENU_INPUT_DOWN) sel = (uint8_t)((sel + 1U) % 5U);
        if (in == MENU_INPUT_RIGHT) d = 1;
        if (in == MENU_INPUT_LEFT)  d = -1;
        uint8_t apply = 0U;

        if (in == MENU_INPUT_SELECT) { run = !run; apply = 1U; }
        if (d != 0)
        {
            switch (sel)
            {
            case 0:
                out = (out == PROBE_1) ? PROBE_2 : PROBE_1;
                rd  = (out == PROBE_1) ? RD_DEFAULT_FOR_PROBE2 : RD_DEFAULT_FOR_PROBE1;  /* پروب دیگه می‌خونه */
                apply = 1U; break;
            case 1:
                fi = (d > 0) ? (uint8_t)((fi + 1U) % FREQ_COUNT) : (uint8_t)((fi + FREQ_COUNT - 1U) % FREQ_COUNT);
                apply = 1U; break;
            case 2:
                if (d > 0) duty = (duty >= 95U) ? 100U : (uint8_t)(duty + 5U);
                else       duty = (duty <= 5U)  ? 0U   : (uint8_t)(duty - 5U);
                apply = 1U; break;
            case 3:
                rd = (d > 0) ? (ReadSrc_t)((rd + 1) % RD_COUNT) : (ReadSrc_t)((rd + RD_COUNT - 1) % RD_COUNT);
                break;
            case 4:
                run = !run; apply = 1U; break;
            default: break;
            }
        }
        if (apply)
        {
            if (run) Probe_PwmStart(out, k_freqs[fi], duty);
            else     Probe_PwmStop();
        }
        if (in != MENU_INPUT_NONE) redraw = 1U;

        if (redraw)
        {
            redraw = 0U;
            char b[32];
            snprintf(b, sizeof b, "Out : %s", (out == PROBE_1) ? "PROBE1" : "PROBE2");
            UI_Row(0U, UI_FG, (sel == 0U) ? UI_SEL_BG : UI_BG, b);
            snprintf(b, sizeof b, "Freq: %lu Hz", (unsigned long)k_freqs[fi]);
            UI_Row(1U, UI_FG, (sel == 1U) ? UI_SEL_BG : UI_BG, b);
            snprintf(b, sizeof b, "Duty: %u %%", (unsigned)duty);
            UI_Row(2U, UI_FG, (sel == 2U) ? UI_SEL_BG : UI_BG, b);
            snprintf(b, sizeof b, "Read: %s", Probe_ReadName(rd));
            UI_Row(3U, UI_FG, (sel == 3U) ? UI_SEL_BG : UI_BG, b);
            snprintf(b, sizeof b, "Run : %s", run ? "ON" : "OFF");
            UI_Row(4U, run ? UI_OK : UI_FG, (sel == 4U) ? UI_SEL_BG : UI_BG, b);
        }

        if (TICK_DUE(last, 300U))
        {
            last = HAL_GetTick();
            SigStat_t s;
            Probe_Measure(rd, &s);

            LED_Blue_Set(s.signal);

            char b[32], t1[12], t2[12];
            UI_FmtFixed(t1, sizeof t1, s.vmax, 2);
            UI_FmtFixed(t2, sizeof t2, s.vmin, 2);
            snprintf(b, sizeof b, "Hi%s Lo%s", t1, t2);
            UI_Row(5U, UI_FG, UI_BG, b);

            if (s.signal)
            {
                UI_FmtFixed(t1, sizeof t1, s.duty_pct, 1);
                snprintf(b, sizeof b, "Duty %s %%", t1);
                UI_Row(6U, UI_ORANGE, UI_BG, b);
                if (s.freq_ok)       snprintf(b, sizeof b, "Freq %lu Hz", (unsigned long)s.freq_hz);
                else if (s.too_fast) snprintf(b, sizeof b, "Freq too high");
                else                 snprintf(b, sizeof b, "Freq ---");
                UI_Row(7U, UI_CYAN, UI_BG, b);
                UI_Row(8U, 0x07FFU, UI_BG, "SIGNAL");     /* cyan/blue indicator */
            }
            else
            {
                UI_FmtFixed(t1, sizeof t1, s.vavg, 2);
                snprintf(b, sizeof b, "DC %s V", t1);
                UI_Row(6U, UI_FG, UI_BG, b);
                UI_Row(7U, UI_FG, UI_BG, "");
                UI_Row(8U, UI_DIM, UI_BG, "no signal");
            }
        }
    }
    LED_Blue_Set(0);
    Probe_PwmStop();
}

/* ================================================================== */
/*  DAC out                                                            */
/* ================================================================== */
static void App_Dac(void)
{
    LEDs_AllOff();
    UI_Begin("DAC2 SINE");
    UI_Hint("U/D freq L/R amp OK on/off");

    uint32_t freq = 50U;
    float amplitude = 0.50f;
    const float offset = 1.65f;
    uint8_t running = 0U;
    uint8_t redraw = 1U;
    uint32_t last = 0U - 1000U;

    Probe_DacOff();
    running = Probe_DacSineStart(freq, amplitude, offset);

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;

        if (in == MENU_INPUT_UP)
        {
            if (freq < 200U) freq += 1U;
            if (running) { Probe_DacSineStop(); running = Probe_DacSineStart(freq, amplitude, offset); }
            redraw = 1U;
        }
        if (in == MENU_INPUT_DOWN)
        {
            if (freq > 1U) freq -= 1U;
            if (running) { Probe_DacSineStop(); running = Probe_DacSineStart(freq, amplitude, offset); }
            redraw = 1U;
        }
        if (in == MENU_INPUT_RIGHT)
        {
            amplitude += 0.05f;
            if (amplitude > 0.75f) amplitude = 0.75f;
            if (running) { Probe_DacSineStop(); running = Probe_DacSineStart(freq, amplitude, offset); }
            redraw = 1U;
        }
        if (in == MENU_INPUT_LEFT)
        {
            amplitude -= 0.05f;
            if (amplitude < 0.10f) amplitude = 0.10f;
            if (running) { Probe_DacSineStop(); running = Probe_DacSineStart(freq, amplitude, offset); }
            redraw = 1U;
        }
        if (in == MENU_INPUT_SELECT)
        {
            if (running) { Probe_DacSineStop(); running = 0U; }
            else running = Probe_DacSineStart(freq, amplitude, offset);
            redraw = 1U;
        }

        if (redraw || TICK_DUE(last, 300U))
        {
            redraw = 0U;
            last = HAL_GetTick();
            char b[40], t[16];
            snprintf(b, sizeof b, "Output: Probe2 / DAC2"); UI_Row(0U, UI_CYAN, UI_BG, b);
            snprintf(b, sizeof b, "Frequency: %lu Hz", (unsigned long)freq); UI_Row(1U, UI_FG, UI_BG, b);
            UI_FmtFixed(t, sizeof t, amplitude, 2);
            snprintf(b, sizeof b, "Amplitude: +/- %s V", t); UI_Row(2U, UI_FG, UI_BG, b);
            UI_Row(3U, running ? UI_OK : UI_FG, UI_BG, running ? "SINE RUNNING" : "OUTPUT STOPPED");
            UI_Row(5U, UI_DIM, UI_BG, "Offset: 1.65 V");
            UI_Row(6U, UI_DIM, UI_BG, "Probe1 -> Scope / PC4");
        }
    }
    Probe_DacOff();
}

/* ================================================================== */
/*  GPIO test                                                          */
/* ================================================================== */
typedef struct { const char *name; GPIO_TypeDef *port; uint16_t pin; } TestPin_t;
static const TestPin_t k_testPins[] = {
    { "PC0  OE-P1", OE_P1_PORT, OE_P1_PIN },
    { "PA3  OE-P2", OE_P2_PORT, OE_P2_PIN },
    { "PE6  OE-X1", OE_X1_PORT, OE_X1_PIN },
    { "PC13 OE-X2", OE_X2_PORT, OE_X2_PIN },
    { "PE11 DAC1 ", SW_DAC1_PORT, SW_DAC1_PIN },
    { "PB10 DAC2 ", SW_DAC2_PORT, SW_DAC2_PIN },
    { "PE15 OHM-A", GPIOE, GPIO_PIN_15 },
    { "PE14 OHM-B", GPIOE, GPIO_PIN_14 },
};
#define TESTPIN_COUNT (sizeof(k_testPins) / sizeof(k_testPins[0]))

static void App_GpioTest(void)
{
    LEDs_AllOff();
    UI_Begin("GPIO TEST");
    UI_Hint("U/D sel OK toggle BACK");

    Probe_AllOff();
    uint8_t sel = 0U, redraw = 1U;
    uint32_t last = 0U - 1000U;

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_UP)   { sel = (sel == 0U) ? (uint8_t)(TESTPIN_COUNT - 1U) : (uint8_t)(sel - 1U); redraw = 1U; }
        if (in == MENU_INPUT_DOWN) { sel = (uint8_t)((sel + 1U) % TESTPIN_COUNT); redraw = 1U; }
        if (in == MENU_INPUT_SELECT)
        {
            HAL_GPIO_TogglePin(k_testPins[sel].port, k_testPins[sel].pin);
            redraw = 1U;
        }
        if (redraw)
        {
            redraw = 0U;
            for (uint8_t i = 0U; i < TESTPIN_COUNT; i++)
            {
                char b[32];
                GPIO_PinState st = HAL_GPIO_ReadPin(k_testPins[i].port, k_testPins[i].pin);
                snprintf(b, sizeof b, "%s %s", k_testPins[i].name, (st == GPIO_PIN_SET) ? "H" : "L");
                UI_Row(i, (st == GPIO_PIN_SET) ? UI_OK : UI_FG, (i == sel) ? UI_SEL_BG : UI_BG, b);
            }
        }
        if (TICK_DUE(last, 300U))
        {
            last = HAL_GetTick();
            char b[64], t1[10], t2[10], t3[10];
            UI_FmtFixed(t1, sizeof t1, Adc_ReadPinVolts(&hadc1, ADC_CHANNEL_5), 2);
            UI_FmtFixed(t2, sizeof t2, Adc_ReadPinVolts(&hadc1, ADC_CHANNEL_3), 2);
            UI_FmtFixed(t3, sizeof t3, Adc_ReadPinVolts(&hadc1, ADC_CHANNEL_14), 2);
            snprintf(b, sizeof b, "PB1 %s PA6 %s PA2 %s", t1, t2, t3);
            UI_Text(2U, 214U, 1U, UI_DIM, UI_BG, b);
        }
    }
    Probe_AllOff();
}

/* ================================================================== */
/*  Buzzer / Video                                                     */
/* ================================================================== */
static void App_Buzzer(void) { beep_ms(150U); }

static void App_Video(void)
{
    Video_Menu();                       /* با BACK برمی‌گرده */
    LCD_FillColor(LCD_COLOR_BLACK);
    StatusBar_Init();                   /* نوار وضعیت دوباره کشیده بشه */
    Menu_Redraw();
}

/* ================================================================== */
/*  Key test: raw state of the six keys                                */
/* ================================================================== */
static void App_KeyTest(void)
{
    static const struct { const char *name; MenuInput_t k; } keys[] = {
        { "Enter", MENU_INPUT_SELECT }, { "Back ", MENU_INPUT_BACK },
        { "Up   ", MENU_INPUT_UP },     { "Down ", MENU_INPUT_DOWN },
        { "Left ", MENU_INPUT_LEFT },   { "Right", MENU_INPUT_RIGHT },
    };
    LEDs_AllOff();
    UI_Begin("KEY TEST");
    UI_Hint("hold a key; BACK twice = exit");

    uint8_t backCount = 0U;
    uint32_t lastBack = 0U;
    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK)
        {
            if (backCount && (HAL_GetTick() - lastBack) < 1000U) break;
            backCount = 1U; lastBack = HAL_GetTick();
        }
        for (uint8_t i = 0U; i < 6U; i++)
        {
            char b[32];
            uint8_t p = Keypad_RawPressed(keys[i].k);
            snprintf(b, sizeof b, "%s %s", keys[i].name, p ? "PRESSED" : "-");
            UI_Row(i, p ? UI_OK : UI_FG, UI_BG, b);
        }
    }
}

/* ================================================================== */
/*  LED test: each LED in turn                                         */
/* ================================================================== */
static void App_LedTest(void)
{
    UI_Begin("LED TEST");
    static const char *const names[4] = { "yellow", "blue", "pink", "red" };
    static const uint16_t cols[4]     = { 0xFFE0U, 0x041FU, 0x07E0U, 0xF800U };
    for (uint8_t i = 0U; i < 4U; i++)
    {
        UI_Row(0U, cols[i], UI_BG, names[i]);
        LEDs_AllOff();
        LEDs_Set(i, 1U);
        HAL_Delay(500);
    }
    LEDs_AllOff();
}

/* ================================================================== */
/*  Oscilloscope (با فیلتر نویز)                                       */
/* ================================================================== */
//#define SCOPE_N      4096U
//#define SCOPE_Y0     52U
//#define SCOPE_H      150U
//#define SCOPE_GRID   0x2104U
//#define SCOPE_FILT_W 6                      /* نیم‌پنجره‌ی فیلتر: ±6 نمونه */
//static uint16_t s_scopeBuf[SCOPE_N];
//static uint16_t s_scopeFilt[SCOPE_N];
//
//static uint32_t sc_abs_diff(uint32_t a, uint32_t b) { return (a > b) ? (a - b) : (b - a); }
//
///* میزان نویز: میانگین اختلاف نمونه‌های پشت‌سرهم (لبه‌های بزرگ حساب نمی‌شن) */
//static uint32_t sc_noise_level(const uint16_t *b, uint32_t n, uint32_t span)
//{
//    uint32_t lim = span / 3U + 8U, cnt = 0U;
//    uint64_t sum = 0U;
//    for (uint32_t i = 1U; i < n; i++)
//    {
//        uint32_t d = sc_abs_diff(b[i], b[i - 1U]);
//        if (d <= lim) { sum += d; cnt++; }
//    }
//    return (cnt != 0U) ? (uint32_t)(sum / cnt) : 0U;
//}
//
///* median ۵ تایی: مرجع بدون نویز که لبه‌ها رو نگه می‌داره */
//static uint32_t sc_med5(const uint16_t *b, uint32_t i, uint32_t n)
//{
//    uint32_t v[5];
//    for (int k = 0; k < 5; k++)
//    {
//        int32_t j = (int32_t)i + k - 2;
//        if (j < 0) j = 0;
//        if (j >= (int32_t)n) j = (int32_t)n - 1;
//        v[k] = b[j];
//    }
//    for (int x = 1; x < 5; x++)                     /* مرتب‌سازی درجی */
//    {
//        uint32_t t = v[x]; int y = x - 1;
//        while (y >= 0 && v[y] > t) { v[y + 1] = v[y]; y--; }
//        v[y + 1] = t;
//    }
//    return v[2];
//}
//
///* فیلتر حفظ‌کننده‌ی لبه:
// *   ref = median-5 اطراف نمونه (حذف spike، لبه‌ها سالم می‌مونن)
// *   خروجی = میانگین همسایه‌های ±W که از ref بیشتر از T دور نیستن
// *   -> نویز روی سطح‌ها و شیب‌ها میانگین‌گیری می‌شه، لبه‌ی پالس گرد نمی‌شه،
// *      نمونه‌ی پرت (spike) به‌طور خودکار کنار گذاشته می‌شه. */
//static void sc_filter(const uint16_t *in, uint16_t *out, uint32_t n, uint32_t span)
//{
//    uint32_t nd = sc_noise_level(in, n, span);
//    uint32_t T  = 4U * nd + 16U;
//    if (T < span / 16U) T = span / 16U;                       /* شیب‌های نرم (سینوس) هم میانگین‌گیری بشن */
//    uint32_t Tmax = (span / 3U > 16U) ? span / 3U : 16U;     /* لبه‌ها رو نخوره */
//    if (T > Tmax) T = Tmax;
//
//    for (uint32_t i = 0U; i < n; i++)
//    {
//        uint32_t ref = sc_med5(in, i, n);
//        uint32_t sum = 0U, cnt = 0U;
//        uint32_t j0 = (i >= (uint32_t)SCOPE_FILT_W) ? i - (uint32_t)SCOPE_FILT_W : 0U;
//        uint32_t j1 = i + (uint32_t)SCOPE_FILT_W;
//        if (j1 >= n) j1 = n - 1U;
//        for (uint32_t j = j0; j <= j1; j++)
//        {
//            uint32_t v = in[j];
//            if (sc_abs_diff(v, ref) <= T) { sum += v; cnt++; }
//        }
//        out[i] = (cnt != 0U) ? (uint16_t)(sum / cnt) : (uint16_t)ref;
//    }
//}
//
///* پس‌زمینه‌ی [ya, yb) از ستون x با خط‌چین شبکه (بدون پاک‌کردن کل صفحه = بدون پرش) */
//static void sc_col_bg(uint16_t x, uint16_t ya, uint16_t yb)
//{
//    for (uint16_t g = 1U; g < 4U; g++)
//    {
//        uint16_t gy = (uint16_t)(SCOPE_Y0 + (SCOPE_H * g) / 4U);
//        if (gy >= ya && gy < yb)
//        {
//            if (gy > ya) LCD_FillRect(x, ya, 1U, (uint16_t)(gy - ya), 0x0000U);
//            LCD_FillRect(x, gy, 1U, 1U, SCOPE_GRID);
//            ya = (uint16_t)(gy + 1U);
//        }
//    }
//    if (ya < yb) LCD_FillRect(x, ya, 1U, (uint16_t)(yb - ya), 0x0000U);
//}
//
//__attribute__((unused)) static void App_ScopeOld(void)
//{
//    LEDs_AllOff();
//    UI_Begin("SCOPE - P2 SINE TEST");
//    UI_Hint("U/D src L/R zoom OK filter");
//
//    /* Loopback test: keep DAC2 sine active while the scope samples Probe1.
//       DAC2 output: 50 Hz, 0.50 V peak, 1.65 V offset. */
//    uint8_t testSineRunning = Probe_DacSineStart(50U, 0.50f, 1.65f);
//    UI_Row(0U, testSineRunning ? UI_OK : UI_ORANGE, UI_BG,
//           testSineRunning ? "P2 SINE: 50Hz +/-0.50V" : "P2 SINE START FAILED");
//
//    LCD_FillRect(0U, SCOPE_Y0, LCD_WIDTH, SCOPE_H, 0x0000U);
//
//    static const uint8_t zoom[] = { 1U, 2U, 4U, 8U, 16U };
//    uint8_t zi = 1U;
//    uint8_t filt = 1U;
//    ReadSrc_t src = RD_PB1;   /* Probe1 (U7A) */
//    uint32_t last = 0U - 1000U;
//
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK) break;
//        if (in == MENU_INPUT_RIGHT) zi = (uint8_t)((zi + 1U) % sizeof(zoom));
//        if (in == MENU_INPUT_LEFT)  zi = (uint8_t)((zi + sizeof(zoom) - 1U) % sizeof(zoom));
//        if (in == MENU_INPUT_SELECT) filt = (uint8_t)!filt;
//        if (in == MENU_INPUT_UP || in == MENU_INPUT_DOWN) src = (src == RD_PB1) ? RD_PA6 : (src == RD_PA6) ? RD_PA2 : (src == RD_PA2) ? RD_PC1 : (src == RD_PC1) ? RD_PC4 : RD_PB1;
//        if (!TICK_DUE(last, 120U)) continue;
//        last = HAL_GetTick();
//
//        uint32_t cyc = Probe_Capture(src, s_scopeBuf, SCOPE_N);
//        if (cyc == 0U) continue;
//
//        /* بازه‌ی خام برای تخمین نویز */
//        uint32_t mn = 65535U, mx = 0U;
//        for (uint32_t i = 0U; i < SCOPE_N; i++)
//        {
//            if (s_scopeBuf[i] < mn) mn = s_scopeBuf[i];
//            if (s_scopeBuf[i] > mx) mx = s_scopeBuf[i];
//        }
//        const uint16_t *d = s_scopeBuf;
//        if (filt)
//        {
//            sc_filter(s_scopeBuf, s_scopeFilt, SCOPE_N, mx - mn);
//            d = s_scopeFilt;
//            mn = 65535U; mx = 0U;                         /* min/max/ماشه از داده‌ی فیلترشده */
//            for (uint32_t i = 0U; i < SCOPE_N; i++)
//            {
//                if (d[i] < mn) mn = d[i];
//                if (d[i] > mx) mx = d[i];
//            }
//        }
//
//        /* ماشه: اولین لبه‌ی بالارونده‌ی وسط سطح، با هیسترزیس (۴۰٪ -> ۶۰٪) */
//        uint32_t span = mx - mn, start = 0U;
//        if (span > 2000U)
//        {
//            uint32_t lo = mn + (span * 4U) / 10U, hi = mn + (span * 6U) / 10U;
//            uint8_t armed = 0U;
//            for (uint32_t i = 0U; i < SCOPE_N / 2U; i++)
//            {
//                if (d[i] <= lo) armed = 1U;
//                else if (armed && d[i] >= hi) { start = i; break; }
//            }
//        }
//
//        uint32_t top = (mx > 6000U) ? mx + mx / 8U : 6000U;   /* مقیاس عمودی: 0 .. top */
//        const uint16_t yMin = SCOPE_Y0, yMax = (uint16_t)(SCOPE_Y0 + SCOPE_H - 1U);
//        uint16_t prevY = 0xFFFFU;
//
//        for (uint16_t x = 0U; x < LCD_WIDTH; x++)
//        {
//            uint32_t idx = start + (uint32_t)x * zoom[zi];
//            if (idx >= SCOPE_N)                           /* داده تموم شد: فقط پس‌زمینه */
//            {
//                sc_col_bg(x, yMin, (uint16_t)(yMax + 1U));
//                continue;
//            }
//            /* هر پیکسل = میانگین zoom نمونه (نویز کمتر، بدون aliasing) */
//            uint32_t sum = 0U, cnt = 0U;
//            for (uint32_t k = idx; k < idx + zoom[zi] && k < SCOPE_N; k++) { sum += d[k]; cnt++; }
//            uint32_t v = sum / cnt;
//            uint16_t y = (uint16_t)(yMax - (v * (SCOPE_H - 1U)) / top);
//            if (y < yMin) y = yMin;
//
//            uint16_t y0 = (prevY == 0xFFFFU) ? y : ((y < prevY) ? y : prevY);
//            uint16_t y1 = (prevY == 0xFFFFU) ? y : ((y < prevY) ? prevY : y);
//            if (y1 <= y0) { if (y1 < yMax) y1++; else y0--; }      /* ضخامت حداقل ۲ پیکسل */
//            prevY = y;
//
//            sc_col_bg(x, yMin, y0);
//            LCD_FillRect(x, y0, 1U, (uint16_t)(y1 - y0 + 1U), UI_OK);
//            sc_col_bg(x, (uint16_t)(y1 + 1U), (uint16_t)(yMax + 1U));
//        }
//
//        char b[40], t1[12], t2[12];
//        float dt_us = ((float)cyc / (float)SystemCoreClock) * 1.0e6f / (float)SCOPE_N;
//        float win_ms = dt_us * (float)zoom[zi] * (float)LCD_WIDTH / 1000.0f;
//        UI_FmtFixed(t1, sizeof t1, Probe_CountsToVolts(src, mx), 2);
//        UI_FmtFixed(t2, sizeof t2, win_ms, 2);
//        snprintf(b, sizeof b, "%s Vmax %s %sms F:%s", Probe_ReadName(src), t1, t2, filt ? "ON" : "OFF");
//        UI_Text(2U, 204U, 1U, UI_DIM, UI_BG, b);
//        LED_Blue_Set(span > 2000U);
//    }
//    /* Stop the generator only when leaving the scope page. */
//    Probe_DacOff();
//    LEDs_AllOff();
//}
#define SCOPE_N      4096U
#define SCOPE_Y0     52U
#define SCOPE_H      150U
#define SCOPE_GRID   0x2104U
#define SCOPE_FILT_W 2                      /* کاهش پنجره فیلتر برای جلوگیری از گرد شدن لبه‌های مربعی */
static uint16_t s_scopeBuf[SCOPE_N];
static uint16_t s_scopeFilt[SCOPE_N];

static uint32_t sc_abs_diff(uint32_t a, uint32_t b) { return (a > b) ? (a - b) : (b - a); }

/* تابع محاسبه نویز ساده‌شده */
static uint32_t sc_noise_level(const uint16_t *b, uint32_t n, uint32_t span)
{
    uint32_t lim = span / 4U + 4U, cnt = 0U;
    uint64_t sum = 0U;
    for (uint32_t i = 1U; i < n; i++)
    {
        uint32_t d = sc_abs_diff(b[i], b[i - 1U]);
        if (d <= lim) { sum += d; cnt++; }
    }
    return (cnt != 0U) ? (uint32_t)(sum / cnt) : 0U;
}

/* فیلتر سبک‌تر برای حفظ لبه‌های تیز موج مربعی */
static void sc_filter(const uint16_t *in, uint16_t *out, uint32_t n, uint32_t span)
{
    uint32_t nd = sc_noise_level(in, n, span);
    uint32_t T  = 2U * nd + 8U;

    for (uint32_t i = 0U; i < n; i++)
    {
        uint32_t sum = (uint32_t)in[i];
        uint32_t cnt = 1U;
        uint32_t j0 = (i >= (uint32_t)SCOPE_FILT_W) ? i - (uint32_t)SCOPE_FILT_W : 0U;
        uint32_t j1 = i + (uint32_t)SCOPE_FILT_W;
        if (j1 >= n) j1 = n - 1U;

        for (uint32_t j = j0; j <= j1; j++)
        {
            if (j == i) continue;
            uint32_t v = in[j];
            if (sc_abs_diff(v, in[i]) <= T) { sum += v; cnt++; }
        }
        out[i] = (uint16_t)(sum / cnt);
    }
}

/* پس‌زمینه‌ی [ya, yb) از ستون x با خط‌چین شبکه */
static void sc_col_bg(uint16_t x, uint16_t ya, uint16_t yb)
{
    for (uint16_t g = 1U; g < 4U; g++)
    {
        uint16_t gy = (uint16_t)(SCOPE_Y0 + (SCOPE_H * g) / 4U);
        if (gy >= ya && gy < yb)
        {
            if (gy > ya) LCD_FillRect(x, ya, 1U, (uint16_t)(gy - ya), 0x0000U);
            LCD_FillRect(x, gy, 1U, 1U, SCOPE_GRID);
            ya = (uint16_t)(gy + 1U);
        }
    }
    if (ya < yb) LCD_FillRect(x, ya, 1U, (uint16_t)(yb - ya), 0x0000U);
}
__attribute__((unused)) static void App_ScopeOld(void)
{
    LEDs_AllOff();
    UI_Begin("SCOPE - SQUARE WAVE STABLE");
    UI_Hint("U/D src L/R zoom OK filter");

    uint8_t testSineRunning = Probe_DacSineStart(50U, 0.50f, 1.65f);
    UI_Row(0U, testSineRunning ? UI_OK : UI_ORANGE, UI_BG,
           testSineRunning ? "SCOPE: STABLE TRIGGER ACTIVE" : "DAC START FAILED");

    LCD_FillRect(0U, SCOPE_Y0, LCD_WIDTH, SCOPE_H, 0x0000U);

    static const uint8_t zoom[] = { 1U, 2U, 4U, 8U, 16U };
    uint8_t zi = 1U;
    uint8_t filt = 0U;
    ReadSrc_t src = RD_PB1;
    uint32_t last = 0U - 1000U;

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_RIGHT) zi = (uint8_t)((zi + 1U) % sizeof(zoom));
        if (in == MENU_INPUT_LEFT)  zi = (uint8_t)((zi + sizeof(zoom) - 1U) % sizeof(zoom));
        if (in == MENU_INPUT_SELECT) filt = (uint8_t)!filt;
        if (in == MENU_INPUT_UP || in == MENU_INPUT_DOWN) src = (src == RD_PB1) ? RD_PA6 : (src == RD_PA6) ? RD_PA2 : (src == RD_PA2) ? RD_PC1 : (src == RD_PC1) ? RD_PC4 : RD_PB1;
        if (!TICK_DUE(last, 120U)) continue;
        last = HAL_GetTick();

        uint32_t cyc = Probe_Capture(src, s_scopeBuf, SCOPE_N);
        if (cyc == 0U) continue;

        uint32_t mn = 65535U, mx = 0U;
        for (uint32_t i = 0U; i < SCOPE_N; i++)
        {
            if (s_scopeBuf[i] < mn) mn = s_scopeBuf[i];
            if (s_scopeBuf[i] > mx) mx = s_scopeBuf[i];
        }

        const uint16_t *d = s_scopeBuf;
        if (filt)
        {
            sc_filter(s_scopeBuf, s_scopeFilt, SCOPE_N, mx - mn);
            d = s_scopeFilt;
            mn = 65535U; mx = 0U;
            for (uint32_t i = 0U; i < SCOPE_N; i++)
            {
                if (d[i] < mn) mn = d[i];
                if (d[i] > mx) mx = d[i];
            }
        }

        /* [اصلاح‌شده]: تریگر پایدار بر روی لبه‌ی بالارونده (Rising Edge) با هیسترزیس */
        uint32_t span = mx - mn, start = 0U;
        if (span > 100U)
        {
            uint32_t low_thresh  = mn + (span * 30U) / 100U;
            uint32_t high_thresh = mn + (span * 70U) / 100U;
            uint8_t state = 0; /* 0: منتظر سطح پایین، 1: لبه بالارونده کشف شد */

            for (uint32_t i = 0U; i < SCOPE_N - 10U; i++)
            {
                if (state == 0 && d[i] < low_thresh) {
                    state = 1;
                } else if (state == 1 && d[i] >= high_thresh) {
                    start = i; /* قفل شدن دقیق روی لبه‌ی پالس برای حذف لرزش (Jitter) */
                    break;
                }
            }
        }

        /* مقیاس عمودی پویای دقیق */
        uint32_t top = (mx > mn) ? (mx - mn) : 4096U;
        if (top < 10U) top = 10U;

        const uint16_t yMin = SCOPE_Y0, yMax = (uint16_t)(SCOPE_Y0 + SCOPE_H - 1U);
        uint16_t prevY = 0xFFFFU;

        for (uint16_t x = 0U; x < LCD_WIDTH; x++)
        {
            uint32_t idx = start + (uint32_t)x * zoom[zi];
            if (idx >= SCOPE_N)
            {
                sc_col_bg(x, yMin, (uint16_t)(yMax + 1U));
                continue;
            }

            uint32_t sum = 0U, cnt = 0U;
            for (uint32_t k = idx; k < idx + zoom[zi] && k < SCOPE_N; k++) { sum += d[k]; cnt++; }
            uint32_t v = (cnt != 0U) ? (sum / cnt) : mn;

            if (v < mn) v = mn;
            if (v > mx) v = mx;

            /* [اصلاح‌شده]: حذف شیب‌های کاذب و تثبیت سطوح بالا و پایین پالس مربعی */
            if (span > 100U) {
                uint32_t mid = mn + span / 2U;
                if (v > mid + (span / 10U)) v = mx;       /* فیکس کردن سطح بالا روی خط صاف */
                else if (v < mid - (span / 10U)) v = mn;  /* فیکس کردن سطح پایین روی زمین */
            }

            uint16_t y = (uint16_t)(yMax - ((v - mn) * (SCOPE_H - 1U)) / top);
            if (y < yMin) y = yMin;
            if (y > yMax) y = yMax;

            uint16_t y0 = (prevY == 0xFFFFU) ? y : ((y < prevY) ? y : prevY);
            uint16_t y1 = (prevY == 0xFFFFU) ? y : ((y < prevY) ? prevY : y);
            if (y1 <= y0) { if (y1 < yMax) y1++; else y0--; }
            prevY = y;

            sc_col_bg(x, yMin, y0);
            LCD_FillRect(x, y0, 1U, (uint16_t)(y1 - y0 + 1U), UI_OK);
            sc_col_bg(x, (uint16_t)(y1 + 1U), (uint16_t)(yMax + 1U));
        }

        char b[40], t1[12], t2[12];
        float dt_us = ((float)cyc / (float)SystemCoreClock) * 1.0e6f / (float)SCOPE_N;
        float win_ms = dt_us * (float)zoom[zi] * (float)LCD_WIDTH / 1000.0f;
        UI_FmtFixed(t1, sizeof t1, Probe_CountsToVolts(src, mx), 2);
        UI_FmtFixed(t2, sizeof t2, win_ms, 2);
        snprintf(b, sizeof b, "%s Vmax %s %sms F:%s", Probe_ReadName(src), t1, t2, filt ? "ON" : "OFF");
        UI_Text(2U, 204U, 1U, UI_DIM, UI_BG, b);
        LED_Blue_Set(span > 100U);
    }
    Probe_DacOff();
    LEDs_AllOff();
}
//__attribute__((unused)) static void App_ScopeOld(void)
//{
//    LEDs_AllOff();
//    UI_Begin("SCOPE - P2 SINE TEST");
//    UI_Hint("U/D src L/R zoom OK filter");
//
//    uint8_t testSineRunning = Probe_DacSineStart(50U, 0.50f, 1.65f);
//    UI_Row(0U, testSineRunning ? UI_OK : UI_ORANGE, UI_BG,
//           testSineRunning ? "P2 SINE: 50Hz +/-0.50V" : "P2 SINE START FAILED");
//
//    LCD_FillRect(0U, SCOPE_Y0, LCD_WIDTH, SCOPE_H, 0x0000U);
//
//    static const uint8_t zoom[] = { 1U, 2U, 4U, 8U, 16U };
//    uint8_t zi = 1U;
//    uint8_t filt = 0U;         /* پیش‌فرض فیلتر خاموش برای جلوگیری از گرد شدن لبه‌ها */
//    ReadSrc_t src = RD_PB1;
//    uint32_t last = 0U - 1000U;
//
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK) break;
//        if (in == MENU_INPUT_RIGHT) zi = (uint8_t)((zi + 1U) % sizeof(zoom));
//        if (in == MENU_INPUT_LEFT)  zi = (uint8_t)((zi + sizeof(zoom) - 1U) % sizeof(zoom));
//        if (in == MENU_INPUT_SELECT) filt = (uint8_t)!filt;
//        if (in == MENU_INPUT_UP || in == MENU_INPUT_DOWN) src = (src == RD_PB1) ? RD_PA6 : (src == RD_PA6) ? RD_PA2 : (src == RD_PA2) ? RD_PC1 : (src == RD_PC1) ? RD_PC4 : RD_PB1;
//        if (!TICK_DUE(last, 120U)) continue;
//        last = HAL_GetTick();
//
//        uint32_t cyc = Probe_Capture(src, s_scopeBuf, SCOPE_N);
//        if (cyc == 0U) continue;
//
//        uint32_t mn = 65535U, mx = 0U;
//        for (uint32_t i = 0U; i < SCOPE_N; i++)
//        {
//            if (s_scopeBuf[i] < mn) mn = s_scopeBuf[i];
//            if (s_scopeBuf[i] > mx) mx = s_scopeBuf[i];
//        }
//
//        const uint16_t *d = s_scopeBuf;
//        if (filt)
//        {
//            sc_filter(s_scopeBuf, s_scopeFilt, SCOPE_N, mx - mn);
//            d = s_scopeFilt;
//            mn = 65535U; mx = 0U;
//            for (uint32_t i = 0U; i < SCOPE_N; i++)
//            {
//                if (d[i] < mn) mn = d[i];
//                if (d[i] > mx) mx = d[i];
//            }
//        }
//
//        /* محاسبه دقیق‌تر سطح ماشه (Trigger) برای پایداری موج مربعی */
//        uint32_t span = mx - mn, start = 0U;
//        if (span > 100U)
//        {
//            uint32_t threshold = mn + span / 2U;
//            uint8_t armed = 0U;
//            for (uint32_t i = 0U; i < SCOPE_N / 2U; i++)
//            {
//                if (d[i] < threshold) armed = 1U;
//                else if (armed && d[i] >= threshold) { start = i; break; }
//            }
//        }
//
//        /* مقیاس عمودی پویای دقیق بر اساس مینیمم و ماکزیمم واقعی */
//        uint32_t top = (mx > mn) ? (mx - mn) : 4096U;
//        if (top < 10U) top = 10U;
//
//        const uint16_t yMin = SCOPE_Y0, yMax = (uint16_t)(SCOPE_Y0 + SCOPE_H - 1U);
//        uint16_t prevY = 0xFFFFU;
//
//        for (uint16_t x = 0U; x < LCD_WIDTH; x++)
//        {
//            uint32_t idx = start + (uint32_t)x * zoom[zi];
//            if (idx >= SCOPE_N)
//            {
//                sc_col_bg(x, yMin, (uint16_t)(yMax + 1U));
//                continue;
//            }
//
//            uint32_t sum = 0U, cnt = 0U;
//            for (uint32_t k = idx; k < idx + zoom[zi] && k < SCOPE_N; k++) { sum += d[k]; cnt++; }
//            uint32_t v = (cnt != 0U) ? (sum / cnt) : mn;
//
//            if (v < mn) v = mn;
//            if (v > mx) v = mx;
//
//            uint16_t y = (uint16_t)(yMax - ((v - mn) * (SCOPE_H - 1U)) / top);
//            if (y < yMin) y = yMin;
//            if (y > yMax) y = yMax;
//
//            uint16_t y0 = (prevY == 0xFFFFU) ? y : ((y < prevY) ? y : prevY);
//            uint16_t y1 = (prevY == 0xFFFFU) ? y : ((y < prevY) ? prevY : y);
//            if (y1 <= y0) { if (y1 < yMax) y1++; else y0--; }
//            prevY = y;
//
//            sc_col_bg(x, yMin, y0);
//            LCD_FillRect(x, y0, 1U, (uint16_t)(y1 - y0 + 1U), UI_OK);
//            sc_col_bg(x, (uint16_t)(y1 + 1U), (uint16_t)(yMax + 1U));
//        }
//
//        char b[40], t1[12], t2[12];
//        float dt_us = ((float)cyc / (float)SystemCoreClock) * 1.0e6f / (float)SCOPE_N;
//        float win_ms = dt_us * (float)zoom[zi] * (float)LCD_WIDTH / 1000.0f;
//        UI_FmtFixed(t1, sizeof t1, Probe_CountsToVolts(src, mx), 2);
//        UI_FmtFixed(t2, sizeof t2, win_ms, 2);
//        snprintf(b, sizeof b, "%s Vmax %s %sms F:%s", Probe_ReadName(src), t1, t2, filt ? "ON" : "OFF");
//        UI_Text(2U, 204U, 1U, UI_DIM, UI_BG, b);
//        LED_Blue_Set(span > 100U);
//    }
//    Probe_DacOff();
//    LEDs_AllOff();

/* ---- اسکوپ: صفحه‌ی مستقل ، ولی با همون نمونه‌برداری PC4 ی که توی ولت‌متر درست کار می‌کنه ---- */
/* ================================================================== */
/*  اسکوپ: بازسازی موج دیجیتال (ضد افت / رمپ)                              */
/*  روی برد، موج مربعی بعد از هر لبه‌ی بالا‌رونده کم‌کم افت می‌کرد (شبیه دندانه‌ی  */
/*  اره). تشخیص «لبه» فقط به پرش ناگهانی ولتاژ نگاه می‌کنه، نه به سطح، پس       */
/*  افت آهسته رو نادیده می‌گیره. بعد از پیدا کردن لبه‌ها یه موج مربعی تمیز با      */
/*  دو سطح ثابت می‌سازیم و همونو رسم می‌کنیم. فرکانس و دیوتی هم از لبه‌ها حساب   */
/*  می‌شه. برای موج‌های غیر دیجیتال (سینوس و DC) همون رسم خام قبلی می‌مونه.      */
/* ================================================================== */
/* پنجره‌ی ثبت اسکوپ ۳ برابر پنجره‌ی نمایش: ماشه توی بخش اضافه می‌گرده و اضافه‌ها دور ریخته می‌شن */
#define SC_COMP       1                /* جبران نرم‌افزاری RC روشن (۱) ؛ با یادگیری پایدار */
#define SC_WARM       400U             /* نمونه‌های اول بعد از فیلتر معکوس = گذرای راه‌اندازی ؛ نه تو آمار می‌آد نه تو رسم */
#define SC_N          6144U            /* نمونه‌ی ثبت‌شده */
#define SC_SHOW       2048U            /* نمونه‌ی نمایش‌داده‌شده (۱/۳) */
#define SC_PERIODS    12.0f            /* AUTO: کل ثبت حدود ۱۲ پریود از سیگنال رو بگیره (۴ تاش نمایش داده می‌شه) */
#define SC_AUTO_MAX_US 100U
#define SC_NB         256U             /* تعداد بین‌های هم‌پوشانی پریود */
static uint16_t s_scBuf[SC_N];
static float s_scV[SC_N];      /* ولت خام */
static float s_scR[SC_N];      /* موج بازسازی‌شده (فقط برای اندازه‌گیری؛ نمایش داده نمی‌شه) */
static float s_scC[SC_N];
/* PB0 کف ۰٫۰۹۵ولتی PC4 رو نداره؛ فقط ضریب تقسیم خطی. */
static float sc_counts_to_vin(uint32_t c) { return Adc_PinToVinLinear((float)c * 3.3f / 65535.0f); }      /* ولت بعد از جبران RC ورودی = چیزی که روی صفحه رسم می‌شه */

/* ---------------------------------------------------------------------------------------------------
 * جبران خودکار ورودی پروب ۱
 *
 * مشاهده روی برد: لبه‌ی بالارونده‌ی موج مربعی کامل می‌آد (خیلی بزرگ‌تر از سطح واقعی)، بعدش سقف مثل یه خازن در حال خالی‌شدن
 * افت نمایی می‌کنه و پایین موج هم کلیپ می‌شه. یعنی ورودی مثل «تقسیم‌کننده‌ی جبران‌شده‌ی بیش‌ازحد» رفتار می‌کنه:
 *     H(s) = (1 + s·ta) / (1 + s·tb)      ، بهره‌ی HF / بهره‌ی DC = rho = ta/tb  ،  tb = ثابت زمانی افت
 * همین رو با فیلتر معکوسش خنثی می‌کنیم (پایدار، و نویز HF رو کم می‌کنه):
 *     ta·dy/dt + y = x + tb·dx/dt          (x = سیگنال اندازه‌گیری‌شده با ضریب DC ، y = سیگنال واقعی)
 * rho و tb خودشون از روی «سقف» پالس‌های مربعی ثبت‌شده یاد گرفته می‌شن (فیت U + W·exp(-t/tb)). اگه سقف صافِ صاف بود
 * (سخت‌افزار درست شده) rho به ۱ می‌رسه و جبران خودش خاموش می‌شه.
 * پایین موج روی GND کلیپ می‌شه (کلمپ ورودی) ؛ هر جا نمونه ≈ ۰ بود حالت فیلتر ریست و خروجی ۰ در نظر گرفته می‌شه.
 * محدودیت: موج‌های سینوسی/مثلثی که به زیر ۰ می‌رن فقط تقریبی درست می‌شن (اطلاعات کلیپ‌شده برنمی‌گرده).
 * --------------------------------------------------------------------------------------------------- */
#define SC_CMP_RHO0      1.00f        /* قبل از کالیبراسیون: بدون جبران (r=1) */
#define SC_CMP_TAU0      0.000071f    /* مقدار اولیه‌ی tb (ثانیه) = (R8||R6)·(C4+خازن اضافه) */
#define SC_CMP_CLIP_V    0.15f        /* زیر این ولت (مقیاس ورودی) = کلیپ‌شده‌ی صفر */
static float s_cmpRho = SC_CMP_RHO0;
static float s_cmpTau = SC_CMP_TAU0;
static uint8_t s_cmpCal = 0U, s_cmpCalOk = 0U, s_cmpCalLocked = 0U; static float s_cmpLr = 0.0f, s_cmpLt = 0.0f;      /* 1 = r,tb از کالیبراسیون روی مربع تست قفل شده */

/* یادگیری rho/tb از سقف پالس‌ها ؛ خروجی 1 = چیزی یاد گرفته شد */
__attribute__((unused)) static uint8_t sc_cmp_learn(const float *v, uint32_t n, float dt)
{
    float mn = v[0], mx = v[0];
    for (uint32_t i = 1U; i < n; i++) { if (v[i] < mn) mn = v[i]; if (v[i] > mx) mx = v[i]; }
    const float span = mx - mn;
    if (span < 1.0f || dt <= 0.0f) return 0U;

    float accRho = 0.0f, accTau = 0.0f; uint32_t cnt = 0U;

    uint32_t i = 4U;
    while (i + 8U < n)
    {
        if ((v[i] - v[i - 4U]) <= 0.35f * span) { i++; continue; }
        /* لبه‌ی بالارونده: صبر کن تا شیب بخوابه */
        uint32_t k = i;
        while (k + 1U < n && (v[k] - v[k - 1U]) > 0.02f * span) k++;
        uint32_t p0 = k + 2U;
        /* انتهای سقف: اولین افت بزرگ */
        uint32_t j = p0 + 4U;
        while (j < n && (v[j - 4U] - v[j]) <= 0.12f * span) j++;
        if (j >= n) break;                                   /* سقف تا آخر بافر کامل نیست */
        uint32_t p1 = j - 4U;
        i = j + 4U;
        if (p1 <= p0 + 40U) continue;
        const uint32_t M = p1 - p0;
        const float L = (float)M * dt;
        if (L < 250e-6f) continue;                           /* سقف خیلی کوتاه: قابل اطمینان نیست */

        float Ss = 0.0f, Ss2 = 0.0f;
        for (uint32_t m = 0U; m < M; m++) { float x = v[p0 + m]; Ss += x; Ss2 += x * x; }
        float bestSse = 1.0e30f, bU = 0.0f, bW = 0.0f, bTau = 0.0f;
        float tau = 40.0e-6f;
        for (uint32_t g = 0U; g < 44U; g++, tau *= 1.12f)       /* شبکه‌ی هندسی 40us .. ~5ms با گام ۱۲٪ */
        {
            if (tau > 3.0f * L) break;
            const float q = expf(-dt / tau);
            float e = 1.0f, Se = 0.0f, Se2 = 0.0f, Ses = 0.0f;
            for (uint32_t m = 0U; m < M; m++) { Se += e; Se2 += e * e; Ses += e * v[p0 + m]; e *= q; }
            /* مجموع e^0..e^(M-1)=Se ؛ نرمال‌ایکویشن 2×2 برای [U, W] */
            const float Mf = (float)M;
            float det = Mf * Se2 - Se * Se;
            if (det < 1.0e-6f) continue;
            float U = (Ss * Se2 - Se * Ses) / det;
            float W = (Mf * Ses - Se * Ss) / det;
            float sse = Ss2 - U * Ss - W * Ses;
            if (sse < bestSse) { bestSse = sse; bU = U; bW = W; bTau = tau; }
        }
        if (bTau <= 0.0f || bU <= 0.2f) continue;
        const float rms = sqrtf(fmaxf(bestSse, 0.0f) / (float)M);
        if (rms > 0.05f * (bU + bW)) continue;               /* فیت قابل اعتماد نیست (سقف نمایی نیست) */
        float rho = (bU + bW) / bU;
        if (rho < 1.05f) { accRho += 1.0f; accTau += s_cmpTau; cnt++; continue; }   /* سقف صاف = بدون اعوجاج */
        if (rho > 40.0f || bW < 0.0f) continue;
        accRho += rho; accTau += bTau; cnt++;
    }
    if (cnt == 0U) return 0U;
    const float r = accRho / (float)cnt, t = accTau / (float)cnt;
    /* میانگین‌گیری رو‌به‌همگرا: اولین اندازه‌گیری‌ها سریع می‌شینن (۱/k) ، بعدش پارامترها تقریباً قفل می‌شن (حداقل ۱/۴۰)
     * -> جبران دیگه از فریمی به فریم دیگه نمی‌لرزه و شکل موج ثابت می‌مونه */
    static uint16_t kN = 0U;
    float al = 1.0f / (float)(kN + 1U);
    if (al < 0.025f) al = 0.025f;
    if (kN < 60000U) kN++;
    s_cmpRho += al * (r - s_cmpRho);
    s_cmpTau += al * (t - s_cmpTau);
    if (s_cmpRho < 1.0f) s_cmpRho = 1.0f;
    if (s_cmpRho > 30.0f) s_cmpRho = 30.0f;
    return 1U;
}

/* ---------------------------------------------------------------------------------------------------
 * فیلتر معکوس دقیق (گسسته) برای هر r > 0 :
 *     مدل ورودی:  H(s) = K·(r + (1-r)/(1 + s·tb))      ،  r = بهره‌ی HF / بهره‌ی DC = ta/tb
 *       r < 1 : لبه‌ها گرد می‌شن (فیلتر پایین‌گذر ؛ مثلاً بعد از اضافه‌کردن خازن شنت)
 *       r = 1 : ورودی تنظیم‌شده (بدون اعوجاج)
 *       r > 1 : لبه جهش می‌کنه و سقف افت می‌کنه (جبران بیش‌ازحد)
 *     x[i] = ( v[i] - (1-r)·α·z[i-1] ) / ( r + (1-r)(1-α) ) ،  z[i] = α·z[i-1] + (1-α)·x[i] ،  α = exp(-dt/tb)
 *   برای هر dt و هر r>0 پایداره ؛ r کوچیک نویز HF رو تقویت می‌کنه (۱/r) پس r ≥ 0.12 محدود می‌شه.
 *   روی نمونه‌ی ≈۰ (کلیپ ورودی/ADC) حالت فیلتر ریست می‌شه.
 * --------------------------------------------------------------------------------------------------- */
static void sc_inv(const float *in, float *out, uint32_t n, float dt, float r, float tb, float z0)
{
    const float al = expf(-dt / tb);
    const float D  = r + (1.0f - r) * (1.0f - al);
    const float c1 = (1.0f - r) * al;
    float z = z0;
    for (uint32_t i = 0U; i < n; i++)
    {
        const float v = in[i];
        float x;
        if (v <= SC_CMP_CLIP_V) { x = 0.0f; z = 0.0f; }
        else { x = (v - c1 * z) / D; if (x < 0.0f) x = 0.0f; z = al * z + (1.0f - al) * x; }
        out[i] = x;
    }
}

static void sc_cmp_apply(const float *in, float *out, uint32_t n, float dt)
{
    float r = s_cmpRho;
    if (r < 0.12f) r = 0.12f;
    if (r > 30.0f) r = 30.0f;
    if ((r > 0.98f && r < 1.02f) || dt <= 0.0f || s_cmpTau <= 0.0f) { for (uint32_t i = 0U; i < n; i++) out[i] = in[i]; return; }
    sc_inv(in, out, n, dt, r, s_cmpTau, in[0]);
}

#define SC_MAXE   512U
#define SC_HYST   0.15f      /* هیسترزیس ±۱۵٪ دامنه دور سطح میانی */
#define SC_MINW   3U         /* پالس کوتاه‌تر از این (نمونه) = گلیچ و حذف می‌شه */

static float sc_med3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { b = c; }
    return (a > b) ? a : b;
}

/* v: ولت خام (n نمونه با فاصله‌ی یکنواخت dt) ؛ out: موج دیجیتال تمیز.
 * برمی‌گردونه 0 اگه سیگنال دیجیتال/پریودیک نیست (سینوس، DC، نویز). */
static float s_scPos[SC_MAXE]; static int8_t s_scDir[SC_MAXE]; static uint32_t s_scNe;

static uint8_t sc_digital(const float *v, uint32_t n, float dt, float *out,
                          float *hz, float *duty, float *lo, float *hi, uint32_t *first, float *r0out)
{
    /* ۱) حذف spike تک‌نمونه‌ای (median-3) -> out موقتاً نگه‌دارنده‌ی نسخه‌ی تمیز است */
    out[0] = v[0]; out[n - 1U] = v[n - 1U];
    for (uint32_t i = 1U; i + 1U < n; i++) out[i] = sc_med3(v[i - 1U], v[i], v[i + 1U]);

    float mn = out[0], mx = out[0];
    for (uint32_t i = 1U; i < n; i++) { if (out[i] < mn) mn = out[i]; if (out[i] > mx) mx = out[i]; }
    float span = mx - mn;
    if (span < 0.8f) return 0U;
    float mid = 0.5f * (mx + mn);
    float thH = mid + SC_HYST * span, thL = mid - SC_HYST * span;

    /* ۲) Schmitt trigger روی سطح (نه روی شیب) ؛ لبه = عبور از mid با درون‌یابی */
    float *pos = s_scPos; int8_t *dir = s_scDir; uint32_t ne = 0U;
    uint8_t st = (out[0] > mid) ? 1U : 0U;
    for (uint32_t i = 1U; i < n && ne < SC_MAXE; i++)
    {
        float x = out[i];
        if (!st && x > thH)
        {
            uint32_t j = i; uint32_t lim = (i > 24U) ? i - 24U : 0U;
            while (j > lim && out[j - 1U] > mid) j--;           /* اولین نمونه‌ی بالای mid */
            float a = (j > 0U) ? out[j - 1U] : out[j], b = out[j];
            float f = (b > a) ? (mid - a) / (b - a) : 0.0f;
            pos[ne] = (j > 0U) ? ((float)(j - 1U) + f) : (float)j; dir[ne] = 1; ne++; st = 1U;
        }
        else if (st && x < thL)
        {
            uint32_t j = i; uint32_t lim = (i > 24U) ? i - 24U : 0U;
            while (j > lim && out[j - 1U] < mid) j--;
            float a = (j > 0U) ? out[j - 1U] : out[j], b = out[j];
            float f = (a > b) ? (a - mid) / (a - b) : 0.0f;
            pos[ne] = (j > 0U) ? ((float)(j - 1U) + f) : (float)j; dir[ne] = -1; ne++; st = 0U;
        }
    }

    /* ۳) حذف پالس‌های خیلی کوتاه (جفت لبه‌ی متوالی نزدیک هم) */
    for (uint32_t k = 0U; k + 1U < ne; )
    {
        if ((pos[k + 1U] - pos[k]) < (float)SC_MINW)
        {
            for (uint32_t m = k; m + 2U < ne; m++) { pos[m] = pos[m + 2U]; dir[m] = dir[m + 2U]; }
            ne -= 2U;
            if (k > 0U) k--;
        }
        else k++;
    }
    if (ne < 4U) return 0U;
    s_scNe = ne;

    /* ۴) سطح پایین/بالا: میانگین نمونه‌های «داخل» هر پالس (دور از لبه‌ها، بدون overshoot) */
    float lsum = 0.0f, hsum = 0.0f; uint32_t lc = 0U, hc2 = 0U;
    for (uint32_t k = 0U; k + 1U < ne; k++)
    {
        uint32_t a = (uint32_t)pos[k] + 4U, b = (uint32_t)pos[k + 1U];
        if (b < 4U || b <= a + 1U) continue;
        b -= 4U; if (b <= a) continue;
        float s = 0.0f; for (uint32_t q = a; q < b && q < n; q++) s += out[q];
        uint32_t c = (b < n ? b : n) - a;
        if (dir[k] > 0) { hsum += s; hc2 += c; } else { lsum += s; lc += c; }
    }
    float L = mn, H = mx;
    if (lc > 0U && hc2 > 0U)
    {
        float l2 = lsum / (float)lc, h2 = hsum / (float)hc2;
        if (h2 > l2 + 0.5f * span) { L = l2; H = h2; }
    }

    /* ۵) موج تمیز */
    uint8_t s0 = (dir[0] > 0) ? 0U : 1U;
    uint32_t k = 0U;
    for (uint32_t q = 0U; q < n; q++)
    {
        while (k < ne && (float)q >= pos[k]) { s0 = (dir[k] > 0) ? 1U : 0U; k++; }
        out[q] = s0 ? H : L;
    }

    /* ۶) فرکانس و دیوتی از لبه‌های بالارونده */
    float r0 = -1.0f, rl = 0.0f, highT = 0.0f; uint32_t rc = 0U, hn = 0U;
    for (uint32_t m = 0U; m < ne; m++)
    {
        if (dir[m] > 0)
        {
            if (rc == 0U) r0 = pos[m];
            rl = pos[m]; rc++;
            if (m + 1U < ne && dir[m + 1U] < 0) { highT += pos[m + 1U] - pos[m]; hn++; }
        }
    }
    *hz = 0.0f; *duty = 0.0f; *r0out = r0;
    if (rc >= 2U)
    {
        float per = (rl - r0) / (float)(rc - 1U);
        if (per > 1.0f && dt > 0.0f)
        {
            *hz = 1.0f / (per * dt);
            if (hn > 0U) { *duty = 100.0f * (highT / (float)hn) / per; if (*duty > 100.0f) *duty = 100.0f; }
        }
    }
    *lo = L; *hi = H;
    *first = 0U;
    for (uint32_t m = 0U; m < ne; m++) if (dir[m] > 0 && pos[m] < (float)VM_TRIG_ZONE) { *first = (uint32_t)(pos[m] + 0.5f); break; }
    return 1U;
}

/* ---------------------------------------------------------------------------------------------------
 * کالیبراسیون ورودی روی موج مربعی ۱kHz ، ۵۰٪ (پروب ۲ -> پروب ۱) :
 * (r , tb) رو طوری پیدا می‌کنه که بعد از فیلتر معکوس، «سقف و کف» پالس‌ها تا حد ممکن صاف بشن.
 * فقط وقتی اجرا می‌شه که سیگنال واقعاً همون مربع تست باشه ؛ برای هر سیگنال دیگه‌ای پارامترها دست‌نخورده می‌مونن.
 * v: ولت خام ، pos/dir/ne: لبه‌های همین v (از sc_digital) ؛ خروجی 1 = r,tb جدید توی *rOut, *tOut
 * --------------------------------------------------------------------------------------------------- */
/* هزینه‌ی «دو سطحی بودن» : هر نمونه به نزدیک‌ترین سطح (بالا/پایین) نسبت داده می‌شه و مجموع مربع فاصله‌ها حساب می‌شه.
 * سقف/کف صاف و لبه‌ی تیز = هزینه‌ی کم ؛ جهش، افت، گردی لبه، و تقویت نویز = هزینه‌ی زیاد. */
static float sc_cost2(const float *x, uint32_t n)
{
    float mn = x[0], mx = x[0];
    for (uint32_t i = 1U; i < n; i++) { if (x[i] < mn) mn = x[i]; if (x[i] > mx) mx = x[i]; }
    float lo = mn, hi = mx;
    for (uint32_t it = 0U; it < 2U; it++)
    {
        const float mid = 0.5f * (lo + hi);
        float sl = 0.0f, sh = 0.0f; uint32_t nl = 0U, nh = 0U;
        for (uint32_t i = 0U; i < n; i++) { if (x[i] > mid) { sh += x[i]; nh++; } else { sl += x[i]; nl++; } }
        if (nl == 0U || nh == 0U) return 1.0e30f;
        lo = sl / (float)nl; hi = sh / (float)nh;
    }
    const float mid = 0.5f * (lo + hi);
    float c = 0.0f;
    for (uint32_t i = 0U; i < n; i++) { const float d = (x[i] > mid) ? (x[i] - hi) : (x[i] - lo); c += d * d; }
    const float amp = hi - lo;                       /* نرمال به دامنه: فیلتری که فقط موج رو صاف/کوچیک کنه برنده نشه */
    return (amp > 0.2f) ? (c / (amp * amp)) : 1.0e30f;
}

static uint8_t sc_cmp_calibrate(const float *v, uint32_t n, float dt, float *rOut, float *tOut)
{
    const uint32_t ne = s_scNe;
    if (ne < 6U || dt <= 0.0f) return 0U;
    uint32_t e0 = 0U; while (e0 < ne && s_scDir[e0] < 0) e0++;
    if (e0 + 6U >= ne) return 0U;
    if (s_scPos[e0] < 16.0f) return 0U;
    const uint32_t w0 = (uint32_t)s_scPos[e0] - 8U, w1 = (uint32_t)s_scPos[e0 + 6U];     /* ۳ پریود */
    if (w0 + 64U >= w1 || w1 > n) return 0U;
    const uint32_t W = w1 - w0;
    if (W > 2400U) return 0U;

    static float tmp[2400];
    float bestC = 1.0e30f, bestR = 1.0f, bestT = s_cmpTau;
    float tbList[16]; { float t = 20.0e-6f; for (uint32_t i = 0U; i < 16U; i++) { tbList[i] = t; t *= 1.3f; } }

    for (uint32_t pass = 0U; pass < 2U; pass++)
    {
        const uint32_t NR = (pass == 0U) ? 26U : 5U, NT = (pass == 0U) ? 16U : 5U;
        const float r0 = bestR, t0 = bestT;
        for (uint32_t ir = 0U; ir < NR; ir++)
        {
            float r = (pass == 0U) ? (0.12f * powf(1.2f, (float)ir)) : (r0 * powf(1.045f, (float)ir - 2.0f));
            if (r < 0.12f) r = 0.12f;
            for (uint32_t it = 0U; it < NT; it++)
            {
                const float tb = (pass == 0U) ? tbList[it] : (t0 * powf(1.065f, (float)it - 2.0f));
                sc_inv(v + w0, tmp, W, dt, r, tb, v[w0]);
                const float c = sc_cost2(tmp, W);
                if (c < bestC) { bestC = c; bestR = r; bestT = tb; }
            }
        }
    }
    if (bestC > 1.0e29f) return 0U;
    if (bestC / (float)W > 0.02f) return 0U;                     /* بعد از بهترین جبران هم «دو‌سطحی» نیست = مربع نیست (سینوس/نویز) : یاد نگیر */
    const float cId = sc_cost2(v + w0, W);                       /* بدون جبران */
    *tOut = bestT;
    *rOut = (bestC > 0.7f * cId) ? 1.0f : bestR;                  /* اگه جبران قابل‌توجه بهتر نیست: r=1 */
    return 1U;
}

static uint16_t s_scTop[LCD_WIDTH], s_scBot[LCD_WIDTH];

/* فاز کسری 0..1 بدون libm */
static float sc_frac(float x)
{
    float f = x - (float)(int32_t)x;
    return (f < 0.0f) ? f + 1.0f : f;
}

static void App_Scope(void)
{
    LEDs_AllOff();
    UI_Begin("SCOPE");
    UI_Hint("L/R zoom U/D rate OK hold L@x1:src");
    LCD_FillRect(0U, SCOPE_Y0, LCD_WIDTH, SCOPE_H, 0x0000U);
    for (uint16_t x = 0U; x < LCD_WIDTH; x++) { s_scTop[x] = 0xFFFFU; s_scBot[x] = 0xFFFFU; }

    static const uint16_t k_tbUs[] = { 0U, 2U, 5U, 10U, 20U, 50U, 100U, 200U };
    static const uint8_t  k_zm[]   = { 1U, 2U, 4U, 8U, 16U };
    uint8_t  tb = 0U, zi = 0U, hold = 0U;
    uint32_t autoGapUs = VM_AUTO_DEF_US;
    uint32_t last = 0U - 1000U;
    uint8_t  autoFail = 0U;
    s_cmpRho = SC_CMP_RHO0; s_cmpTau = SC_CMP_TAU0; s_cmpCal = 0U; s_cmpCalOk = 0U; s_cmpCalLocked = 0U; s_cmpLr = 0.0f; s_cmpLt = 0.0f;   /* هر بار ورود به اسکوپ دوباره کالیبره می‌شه */
    uint8_t  srcI = 0U;       /* پیش‌فرض: PB0 با جبران نرم‌افزاری RC برای C4=780pF */
    uint8_t  src = 0U, nc = 1U;
    if (s_cpuRealHz == 0U)
    {
        const uint32_t m = Clock_MeasureCpuHz();
        if (m > (SystemCoreClock / 3U) && m < (SystemCoreClock * 3U)) s_cpuRealHz = m;
    }
    Probe_PwmStart(PROBE_2, 1000U, 50U);    /* پروب ۲ همیشه موج مربعی ۱kHz ، ۵۰٪ می‌ده برای تست (پروب ۱ فقط می‌خونه) */
    /* منبع: 1 = خروجی OPAMP (PC4 ، کم‌نویز) ، 0 = مستقیم از PB0 ؛ با چپ در زوم x1 عوض می‌شه */

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_RIGHT && zi + 1U < sizeof(k_zm)) { zi++; last = 0U - 1000U; }
        if (in == MENU_INPUT_LEFT  && zi > 0U)                { zi--; last = 0U - 1000U; }
        else if (in == MENU_INPUT_LEFT)                       { srcI = (uint8_t)((srcI + 1U) % 6U); last = 0U - 1000U; }
        if (in == MENU_INPUT_UP   && tb + 1U < (sizeof(k_tbUs) / sizeof(k_tbUs[0]))) { tb++; last = 0U - 1000U; }
        if (in == MENU_INPUT_DOWN && tb > 0U)                 { tb--; last = 0U - 1000U; }
        if (in == MENU_INPUT_SELECT) hold = (uint8_t)!hold;
        src = (uint8_t)(srcI % 3U); nc = (uint8_t)(srcI >= 3U);
        if (hold || !TICK_DUE(last, 100U)) continue;
        last = HAL_GetTick();

        uint32_t gapUs = (tb == 0U) ? autoGapUs : (uint32_t)k_tbUs[tb];
        uint32_t gap   = (cpu_hz() / 1000000U) * gapUs;
        uint32_t cyc   = (src == 2U) ? Adc_CapturePA6(s_scBuf, SC_N, gap) : src ? Adc_CapturePC4(s_scBuf, SC_N, gap) : Adc_CapturePB0(s_scBuf, SC_N, gap);
        if (cyc == 0U) continue;

        uint32_t mn = 65535U, mx = 0U;
        for (uint32_t i = 0U; i < SC_N; i++)
        {
            if (s_scBuf[i] < mn) mn = s_scBuf[i];
            if (s_scBuf[i] > mx) mx = s_scBuf[i];
            s_scV[i] = (src == 2U) ? Probe_CountsToVolts(RD_PA6, s_scBuf[i]) : src ? vm_counts_to_vin(s_scBuf[i]) : sc_counts_to_vin(s_scBuf[i]);
        }
        float vLo = (src == 2U) ? Probe_CountsToVolts(RD_PA6, mn) : src ? vm_counts_to_vin(mn) : sc_counts_to_vin(mn);
        float vHi = (src == 2U) ? Probe_CountsToVolts(RD_PA6, mx) : src ? vm_counts_to_vin(mx) : sc_counts_to_vin(mx);
        const float dt = ((float)cyc / (float)cpu_hz()) / (float)SC_N;

        /* جبران فیلتر RC ورودی (s_scR موقتاً بافر میانگین‌گیری) ؛ بعدش همه‌چیز (اندازه‌گیری و رسم) روی داده‌ی جبران‌شده */
        /* ۱) لبه‌های خام -> یادگیری rho/tb از سقف پالس‌ها ؛ ۲) فیلتر معکوس */
        if (src == 2U || nc || !SC_COMP)      /* PA6 (یا جبران خاموش) = خروجی بافر پروب۲ (فالوئر، DC-coupled) : مسیر تخت، بدون هیچ جبرانی -> شکل موج واقعی */
        {
            for (uint32_t i = 0U; i < SC_N; i++) s_scC[i] = s_scV[i];
        }
        else
        {
            /* کالیبراسیون یک‌بار‌مصرف: وقتی ورودی همون مربع تست (۱kHz ، ۵۰٪ ، ~۳.۳V) باشه ، ۳ بار موفق -> r,tb قفل می‌شن */
                        if (!s_cmpCalLocked)
            {
                float hz0, du0, lo0, hi0, r00; uint32_t f0;
                if (sc_digital(s_scV + SC_WARM, SC_N - SC_WARM, dt, s_scR, &hz0, &du0, &lo0, &hi0, &f0, &r00))
                {
                    /* گیت: لبه‌های بالارونده‌ی تقریباً هم‌فاصله + دامنه‌ی کافی ؛ (کیفیت دو‌سطحی‌بودن بعد از جبران داخل sc_cmp_calibrate چک می‌شه) */
                    float pr[8]; uint32_t np = 0U;
                    for (uint32_t m = 0U; m < s_scNe && np < 8U; m++) if (s_scDir[m] > 0) pr[np++] = s_scPos[m];
                    uint8_t reg = (np >= 4U) ? 1U : 0U;
                    if (reg)
                    {
                        const float p0 = pr[1] - pr[0];
                        for (uint32_t m = 2U; m < np; m++) { const float q = pr[m] - pr[m - 1U]; if (q < 0.95f * p0 || q > 1.05f * p0) reg = 0U; }
                    }
                    if (reg && (hi0 - lo0) > 1.5f && hz0 > 100.0f && hz0 < 20000.0f)
                    {
                        float rr, tt;
                        if (sc_cmp_calibrate(s_scV + SC_WARM, SC_N - SC_WARM, dt, &rr, &tt))
                        {
                            const float lr = logf(rr), lt = logf(tt);
                            if (s_cmpCalOk > 0U && (fabsf(lr - s_cmpLr / (float)s_cmpCalOk) > 0.5f || fabsf(lt - s_cmpLt / (float)s_cmpCalOk) > 0.5f))
                            { s_cmpCalOk = 0U; s_cmpLr = 0.0f; s_cmpLt = 0.0f; }      /* نتیجه‌ها با هم نمی‌خونن: از اول */
                            s_cmpLr += lr; s_cmpLt += lt;
                            if (++s_cmpCalOk >= 3U)
                            {
                                s_cmpRho = expf(s_cmpLr / 3.0f); s_cmpTau = expf(s_cmpLt / 3.0f); s_cmpCalLocked = 1U; s_cmpCal = 1U;
                            }
                        }
                    }
                }
            }
            sc_cmp_apply(s_scV, s_scC, SC_N, dt);
        }
        float cLo = 1.0e9f, cHi = -1.0e9f;
        for (uint32_t i = SC_WARM; i < SC_N; i++) { if (s_scC[i] < cLo) cLo = s_scC[i]; if (s_scC[i] > cHi) cHi = s_scC[i]; }
        if (cLo < 0.0f) cLo = 0.0f;
        const float dispHi = cHi;                       /* مقیاس رسم = بیشینه‌ی واقعی موج (نه سطح میانگین‌شده) */

        float hzD = 0.0f, dutyD = 0.0f, loD = 0.0f, hiD = 0.0f, r0 = 0.0f; uint32_t firstD = 0U;
        s_scNe = 0U;
        uint8_t dig = sc_digital(s_scC + SC_WARM, SC_N - SC_WARM, dt, s_scR, &hzD, &dutyD, &loD, &hiD, &firstD, &r0);
        if (dig) { vLo = loD; vHi = hiD; }
        else     { vLo = cLo; vHi = cHi; }
        uint8_t sig = ((vHi - vLo) > VM_SIG_SPAN_V) ? 1U : 0U;

        /* ---- بازسازی قفل‌شده روی ماشه (مثل DSO): پریود و عرض پالس = «میانه»ی همه‌ی لبه‌های ثبت‌شده.
         *      گلیچ و لرزش تک‌لبه اثری ندارن و هر فریم دقیقاً همون موج رو می‌کشه. ---- */
//        uint8_t  fold = 0U;
//        float    dutyF = dutyD, jit = 0.0f;
//        const float *dv = dig ? s_scR : s_scV;
//        uint32_t start = 0U;
//        if (dig && sig && hzD > 0.0f)
//        {
//            static float per[SC_MAXE / 2U], wid[SC_MAXE / 2U];
//            uint32_t np = 0U, nw = 0U; float lastR = -1.0f;
//            for (uint32_t m = 0U; m < s_scNe; m++)
//            {
//                if (s_scDir[m] > 0)
//                {
//                    if (lastR >= 0.0f && np < SC_MAXE / 2U) per[np++] = s_scPos[m] - lastR;
//                    lastR = s_scPos[m];
//                    if (m + 1U < s_scNe && s_scDir[m + 1U] < 0 && nw < SC_MAXE / 2U) wid[nw++] = s_scPos[m + 1U] - s_scPos[m];
//                }
//            }
//            if (np >= 2U && nw >= 2U)
//            {
//                for (uint32_t i = 1U; i < np; i++) { float k = per[i]; uint32_t j = i; while (j > 0U && per[j - 1U] > k) { per[j] = per[j - 1U]; j--; } per[j] = k; }
//                for (uint32_t i = 1U; i < nw; i++) { float k = wid[i]; uint32_t j = i; while (j > 0U && wid[j - 1U] > k) { wid[j] = wid[j - 1U]; j--; } wid[j] = k; }
//                float P = per[np / 2U], W = wid[nw / 2U];
//                jit = (per[np - 1U - np / 8U] - per[np / 8U]) / P;                 /* پخش پریودها (۱۰–۹۰٪) */
//                if (P >= 8.0f && W > 1.0f && W < P && jit < 0.35f)
//                {
//                    fold = 1U;
//                    dutyF = 100.0f * W / P;
//                    hzD   = 1.0f / (P * dt);
//                    const float invP = 1.0f / P, dth = W * invP;
//                    for (uint32_t j = 0U; j < SC_SHOW; j++)
//                        s_scR[j] = (sc_frac((float)j * invP - 0.04f) < dth + 0.0f && sc_frac((float)j * invP - 0.04f) >= 0.0f) ? hiD : loD;
//                    dv = s_scR;
//                    start = 0U;
//                }
//            }
//        }

        /* ---- LOCK پایدار با هیسترزیس ---- */
        static uint8_t lockState = 0U;
        static uint8_t goodFrames = 0U;
        static uint8_t badFrames = 0U;

        static float lockP = 0.0f;
        static float lockW = 0.0f;
        static float lockHi = 0.0f;
        static float lockLo = 0.0f;

        uint8_t fold = 0U;
        float dutyF = dutyD;
        float jit = 0.0f;
        const float *dv = s_scC;      /* همیشه نمونه‌های واقعی (جبران‌شده)؛ هیچ موجی ساخته نمی‌شه (مثل DSO) */
        uint32_t start = SC_WARM;

        uint8_t candidate = 0U;
        float candidateP = 0.0f;
        float candidateW = 0.0f;

        if (dig && sig && hzD > 0.0f)
        {
            static float per[SC_MAXE / 2U];
            static float wid[SC_MAXE / 2U];

            uint32_t np = 0U;
            uint32_t nw = 0U;
            float lastR = -1.0f;

            for (uint32_t m = 0U; m < s_scNe; m++)
            {
                if (s_scDir[m] > 0)
                {
                    if (lastR >= 0.0f && np < SC_MAXE / 2U)
                        per[np++] = s_scPos[m] - lastR;

                    lastR = s_scPos[m];

                    if (m + 1U < s_scNe &&
                        s_scDir[m + 1U] < 0 &&
                        nw < SC_MAXE / 2U)
                    {
                        wid[nw++] = s_scPos[m + 1U] - s_scPos[m];
                    }
                }
            }

            if (np >= 2U && nw >= 2U)
            {
                for (uint32_t i = 1U; i < np; i++)
                {
                    float k = per[i];
                    uint32_t j = i;

                    while (j > 0U && per[j - 1U] > k)
                    {
                        per[j] = per[j - 1U];
                        j--;
                    }
                    per[j] = k;
                }

                for (uint32_t i = 1U; i < nw; i++)
                {
                    float k = wid[i];
                    uint32_t j = i;

                    while (j > 0U && wid[j - 1U] > k)
                    {
                        wid[j] = wid[j - 1U];
                        j--;
                    }
                    wid[j] = k;
                }

                float P = per[np / 2U];
                float W = wid[nw / 2U];

                if (P > 0.0f)
                    jit = (per[np - 1U - np / 8U] -
                           per[np / 8U]) / P;

                if (P >= 8.0f &&
                    W > 1.0f &&
                    W < P &&
                    jit < 0.35f)
                {
                    candidate = 1U;
                    candidateP = P;
                    candidateW = W;
                }
            }
        }

        /* برای ورود به LOCK سه فریم خوب؛ برای خروج پنج فریم بد */
        if (candidate)
        {
            if (goodFrames < 3U) goodFrames++;
            badFrames = 0U;

            if (!lockState && goodFrames >= 3U)
            {
                lockState = 1U;
                lockP = candidateP;
                lockW = candidateW;
                lockHi = hiD;
                lockLo = loD;
            }
            else if (lockState)
            {
                /* هموارسازی تغییرات جزئی پریود و عرض پالس */
                lockP = lockP * 0.75f + candidateP * 0.25f;
                lockW = lockW * 0.75f + candidateW * 0.25f;

                lockHi = lockHi * 0.75f + hiD * 0.25f;
                lockLo = lockLo * 0.75f + loD * 0.25f;
            }
        }
        else
        {
            goodFrames = 0U;

            if (badFrames < 5U) badFrames++;

            if (badFrames >= 5U)
                lockState = 0U;
        }

        if (0 && lockState && lockP >= 8.0f &&      /* بازسازی مصنوعی خاموش */
            lockW > 1.0f && lockW < lockP)
        {
            fold = 1U;

            dutyF = 100.0f * lockW / lockP;
            hzD = 1.0f / (lockP * dt);

            const float invP = 1.0f / lockP;
            const float dth = lockW * invP;

            for (uint32_t j = 0U; j < SC_SHOW; j++)
            {
                s_scR[j] =
                    (sc_frac((float)j * invP - 0.04f) < dth &&
                     sc_frac((float)j * invP - 0.04f) >= 0.0f)
                    ? lockHi : lockLo;
            }

            dv = s_scR;
            start = 0U;
        }
//        if (!fold && sig)
//        {
//            const float midv = 0.5f * (vLo + vHi);
//            for (uint32_t i = 1U; i < (SC_N - SC_SHOW); i++)
//                if (dv[i - 1U] < midv && dv[i] >= midv) { start = i; break; }
//        }

        {
            /* ماشه‌ی لبه‌ی بالارونده روی وسط دامنه‌ی واقعی با هیسترزیس (مثل DSO) ؛ بدون ماشه = آزادگرد (Auto) */
            const float spanV = cHi - cLo;
            const float midv  = 0.5f * (cHi + cLo);
            const float armV  = midv - 0.10f * spanV;
            if (spanV > VM_SIG_SPAN_V)
            {
                uint8_t armed = (dv[SC_WARM] < armV) ? 1U : 0U;
                for (uint32_t i = SC_WARM + 1U; i < (SC_N - SC_SHOW); i++)
                {
                    if (dv[i] < armV) armed = 1U;
                    else if (armed && dv[i] >= midv) { start = i; break; }
                }
            }
        }

        static float    vtopS = 5.0f;
        static uint8_t  vtopDn = 0U;
        {   /* مقیاس عمودی با هیسترزیس: بالا رفتن فوری ، پایین اومدن فقط بعد از ۸ فریم پشت‌سرهم (دیگه بالا/پایین نمی‌پره) */
            const float want = vm_pick_scale(dispHi);
            if (want > vtopS)      { vtopS = want; vtopDn = 0U; }
            else if (want < vtopS) { if (++vtopDn >= 8U) { vtopS = want; vtopDn = 0U; } }
            else vtopDn = 0U;
        }
        float    vtop  = vtopS;
        uint32_t nShow = SC_SHOW / (uint32_t)k_zm[zi];
        const uint16_t yMin = SCOPE_Y0, yMax = (uint16_t)(SCOPE_Y0 + SCOPE_H - 1U);
        uint16_t prevY = 0xFFFFU;

        for (uint16_t x = 0U; x < LCD_WIDTH; x++)
        {
            uint32_t a = start + ((uint32_t)x * nShow) / LCD_WIDTH;
            uint32_t e = start + ((uint32_t)(x + 1U) * nShow) / LCD_WIDTH;
            if (e <= a) e = a + 1U;
            if (e > SC_N) e = SC_N;
            if (a >= SC_N) a = SC_N - 1U;
            float lo = 1.0e9f, hi = -1.0e9f;
            for (uint32_t i = a; i < e; i++)
            {
                if (dv[i] < lo) lo = dv[i];
                if (dv[i] > hi) hi = dv[i];
            }
            float fh = hi / vtop, fl = lo / vtop;
            if (fh < 0.0f) { fh = 0.0f; }
            if (fl < 0.0f) { fl = 0.0f; }
            if (fh > 1.0f) { fh = 1.0f; }
            if (fl > 1.0f) { fl = 1.0f; }
            uint16_t yTop = (uint16_t)(yMax - (uint16_t)(fh * (float)(SCOPE_H - 1U)));
            uint16_t yBot = (uint16_t)(yMax - (uint16_t)(fl * (float)(SCOPE_H - 1U)));
            if (prevY != 0xFFFFU) { if (prevY < yTop) yTop = prevY; if (prevY > yBot) yBot = prevY; }
            if (yBot <= yTop) { if (yBot < yMax) yBot++; else yTop--; }
            {   float fy = dv[e - 1U] / vtop; if (fy < 0.0f) fy = 0.0f; if (fy > 1.0f) fy = 1.0f;
                prevY = (uint16_t)(yMax - (uint16_t)(fy * (float)(SCOPE_H - 1U))); }
            if (prevY < yMin) prevY = yMin;

            if (s_scTop[x] == yTop && s_scBot[x] == yBot) continue;     /* ستون عوض نشده = دست نمی‌زنیم (بدون لرزش) */
            if (s_scTop[x] == 0xFFFFU)
            {
                sc_col_bg(x, yMin, yTop);
                LCD_FillRect(x, yTop, 1U, (uint16_t)(yBot - yTop + 1U), UI_OK);
                sc_col_bg(x, (uint16_t)(yBot + 1U), (uint16_t)(yMax + 1U));
            }
            else
            {
                uint16_t ot = s_scTop[x], ob = s_scBot[x];
                if (ot < yTop) sc_col_bg(x, ot, (yTop < (uint16_t)(ob + 1U)) ? yTop : (uint16_t)(ob + 1U));
                if (ob > yBot) sc_col_bg(x, (yBot + 1U > ot) ? (uint16_t)(yBot + 1U) : ot, (uint16_t)(ob + 1U));
                LCD_FillRect(x, yTop, 1U, (uint16_t)(yBot - yTop + 1U), UI_OK);
            }
            s_scTop[x] = yTop; s_scBot[x] = yBot;
        }

        float hz = 0.0f, duty = 0.0f; uint8_t tooFast = 0U;
        uint8_t fok;
        if (dig && sig && hzD > 0.0f) { hz = hzD; duty = dutyF; fok = 1U; }
        else if (sig)
        {
            for (uint32_t i = 0U; i < VM_N; i++) s_vmBuf[i] = s_scBuf[i];          /* vm_analyze روی ۲۰۴۸ نمونه‌ی اول */
            fok = vm_analyze((uint16_t)mn, (uint16_t)mx, (uint32_t)(((uint64_t)cyc * VM_N) / SC_N), &hz, &duty, &tooFast);
        }
        else fok = 0U;

        if (tb == 0U)
        {
            if (fok || !sig) autoFail = 0U;
            if (!sig) autoGapUs = VM_AUTO_DEF_US;
            else if (fok)
            {
                float baseS = dt - (float)gapUs * 1.0e-6f;
                if (baseS < 1.0e-7f) baseS = 1.0e-7f;
                float g = (SC_PERIODS / (hz * (float)SC_N) - baseS) * 1.0e6f;
                if (g < 0.0f) g = 0.0f;
                if (g > (float)SC_AUTO_MAX_US) g = (float)SC_AUTO_MAX_US;
                uint32_t gn = (uint32_t)(g + 0.5f);
                uint32_t df = (gn > autoGapUs) ? (gn - autoGapUs) : (autoGapUs - gn);
                if (df * 2U > autoGapUs + 4U) autoGapUs = gn;   /* فقط با تغییر بیش از ۵۰٪ تایم‌بیس عوض بشه */
            }
            else if (tooFast) autoGapUs = 0U;
            else
            {
                /* فقط بعد از ۴ فریم ناموفق پشت سر هم تایم‌بیس رو عوض کن (یه فریم خراب نباید همه‌چیز رو بهم بریزه) */
                if (autoFail < 255U) autoFail++;
                if (autoFail >= 4U) { autoFail = 0U; autoGapUs = autoGapUs * 2U + 10U; if (autoGapUs > SC_AUTO_MAX_US) autoGapUs = SC_AUTO_MAX_US; }
            }
        }

        char b[40], t1[12], t2[12], t3[12];
        UI_FmtFixed(t1, sizeof t1, vHi, 1U);
        if (fok) { UI_FmtFixed(t2, sizeof t2, hz / 1000.0f, 2U); UI_FmtFixed(t3, sizeof t3, duty, 0U); }
        else     { snprintf(t2, sizeof t2, "--"); snprintf(t3, sizeof t3, "--"); }
        snprintf(b, sizeof b, "H%sV %skHz D%s%%   ", t1, t2, t3);
        UI_Text(2U, 206U, 1U, UI_DIM, UI_BG, b);
        snprintf(b, sizeof b, "TB:%s Z:x%u %s %s%s r%u%s C%lu ", (tb == 0U) ? "AUTO" : "man", (unsigned)k_zm[zi], fold ? "LOCK" : "RAW", (src == 2U) ? "P2/PA6" : src ? "PC4" : "PB0", nc ? "-RAW" : "", nc ? 100U : (unsigned)(s_cmpRho * 100.0f + 0.5f), s_cmpCal ? "*" : "", (unsigned long)((cpu_hz() + 500000U) / 1000000U));
        UI_Text(2U, 218U, 1U, UI_DIM, UI_BG, b);
        LED_Blue_Set(sig);
    }
    Probe_PwmStop();
    Probe_DacOff();
    LEDs_AllOff();
}

/* ================================================================== */
/*  Wiring test (تست سیم‌کشی)                                           */
/*                                                                      */
/*  سناریو: یه سر سیم به ECU ماشین وصله، سر دیگه بیرونه.                    */
/*   - Probe2 رو به سر آزاد سیم وصل کن: یه PWM مشخص (۵۰٪) توی سیم می‌فرسته.   */
/*   - فقط Probe1 رو (سوزنی / back-probe) روی نقطه‌های مختلف سیم بکش.        */
/*   - هر جا Probe1 همون PWM رو (با تلورانس) بگیره = سیم تا اونجا سالمه.        */
/*     هر جا نگرفت = سیم بین اون نقطه و Probe2 قطعه -> دستگاه بوق می‌زنه.        */
/*                                                                      */
/*  روش تشخیص (هم‌زمان‌شده با PWM، lock-in):                               */
/*   - ADC دقیقاً ۲۴ نمونه در هر پریود PWM می‌گیره (زمان‌بندی قطعی داخل RAM)     */
/*     روی ۸ پریود. چون PWM و نمونه‌برداری هر دو از یک کریستال‌اند، فرکانس        */
/*     «دقیقاً» معلومه و فقط روی همون فرکانس DFT می‌گیریم.                      */
/*   - swing  = دامنه‌ی معادل موج مربعی (ولت)                                */
/*   - purity = سهم انرژی موج که روی فرکانس ارسالی ماست (نویز/سیگنال ECU = کم)   */
/*   - lock   = هم‌فازی نیمه‌ی اول و دوم پنجره؛ تلورانس فرکانس ≈ ±۳٪            */
/*  چون سر دیگه به ECU وصله و بار می‌ذاره، با OK روی نقطه‌ی تزریق (کنار Probe2)   */
/*  شکل موج «سالم» به‌عنوان مرجع یاد گرفته می‌شه؛ بعد هر جا حداقل ۵۰٪ دامنه‌ی     */
/*  مرجع رو داشت = سالم.                                                  */
/* ================================================================== */
typedef enum { WT_OK = 0, WT_NO_SIGNAL, WT_WEAK, WT_NOISY, WT_TOO_FAST } WtState_t;

static const char *const k_wtReason[] = { "match", "no signal", "weak", "not our PWM", "too fast" };
static const uint32_t    k_wtFreq[] = { 200U, 500U, 1000U, 2000U, 5000U };
#define WT_FREQ_COUNT      ((uint8_t)(sizeof(k_wtFreq) / sizeof(k_wtFreq[0])))
#define WT_FREQ_DEFAULT    2U                 /* 1000 Hz */
#define WT_DUTY            50U                /* دیوتی PWM ارسالی (٪) */

#define WT_SPP             24U                /* نمونه در هر پریود PWM */
#define WT_PERIODS         8U                 /* تعداد پریود در هر اندازه‌گیری (~8ms @1kHz) */
#define WT_N               (WT_SPP * WT_PERIODS)

/* ---- تلورانس‌ها (اینجا تنظیم کن) ---- */
#define WT_NOISE_V         0.10f    /* دامنه‌ی کمتر از این = اصلاً سیگنالی نیست */
#define WT_MIN_SWING_V     0.30f    /* بدون مرجع: حداقل دامنه */
#define WT_REF_FRACTION    0.50f    /* با مرجع: دامنه باید حداقل این کسر از مرجع باشه (تلورانس دامنه) */
#define WT_PURITY_MIN      0.35f    /* حداقل سهم انرژی روی فرکانس ما (موج مربعی ایده‌آل ≈ ۰٫۸۱) */
#define WT_PURITY_REF_FR   0.60f    /* با مرجع: purity باید حداقل این کسر از مرجع باشه */
#define WT_LOCK_MIN        0.85f    /* هم‌فازی دو نیمه‌ی پنجره (≈ تلورانس فرکانس ±۳٪) */
#define WT_BAD_FRAMES      2U       /* چند اندازه‌گیری ناموفق پشت‌سرهم تا «قطع» اعلام بشه */
#define WT_GOOD_FRAMES     2U       /* چند اندازه‌گیری موفق پشت‌سرهم تا «سالم» اعلام بشه */

typedef struct {
    float   swing;      /* دامنه‌ی معادل موج مربعی (پیک تا پیک، ولت) */
    float   purity;     /* 0..1 */
    float   lock;       /* 0..1 : هم‌فازی دو نیمه‌ی پنجره */
    float   pp;         /* پیک‌تاپیک خام (ولت) */
    uint8_t tooFast;    /* 1 = نمونه‌برداری برای این فرکانس کافی نیست */
} WtMeas_t;

/* شکل موج «سالم» که کاربر یاد داده */
typedef struct { uint8_t valid; float swing; float purity; } WtRef_t;

static WtState_t wt_check(const WtMeas_t *m, const WtRef_t *ref)
{
    if (m->tooFast) return WT_TOO_FAST;
    float minSw = ref->valid ? (ref->swing * WT_REF_FRACTION) : WT_MIN_SWING_V;
    if (minSw < WT_NOISE_V) minSw = WT_NOISE_V;
    float minPu = ref->valid ? (ref->purity * WT_PURITY_REF_FR) : WT_PURITY_MIN;
    if (minPu < WT_PURITY_MIN) minPu = WT_PURITY_MIN;

    if (m->swing < WT_NOISE_V)  return WT_NO_SIGNAL;         /* هیچ نوسانی نیومده */
    if (m->purity < minPu || m->lock < WT_LOCK_MIN) return WT_NOISY;   /* نوسان هست ولی همون PWM ما نیست */
    if (m->swing < minSw)       return WT_WEAK;
    return WT_OK;
}

/* sin/cos بدون libm (لینکر libm رو دور می‌ریزه): برای 0 <= w <= ~3 rad ، تیلور روی w/4 و بعد دو بار زاویه‌ی دوبرابر */
static void wt_sincos(float w, float *sn, float *cs)
{
    float a = w * 0.25f, a2 = a * a;
    float s = a * (1.0f - a2 / 6.0f  * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
    float c = 1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f * (1.0f - a2 / 30.0f * (1.0f - a2 / 56.0f)));
    for (uint8_t i = 0U; i < 2U; i++)
    {
        float s2 = 2.0f * s * c;
        float c2 = 1.0f - 2.0f * s * s;
        s = s2; c = c2;
    }
    *sn = s; *cs = c;
}

/* خطی، بدون کلمپ زیر صفر (Adc_PinToVinLinear): تصحیح مخصوص ولت‌متر DC موج کوچیک رو خراب می‌کنه */
static float wt_counts_to_vin(uint32_t c)
{
    return Adc_PinToVinLinear((float)c * 3.3f / 65535.0f);
}

/* فرکانس واقعی PWM از روی رجیسترهای TIM15 (همون فرمول Probe_PwmStart) */
static float wt_pwm_actual_hz(void)
{
    float tclk = (float)(HAL_RCC_GetPCLK2Freq() * 2U);
    return tclk / (((float)TIM15->PSC + 1.0f) * ((float)TIM15->ARR + 1.0f));
}

/* یک اندازه‌گیری روی Probe1 (مسیر PC4 = خروجی OPAMP، امپدانس پایین).
 * گام نمونه‌برداری طوری انتخاب می‌شه که دقیقاً WT_SPP نمونه در هر پریود PWM باشه. */
static void wt_measure(WtMeas_t *m)
{
    static float x[WT_N];
    memset(m, 0, sizeof *m);

    const float f    = wt_pwm_actual_hz();
    uint32_t    step = (uint32_t)((float)SystemCoreClock / (f * (float)WT_SPP) + 0.5f);
    if (step < ADC_CAP_MIN_STEP_CYC + 60U) { m->tooFast = 1U; return; }

    uint32_t cyc = Adc_CapturePC4(s_vmBuf, WT_N, step - ADC_CAP_MIN_STEP_CYC);
    if (cyc == 0U) return;
    uint32_t stepEff = cyc / WT_N;
    if (stepEff > step + 1U) { m->tooFast = 1U; return; }          /* ADC به این گام نمی‌رسه */

    const float dt = (float)stepEff / (float)SystemCoreClock;
    const float w  = 6.2831853f * f * dt;                          /* رادیان در هر نمونه */
    float sw, cw;
    wt_sincos(w, &sw, &cw);

    float mean = 0.0f, mn = 1.0e9f, mx = -1.0e9f;
    for (uint32_t i = 0U; i < WT_N; i++)
    {
        x[i] = wt_counts_to_vin(s_vmBuf[i]);
        mean += x[i];
        if (x[i] < mn) mn = x[i];
        if (x[i] > mx) mx = x[i];
    }
    mean /= (float)WT_N;
    m->pp = mx - mn;

    /* DFT تک‌فرکانسی؛ جمع‌های جداگانه برای نیمه‌ی اول و دوم (برای lock) */
    float c = 1.0f, s = 0.0f, var = 0.0f;
    float re1 = 0.0f, im1 = 0.0f, re2 = 0.0f, im2 = 0.0f;
    for (uint32_t i = 0U; i < WT_N; i++)
    {
        float xi = x[i] - mean;
        var += xi * xi;
        if (i < (WT_N / 2U)) { re1 += xi * c; im1 -= xi * s; }
        else                 { re2 += xi * c; im2 -= xi * s; }
        float cn = c * cw - s * sw;
        s = s * cw + c * sw;
        c = cn;
    }
    var /= (float)WT_N;

    const float re = re1 + re2, im = im1 + im2;
    const float a1 = 2.0f * __builtin_sqrtf(re * re + im * im) / (float)WT_N;     /* دامنه‌ی هارمونیک اول */
    m->swing = a1 * 1.5707963f;                                                   /* ×π/2 : معادل موج مربعی ۵۰٪ */
    float pu = (var > 1.0e-6f) ? (0.5f * a1 * a1) / var : 0.0f;
    m->purity = (pu < 0.0f) ? 0.0f : ((pu > 1.0f) ? 1.0f : pu);

    const float n1 = __builtin_sqrtf(re1 * re1 + im1 * im1);
    const float n2 = __builtin_sqrtf(re2 * re2 + im2 * im2);
    const float nt = __builtin_sqrtf(re * re + im * im);
    m->lock = ((n1 + n2) > 1.0e-6f) ? nt / (n1 + n2) : 0.0f;
}

/* خط رو فقط وقتی متن/رنگش عوض شده دوباره می‌کشه (بدون چشمک) */
typedef struct { char txt[24]; uint16_t fg; } WtLine_t;

static void wt_row(WtLine_t *l, uint8_t row, uint16_t fg, const char *s)
{
    if (l->fg == fg && strncmp(l->txt, s, sizeof l->txt) == 0) return;
    snprintf(l->txt, sizeof l->txt, "%s", s);
    l->fg = fg;
    UI_Row(row, fg, UI_BG, s);
}

/* نوار دامنه: پُری = دامنه‌ی فعلی نسبت به مرجع */
static void wt_bar(uint16_t w, uint16_t col)
{
    LCD_FillRect(4U, 172U, 232U, 14U, UI_DIM);
    LCD_FillRect(6U, 174U, 228U, 10U, UI_BG);
    if (w != 0U) LCD_FillRect(6U, 174U, w, 10U, col);
}

static void wt_buzzer(uint8_t on)
{
    HAL_GPIO_WritePin(Buzzer_GPIO_Port, Buzzer_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* مرجع: ۶ اندازه‌گیری پشت‌سرهم از موج سالم؛ حداقل ۵ تاش باید معتبر باشه */
static uint8_t wt_learn(WtRef_t *ref)
{
    float sw = 0.0f, pu = 0.0f; uint8_t good = 0U;
    for (uint8_t i = 0U; i < 6U; i++)
    {
        WtMeas_t m;
        wt_measure(&m);
        if (!m.tooFast && m.swing >= WT_MIN_SWING_V && m.purity >= WT_PURITY_MIN && m.lock >= WT_LOCK_MIN)
        {
            sw += m.swing; pu += m.purity; good++;
        }
        HAL_Delay(5);
    }
    if (good < 5U) return 0U;
    ref->valid  = 1U;
    ref->swing  = sw / (float)good;
    ref->purity = pu / (float)good;
    return 1U;
}

/* ================================================================== */
/*  کلیدها:  OK = یاد گرفتن مرجع (Probe1 کنار Probe2 روی سیم سالم)          */
/*           L/R = حالت بوق (قطع / سالم / بی‌صدا)   U/D = فرکانس تست        */
/* ================================================================== */
/* ================================================================== */
/*  تست سیم‌کشی: کوپلاژ خازنی از روی روکش (یا تماس مستقیم)                                          */
/*                                                                                              */
/*  P2 یه PWM ۵۰٪ توی سیم می‌فرسته؛ P1 رو روی سیم (حتی روی روکش پلاستیکی) می‌کشی. هر جا همون PWM رو دید     */
/*  = WIRE OK ، هر جا ندید = BROKEN + بوق.                                                          */
/*                                                                                              */
/*  ⚠ چرا فرکانس «خیلی بالا» همیشه بهتر نیست؟ ورودی P1 (R8 → گره‌ی PB0 با C4=220pF به زمین) یه فیلتر          */
/*    پایین‌گذره. سیگنال کوپل‌شده از روکش (خازن Cc ~ چند pF) توی گره‌ی PB0 تقریباً ≈ V × Cc / C4 می‌شه، یعنی     */
/*    مستقل از فرکانس، ولی فقط تا جایی که 1/(2π·f·R8·C4) >> 1 و جریان از R8 رد بشه (چند ده kHz تا چند صد kHz).  */
/*    بالاتر، خازن پراکنده‌ی نوک پروب جریان رو می‌گیره و بعدش فیلتر R8·C4 دوباره ۱۰۰ برابر ضعیفش می‌کنه:       */
/*    در 20MHz کوپلاژ از روکش چند ده میکروولت می‌شه (زیر نویز ADC) ؛ در عوض تماس مستقیم فلز-به-فلز در همه‌ی       */
/*    فرکانس‌ها دیده می‌شه. به‌جای حدس زدن، دکمه‌ی OK همه‌ی فرکانس‌ها (5kHz..20MHz) رو اسکن می‌کنه و بهترین       */
/*    نسبت سیگنال/نویز رو با P1 روی سیم انتخاب می‌کنه.                                               */
/*    ۱۰۰MHz با این سخت‌افزار شدنی نیست (74HC125 + R35=100Ω + کابل + ADC با ~0.77MS/s) ؛ سقف عملی ≈ 20MHz.    */
/*                                                                                              */
/*  تشخیص: ۲۰۴۸ نمونه‌ی ADC از PB0 با گام ثابت (هم‌زمان با کلاک PWM) ؛ دامنه‌ی مولفه‌ی دقیقاً هم‌فرکانس PWM با       */
/*  lock-in (پنجره‌ی Hann) ؛ نویز از دو «فرکانس خالی» (×1.37 و ×0.61) توی همون ضبط → PWM هیچ‌وقت قطع نمی‌شه      */
/*  (قبلاً هر ۱۵ms برای اندازه‌گیری نویز خاموش/روشن می‌شد).                                          */
/*  کلیدها: OK = اسکن فرکانس (P1 روی سیم)   L/R = فرکانس دستی   U/D = صدا   BACK                      */
/* ================================================================== */
#define WP_N          2048U      /* نمونه‌ی ADC در هر اندازه‌گیری (~2.7ms ؛ باید ≤ VM_N باشه) */
#define WP_GAP        4U         /* گام حداقلی = 360+4 سیکل ؛ گام واقعی با wp_pick_step انتخاب می‌شه */
#define WP_NOISE_FLOOR 1.5       /* کف نویز دامنه (شمارش ADC ؛ هر شمارش ≈ 50µV) */
#define WP_SNR_MIN    4.0        /* دامنه باید حداقل این‌قدر برابر نویز باشه (نگه‌داشتن ۱۲۰ms چند اندازه‌گیری رو جمع می‌کنه) */
#define WP_AMP_MIN    5.0        /* ... و حداقل این‌قدر شمارش (≈ 250µV روی پایه‌ی ADC) */
#define WP_REF_FRAC   0.30       /* بعد از اسکن: حداقل این کسر از دامنه‌ی موقع اسکن (تلورانس دامنه) */
#define WP_BEEP_HOLD  120U       /* ms: قطع‌شدن‌های خیلی کوتاه هم شنیده بشن */

static const uint32_t k_wpF[] = { 5000U, 10000U, 20000U, 50000U, 100000U, 200000U, 500000U,
                                  1000000U, 2000000U, 5000000U, 10000000U, 20000000U };
#define WP_F_COUNT   ((uint8_t)(sizeof k_wpF / sizeof k_wpF[0]))
#define WP_F_DEFAULT 3U          /* 50 kHz تا اسکن انجام بشه */

static double wp_sqrt(double a2)
{
    if (a2 <= 0.0) return 0.0;
    double r = (a2 > 1.0) ? a2 : 1.0;
    for (int it = 0; it < 40; it++) r = 0.5 * (r + a2 / r);
    return r;
}

/* گام نمونه‌برداری (سیکل CPU): نسبت f/fs برای همه‌ی تن‌ها (سیگنال و دو تن نویز) از 0 و 0.5 دور باشه
 * (وگرنه بعد از aliasing روی DC یا روی خودش می‌افتن). */
static uint32_t wp_pick_step(double f, const double *tones, uint32_t nt, uint32_t minStep, double clk)
{
    uint32_t best = minStep; double bestM = -1.0;
    for (uint32_t st = minStep; st < minStep + 120U; st++)
    {
        double m = 1.0;
        for (uint32_t k = 0U; k < nt; k++)
        {
            double q = tones[k] * f * (double)st / clk; q -= (double)(uint64_t)q;     /* 0..1 */
            double d = (q < 0.25) ? q : (q < 0.5) ? 0.5 - q : (q < 0.75) ? q - 0.5 : 1.0 - q;
            if (d < m) m = d;
        }
        if (m > bestM) { bestM = m; best = st; }
    }
    return best;
}

/* دامنه‌ی قله‌ی مولفه‌ی f (شمارش ADC) با lock-in و پنجره‌ی Hann (نشت از سیگنال قوی به تن‌های نویز ناچیز) ؛
 * فاز مجهول ، DC و کند حذف. نمونه i در زمان i*step/clk گرفته شده. */
static double wp_demod(const uint16_t *x, uint32_t n, double f, uint32_t step, double clk)
{
    double mean = 0.0;
    for (uint32_t i = 0U; i < n; i++) mean += (double)x[i];
    mean /= (double)n;

    double q = f * (double)step / clk; q -= (double)(uint64_t)q;
    double th = 6.283185307179586 * q; int neg = 0;
    if (th > 3.141592653589793) { th = 6.283185307179586 - th; neg = 1; }
    float sn, cs; wt_sincos((float)th, &sn, &cs);
    const double c = (double)cs, s = neg ? -(double)sn : (double)sn;
    float hs_, hc_; wt_sincos((float)(6.283185307179586 / (double)n), &hs_, &hc_);
    const double hc = (double)hc_, hs = (double)hs_;

    double pr = 1.0, pi = 0.0, hr = 1.0, hi = 0.0, I = 0.0, Q = 0.0, W = 0.0;
    for (uint32_t i = 0U; i < n; i++)
    {
        const double w = 0.5 - 0.5 * hr;                      /* Hann */
        const double d = ((double)x[i] - mean) * w;
        I += d * pr; Q += d * pi; W += w;
        double nr = pr * c - pi * s, ni = pr * s + pi * c;
        double nhr = hr * hc - hi * hs, nhi = hr * hs + hi * hc;
        if ((i & 63U) == 63U)                                 /* مدول 1 بمونه (خطای گردکردن) */
        {
            double m2 = 1.5 - 0.5 * (nr * nr + ni * ni);   nr *= m2; ni *= m2;
            double h2 = 1.5 - 0.5 * (nhr * nhr + nhi * nhi); nhr *= h2; nhi *= h2;
        }
        pr = nr; pi = ni; hr = nhr; hi = nhi;
    }
    return (W > 0.0) ? 2.0 * wp_sqrt(I * I + Q * Q) / W : 0.0;
}

/* ضریب بین «فرکانسی که از رجیسترها حساب می‌شه» و فرکانس واقعی روی سیم ؛ در اسکن پیدا می‌شه (1 ، 0.5 یا 2).
 * (روی این برد PWM نصف فرکانس درخواستی بیرون می‌اومد ؛ کلاک تایمر الان با DWT اندازه‌گیری می‌شه ولی این ضریب
 *  هم همیشه با خود سیگنال چک می‌شه تا به هیچ فرضی بستگی نداشته باشه.) */
static double s_wpK = 1.0;

/* فرکانس PWM از رجیسترها: کلاک تایمر (اندازه‌گیری‌شده) ÷ (PSC+1) ÷ (ARR+1) */
static double wp_f_actual(void)
{
    return (double)Probe_TimerClkHz() / (((double)TIM15->PSC + 1.0) * ((double)TIM15->ARR + 1.0)) * s_wpK;
}

/* یک ضبط از PB0: دامنه‌ی سیگنال (f) و نویز (بیشینه‌ی دو فرکانس خالی) ، هر دو شمارش ADC */
static uint8_t wp_capture(double f, double *aSig, double *aNz)
{
    static const double k_t[3] = { 1.0, 1.37, 0.61 };
    const double clk = (double)SystemCoreClock;
    uint32_t step = wp_pick_step(f, k_t, 3U, ADC_CAP_MIN_STEP_CYC + WP_GAP, clk);
    uint32_t cyc  = Adc_CapturePB0(s_vmBuf, WP_N, step - ADC_CAP_MIN_STEP_CYC);
    if (cyc == 0U) return 0U;
    const uint32_t se = cyc / WP_N;
    *aSig = wp_demod(s_vmBuf, WP_N, f * k_t[0], se, clk);
    const double n1 = wp_demod(s_vmBuf, WP_N, f * k_t[1], se, clk);
    const double n2 = wp_demod(s_vmBuf, WP_N, f * k_t[2], se, clk);
    double nz = (n1 > n2) ? n1 : n2;
    *aNz = (nz < WP_NOISE_FLOOR) ? WP_NOISE_FLOOR : nz;
    return 1U;
}

typedef struct { uint8_t valid; double ref; double air; } WpCal_t;     /* دامنه‌ی موقع اسکن */

/* اسکن همه‌ی فرکانس‌ها با P1 روی سیم: بهترین نسبت سیگنال/نویز ؛ بین فرکانس‌هایی که ≥70٪ بهترین‌ان کمترین رو
 * برمی‌دارم (فرکانس پایین‌تر = کم‌حساس‌تر به کابل و پراکندگی). 1 = موفق. */
__attribute__((unused)) static uint8_t wp_sweep(uint8_t *bestFi, double *refAmp)
{
    static const double k_kk[3] = { 1.0, 0.5, 2.0 };
    double sc[WP_F_COUNT], am[WP_F_COUNT], best = -1.0;
    double kb[WP_F_COUNT];
    char b[24];
    for (uint8_t i = 0U; i < WP_F_COUNT; i++)
    {
        sc[i] = 0.0; am[i] = 0.0; kb[i] = 1.0;
        s_wpK = 1.0;
        Probe_PwmStart(PROBE_2, k_wpF[i], 50U);
        HAL_Delay(3);
        const double f0 = wp_f_actual();
        for (uint8_t k = 0U; k < 3U; k++)                  /* فرکانس واقعی = ضریب × رجیستر (معمولاً 1) */
        {
            double a1, n1, a2, n2;
            if (!wp_capture(f0 * k_kk[k], &a1, &n1)) continue;
            if (!wp_capture(f0 * k_kk[k], &a2, &n2)) { a2 = a1; n2 = n1; }
            const double aa = (a1 < a2) ? a1 : a2;          /* کمینه‌ی دو ضبط: ضبط‌های اتفاقی رو حذف می‌کنه */
            const double nz = (n1 > n2) ? n1 : n2;
            const double scr = (aa >= WP_AMP_MIN) ? aa / nz : 0.0;
            if (scr > sc[i]) { sc[i] = scr; am[i] = aa; kb[i] = k_kk[k]; }
        }
        if (sc[i] > best) best = sc[i];
        snprintf(b, sizeof b, "Scan %u/%u      ", (unsigned)(i + 1U), (unsigned)WP_F_COUNT);
        UI_Text(10U, 100U, 2U, UI_ORANGE, UI_BG, b);
    }
    if (best < WP_SNR_MIN) { s_wpK = 1.0; return 0U; }       /* با P1 روی سیم هم چیزی دیده نشد */
    for (uint8_t i = 0U; i < WP_F_COUNT; i++)
        if (sc[i] >= 0.7 * best) { *bestFi = i; *refAmp = am[i]; s_wpK = kb[i]; return 1U; }
    return 0U;
}

static void wp_line(uint16_t y, uint16_t fg, const char *txt, char *cache)
{
    if (strncmp(cache, txt, 31U) == 0) return;          /* فقط وقتی متن عوض شده دوباره بکش (بدون چشمک) */
    snprintf(cache, 32U, "%s", txt);
    UI_Text(10U, y, 2U, fg, UI_BG, txt);
}

static void wp_fmt_f(char *dst, size_t n, uint32_t f)
{
    if (f >= 1000000U) snprintf(dst, n, "%luMHz", (unsigned long)(f / 1000000U));
    else               snprintf(dst, n, "%lukHz", (unsigned long)(f / 1000U));
}

static void App_WiringTest(void)
{
    LEDs_AllOff();
    Probe_AllOff();
    UI_Begin("WIRING TEST");
    UI_Hint("OK calib L/R f U/D snd");

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    uint8_t  fi = WP_F_DEFAULT, snd = 1U, ok = 0U, shown = 2U;
    uint32_t last = 0U - 100U, lastGood = 0U - 1000U, lastDraw = 0U - 1000U, msgUntil = 0U;
    char     c1[32] = "", c2[32] = "", c3[32] = "", c4[32] = "", msg[24] = "";
    WpCal_t  cal = { 0U, 0.0, 0.0 };
    double   aEma = 0.0;
    s_wpK = 1.0;
    uint32_t cpuHz = 0U; uint8_t cpuDone = 0U;           /* مرجع مستقل (HSI) ؛ بعد از اولین رسم اندازه‌گیری می‌شه تا ورود به صفحه کند نباشه */

    Probe_PwmStart(PROBE_2, k_wpF[fi], 50U);            /* P2 خروجی می‌ده ، P1 فقط می‌خونه (OE=Hi-Z) */
    HAL_Delay(5);

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_LEFT  && fi > 0U)              { fi--; cal.valid = 0U; Probe_PwmStart(PROBE_2, k_wpF[fi], 50U); HAL_Delay(3); shown = 2U; lastDraw = 0U - 1000U; }
        if (in == MENU_INPUT_RIGHT && fi + 1U < WP_F_COUNT) { fi++; cal.valid = 0U; Probe_PwmStart(PROBE_2, k_wpF[fi], 50U); HAL_Delay(3); shown = 2U; lastDraw = 0U - 1000U; }
        if (in == MENU_INPUT_UP || in == MENU_INPUT_DOWN)   { snd = (uint8_t)!snd; lastDraw = 0U - 1000U; }
        if (in == MENU_INPUT_SELECT)       /* کالیبراسیون روی همین فرکانس: اول P1 دور از سیم (هوا) ، بعد روی سیم */
        {
            wt_buzzer(0U);
            LCD_FillRect(0U, 52U, LCD_WIDTH, 60U, UI_BG);
            UI_Text(10U, 64U, 2U, UI_ORANGE, UI_BG, "P1 AWAY from wire");
            HAL_Delay(2000);
            double airA = 0.0, airN = WP_NOISE_FLOOR;
            for (uint8_t k = 0U; k < 12U; k++)                    /* هوا: بیشینه (بدترین حالت نشت مستقیم P2->P1) */
            {
                double a, n;
                if (wp_capture(wp_f_actual(), &a, &n)) { if (a > airA) airA = a; if (n > airN) airN = n; }
                HAL_Delay(5);
            }
            wt_buzzer(1U); HAL_Delay(150); wt_buzzer(0U);
            LCD_FillRect(0U, 52U, LCD_WIDTH, 60U, UI_BG);
            UI_Text(10U, 64U, 2U, UI_ORANGE, UI_BG, "P1 ON the wire..");
            HAL_Delay(2500);
            double wsum = 0.0; uint8_t wn = 0U;
            for (uint8_t k = 0U; k < 16U; k++)                    /* سیم: میانگین */
            {
                double a, n;
                if (wp_capture(wp_f_actual(), &a, &n)) { wsum += a; wn++; }
                HAL_Delay(5);
            }
            const double wireA = wn ? wsum / (double)wn : 0.0;
            const double base  = (airA > airN) ? airA : airN;
            const double ratio = wireA / ((base > 1.0) ? base : 1.0);
            char t[12]; wp_fmt_f(t, sizeof t, k_wpF[fi]);
            if (wireA >= WP_AMP_MIN && ratio >= 1.6)
            {
                cal.valid = 1U; cal.ref = wireA; cal.air = base; aEma = 0.0;
                snprintf(msg, sizeof msg, "OK %s x%u.%u", t, (unsigned)ratio, (unsigned)((ratio - (double)(unsigned)ratio) * 10.0));
            }
            else
            {
                cal.valid = 0U;
                snprintf(msg, sizeof msg, "%s no diff x%u", t, (unsigned)ratio);
            }
            msgUntil = HAL_GetTick() + 3000U;
            shown = 2U; lastDraw = 0U - 1000U;
            c1[0] = c2[0] = c3[0] = c4[0] = '\0';
        }

        if (!TICK_DUE(last, 15U)) continue;
        last = HAL_GetTick();

        double aSig = 0.0, aNz = WP_NOISE_FLOOR;
        uint8_t got = wp_capture(wp_f_actual(), &aSig, &aNz);
        aEma = (aEma == 0.0) ? aSig : (0.6 * aEma + 0.4 * aSig);          /* هموارسازی: عدد ضعیف نپره */
        const double thr = cal.valid ? (cal.air + 0.5 * (cal.ref - cal.air)) : 0.0;   /* وسط «هوا» و «سیم» */
        uint8_t good = cal.valid ? ((got && aEma >= thr) ? 1U : 0U)
                                 : ((got && aSig >= WP_AMP_MIN && aSig >= WP_SNR_MIN * aNz) ? 1U : 0U);
        if (good) lastGood = HAL_GetTick();
        ok = ((uint32_t)(HAL_GetTick() - lastGood) < WP_BEEP_HOLD) ? 1U : 0U;
        wt_buzzer((uint8_t)(snd && !ok));                 /* هر جا PWM نیومد = قطع = بوق */
        LED_Blue_Set(ok);

        if (!TICK_DUE(lastDraw, 150U) && ok == shown) continue;   /* LCD کند است: خیلی کم‌تر از اندازه‌گیری رسم می‌شه */
        lastDraw = HAL_GetTick();

        if (ok != shown)
        {
            shown = ok;
            LCD_FillRect(0U, 52U, LCD_WIDTH, 60U, UI_BG);
            UI_Text(10U, 64U, 4U, ok ? UI_OK : UI_BAD, UI_BG, ok ? "WIRE OK" : "BROKEN");
        }
        char t[12], line[32];
        wp_fmt_f(t, sizeof t, k_wpF[fi]);
        snprintf(line, sizeof line, "P2 %s %s       ", t, cal.valid ? "CAL" : "RAW");
        wp_line(124U, UI_DIM, line, c1);
        snprintf(line, sizeof line, "sig %luuV       ", (unsigned long)((cal.valid ? aEma : aSig) * 50.35));
        wp_line(148U, UI_CYAN, line, c2);
        unsigned snr = (unsigned)((aNz > 0.0) ? aSig / aNz : 0.0); if (snr > 999U) snr = 999U;
        if (cal.valid) snprintf(line, sizeof line, "air %lu thr %luuV   ", (unsigned long)(cal.air * 50.35), (unsigned long)(thr * 50.35));
        else           snprintf(line, sizeof line, "nz %luuV x%u     ", (unsigned long)(aNz * 50.35), snr);
        wp_line(172U, UI_CYAN, line, c3);
        if (HAL_GetTick() < msgUntil) snprintf(line, sizeof line, "%-15s", msg);
        else                          snprintf(line, sizeof line, "%s C%luM x%s     ", snd ? "S:ON" : "S:OFF", (unsigned long)((cpuHz + 500000U) / 1000000U), (s_wpK > 1.5) ? "2" : (s_wpK < 0.75) ? "0.5" : "1");
        wp_line(196U, UI_DIM, line, c4);
        if (!cpuDone) { cpuDone = 1U; cpuHz = Clock_MeasureCpuHz(); c4[0] = '\0'; }
    }

    wt_buzzer(0U);
    LED_Blue_Set(0);
    Probe_PwmStop();
    Probe_AllOff();
    LEDs_AllOff();
}

__attribute__((unused)) static void App_WiringTestAdc(void)
{
    static const char *const k_beepName[3] = { "on BREAK", "on OK   ", "OFF     " };

    LEDs_AllOff();
    Probe_AllOff();                                   /* پول‌آپ/DAC/اهم‌متر خاموش؛ Probe1 فقط Hi-Z و خواننده */
    UI_Begin("WIRING TEST");
    UI_Hint("OK ref L/R beep U/D freq");
    UI_Row(0U, UI_FG, UI_BG, "P2 PWM > P1 reads");

    uint8_t   fi = WT_FREQ_DEFAULT, beepMode = 0U, restart = 1U;
    uint8_t   broken = 1U, okRun = 0U, badRun = 0U, shown = 2U;
    uint32_t  pass = 0U, fail = 0U, drops = 0U;
    WtRef_t   ref = {0U, 0.0f, 0.0f};
    char      msg[20] = "";
    uint32_t  msgUntil = 0U;
    uint32_t  last = 0U - 1000U;
    uint16_t  barW = 0xFFFFU, barCol = 0U;
    WtLine_t  ln[8];
    memset(ln, 0, sizeof ln);

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_UP)    { fi = (uint8_t)((fi + 1U) % WT_FREQ_COUNT); restart = 1U; ref.valid = 0U; }
        if (in == MENU_INPUT_DOWN)  { fi = (uint8_t)((fi + WT_FREQ_COUNT - 1U) % WT_FREQ_COUNT); restart = 1U; ref.valid = 0U; }
        if (in == MENU_INPUT_RIGHT) beepMode = (uint8_t)((beepMode + 1U) % 3U);
        if (in == MENU_INPUT_LEFT)  beepMode = (uint8_t)((beepMode + 2U) % 3U);

        if (restart)
        {
            restart = 0U;
            Probe_PwmStart(PROBE_2, k_wtFreq[fi], WT_DUTY);      /* Probe2 خروجی می‌ده ، Probe1 Hi-Z می‌مونه */
            HAL_Delay(10);
            okRun = 0U; badRun = 0U; broken = 1U; shown = 2U;
            last = 0U - 1000U;
        }
        if (in == MENU_INPUT_SELECT)                             /* Probe1 باید روی نقطه‌ی سالم (کنار Probe2) باشه */
        {
            wt_buzzer(0U);
            snprintf(msg, sizeof msg, "%s", wt_learn(&ref) ? "Ref saved" : "No signal!");
            msgUntil = HAL_GetTick() + 1500U;
            pass = 0U; fail = 0U; drops = 0U; okRun = 0U; badRun = 0U;
            last = 0U - 1000U;
        }
        if (!TICK_DUE(last, 30U)) continue;
        last = HAL_GetTick();

        WtMeas_t m;
        wt_measure(&m);
        WtState_t st = wt_check(&m, &ref);

        /* دیبانس: چند اندازه‌گیری پشت‌سرهم لازمه تا وضعیت عوض بشه (کشیدن پروب = تماس لحظه‌ای) */
        if (st == WT_OK)
        {
            if (pass < 99999U) pass++;
            badRun = 0U;
            if (okRun < 250U) okRun++;
            if (broken && okRun >= WT_GOOD_FRAMES) broken = 0U;
        }
        else
        {
            if (fail < 99999U) fail++;
            okRun = 0U;
            if (badRun < 250U) badRun++;
            if (!broken && badRun >= WT_BAD_FRAMES) { broken = 1U; if (drops < 9999U) drops++; }
        }
        LED_Blue_Set((uint8_t)!broken);
        wt_buzzer((uint8_t)(((beepMode == 0U) && broken) || ((beepMode == 1U) && !broken)));

        char b[32], t1[12], t2[12];
        uint16_t col = broken ? UI_BAD : UI_OK;

        /* وضعیت بزرگ */
        if (shown != broken)
        {
            shown = broken;
            LCD_FillRect(0U, 96U, LCD_WIDTH, 28U, UI_BG);
            UI_Text(4U, 96U, 3U, col, UI_BG, broken ? "BROKEN" : "WIRE OK");
        }

        if (HAL_GetTick() < msgUntil)
        {
            wt_row(&ln[1], 1U, UI_TITLE, msg);
        }
        else
        {
            snprintf(b, sizeof b, "Out %luHz D%u%%", (unsigned long)k_wtFreq[fi], (unsigned)WT_DUTY);
            wt_row(&ln[1], 1U, UI_CYAN, b);
        }

        UI_FmtFixed(t1, sizeof t1, m.swing, 1U);
        if (ref.valid) { UI_FmtFixed(t2, sizeof t2, ref.swing, 1U); snprintf(b, sizeof b, "Amp%s Ref%sV", t1, t2); }
        else           { snprintf(b, sizeof b, "Amp%sV  no ref", t1); }
        wt_row(&ln[4], 4U, UI_FG, b);

        snprintf(b, sizeof b, "Match %u%% Lock %u%%", (unsigned)(m.purity * 100.0f + 0.5f), (unsigned)(m.lock * 100.0f + 0.5f));
        wt_row(&ln[5], 5U, UI_ORANGE, b);

        /* نوار دامنه نسبت به مرجع (بدون مرجع: نسبت به ۵ ولت) */
        {
            float full = ref.valid ? ref.swing : 5.0f;
            float fr = (full > 0.1f) ? m.swing / full : 0.0f;
            if (fr > 1.0f) fr = 1.0f;
            if (fr < 0.0f) fr = 0.0f;
            uint16_t w = (uint16_t)(fr * 228.0f);
            if (w != barW || col != barCol) { barW = w; barCol = col; wt_bar(w, col); }
        }

        snprintf(b, sizeof b, "Drops %lu Beep %s", (unsigned long)drops, k_beepName[beepMode]);
        wt_row(&ln[7], 7U, (drops != 0U) ? UI_BAD : UI_OK, b);

        snprintf(b, sizeof b, "Ok%lu Bad%lu %s   ", (unsigned long)pass, (unsigned long)fail, k_wtReason[st]);
        UI_Text(4U, 212U, 1U, UI_DIM, UI_BG, b);
    }
    wt_buzzer(0U);
    LED_Blue_Set(0);
    Probe_PwmStop();
    Probe_AllOff();
    LEDs_AllOff();
}

/* ================================================================== */
/*  Coming soon                                                        */
/* ================================================================== */
static void App_ComingSoon(uint8_t idx)
{
    LEDs_AllOff();
    const UiTheme_t *th = UI_Theme();
    LCD_FillRect(0U, 24U, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - 24U), th->bg);
    const PageTitle_t *t = &g_pageTitles[idx];
    LCD_DrawBitmap1bpp((uint16_t)((LCD_WIDTH - t->w) / 2U), 44U, t->w, t->h, t->bits, th->dark, th->bg);
    UI_DrawPageIcon((LCD_WIDTH - PAGE_ICON_W) / 2U, 84U, idx);
    const PageTitle_t *c = &g_pageTitles[TITLE_COMING_SOON];
    LCD_DrawBitmap1bpp((uint16_t)((LCD_WIDTH - c->w) / 2U), 192U, c->w, c->h, c->bits, th->accent, th->bg);
    LCD_DrawString(78U, 218U, "Coming soon", th->dark, th->bg, 1U);
    for (;;)
    {
        if (UI_Poll() == MENU_INPUT_BACK) break;
    }
}

/* ================================================================== */
/*  Sub menus (UP / DOWN = move, ENTER = open, BACK = leave)           */
/* ================================================================== */
static void RunSubMenu(const MenuScreen_t *scr)
{
    Menu_Init(scr);
    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in != MENU_INPUT_NONE) Menu_HandleInput(in);
        Menu_Draw();
    }
}

static const MenuItem_t s_testItems[] = {
    { "GPIO Test", App_GpioTest },
    { "Key Test",  App_KeyTest  },
    { "LED Test",  App_LedTest  },
    { "Video",     App_Video    },
    { "PWM Gen/Read", App_Pwm   },
    { "DAC Out",      App_Dac   },
};
static const MenuScreen_t s_testScreen = {
    .title = "TESTS", .items = s_testItems,
    .count = (uint8_t)(sizeof(s_testItems) / sizeof(s_testItems[0])),
};
static void App_Tests(void) { RunSubMenu(&s_testScreen); }

/* ================================================================== */
/*  شبیه‌ساز سنسور (طبق دفترچه‌ی مولتی‌پروب نگارخودرو)                  */
/*  فرکانسی‌ها PWM روی Probe1 (دور موتور: Probe2 = PWM not) ؛ بقیه DAC1.  */
/* ================================================================== */
typedef enum { SS_FREQ_RPM, SS_FREQ_CAM, SS_FREQ_SPEED, SS_ANALOG, SS_ANALOG_INV, SS_DUAL_PEDAL, SS_DUAL_TPS } SsKind_t;
typedef struct {
    const char *name;     /* اسم روی منو */
    const char *unit;
    SsKind_t    kind;
    int32_t     vmin, vmax, step, init;
    float       vlo, vhi; /* آنالوگ: ولتاژ DAC در حداقل / حداکثر مقدار */
} SensorDef_t;

static const SensorDef_t k_sensors[] = {
    { "Engine RPM",     "rpm",  SS_FREQ_RPM,    0, 8000, 50, 800,  0, 0 },
    { "Camshaft",       "rpm",  SS_FREQ_CAM,    0, 4000, 25, 400,  0, 0 },
    { "Speed",          "km/h", SS_FREQ_SPEED,  0,  250,  1,  60,  0, 0 },
    { "Water Temp",     "C",    SS_ANALOG_INV,  0,  100,  1,  80,  0.4f, 3.0f },
    { "Ambient Temp",   "%",    SS_ANALOG_INV,  0,  100,  1,  50,  0.4f, 3.0f },
    { "Air Temp",       "%",    SS_ANALOG_INV,  0,  100,  1,  50,  0.4f, 3.0f },
    { "Gas Rail Temp",  "%",    SS_ANALOG_INV,  0,  100,  1,  50,  0.4f, 3.0f },
    { "Ambient Press",  "%",    SS_ANALOG,      0,  100,  1,  50,  0.5f, 3.0f },
    { "Manifold Press", "%",    SS_ANALOG,      0,  100,  1,  50,  0.5f, 3.0f },
    { "Gas Tank Press","%"  ,    SS_ANALOG,      0,  100,  1,  50,  0.5f, 3.0f },
    { "Gas Rail Press", "%",    SS_ANALOG,      0,  100,  1,  50,  0.5f, 3.0f },
    { "Electric Gas Pedal", "%",    SS_DUAL_PEDAL,      0,  100,  1,   0,  0.5f, 3.0f },
    { "Electric Throttle", "%",    SS_DUAL_TPS,      0,  100,  1,   0,  0.5f, 3.0f },
    { "Oxygen Sensor",  "%",    SS_ANALOG,      0,  100,  1,  50,  0.1f, 0.9f },
};
#define SENSOR_COUNT ((uint8_t)(sizeof(k_sensors) / sizeof(k_sensors[0])))

/* مسیر خروجی هر سنسور (طبق طراحی برد):
 *  دور موتور        : Probe1 = PWM ، Probe2 = PWM not  (چرخ ۶۰-۲ دندانه)
 *  میل سوپاپ / سرعت  : Probe1 = PWM
 *  دماها ، فشارها ، اکسیژن : Probe1 = DAC
 *  پدال گاز / دریچه‌ی گاز برقی : Probe1 = DAC و Probe2 = DAC (دو ترک) */
static void ss_output(const SensorDef_t *d, int32_t v, uint8_t on, char *info, size_t n)
{
    Probe_PwmStop();
    Probe_DacOff();
    info[0] = '\0';
    if (!on) { snprintf(info, n, "OUTPUT OFF"); return; }

    if (d->kind == SS_FREQ_RPM)
    {
        uint32_t hz = (uint32_t)v;                        /* ۶۰ دندانه: Hz = rpm */
        if (hz == 0U) { snprintf(info, n, "P1/P2 0 Hz"); return; }
        Probe_PwmStartCrank(hz);                          /* ۶۰-۲: دو دندانه‌ی آخر خالی */
        snprintf(info, n, "P1 PWM P2 /PWM");
    }
    else if (d->kind == SS_FREQ_CAM || d->kind == SS_FREQ_SPEED)
    {
        uint32_t hz = (d->kind == SS_FREQ_CAM) ? (uint32_t)((v * 4) / 60)       /* ۴ دندانه */
                                               : (uint32_t)((v * 4000) / 3600); /* ۴۰۰۰ پالس بر کیلومتر */
        if (hz == 0U) { snprintf(info, n, "P1 0 Hz"); return; }
        Probe_PwmStart(PROBE_1, hz, 50U);
        snprintf(info, n, "P1 %luHz", (unsigned long)hz);
    }
    else
    {
        float f = (float)(v - d->vmin) / (float)(d->vmax - d->vmin);
        if (d->kind == SS_ANALOG_INV) f = 1.0f - f;          /* NTC: گرم‌تر = ولتاژ کمتر */
        float v1 = d->vlo + f * (d->vhi - d->vlo);
        Probe_DacSwitch(1U, 1U);
        Probe_DacSet(1U, v1);
        char t[12], t2[12];
        UI_FmtFixed(t, sizeof t, v1, 2U);
        if (d->kind == SS_DUAL_PEDAL || d->kind == SS_DUAL_TPS)
        {
            /* پدال و دریچه: Probe2 = NOT ی Probe1 (قرینه نسبت به وسط بازه) */
            float v2 = d->vlo + d->vhi - v1;
            Probe_DacSwitch(2U, 1U);
            Probe_DacSet(2U, v2);
            UI_FmtFixed(t2, sizeof t2, v2, 2U);
            snprintf(info, n, "P1 %s P2 %sV", t, t2);
        }
        else snprintf(info, n, "P1 DAC %sV", t);
    }
}

static void SensorIcon(uint8_t idx, uint16_t x, uint16_t y, uint16_t k);
static void SensorRun(uint8_t idx)
{
    const SensorDef_t *d = &k_sensors[idx];
    LEDs_AllOff();
    UI_Begin("SENSOR");
    UI_Hint("U/D x10 L/R +/- OK on/off");

    int32_t v = d->init;
    uint8_t on = 1U, redraw = 1U, apply = 1U;
    char info[24] = "";
    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        int32_t dv = 0;
        if (in == MENU_INPUT_RIGHT) dv =  d->step;
        if (in == MENU_INPUT_LEFT)  dv = -d->step;
        if (in == MENU_INPUT_UP)    dv =  d->step * 10;
        if (in == MENU_INPUT_DOWN)  dv = -d->step * 10;
        if (dv != 0)
        {
            v += dv;
            if (v < d->vmin) v = d->vmin;
            if (v > d->vmax) v = d->vmax;
            apply = 1U; redraw = 1U;
        }
        if (in == MENU_INPUT_SELECT) { on = (uint8_t)!on; apply = 1U; redraw = 1U; }

        if (apply) { ss_output(d, v, on, info, sizeof info); apply = 0U; }
        if (redraw)
        {
            redraw = 0U;
            char b[24];
            snprintf(b, sizeof b, "%-6ld", (long)v);
            LCD_FillRect(0U, 50U, LCD_WIDTH, 102U, UI_BG);
            {   /* اسم کامل سنسور ؛ اگه از ۲۰ حرف بیشتر بود ریزتر می‌شه */
                uint8_t ns = (strlen(d->name) * 14U <= 232U) ? 2U : 1U;
                UI_Text(4U, 52U, ns, UI_TITLE, UI_BG, d->name);
            }
            UI_Text(10U, 74U, 4U, on ? UI_WHITE : UI_DIM, UI_BG, b);
            UI_Text(10U, 108U, 2U, UI_CYAN, UI_BG, d->unit);
            SensorIcon(idx, 200U, 78U, 1U);
            /* نوار مقدار */
            uint16_t w = (uint16_t)(((int64_t)(v - d->vmin) * 220) / (d->vmax - d->vmin));
            LCD_FillRect(8U, 126U, 224U, 14U, UI_FG);
            LCD_FillRect(10U, 128U, 220U, 10U, UI_BG);
            if (w) LCD_FillRect(10U, 128U, w, 10U, on ? UI_OK : UI_DIM);
            LCD_FillRect(0U, 150U, LCD_WIDTH, 40U, UI_BG);
            UI_Text(10U, 154U, 2U, on ? UI_ORANGE : UI_DIM, UI_BG, info);
            UI_Text(10U, 178U, 1U, UI_DIM, UI_BG, "Probe1 (+Probe2 where needed)");
        }
    }
    Probe_PwmStop();
    Probe_DacOff();
    Probe_AllOff();
}

/* ---------------- آیکن‌های سنسورها (طراحی روی شبکه‌ی ۳۲×۳۲ ، k = ضریب بزرگ‌نمایی) ---------------- */
static uint16_t g_icx, g_icy, g_ick;
static void ic_r(int x, int y, int w, int h, uint16_t c)
{
    LCD_FillRect((uint16_t)(g_icx + x * (int)g_ick), (uint16_t)(g_icy + y * (int)g_ick),
                 (uint16_t)(w * (int)g_ick), (uint16_t)(h * (int)g_ick), c);
}
static void ic_circle(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++)
    {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        ic_r(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}
static void ic_ring(int cx, int cy, int r, int t, uint16_t c)
{
    ic_circle(cx, cy, r, c);
    ic_circle(cx, cy, r - t, UI_BG);
}
static void ic_line(int x0, int y0, int x1, int y1, int t, uint16_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int n = ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (n == 0) n = 1;
    for (int i = 0; i <= n; i++) ic_r(x0 + dx * i / n - t / 2, y0 + dy * i / n - t / 2, t, t, c);
}
static void ic_gauge(uint16_t col, int needleX, int needleY)
{
    ic_ring(16, 17, 14, 2, col);
    for (int i = 0; i < 7; i++)                                   /* خط‌های مقیاس */
    {
        static const int8_t tx[7] = { 4, 6, 10, 16, 22, 26, 28 }, ty[7] = { 20, 11, 6, 4, 6, 11, 20 };
        ic_r(tx[i], ty[i], 2, 2, UI_FG);
    }
    ic_line(16, 17, needleX, needleY, 2, UI_BAD);
    ic_circle(16, 17, 2, UI_WHITE);
}
static void ic_thermo(uint16_t col)
{
    ic_r(13, 3, 6, 18, col);  ic_circle(16, 24, 6, col);
    ic_r(14, 4, 4, 17, UI_BG); ic_circle(16, 24, 4, UI_BAD);
    ic_r(15, 11, 2, 12, UI_BAD);
    for (int y = 6; y < 18; y += 4) ic_r(20, y, 4, 1, col);
}
static void ic_pipe_h(uint16_t col) { ic_r(0, 12, 32, 8, col); ic_r(0, 14, 32, 4, UI_BG); }

static void SensorIcon(uint8_t idx, uint16_t x, uint16_t y, uint16_t k)
{
    g_icx = x; g_icy = y; g_ick = k;
    const uint16_t ac = UI_Theme()->accent;
    switch (idx)
    {
    case 0:  /* Engine RPM: گیج + دندانه‌ی چرخ */
        ic_gauge(UI_ORANGE, 25, 8); ic_r(6, 28, 20, 3, UI_FG);
        for (int i = 6; i < 26; i += 4) ic_r(i, 26, 2, 2, UI_FG);
        break;
    case 1:  /* Camshaft: میل‌بادامک */
        ic_r(1, 14, 30, 4, UI_DIM);
        ic_circle(9, 16, 6, ac); ic_circle(7, 14, 3, UI_BG); ic_circle(7, 14, 3, ac);
        ic_circle(23, 16, 5, UI_ORANGE); ic_r(24, 8, 4, 4, UI_ORANGE);
        ic_circle(9, 16, 1, UI_WHITE);
        break;
    case 2:  /* Speed: سرعت‌سنج نیم‌دایره */
        ic_ring(16, 20, 14, 3, UI_CYAN); ic_r(0, 21, 32, 11, UI_BG);
        ic_line(16, 20, 26, 10, 2, UI_BAD); ic_circle(16, 20, 2, UI_WHITE);
        ic_r(6, 26, 20, 4, UI_OK); ic_r(8, 28, 16, 2, UI_BG);
        break;
    case 3:  /* Water temp: دماسنج + قطره */
        ic_thermo(UI_CYAN); ic_circle(26, 20, 3, UI_CYAN); ic_r(26, 14, 1, 4, UI_CYAN); ic_r(25, 16, 3, 2, UI_CYAN);
        break;
    case 4:  /* Ambient: دماسنج + خورشید */
        ic_thermo(UI_OK); ic_circle(26, 8, 3, UI_TITLE);
        ic_r(25, 2, 2, 2, UI_TITLE); ic_r(25, 13, 2, 2, UI_TITLE); ic_r(20, 7, 2, 2, UI_TITLE); ic_r(31, 7, 1, 2, UI_TITLE);
        break;
    case 5:  /* Air: دماسنج + باد */
        ic_thermo(UI_MAGENTA); ic_r(21, 18, 10, 2, UI_WHITE); ic_r(23, 22, 8, 2, UI_WHITE); ic_r(21, 26, 10, 2, UI_WHITE);
        break;
    case 6:  /* Gas rail temp: دماسنج + شعله */
        ic_thermo(UI_ORANGE); ic_circle(26, 22, 4, UI_ORANGE); ic_r(25, 12, 3, 8, UI_ORANGE); ic_circle(26, 23, 2, UI_TITLE);
        break;
    case 7:  /* Ambient pressure: بارومتر */
        ic_gauge(UI_CYAN, 9, 10); ic_r(13, 26, 6, 3, UI_CYAN);
        break;
    case 8:  /* Manifold: لوله‌ی منیفولد با فلش */
        ic_r(0, 8, 32, 5, UI_ORANGE); ic_r(0, 19, 32, 5, UI_ORANGE);
        ic_r(6, 13, 4, 6, UI_ORANGE); ic_r(22, 13, 4, 6, UI_ORANGE);
        ic_r(10, 15, 10, 2, UI_WHITE); ic_r(18, 13, 2, 6, UI_WHITE);
        break;
    case 9:  /* Fuel/Gas tank: مخزن */
        ic_circle(8, 16, 7, UI_OK); ic_circle(24, 16, 7, UI_OK); ic_r(8, 9, 16, 14, UI_OK);
        ic_r(10, 12, 12, 8, UI_BG); ic_r(14, 5, 4, 4, UI_DIM); ic_r(12, 3, 8, 2, UI_DIM);
        ic_r(13, 14, 6, 4, UI_TITLE);
        break;
    case 10: /* Gas rail pressure: ریل + انژکتور */
        ic_r(0, 8, 32, 6, UI_MAGENTA);
        for (int i = 3; i < 30; i += 8) { ic_r(i, 14, 4, 8, UI_DIM); ic_r(i + 1, 22, 2, 5, UI_TITLE); }
        break;
    case 11: /* Throttle pedal: پدال */
        ic_line(10, 4, 20, 24, 5, UI_ORANGE); ic_r(6, 26, 20, 4, UI_DIM);
        ic_r(18, 20, 6, 8, UI_DIM);
        break;
    case 12: /* Throttle valve: پروانه‌ی دریچه */
        ic_ring(16, 16, 14, 3, UI_CYAN); ic_line(6, 24, 26, 8, 3, UI_ORANGE); ic_circle(16, 16, 2, UI_WHITE);
        break;
    default: /* Oxygen: لامبدا */
        ic_r(12, 2, 8, 8, UI_DIM); ic_r(10, 10, 12, 4, ac); ic_r(13, 14, 6, 12, UI_OK);
        ic_line(16, 26, 16, 31, 2, UI_FG);
        ic_r(24, 4, 2, 6, UI_WHITE); ic_r(28, 4, 2, 6, UI_WHITE); ic_r(24, 4, 6, 2, UI_WHITE); ic_r(24, 8, 6, 2, UI_WHITE);
        break;
    }
}

/* صفحه‌ی انتخاب سنسور: یک سنسور در هر صفحه با آیکن بزرگ؛ چپ/راست = بعدی/قبلی ، OK = ورود */
static void App_SensorSim(void)
{
    uint8_t idx = 0U, redraw = 1U;
    for (;;)
    {
        if (redraw)
        {
            redraw = 0U;
            LEDs_AllOff();
            UI_Begin("SENSOR SIM");
            const UiTheme_t *th = UI_Theme();
            const SensorDef_t *d = &k_sensors[idx];
            LCD_FillRect(0U, 48U, LCD_WIDTH, 170U, UI_BG);
            /* قاب دور آیکن */
            LCD_FillRect(76U, 56U, 88U, 88U, th->accent);
            LCD_FillRect(78U, 58U, 84U, 84U, UI_BG);
            SensorIcon(idx, 88U, 68U, 2U);
            /* اسم */
            uint8_t nsc = (strlen(d->name) * 14U <= 232U) ? 2U : 1U;
            uint16_t w = (uint16_t)(strlen(d->name) * 7U * nsc);
            UI_Text((uint16_t)((LCD_WIDTH - w) / 2U), (uint16_t)(nsc == 2U ? 152U : 156U), nsc, UI_TITLE, UI_BG, d->name);
            const char *kind = (d->kind <= SS_FREQ_SPEED) ? "PWM output" : (d->kind >= SS_DUAL_PEDAL) ? "DAC output P1+P2" : "DAC output P1";
            w = (uint16_t)(strlen(kind) * 7U);
            UI_Text((uint16_t)((LCD_WIDTH - w) / 2U), 176U, 1U, UI_DIM, UI_BG, kind);
            /* شماره */
            char b[12];
            snprintf(b, sizeof b, "%u/%u", (unsigned)(idx + 1U), (unsigned)SENSOR_COUNT);
            w = (uint16_t)(strlen(b) * 14U);
            UI_Text((uint16_t)((LCD_WIDTH - w) / 2U), 192U, 2U, th->light, UI_BG, b);
            /* مثلث‌های چپ/راست */
            for (int r = -10; r <= 10; r++)
            {
                int a = (r < 0) ? -r : r;
                uint16_t ww = (uint16_t)(14 - (a * 14) / 10 + 1);
                LCD_FillRect((uint16_t)(30U - ww), (uint16_t)(100 + r), ww, 1U, th->accent);
                LCD_FillRect(210U, (uint16_t)(100 + r), ww, 1U, th->accent);
            }
            UI_Hint("L/R sensor  OK open");
        }
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_RIGHT || in == MENU_INPUT_DOWN) { idx = (uint8_t)((idx + 1U) % SENSOR_COUNT); redraw = 1U; }
        if (in == MENU_INPUT_LEFT  || in == MENU_INPUT_UP)   { idx = (idx == 0U) ? (uint8_t)(SENSOR_COUNT - 1U) : (uint8_t)(idx - 1U); redraw = 1U; }
        if (in == MENU_INPUT_SELECT) { SensorRun(idx); redraw = 1U; }
    }
}

/* ================================================================== */
/*  Logic analyzer (مثل نگارخودرو): تشخیص سطح 12V / 5V / 3.3V / GND / SIG   */
/*  + پایین صفحه: CAN-H ، CAN-L ، K-Line با شکل موجشون ؛ هرکدوم که روی       */
/*  پروب دیده بشه سبز می‌شه.                                                */
/* ================================================================== */
#define LG_ROW_Y0   100U
#define LG_ROW_H    36U

/* شکل موج: هر ستون یک سطح (0 بالا ، 1 وسط ، 2 پایین) */
static void lg_icon(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *lv, uint8_t n, uint16_t col)
{
    uint16_t sw = (uint16_t)(w / n);
    uint16_t yy[3] = { y, (uint16_t)(y + h / 2U), (uint16_t)(y + h - 2U) };
    for (uint8_t i = 0U; i < n; i++)
    {
        uint16_t xi = (uint16_t)(x + i * sw);
        LCD_FillRect(xi, yy[lv[i]], sw, 2U, col);
        if (i + 1U < n && lv[i + 1U] != lv[i])
        {
            uint16_t a = yy[lv[i]], b = yy[lv[i + 1U]];
            uint16_t top = (a < b) ? a : b, hh = (uint16_t)((a < b) ? (b - a) : (a - b));
            LCD_FillRect((uint16_t)(xi + sw - 1U), top, 2U, (uint16_t)(hh + 2U), col);
        }
    }
}

static void lg_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    LCD_FillRect(x, y, w, 1U, c);
    LCD_FillRect(x, (uint16_t)(y + h - 1U), w, 1U, c);
    LCD_FillRect(x, y, 1U, h, c);
    LCD_FillRect((uint16_t)(x + w - 1U), y, 1U, h, c);
}

/* چیپ پایین صفحه: اسم باس + شکل موج ؛ فعال = سبز */
static void lg_chip(uint8_t i, const char *name, const uint8_t *lv, uint8_t on)
{
    uint16_t x = (uint16_t)(4U + i * 78U), y = 186U;
    LCD_FillRect(x, y, 74U, 46U, UI_BG);
    lg_box(x, y, 74U, 46U, on ? UI_OK : UI_DIM);
    UI_Text((uint16_t)(x + 6U), (uint16_t)(y + 4U), 1U, on ? UI_OK : UI_FG, UI_BG, name);
    lg_icon((uint16_t)(x + 5U), (uint16_t)(y + 16U), 64U, 24U, lv, 8U, on ? UI_OK : UI_CYAN);
}

#define LG_X0   30U      /* جعبه‌ی وسط */
#define LG_W    142U
#define LG_RX   176U     /* ستون راست */
#define LG_RW   60U

static void App_Logic(void)
{
    LEDs_AllOff();
    UI_Begin("LOGIC");
    const uint16_t ac = UI_Theme()->accent;
    static const uint8_t lvH[8] = { 1,0,0,1,0,0,1,1 };   /* CAN-H: 2.5V با پالس بالا   */
    static const uint8_t lvL[8] = { 1,2,2,1,2,2,1,1 };   /* CAN-L: 2.5V با پالس پایین  */
    static const uint8_t lvK[8] = { 0,2,0,0,2,2,0,0 };   /* K-Line: بالا با پالس به GND */
    uint8_t hold[3] = { 0U, 0U, 0U };
    uint8_t conf[3] = { 0U, 0U, 0U };        /* تأیید تشخیص باس: تک‌گلیچ/جهش موقع وصل‌کردن باس حساب نمی‌شه */
    uint8_t shown[3] = { 2U, 2U, 2U };
    static uint16_t s_lgTop[LG_W], s_lgBot[LG_W];
    for (uint16_t i = 0U; i < LG_W; i++) { s_lgTop[i] = 0xFFFFU; s_lgBot[i] = 0xFFFFU; }
    char prevC[16] = "";
    int16_t prevBar = -1;
    uint8_t frame = 0U;
    uint32_t last = 0U - 1000U;

    /* قاب‌های ثابت */
    lg_box(LG_X0, 70U, LG_W, 48U, ac);                 /* عدد/اسم */
    lg_box(LG_X0, 122U, LG_W, 58U, ac);                /* شکل موج */
    lg_box(4U, 70U, 22U, 110U, ac);                    /* نوار سطح */
    for (uint8_t i = 0U; i < 5U; i++) lg_box(LG_RX, (uint16_t)(70U + i * 22U), LG_RW, 22U, ac);

    for (;;)
    {
        if (UI_Poll() == MENU_INPUT_BACK) break;
        if (!TICK_DUE(last, 120U)) continue;
        last = HAL_GetTick();

        static const uint16_t k_gap[3] = { 0U, 20U, 150U };      /* µs: سریع (CAN) تا کند (K-Line) */
        uint32_t gap = (SystemCoreClock / 1000000U) * (uint32_t)k_gap[frame % 3U];
        frame++;
        uint32_t cyc = Adc_CapturePC4(s_vmBuf, VM_N, gap);
        if (cyc == 0U) continue;

        uint32_t mn = 65535U, mx = 0U; uint64_t sum = 0U;
        for (uint32_t i = 0U; i < VM_N; i++)
        {
            uint32_t v = s_vmBuf[i];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            sum += v;
        }
        float vLo = vm_counts_to_vin(mn), vHi = vm_counts_to_vin(mx);
        float vAv = vm_counts_to_vin((uint32_t)(sum / VM_N));
        float span = vHi - vLo;

        /* نویز روی سطح DC (مثلاً 3.3V با ±0.3V) سیگنال حساب نمی‌شه: نوسان باید >0.8V و >30٪ ولتاژ متوسط باشه */
        uint8_t sig = (span > 0.8f && span > 0.3f * vAv) ? 1U : 0U;
        uint8_t det[3] = { 0U, 0U, 0U };                         /* CAN-H ، CAN-L ، K-Line */
        /* تعداد لبه‌ها (هیسترزیس ۴۰٪/۶۰٪): فقط یک پله/رمپ (مثل وصل‌کردن به 3.3V) باس حساب نمی‌شه */
        uint32_t edges = 0U;
        if (sig)
        {
            uint32_t th = mn + ((mx - mn) * 6U) / 10U, tl = mn + ((mx - mn) * 4U) / 10U;
            uint8_t hi = (s_vmBuf[0] >= th) ? 1U : 0U;
            uint32_t run = 0U;                                   /* لبه فقط وقتی ≥۳ نمونه‌ی پشت‌سرهم اون سمت بمونه (حذف گلیچ تک‌نمونه) */
            for (uint32_t i = 1U; i < VM_N; i++)
            {
                uint32_t v = s_vmBuf[i];
                uint8_t opp = hi ? (v <= tl) : (v >= th);
                if (opp) { if (++run >= 3U) { hi = (uint8_t)!hi; edges++; run = 0U; } }
                else run = 0U;
            }
        }
        if (sig && edges >= 6U)
        {
            /* بازه‌ها با تلورانس ±10٪ :
               K-Line  0..12V   | CAN-L 0..2.5V | CAN-H 2.5..5V */
            if (vLo <= 1.2f && vHi >= 10.8f && vHi <= 13.2f)                    det[2] = 1U;
            else if (vLo >= 2.25f && vLo <= 2.75f && vHi >= 4.5f && vHi <= 5.5f) det[0] = 1U;
            else if (vLo <= 0.25f && vHi >= 2.25f && vHi <= 2.75f)              det[1] = 1U;
        }
        int8_t bus = -1;
        for (uint8_t i = 0U; i < 3U; i++)
        {
            /* باس وقتی اعلام می‌شه که دو بار پشت‌هم دیده بشه؛ بعد از آخرین دیدن حدود ۳ ثانیه می‌مونه و
               خودکار پاک می‌شه (حتی اگه ولتاژ DC همون بمونه): ۱۲V ثابت هیچ‌وقت K-LINE نمی‌مونه. */
            if (det[i]) { if (conf[i] < 6U) conf[i] = (uint8_t)(conf[i] + 2U); }
            else if (conf[i] != 0U) conf[i]--;
            if (det[i] && conf[i] >= 4U) hold[i] = 25U;
            else if (hold[i] != 0U) hold[i]--;
            if (hold[i] != 0U && bus < 0) bus = (int8_t)i;
        }

        const char *st; uint16_t sc;
        if (sig)                                    { st = "SIG";  sc = UI_WHITE;  }
        else if (vAv < 0.5f)                        { st = "GND";  sc = UI_CYAN;   }
        else if (vAv >= 3.0f  && vAv <= 3.7f)       { st = "3.3V"; sc = UI_MAGENTA;}
        else if (vAv >= 4.4f  && vAv <= 5.4f)       { st = "5V";   sc = UI_OK;     }
        else if (vAv >= 10.0f && vAv <= 15.0f)      { st = "12V";  sc = UI_BAD;    }
        else                                        { st = "--";   sc = UI_DIM;    }
        if (sig) LEDs_AllOff(); else LEDs_UpdateVoltage(vAv);
        LED_Blue_Set(sig);   /* آبی فقط برای سیگنال نوسان‌دار ؛ پروب آزاد ≈ 0V نباید روشنش کنه */

        char t[12], b[16];

        /* بالا: نوع سطح */
        snprintf(b, sizeof b, "%-4s", st);
        UI_Text(4U, 50U, 2U, sc, UI_BG, b);

        /* وسط: اسم باس (زرد) یا ولتاژ (سفید) ؛ فقط وقتی متن عوض شد دوباره کشیده می‌شه (بدون چشمک) */
        {
            char key[16];
            if (bus >= 0) snprintf(key, sizeof key, "B%d", (int)bus);
            else { UI_FmtFixed(t, sizeof t, vAv, 2U); snprintf(key, sizeof key, "V%s", t); }
            if (strcmp(key, prevC) != 0)
            {
                strcpy(prevC, key);
                LCD_FillRect((uint16_t)(LG_X0 + 1U), 71U, (uint16_t)(LG_W - 2U), 46U, UI_BG);
                if (bus >= 0)
                {
                    const char *nm = (bus == 0) ? "CAN-H" : (bus == 1) ? "CAN-L" : "K-LINE";
                    uint16_t w = (uint16_t)(strlen(nm) * 18U);
                    UI_Text((uint16_t)(LG_X0 + (LG_W - w) / 2U), 80U, 3U, UI_TITLE, UI_BG, nm);
                }
                else
                {
                    uint16_t w = (uint16_t)(strlen(t) * 24U);
                    UI_Text((uint16_t)(LG_X0 + (LG_W - w) / 2U), 78U, 4U, UI_WHITE, UI_BG, t);
                }
            }
        }

        /* نوار سطح (چپ) */
        {
            float f = vAv / 15.0f; if (f > 1.0f) f = 1.0f;
            uint16_t hh = (uint16_t)(f * 106.0f);
            if ((int16_t)hh != prevBar)
            {
                prevBar = (int16_t)hh;
                LCD_FillRect(5U, 71U, 20U, 108U, UI_BG);
                if (hh) LCD_FillRect(6U, (uint16_t)(177U - hh), 18U, hh, UI_BAD);
            }
        }

        /* شکل موج زنده (وسط پایین) */
        {
            float vtop = vm_pick_scale(vHi);
            uint32_t start = 0U;
            if (sig)
            {
                uint32_t mid = (mn + mx) / 2U;
                for (uint32_t i = 1U; i < VM_TRIG_ZONE; i++)
                    if (s_vmBuf[i - 1U] < mid && s_vmBuf[i] >= mid) { start = i; break; }
            }
            const uint16_t wx = (uint16_t)(LG_X0 + 2U), ww = (uint16_t)(LG_W - 4U);
            const uint16_t wy = 124U, wh = 54U;
            uint16_t prevY = 0xFFFFU;
            for (uint16_t x = 0U; x < ww; x++)
            {
                uint32_t a = start + ((uint32_t)x * (VM_SHOW / 2U)) / ww;
                uint32_t e = start + ((uint32_t)(x + 1U) * (VM_SHOW / 2U)) / ww;
                if (e <= a) e = a + 1U;
                if (e > VM_N) e = VM_N;
                if (a >= VM_N) a = VM_N - 1U;
                uint16_t lo = 65535U, hi = 0U;
                for (uint32_t i = a; i < e; i++)
                {
                    if (s_vmBuf[i] < lo) lo = s_vmBuf[i];
                    if (s_vmBuf[i] > hi) hi = s_vmBuf[i];
                }
                float fh = vm_counts_to_vin(hi) / vtop, fl = vm_counts_to_vin(lo) / vtop;
                if (fh > 1.0f) fh = 1.0f;
                if (fl > 1.0f) fl = 1.0f;
                uint16_t yT = (uint16_t)(wy + wh - 2U - (uint16_t)(fh * (float)(wh - 4U)));
                uint16_t yB = (uint16_t)(wy + wh - 2U - (uint16_t)(fl * (float)(wh - 4U)));
                if (prevY != 0xFFFFU)
                {
                    if (prevY < yT) yT = prevY;
                    if (prevY > yB) yB = prevY;
                }
                if (yB <= yT) yB = (uint16_t)(yT + 1U);
                prevY = (uint16_t)(wy + wh - 2U - (uint16_t)(vm_counts_to_vin(s_vmBuf[e - 1U]) / vtop * (float)(wh - 4U)));
                if (s_lgTop[x] == yT && s_lgBot[x] == yB) continue;           /* ستون عوض نشده = دست نمی‌زنیم */
                if (s_lgTop[x] != 0xFFFFU)
                {
                    uint16_t ot = s_lgTop[x], ob = s_lgBot[x];
                    if (ot < yT) LCD_FillRect((uint16_t)(wx + x), ot, 1U, (uint16_t)(((yT < ob + 1U) ? yT : (ob + 1U)) - ot), UI_BG);
                    if (ob > yB) { uint16_t y0 = (yB + 1U > ot) ? (uint16_t)(yB + 1U) : ot; LCD_FillRect((uint16_t)(wx + x), y0, 1U, (uint16_t)(ob + 1U - y0), UI_BG); }
                }
                else LCD_FillRect((uint16_t)(wx + x), wy, 1U, wh, UI_BG);
                LCD_FillRect((uint16_t)(wx + x), yT, 1U, (uint16_t)(yB - yT + 1U), UI_TITLE);
                s_lgTop[x] = yT; s_lgBot[x] = yB;
            }
        }

        /* ستون راست: L / A / H / Hz / D */
        float hz = 0.0f, duty = 0.0f; uint8_t tooFast = 0U;
        uint8_t fok = sig ? vm_analyze((uint16_t)mn, (uint16_t)mx, cyc, &hz, &duty, &tooFast) : 0U;
        UI_FmtFixed(t, sizeof t, vLo, 1U); snprintf(b, sizeof b, "L%-4s", t);
        UI_Text((uint16_t)(LG_RX + 3U), 74U, 2U, UI_CYAN, UI_BG, b);
        UI_FmtFixed(t, sizeof t, vAv, 1U); snprintf(b, sizeof b, "A%-4s", t);
        UI_Text((uint16_t)(LG_RX + 3U), 96U, 2U, UI_WHITE, UI_BG, b);
        UI_FmtFixed(t, sizeof t, vHi, 1U); snprintf(b, sizeof b, "H%-4s", t);
        UI_Text((uint16_t)(LG_RX + 3U), 118U, 2U, UI_ORANGE, UI_BG, b);
        if (fok)
        {
            if (hz < 1000.0f) { UI_FmtFixed(t, sizeof t, hz, 0U); snprintf(b, sizeof b, "%sHz", t); }
            else              { UI_FmtFixed(t, sizeof t, hz / 1000.0f, 1U); snprintf(b, sizeof b, "%sk", t); }
        }
        else snprintf(b, sizeof b, "--Hz");
        { char c[10]; snprintf(c, sizeof c, "%-5s", b); UI_Text((uint16_t)(LG_RX + 3U), 140U, 2U, fok ? UI_CYAN : UI_DIM, UI_BG, c); }
        if (fok) { UI_FmtFixed(t, sizeof t, duty, 0U); snprintf(b, sizeof b, "D%s%%", t); }
        else     snprintf(b, sizeof b, "D--%%");
        { char c[10]; snprintf(c, sizeof c, "%-5s", b); UI_Text((uint16_t)(LG_RX + 3U), 162U, 2U, fok ? UI_ORANGE : UI_DIM, UI_BG, c); }

        /* چیپ‌های پایین */
        for (uint8_t i = 0U; i < 3U; i++)
        {
            uint8_t on = (hold[i] != 0U) ? 1U : 0U;
            if (on != shown[i])
            {
                shown[i] = on;
                lg_chip(i, (i == 0U) ? "CAN-H" : (i == 1U) ? "CAN-L" : "K-LINE",
                        (i == 0U) ? lvH : (i == 1U) ? lvL : lvK, on);
            }
        }
    }
    LEDs_AllOff();
}

/* ================================================================== */
/*  Home pages -> functions                                            */
/*   0 Dashboard  1 Tests  2 Logic analyzer  3 Voltmeter  4 Simulator  */
/*   5 Scope  6 Ohmmeter  7 Buzzer  8 Wiring test  9 ECU flash                      */
/* ================================================================== */
void App_RunPage(uint8_t idx)
{
    UI_SetTheme(idx);          /* every screen opened from this page uses its color */
    switch (idx)
    {
    case 1: App_Tests();     break;
    case 2: App_Logic();     break;
    case 3: App_Voltmeter(); break;
    case 4: App_SensorSim(); break;
    case 5: App_Scope();     break;
    case 6: App_Ohmmeter();  break;
    case 7: App_Buzzer();    break;
    case 8: App_WiringTest(); break;
    default: App_ComingSoon(idx); break;     /* 0, 2, 9 */
    }
}

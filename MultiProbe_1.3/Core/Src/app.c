//#include "app.h"
//#include "ui.h"
//#include "lcd.h"
//#include "leds.h"
//#include "adc_probe.h"
//#include "probe_hw.h"
//#include "board_map.h"
//#include "statusbar.h"
//#include "video_player.h"
//#include "keypad.h"
//#include "menu.h"
//#include "carousel.h"
//#include "pages_assets.h"
//#include <stdio.h>
//#include <string.h>
//
//extern ADC_HandleTypeDef hadc1;
//
//#define TICK_DUE(last, ms)  ((HAL_GetTick() - (last)) >= (ms))
//
//static void beep_ms(uint32_t ms)
//{
//    HAL_GPIO_WritePin(Buzzer_GPIO_Port, Buzzer_Pin, GPIO_PIN_SET);
//    HAL_Delay(ms);
//    HAL_GPIO_WritePin(Buzzer_GPIO_Port, Buzzer_Pin, GPIO_PIN_RESET);
//}
//
///* ================================================================== */
///*  Voltmeter + signal shape                                           */
///*   بالا: ولتاژ DC + Hz و Duty ، وسط: شکل موج ، پایین: L / A / H       */
///* ================================================================== */
//#define VM_BOX_X      4U
//#define VM_BOX_Y      98U
//#define VM_BOX_W      232U
//#define VM_BOX_H      70U
//#define VM_IX         (VM_BOX_X + 1U)
//#define VM_IY         (VM_BOX_Y + 1U)
//#define VM_IW         (VM_BOX_W - 2U)          /* 230 ستون */
//#define VM_IH         (VM_BOX_H - 2U)          /* 68 پیکسل */
//#define VM_N          2048U                    /* تعداد نمونه‌ی هر فریم */
//#define VM_TRIG_ZONE  512U                     /* ماشه توی این بخش اول دنبال لبه‌ی بالارونده می‌گرده */
//#define VM_SHOW       (VM_N - VM_TRIG_ZONE)    /* نمونه‌هایی که رسم می‌شن */
//#define VM_BG         0x0006U                  /* آبی خیلی تیره */
//#define VM_GRID       0x31A6U
//#define VM_TRACE      0xFFE0U                  /* خط زرد مثل ویدیو */
//#define VM_SIG_SPAN_V 0.8f                     /* اختلاف Hi-Lo بیشتر از این = سیگنال */
//
//static uint16_t s_vmBuf[VM_N];
//
//static float vm_counts_to_vin(uint32_t c)
//{
//    return Adc_PinToVin((float)c * 3.3f / 65535.0f);
//}
//
//static float vm_pick_scale(float vmax)
//{
//    static const float k[] = { 5.0f, 10.0f, 15.0f, 20.0f, 30.0f, 45.0f };
//    for (uint8_t i = 0U; i < (uint8_t)(sizeof(k) / sizeof(k[0])); i++)
//        if (vmax * 1.05f <= k[i]) return k[i];
//    return 45.0f;
//}
//
//static uint16_t vm_y(uint32_t counts, float vtop)
//{
//    float f = vm_counts_to_vin(counts) / vtop;
//    if (f < 0.0f) f = 0.0f;
//    if (f > 1.0f) f = 1.0f;
//    return (uint16_t)(VM_IY + (VM_IH - 1U) - (uint16_t)(f * (float)(VM_IH - 1U)));
//}
//
///* پس‌زمینه‌ی [ya, yb) از ستون x، با خط‌چین شبکه */
//static void vm_col_bg(uint16_t x, uint16_t ya, uint16_t yb)
//{
//    for (uint16_t g = 1U; g < 4U; g++)
//    {
//        uint16_t gy = (uint16_t)(VM_IY + (VM_IH * g) / 4U);
//        if (gy >= ya && gy < yb)
//        {
//            if (gy > ya) LCD_FillRect(x, ya, 1U, (uint16_t)(gy - ya), VM_BG);
//            LCD_FillRect(x, gy, 1U, 1U, (x & 3U) ? VM_BG : VM_GRID);
//            ya = (uint16_t)(gy + 1U);
//        }
//    }
//    if (ya < yb) LCD_FillRect(x, ya, 1U, (uint16_t)(yb - ya), VM_BG);
//}
//
//static void vm_draw_wave(uint32_t start, float vtop, uint32_t nShow)
//{
//    const uint16_t yMin = VM_IY, yMax = (uint16_t)(VM_IY + VM_IH - 1U);
//    uint16_t prevY = 0xFFFFU;
//
//    for (uint16_t cx = 0U; cx < VM_IW; cx++)
//    {
//        uint32_t a = start + ((uint32_t)cx * nShow) / VM_IW;
//        uint32_t b = start + ((uint32_t)(cx + 1U) * nShow) / VM_IW;
//        if (b <= a) b = a + 1U;
//        if (b > VM_N) b = VM_N;
//        if (a >= VM_N) a = VM_N - 1U;
//
//        uint16_t lo = 65535U, hi = 0U;
//        for (uint32_t i = a; i < b; i++)
//        {
//            if (s_vmBuf[i] < lo) lo = s_vmBuf[i];
//            if (s_vmBuf[i] > hi) hi = s_vmBuf[i];
//        }
//        uint16_t yTop = vm_y(hi, vtop);          /* ولتاژ بیشتر = y کمتر */
//        uint16_t yBot = vm_y(lo, vtop);
//        if (prevY != 0xFFFFU)                    /* لبه‌ی عمودی بین دو ستون وصل بشه */
//        {
//            if (prevY < yTop) yTop = prevY;
//            if (prevY > yBot) yBot = prevY;
//        }
//        if (yBot <= yTop)                        /* خط حداقل ۲ پیکسل ضخامت */
//        {
//            if (yBot < yMax) yBot++; else yTop--;
//        }
//        prevY = vm_y(s_vmBuf[b - 1U], vtop);
//
//        uint16_t x = (uint16_t)(VM_IX + cx);
//        vm_col_bg(x, yMin, yTop);
//        LCD_FillRect(x, yTop, 1U, (uint16_t)(yBot - yTop + 1U), VM_TRACE);
//        vm_col_bg(x, (uint16_t)(yBot + 1U), (uint16_t)(yMax + 1U));
//    }
//}
//
///* فرکانس و duty از لبه‌های بالارونده (با هیسترزیس ۴۰٪/۶۰٪) */
//static uint8_t vm_analyze(uint16_t mn, uint16_t mx, uint32_t cyc, float *hz, float *duty, uint8_t *tooFast)
//{
//    *tooFast = 0U;
//    uint32_t hi = (uint32_t)mn + ((uint32_t)(mx - mn) * 6U) / 10U;
//    uint32_t lo = (uint32_t)mn + ((uint32_t)(mx - mn) * 4U) / 10U;
//    uint8_t  high = (s_vmBuf[0] >= hi) ? 1U : 0U;
//    uint32_t rises = 0U, first = 0U, last = 0U, hcnt = 0U, hlast = 0U;
//
//    for (uint32_t i = 1U; i < VM_N; i++)
//    {
//        uint32_t v = s_vmBuf[i];
//        if (!high && v >= hi)
//        {
//            high = 1U;
//            rises++;
//            if (rises == 1U) { first = i; hcnt = 0U; }
//            else             { last = i;  hlast = hcnt; }
//        }
//        else if (high && v <= lo)
//        {
//            high = 0U;
//        }
//        if (rises >= 1U && high) hcnt++;
//    }
//    if (rises < 2U || last <= first) return 0U;
//
//    float dt = ((float)cyc / (float)SystemCoreClock) / (float)VM_N;
//    float period_samples = (float)(last - first) / (float)(rises - 1U);
//    if (period_samples < 4.0f) { *tooFast = 1U; return 0U; }   /* خیلی سریع: قابل اعتماد نیست */
//    *hz   = 1.0f / (period_samples * dt);
//    *duty = 100.0f * (float)hlast / (float)(last - first);
//    return 1U;
//}
//
//static void vm_fmt_time(char *dst, size_t n, float s)
//{
//    char t[12];
//    if (s < 1.0e-3f)      { UI_FmtFixed(t, sizeof t, s * 1.0e6f, 0U); snprintf(dst, n, "%sus", t); }
//    else if (s < 1.0f)    { UI_FmtFixed(t, sizeof t, s * 1.0e3f, 1U); snprintf(dst, n, "%sms", t); }
//    else                  { UI_FmtFixed(t, sizeof t, s, 1U);          snprintf(dst, n, "%ss",  t); }
//}
//
///* حالت AUTO: بازه‌ی زمانی خودکار از روی فرکانس سیگنال تنظیم می‌شه تا توی زوم ۱x
// * حدود VM_AUTO_PERIODS پریود دیده بشه (دیگه موج مربعی/PWM شبیه سوزن نمی‌شه). */
//#define VM_AUTO_PERIODS  5.0f
//#define VM_AUTO_DEF_US   10U
//#define VM_AUTO_MAX_US   200U
//
//static void App_Voltmeter(void)
//{
//    UI_Begin("VOLTMETER");
//    UI_Hint("OK zero(2x rst) L/R zoom U/D rate");
//    LCD_FillRect(VM_BOX_X, VM_BOX_Y, VM_BOX_W, VM_BOX_H, UI_Theme()->accent);
//    LCD_FillRect(VM_IX, VM_IY, VM_IW, VM_IH, VM_BG);
//
//    /* بالا/پایین = فاصله‌ی نمونه‌ها (باد ریت). tb = 0 -> AUTO ، tb = 1.. -> k_tbUs[tb-1] (µs اضافه) */
//    static const uint16_t k_tbUs[] = { 0U, 2U, 5U, 10U, 20U, 50U, 100U, 200U };
//    const uint8_t TB_N = (uint8_t)(sizeof(k_tbUs) / sizeof(k_tbUs[0]));
//    uint8_t  tb        = 0U;                 /* پیش‌فرض: AUTO */
//    uint32_t autoGapUs = VM_AUTO_DEF_US;
//
//    /* چپ/راست = زوم افقی: چپ = جمع (پریود بیشتر توی صفحه) ، راست = باز (جزئیات بیشتر) */
//    static const uint8_t k_zoom[] = { 1U, 2U, 4U, 8U, 16U };
//    const uint8_t ZN = (uint8_t)(sizeof(k_zoom) / sizeof(k_zoom[0]));
//    uint8_t zi = 1U;                         /* پیش‌فرض ۲x، تا هر دو طرف جا داشته باشه */
//
//    char msg[24] = "";
//    uint32_t msgUntil = 0U;
//    uint32_t lastOk = 0U - 5000U;
//    uint32_t last = 0U - 1000U;
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK) break;
//        if (in == MENU_INPUT_SELECT)
//        {
//            if ((HAL_GetTick() - lastOk) < 800U)          /* دو بار پشت‌سرهم = ریست صفر */
//            {
//                Adc_ResetZero();
//                snprintf(msg, sizeof msg, "Zero reset");
//            }
//            else                                           /* پروب‌ها باید روی GND باشن */
//            {
//                snprintf(msg, sizeof msg, "%s", Adc_CalibrateZero() ? "Zero set" : "Not zero!");
//            }
//            lastOk = HAL_GetTick();
//            msgUntil = HAL_GetTick() + 1500U;
//            last = 0U - 1000U;
//        }
//        if (in == MENU_INPUT_LEFT  && zi > 0U)        { zi--; last = 0U - 1000U; }
//        if (in == MENU_INPUT_RIGHT && zi + 1U < ZN)   { zi++; last = 0U - 1000U; }
//        /* بالا = بازه‌ی زمانی بزرگ‌تر (نمونه‌ها کندتر) ، پایین = کوچک‌تر (سریع‌تر) */
//        if (in == MENU_INPUT_UP   && tb < TB_N) { tb++; last = 0U - 1000U; }
//        if (in == MENU_INPUT_DOWN && tb > 0U)   { tb--; last = 0U - 1000U; }
//        if (!TICK_DUE(last, 200U)) continue;
//        last = HAL_GetTick();
//
//        char t[16], b[32];
//
//        /* --- ولتاژ DC (میانگین کند و دقیق) --- */
//        float vin = Adc_GetVin();
//        LEDs_UpdateVoltage(vin);
//        UI_FmtFixed(t, sizeof t, vin, 2U);
//        snprintf(b, sizeof b, "%sV", t);
//        {
//            char pad[8];
//            snprintf(pad, sizeof pad, "%-6s", b);
//            LCD_DrawString(4U, 54U, pad, UI_OK, UI_BG, 3U);
//        }
//
//        /* --- نمونه‌برداری سریع از PC4 --- */
//        uint32_t gapUs = (tb == 0U) ? autoGapUs : (uint32_t)k_tbUs[tb - 1U];
//        uint32_t gap = (SystemCoreClock / 1000000U) * gapUs;
//        uint32_t cyc = Adc_CapturePC4(s_vmBuf, VM_N, gap);
//        if (cyc != 0U)
//        {
//            uint32_t mn = 65535U, mx = 0U, sum = 0U;
//            for (uint32_t i = 0U; i < VM_N; i++)
//            {
//                uint32_t v = s_vmBuf[i];
//                if (v < mn) mn = v;
//                if (v > mx) mx = v;
//                sum += v;
//            }
//            float vLo  = vm_counts_to_vin(mn);
//            float vHi  = vm_counts_to_vin(mx);
//            float vAvg = vm_counts_to_vin(sum / VM_N);
//            uint8_t sig = ((vHi - vLo) > VM_SIG_SPAN_V) ? 1U : 0U;
//
//            /* ماشه: اولین لبه‌ی بالارونده‌ی وسط سطح */
//            uint32_t start = 0U;
//            if (sig)
//            {
//                uint32_t mid = (mn + mx) / 2U;
//                for (uint32_t i = 1U; i < VM_TRIG_ZONE; i++)
//                {
//                    if (s_vmBuf[i - 1U] < mid && s_vmBuf[i] >= mid) { start = i; break; }
//                }
//            }
//
//            float vtop = vm_pick_scale(vHi);
//            uint32_t nShow = VM_SHOW / (uint32_t)k_zoom[zi];       /* تعداد نمونه‌ی قابل‌مشاهده بعد از زوم */
//            vm_draw_wave(start, vtop, nShow);
//            LED_Blue_Set(sig);
//
//            /* --- Hz / Duty (بالا سمت راست) --- */
//            float hz = 0.0f, duty = 0.0f;
//            uint8_t tooFast = 0U;
//            uint8_t fok = sig ? vm_analyze((uint16_t)mn, (uint16_t)mx, cyc, &hz, &duty, &tooFast) : 0U;
//
//            /* --- AUTO: فاصله‌ی نمونه‌ها رو برای فریم بعدی تنظیم کن --- */
//            if (tb == 0U)
//            {
//                if (!sig)
//                {
//                    autoGapUs = VM_AUTO_DEF_US;
//                }
//                else if (fok)
//                {
//                    float dtS   = ((float)cyc / (float)SystemCoreClock) / (float)VM_N;   /* فاصله‌ی واقعی نمونه‌ها */
//                    float baseS = dtS - (float)gapUs * 1.0e-6f;                           /* بدون فاصله‌ی اضافه */
//                    if (baseS < 1.0e-7f) baseS = 1.0e-7f;
//                    float wantS = VM_AUTO_PERIODS / (hz * (float)VM_SHOW);
//                    float g = (wantS - baseS) * 1.0e6f;
//                    if (g < 0.0f) g = 0.0f;
//                    if (g > (float)VM_AUTO_MAX_US) g = (float)VM_AUTO_MAX_US;
//                    uint32_t gn   = (uint32_t)(g + 0.5f);
//                    uint32_t diff = (gn > autoGapUs) ? (gn - autoGapUs) : (autoGapUs - gn);
//                    if (diff * 4U > autoGapUs + 2U) autoGapUs = gn;                      /* فقط اگه >۲۵٪ فرق کرد */
//                }
//                else if (tooFast)
//                {
//                    autoGapUs = 0U;                                                       /* سریع‌ترین نمونه‌برداری */
//                }
//                else                                                                      /* لبه‌ی کافی نبود: سیگنال کند */
//                {
//                    autoGapUs = autoGapUs * 2U + 10U;
//                    if (autoGapUs > VM_AUTO_MAX_US) autoGapUs = VM_AUTO_MAX_US;
//                }
//            }
//
//            char f7[10], d7[10];
//            if (fok)
//            {
//                if (hz < 1000.0f)       { UI_FmtFixed(t, sizeof t, hz, 0U);            snprintf(b, sizeof b, "%sHz", t); }
//                else if (hz < 1.0e6f)   { UI_FmtFixed(t, sizeof t, hz / 1000.0f, 1U);  snprintf(b, sizeof b, "%skHz", t); }
//                else                    { UI_FmtFixed(t, sizeof t, hz / 1.0e6f, 1U);   snprintf(b, sizeof b, "%sMHz", t); }
//                snprintf(f7, sizeof f7, "%-7s", b);
//                UI_FmtFixed(t, sizeof t, duty, 0U);
//                snprintf(b, sizeof b, "D%s%%", t);
//                snprintf(d7, sizeof d7, "%-7s", b);
//            }
//            else
//            {
//                snprintf(f7, sizeof f7, "%-7s", "--Hz");
//                snprintf(d7, sizeof d7, "%-7s", "D--%");
//            }
//            LCD_DrawString(136U, 52U, f7, fok ? UI_CYAN : UI_DIM, UI_BG, 2U);
//            LCD_DrawString(136U, 74U, d7, fok ? UI_ORANGE : UI_DIM, UI_BG, 2U);
//
//            /* --- L / A / H (زیر نمودار) --- */
//            UI_FmtFixed(t, sizeof t, vLo, 1U);   snprintf(b, sizeof b, "L%s", t);  snprintf(f7, sizeof f7, "%-5s", b);
//            LCD_DrawString(4U,   172U, f7, UI_CYAN,   UI_BG, 2U);
//            UI_FmtFixed(t, sizeof t, vAvg, 1U);  snprintf(b, sizeof b, "A%s", t);  snprintf(f7, sizeof f7, "%-5s", b);
//            LCD_DrawString(82U,  172U, f7, UI_WHITE,  UI_BG, 2U);
//            UI_FmtFixed(t, sizeof t, vHi, 1U);   snprintf(b, sizeof b, "H%s", t);  snprintf(f7, sizeof f7, "%-5s", b);
//            LCD_DrawString(160U, 172U, f7, UI_ORANGE, UI_BG, 2U);
//
//            /* --- خط اطلاعات ریز: PC4 ، صفر ، مقیاس ، پهنای زمانیِ نمودار --- */
//            char s[12];
//            UI_FmtFixed(t, sizeof t, Adc_GetPC4(), 3U);
//            snprintf(b, sizeof b, "PC4 %s", t);   snprintf(s, sizeof s, "%-10s", b);
//            LCD_DrawString(4U, 196U, s, UI_CYAN, UI_BG, 1U);
//            UI_FmtFixed(t, sizeof t, Adc_GetZero(), 3U);
//            snprintf(b, sizeof b, "Z %s", t);     snprintf(s, sizeof s, "%-8s", b);
//            LCD_DrawString(78U, 196U, s, UI_ORANGE, UI_BG, 1U);
//            UI_FmtFixed(t, sizeof t, vtop, 0U);
//            snprintf(b, sizeof b, "0-%sV", t);    snprintf(s, sizeof s, "%-6s", b);
//            LCD_DrawString(138U, 196U, s, UI_DIM, UI_BG, 1U);
//            vm_fmt_time(b, sizeof b, ((float)cyc / (float)SystemCoreClock) / (float)VM_N * (float)nShow);
//            snprintf(s, sizeof s, "%-8s", b);
//            LCD_DrawString(184U, 196U, s, UI_MAGENTA, UI_BG, 1U);
//        }
//
//        /* پیام موقت (Zero set ...) یا وضعیت بازه‌ی زمانی و زوم */
//        if (HAL_GetTick() < msgUntil)
//        {
//            UI_Text(4U, 207U, 2U, UI_TITLE, UI_BG, msg);
//        }
//        else
//        {
//            char info[24];
//            if (tb == 0U) snprintf(info, sizeof info, "TB:AUTO Z:x%u", (unsigned)k_zoom[zi]);
//            else          snprintf(info, sizeof info, "TB:%uus Z:x%u", (unsigned)k_tbUs[tb - 1U], (unsigned)k_zoom[zi]);
//            UI_Text(4U, 207U, 2U, UI_DIM, UI_BG, info);
//        }
//    }
//    LED_Blue_Set(0);
//    LEDs_AllOff();
//}
//
///* ================================================================== */
///*  Ohmmeter                                                           */
///* ================================================================== */
//static const OhmRange_t k_ohmRanges[OHM_RANGES] = OHM_RANGE_TABLE;
//
///* مقاومت سری مسیر Probe2 -> GND (R35 + بافر)؛ از هر اندازه‌گیری کم می‌شه */
//static float s_ohmSeries = OHM_SERIES_DEFAULT;
//
///* Rx (قبل از کم‌کردن مقاومت سری) برحسب اهم؛ منفی = OPEN. vpin = ولتاژ واقعی Probe1.
// * ⚠ PB1 (ADC1_IN5) از طریق C46 خازنی کوپل شده و DC رو نمی‌بینه (فقط برای سیگنال AC) ؛
// *   پس ولتاژ DC پروب از PB0 (قبل از OPAMP) خونده می‌شه. */
//static float ohm_raw(uint8_t r, float *vpin)
//{
//    Probe_OhmSelect(r);                             /* Probe2 = 0V ، پول‌آپ 3.3V با Rref روی Probe1 */
//    HAL_Delay(8);                                   /* صبر برای نشستن سوئیچ‌ها */
//    float v = Adc_GetProbe1Dc();
//    if (v < 0.0f) v = 0.0f;
//    *vpin = v;
//    if (v >= OHM_VREF * OHM_OPEN_FRACTION) return -1.0f;
//    return k_ohmRanges[r].rref * v / (OHM_VREF - v);
//}
//
///* Rx برحسب اهم؛ منفی = OPEN */
//static float ohm_measure(uint8_t r, float *vpin)
//{
//    float rx = ohm_raw(r, vpin);
//    if (rx < 0.0f) return rx;
//    rx -= s_ohmSeries;
//    return (rx < 0.0f) ? 0.0f : rx;
//}
//
//static void fmt_ohm(char *dst, size_t n, float r)
//{
//    char t[16];
//    if (r < 0.0f)            { snprintf(dst, n, "OPEN"); }
//    else if (r < 1000.0f)    { UI_FmtFixed(t, sizeof t, r, 1);           snprintf(dst, n, "%s ohm", t); }
//    else if (r < 1.0e6f)     { UI_FmtFixed(t, sizeof t, r / 1000.0f, 2); snprintf(dst, n, "%s k", t); }
//    else                     { UI_FmtFixed(t, sizeof t, r / 1.0e6f, 2);  snprintf(dst, n, "%s M", t); }
//}
//
//static void App_Ohmmeter(void)
//{
//    LEDs_AllOff();
//    UI_Begin("OHMMETER");
//    UI_Hint("L/R range OK zero(short) BACK");
//
//    uint8_t mode = 0U;                              /* 0 = AUTO ، 1.. = رنج ثابت */
//    uint32_t last = 0U - 1000U;
//    char zmsg[20] = "";
//    uint32_t zmsgUntil = 0U;
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK) break;
//        if (in == MENU_INPUT_SELECT)                /* پروب‌ها رو بهم بزن: مقاومت سری رو اندازه می‌گیره */
//        {
//            float vz, rz = ohm_raw(0U, &vz);        /* رنج 1k */
//            if (rz >= 0.0f && rz < 600.0f)
//            {
//                s_ohmSeries = rz;
//                snprintf(zmsg, sizeof zmsg, "Zero set");
//            }
//            else
//            {
//                snprintf(zmsg, sizeof zmsg, "Short probes!");
//            }
//            zmsgUntil = HAL_GetTick() + 1500U;
//        }
//        if (in == MENU_INPUT_RIGHT) mode = (uint8_t)((mode + 1U) % (OHM_RANGES + 1U));
//        if (in == MENU_INPUT_LEFT)  mode = (uint8_t)((mode + OHM_RANGES) % (OHM_RANGES + 1U));
//        if (in != MENU_INPUT_NONE) last = 0U - 1000U;
//        if (!TICK_DUE(last, 300U)) continue;
//        last = HAL_GetTick();
//
//        uint8_t r = (mode == 0U) ? 0U : (uint8_t)(mode - 1U);
//        float vp, rx = ohm_measure(r, &vp);
//
//        if (mode == 0U)
//        {
//            /* رنجی رو انتخاب کن که Rref ش به Rx نزدیک‌تره (بهترین دقت) */
//            uint8_t best = (rx < 0.0f) ? (uint8_t)(OHM_RANGES - 1U) : r;
//            if (rx >= 0.0f)
//            {
//                float bs = 1.0e30f;
//                for (uint8_t k = 0U; k < OHM_RANGES; k++)
//                {
//                    float q = (rx > 1.0f) ? rx / k_ohmRanges[k].rref : 1.0f / k_ohmRanges[k].rref;
//                    if (q < 1.0f) q = 1.0f / q;
//                    if (q < bs) { bs = q; best = k; }
//                }
//            }
//            if (best != r) { r = best; rx = ohm_measure(r, &vp); }
//        }
//
//        char b[32], t[16];
//        fmt_ohm(b, sizeof b, rx);
//        UI_Text(4U, 56U, 3U, (rx < 0.0f) ? UI_BAD : UI_OK, UI_BG, b);
//
//        snprintf(b, sizeof b, "Range %s", (mode == 0U) ? "AUTO" : "FIXED");
//        UI_Row(3U, UI_FG, UI_BG, b);
//        snprintf(b, sizeof b, "%s", k_ohmRanges[r].name);
//        UI_Row(4U, UI_FG, UI_BG, b);
//        UI_FmtFixed(t, sizeof t, vp, 3);
//        snprintf(b, sizeof b, "P1 %s V", t);
//        UI_Row(5U, UI_CYAN, UI_BG, b);
//        UI_FmtFixed(t, sizeof t, Adc_ReadPinVolts(&hadc1, ADC_CHANNEL_3), 3);   /* PA6 = Probe2 ، باید ~0 باشه */
//        snprintf(b, sizeof b, "P2 pin %s V", t);
//        UI_Row(6U, UI_ORANGE, UI_BG, b);
//        snprintf(b, sizeof b, "SEL active %s", Probe_SwActiveHigh() ? "HIGH" : "LOW");
//        UI_Row(2U, UI_DIM, UI_BG, b);
//        if (HAL_GetTick() < zmsgUntil)
//        {
//            UI_Row(7U, UI_TITLE, UI_BG, zmsg);
//        }
//        else
//        {
//            UI_FmtFixed(t, sizeof t, s_ohmSeries, 0);
//            snprintf(b, sizeof b, "Offset %s ohm", t);
//            UI_Row(7U, UI_DIM, UI_BG, b);
//        }
//    }
//    Probe_OhmOff();
//}
//
///* ================================================================== */
///*  PWM generate / read                                                */
///* ================================================================== */
//static const uint32_t k_freqs[] = { 10U, 50U, 100U, 500U, 1000U, 2000U, 5000U, 10000U, 20000U };
//#define FREQ_COUNT  (sizeof(k_freqs) / sizeof(k_freqs[0]))
//
//static void App_Pwm(void)
//{
//    LEDs_AllOff();
//    UI_Begin("PWM GEN/READ");
//    UI_Hint("U/D sel L/R chg OK run");
//
//    ProbeId_t out  = PROBE_1;
//    uint8_t   fi   = 4U;                 /* 1 kHz */
//    uint8_t   duty = 50U;
//    ReadSrc_t rd   = RD_DEFAULT_FOR_PROBE2;
//    uint8_t   run  = 0U;
//    uint8_t   sel  = 0U;
//    uint8_t   redraw = 1U;
//    uint32_t  last = 0U - 1000U;
//
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK) break;
//
//        int8_t d = 0;
//        if (in == MENU_INPUT_UP)   sel = (sel == 0U) ? 4U : (uint8_t)(sel - 1U);
//        if (in == MENU_INPUT_DOWN) sel = (uint8_t)((sel + 1U) % 5U);
//        if (in == MENU_INPUT_RIGHT) d = 1;
//        if (in == MENU_INPUT_LEFT)  d = -1;
//        uint8_t apply = 0U;
//
//        if (in == MENU_INPUT_SELECT) { run = !run; apply = 1U; }
//        if (d != 0)
//        {
//            switch (sel)
//            {
//            case 0:
//                out = (out == PROBE_1) ? PROBE_2 : PROBE_1;
//                rd  = (out == PROBE_1) ? RD_DEFAULT_FOR_PROBE2 : RD_DEFAULT_FOR_PROBE1;  /* پروب دیگه می‌خونه */
//                apply = 1U; break;
//            case 1:
//                fi = (d > 0) ? (uint8_t)((fi + 1U) % FREQ_COUNT) : (uint8_t)((fi + FREQ_COUNT - 1U) % FREQ_COUNT);
//                apply = 1U; break;
//            case 2:
//                if (d > 0) duty = (duty >= 95U) ? 100U : (uint8_t)(duty + 5U);
//                else       duty = (duty <= 5U)  ? 0U   : (uint8_t)(duty - 5U);
//                apply = 1U; break;
//            case 3:
//                rd = (d > 0) ? (ReadSrc_t)((rd + 1) % RD_COUNT) : (ReadSrc_t)((rd + RD_COUNT - 1) % RD_COUNT);
//                break;
//            case 4:
//                run = !run; apply = 1U; break;
//            default: break;
//            }
//        }
//        if (apply)
//        {
//            if (run) Probe_PwmStart(out, k_freqs[fi], duty);
//            else     Probe_PwmStop();
//        }
//        if (in != MENU_INPUT_NONE) redraw = 1U;
//
//        if (redraw)
//        {
//            redraw = 0U;
//            char b[32];
//            snprintf(b, sizeof b, "Out : %s", (out == PROBE_1) ? "PROBE1" : "PROBE2");
//            UI_Row(0U, UI_FG, (sel == 0U) ? UI_SEL_BG : UI_BG, b);
//            snprintf(b, sizeof b, "Freq: %lu Hz", (unsigned long)k_freqs[fi]);
//            UI_Row(1U, UI_FG, (sel == 1U) ? UI_SEL_BG : UI_BG, b);
//            snprintf(b, sizeof b, "Duty: %u %%", (unsigned)duty);
//            UI_Row(2U, UI_FG, (sel == 2U) ? UI_SEL_BG : UI_BG, b);
//            snprintf(b, sizeof b, "Read: %s", Probe_ReadName(rd));
//            UI_Row(3U, UI_FG, (sel == 3U) ? UI_SEL_BG : UI_BG, b);
//            snprintf(b, sizeof b, "Run : %s", run ? "ON" : "OFF");
//            UI_Row(4U, run ? UI_OK : UI_FG, (sel == 4U) ? UI_SEL_BG : UI_BG, b);
//        }
//
//        if (TICK_DUE(last, 300U))
//        {
//            last = HAL_GetTick();
//            SigStat_t s;
//            Probe_Measure(rd, &s);
//
//            LED_Blue_Set(s.signal);
//
//            char b[32], t1[12], t2[12];
//            UI_FmtFixed(t1, sizeof t1, s.vmax, 2);
//            UI_FmtFixed(t2, sizeof t2, s.vmin, 2);
//            snprintf(b, sizeof b, "Hi%s Lo%s", t1, t2);
//            UI_Row(5U, UI_FG, UI_BG, b);
//
//            if (s.signal)
//            {
//                UI_FmtFixed(t1, sizeof t1, s.duty_pct, 1);
//                snprintf(b, sizeof b, "Duty %s %%", t1);
//                UI_Row(6U, UI_ORANGE, UI_BG, b);
//                if (s.freq_ok)       snprintf(b, sizeof b, "Freq %lu Hz", (unsigned long)s.freq_hz);
//                else if (s.too_fast) snprintf(b, sizeof b, "Freq too high");
//                else                 snprintf(b, sizeof b, "Freq ---");
//                UI_Row(7U, UI_CYAN, UI_BG, b);
//                UI_Row(8U, 0x07FFU, UI_BG, "SIGNAL");     /* cyan/blue indicator */
//            }
//            else
//            {
//                UI_FmtFixed(t1, sizeof t1, s.vavg, 2);
//                snprintf(b, sizeof b, "DC %s V", t1);
//                UI_Row(6U, UI_FG, UI_BG, b);
//                UI_Row(7U, UI_FG, UI_BG, "");
//                UI_Row(8U, UI_DIM, UI_BG, "no signal");
//            }
//        }
//    }
//    LED_Blue_Set(0);
//    Probe_PwmStop();
//}
//
///* ================================================================== */
///*  DAC out                                                            */
///* ================================================================== */
//static void App_Dac(void)
//{
//    LEDs_AllOff();
//    UI_Begin("DAC OUT");
//    UI_Hint("U/D volt L/R ch OK on/off");
//
//    uint8_t ch = 1U, on[3] = { 0U, 0U, 0U };
//    float   v[3] = { 0.0f, 0.0f, 0.0f };
//    uint8_t redraw = 1U;
//    uint32_t last = 0U - 1000U;
//
//    Probe_DacSet(1U, 0.0f);
//    Probe_DacSet(2U, 0.0f);
//
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK) break;
//
//        if (in == MENU_INPUT_LEFT || in == MENU_INPUT_RIGHT) { ch = (ch == 1U) ? 2U : 1U; redraw = 1U; }
//        if (in == MENU_INPUT_UP)   { v[ch] += 0.1f; if (v[ch] > 3.3f) v[ch] = 3.3f; Probe_DacSet(ch, v[ch]); redraw = 1U; }
//        if (in == MENU_INPUT_DOWN) { v[ch] -= 0.1f; if (v[ch] < 0.0f) v[ch] = 0.0f; Probe_DacSet(ch, v[ch]); redraw = 1U; }
//        if (in == MENU_INPUT_SELECT) { on[ch] = !on[ch]; Probe_DacSwitch(ch, on[ch]); redraw = 1U; }
//
//        if (redraw || TICK_DUE(last, 300U))
//        {
//            redraw = 0U;
//            last = HAL_GetTick();
//            char b[32], t[16];
//            snprintf(b, sizeof b, "Channel DAC%u", (unsigned)ch);            UI_Row(0U, UI_FG, UI_BG, b);
//            UI_FmtFixed(t, sizeof t, v[ch], 2);
//            snprintf(b, sizeof b, "Pin %s V", t);                            UI_Row(1U, UI_FG, UI_BG, b);
//            UI_FmtFixed(t, sizeof t, v[ch] * DAC_GAIN, 2);
//            snprintf(b, sizeof b, "Out %s V", t);                            UI_Row(2U, UI_FG, UI_BG, b);
//            snprintf(b, sizeof b, "Switch %s", on[ch] ? "ON" : "OFF");       UI_Row(3U, on[ch] ? UI_OK : UI_FG, UI_BG, b);
//            UI_FmtFixed(t, sizeof t, Probe_ReadVolts(RD_PB1), 2);
//            snprintf(b, sizeof b, "PB1 %s V", t);                            UI_Row(5U, UI_CYAN, UI_BG, b);
//            UI_FmtFixed(t, sizeof t, Probe_ReadVolts(RD_PA6), 2);
//            snprintf(b, sizeof b, "PA6 %s V", t);                            UI_Row(6U, UI_ORANGE, UI_BG, b);
//        }
//    }
//    Probe_DacOff();
//}
//
///* ================================================================== */
///*  GPIO test                                                          */
///* ================================================================== */
//typedef struct { const char *name; GPIO_TypeDef *port; uint16_t pin; } TestPin_t;
//static const TestPin_t k_testPins[] = {
//    { "PC0  OE-P1", OE_P1_PORT, OE_P1_PIN },
//    { "PA3  OE-P2", OE_P2_PORT, OE_P2_PIN },
//    { "PE6  OE-X1", OE_X1_PORT, OE_X1_PIN },
//    { "PC13 OE-X2", OE_X2_PORT, OE_X2_PIN },
//    { "PE11 DAC1 ", SW_DAC1_PORT, SW_DAC1_PIN },
//    { "PB10 DAC2 ", SW_DAC2_PORT, SW_DAC2_PIN },
//    { "PE15 OHM-A", GPIOE, GPIO_PIN_15 },
//    { "PE14 OHM-B", GPIOE, GPIO_PIN_14 },
//};
//#define TESTPIN_COUNT (sizeof(k_testPins) / sizeof(k_testPins[0]))
//
//static void App_GpioTest(void)
//{
//    LEDs_AllOff();
//    UI_Begin("GPIO TEST");
//    UI_Hint("U/D sel OK toggle BACK");
//
//    Probe_AllOff();
//    uint8_t sel = 0U, redraw = 1U;
//    uint32_t last = 0U - 1000U;
//
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK) break;
//        if (in == MENU_INPUT_UP)   { sel = (sel == 0U) ? (uint8_t)(TESTPIN_COUNT - 1U) : (uint8_t)(sel - 1U); redraw = 1U; }
//        if (in == MENU_INPUT_DOWN) { sel = (uint8_t)((sel + 1U) % TESTPIN_COUNT); redraw = 1U; }
//        if (in == MENU_INPUT_SELECT)
//        {
//            HAL_GPIO_TogglePin(k_testPins[sel].port, k_testPins[sel].pin);
//            redraw = 1U;
//        }
//        if (redraw)
//        {
//            redraw = 0U;
//            for (uint8_t i = 0U; i < TESTPIN_COUNT; i++)
//            {
//                char b[32];
//                GPIO_PinState st = HAL_GPIO_ReadPin(k_testPins[i].port, k_testPins[i].pin);
//                snprintf(b, sizeof b, "%s %s", k_testPins[i].name, (st == GPIO_PIN_SET) ? "H" : "L");
//                UI_Row(i, (st == GPIO_PIN_SET) ? UI_OK : UI_FG, (i == sel) ? UI_SEL_BG : UI_BG, b);
//            }
//        }
//        if (TICK_DUE(last, 300U))
//        {
//            last = HAL_GetTick();
//            char b[64], t1[10], t2[10], t3[10];
//            UI_FmtFixed(t1, sizeof t1, Adc_ReadPinVolts(&hadc1, ADC_CHANNEL_5), 2);
//            UI_FmtFixed(t2, sizeof t2, Adc_ReadPinVolts(&hadc1, ADC_CHANNEL_3), 2);
//            UI_FmtFixed(t3, sizeof t3, Adc_ReadPinVolts(&hadc1, ADC_CHANNEL_14), 2);
//            snprintf(b, sizeof b, "PB1 %s PA6 %s PA2 %s", t1, t2, t3);
//            UI_Text(2U, 214U, 1U, UI_DIM, UI_BG, b);
//        }
//    }
//    Probe_AllOff();
//}
//
///* ================================================================== */
///*  Buzzer / Video                                                     */
///* ================================================================== */
//static void App_Buzzer(void) { beep_ms(150U); }
//
//static void App_Video(void)
//{
//    Video_Menu();                       /* با BACK برمی‌گرده */
//    LCD_FillColor(LCD_COLOR_BLACK);
//    StatusBar_Init();                   /* نوار وضعیت دوباره کشیده بشه */
//    Menu_Redraw();
//}
//
///* ================================================================== */
///*  Key test: raw state of the six keys                                */
///* ================================================================== */
//static void App_KeyTest(void)
//{
//    static const struct { const char *name; MenuInput_t k; } keys[] = {
//        { "Enter", MENU_INPUT_SELECT }, { "Back ", MENU_INPUT_BACK },
//        { "Up   ", MENU_INPUT_UP },     { "Down ", MENU_INPUT_DOWN },
//        { "Left ", MENU_INPUT_LEFT },   { "Right", MENU_INPUT_RIGHT },
//    };
//    LEDs_AllOff();
//    UI_Begin("KEY TEST");
//    UI_Hint("hold a key; BACK twice = exit");
//
//    uint8_t backCount = 0U;
//    uint32_t lastBack = 0U;
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK)
//        {
//            if (backCount && (HAL_GetTick() - lastBack) < 1000U) break;
//            backCount = 1U; lastBack = HAL_GetTick();
//        }
//        for (uint8_t i = 0U; i < 6U; i++)
//        {
//            char b[32];
//            uint8_t p = Keypad_RawPressed(keys[i].k);
//            snprintf(b, sizeof b, "%s %s", keys[i].name, p ? "PRESSED" : "-");
//            UI_Row(i, p ? UI_OK : UI_FG, UI_BG, b);
//        }
//    }
//}
//
///* ================================================================== */
///*  LED test: each LED in turn                                         */
///* ================================================================== */
//static void App_LedTest(void)
//{
//    UI_Begin("LED TEST");
//    static const char *const names[4] = { "yellow", "blue", "green", "red" };
//    static const uint16_t cols[4]     = { 0xFFE0U, 0x041FU, 0x07E0U, 0xF800U };
//    for (uint8_t i = 0U; i < 4U; i++)
//    {
//        UI_Row(0U, cols[i], UI_BG, names[i]);
//        LEDs_AllOff();
//        LEDs_Set(i, 1U);
//        HAL_Delay(500);
//    }
//    LEDs_AllOff();
//}
//
///* ================================================================== */
///*  Oscilloscope (با فیلتر نویز)                                       */
///* ================================================================== */
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
//static void App_Scope(void)
//{
//    LEDs_AllOff();
//    UI_Begin("SCOPE");
//    UI_Hint("U/D src L/R zoom OK filter");
//    LCD_FillRect(0U, SCOPE_Y0, LCD_WIDTH, SCOPE_H, 0x0000U);
//
//    static const uint8_t zoom[] = { 1U, 2U, 4U, 8U, 16U };
//    uint8_t zi = 1U;
//    uint8_t filt = 1U;
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
//        if (in == MENU_INPUT_UP || in == MENU_INPUT_DOWN) src = (src == RD_PB1) ? RD_PA6 : RD_PB1;
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
//    LEDs_AllOff();
//}
//
///* ================================================================== */
///*  Wiring test (تست سیم‌کشی)                                           */
///*                                                                      */
///*  سناریو: یه سر سیم به ECU ماشین وصله، سر دیگه بیرونه.                    */
///*   - Probe2 رو به سر آزاد سیم وصل کن: یه PWM مشخص توی سیم می‌فرسته.        */
///*   - Probe1 رو (سوزنی / back-probe) روی نقطه‌های مختلف سیم بذار و بکش جلو.  */
///*   - تا جایی که Probe1 همون PWM رو بگیره = سیم تا اونجا سالمه (WIRE OK ،    */
///*     بوق ممتد). از هر جایی که نگرفت = سیم بین اون نقطه و Probe2 قطعه.        */
///*                                                                      */
///*  چون سر دیگه به ECU وصله و بار می‌ذاره، دامنه/دیوتی موج ممکنه با حالت       */
///*  ایده‌آل فرق کنه. پس اول Probe1 رو بذار روی همون نقطه‌ی تزریق (کنار Probe2)  */
///*  و OK بزن تا شکل موج «سالم» به‌عنوان مرجع یاد گرفته بشه؛ بعد هر جا شبیه       */
///*  مرجع بود = سالم.                                                       */
///* ================================================================== */
//typedef enum { WT_OK = 0, WT_NO_SIGNAL, WT_WEAK, WT_NOISY, WT_TOO_FAST } WtState_t;
//
//static const char *const k_wtReason[] = { "match", "no signal", "weak", "irregular", "too fast" };
//static const uint32_t    k_wtFreq[] = { 100U, 200U, 500U, 1000U, 2000U, 10000U };
//#define WT_FREQ_COUNT      ((uint8_t)(sizeof(k_wtFreq) / sizeof(k_wtFreq[0])))
//#define WT_DUTY            50U
//#define WT_MIN_SWING_V     0.8f     /* بدون مرجع: دامنه‌ی کمتر از این = ضعیف */
//#define WT_NOISE_V         0.3f     /* دامنه‌ی کمتر از این = اصلاً سیگنالی نیست */
//#define WT_REF_FRACTION    0.35f    /* با مرجع: دامنه باید حداقل این کسر از مرجع باشه */
//#define WT_PURITY_MIN      0.30f    /* سهم انرژی موج که دقیقاً روی فرکانس ارسالی ماست (نویز/سیگنال دیگه = کم) */
//
///* نتیجه‌ی یک اندازه‌گیری روی Probe1.
// * روش: تشخیص هم‌فرکانس (DFT فقط روی فرکانس خودمون) ؛ به لبه‌ها و دقت نمونه‌برداری وابسته نیست. */
//typedef struct {
//    float   swing;      /* دامنه‌ی معادل موج مربعی (پیک تا پیک، ولت) */
//    float   purity;     /* 0..1 : چقدر از موج همون فرکانس ارسالی است */
//    uint8_t tooFast;    /* 1 = نمونه‌برداری برای این فرکانس کافی نیست */
//} WtMeas_t;
//
///* شکل موج «سالم» که کاربر یاد داده */
//typedef struct { uint8_t valid; float swing; float purity; } WtRef_t;
//
//static WtState_t wt_check(const WtMeas_t *m, const WtRef_t *ref)
//{
//    if (m->tooFast) return WT_TOO_FAST;
//    float minSw = ref->valid ? (ref->swing * WT_REF_FRACTION) : WT_MIN_SWING_V;
//    if (minSw < WT_NOISE_V) minSw = WT_NOISE_V;
//    float minPu = ref->valid ? (ref->purity * 0.5f) : WT_PURITY_MIN;
//    if (minPu < WT_PURITY_MIN) minPu = WT_PURITY_MIN;
//
//    if (m->swing < WT_NOISE_V) return WT_NO_SIGNAL;          /* هیچ نوسانی نیومده */
//    if (m->swing < minSw)      return WT_WEAK;
//    if (m->purity < minPu)     return WT_NOISY;              /* نوسان هست ولی همون موج ما نیست */
//    return WT_OK;
//}
//
///* sin/cos بدون libm: برای 0 <= w <= ~3 rad ، تیلور روی w/4 و بعد دو بار زاویه‌ی دوبرابر */
//static void wt_sincos(float w, float *sn, float *cs)
//{
//    float a = w * 0.25f, a2 = a * a;
//    float s = a * (1.0f - a2 / 6.0f  * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
//    float c = 1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f * (1.0f - a2 / 30.0f * (1.0f - a2 / 56.0f)));
//    for (uint8_t i = 0U; i < 2U; i++)
//    {
//        float s2 = 2.0f * s * c;
//        float c2 = 1.0f - 2.0f * s * s;
//        s = s2; c = c2;
//    }
//    *sn = s; *cs = c;
//}
//
///* Probe1 رو از مسیر DC خودش (PC4 ، همون مسیر ولت‌متر) می‌خونه، نه PB1 که خازنی کوپل شده.
// * پنجره ≈ ۴ پریود PWM (یا سریع‌ترین نمونه‌برداری ممکن). */
//static void wt_measure(uint32_t fset, WtMeas_t *m)
//{
//    memset(m, 0, sizeof *m);
//    uint32_t per = (uint32_t)(((uint64_t)SystemCoreClock * 4U) / fset) / VM_N;
//    uint32_t gap = (per > 300U) ? (per - 300U) : 0U;
//    uint32_t cyc = Adc_CapturePC4(s_vmBuf, VM_N, gap);
//    if (cyc == 0U) return;
//
//    float dt  = ((float)cyc / (float)SystemCoreClock) / (float)VM_N;   /* فاصله‌ی واقعی نمونه‌ها */
//    float spp = 1.0f / ((float)fset * dt);                              /* نمونه در هر پریود */
//    if (spp < 2.5f) { m->tooFast = 1U; return; }
//    uint32_t n = VM_N;
//    float periods = (float)n / spp;
//    if (periods < 2.0f) return;
//
//    static float x[VM_N];
//    float mean = 0.0f;
//    for (uint32_t i = 0U; i < n; i++) { x[i] = vm_counts_to_vin(s_vmBuf[i]); mean += x[i]; }
//    mean /= (float)n;
//    float var = 0.0f;
//    for (uint32_t i = 0U; i < n; i++) { x[i] -= mean; var += x[i] * x[i]; }
//    var /= (float)n;
//
//    /* فرکانس واقعی PWM و ساعت نمونه‌برداری دقیقاً معلوم نیست؛ پس اطراف فرکانس ارسالی ±۲۵٪ جستجو می‌کنیم
//     * (قدم طوری که روی کل پنجره بیشتر از ~۰.۲ پریود خطای فاز نشه) و بهترین نقطه رو می‌گیریم. */
//    float step = 0.2f / periods;
//    float bestA = 0.0f;
//    for (float r = 0.75f; r <= 1.25f; r += step)
//    {
//        float sw, cw;
//        float w = 6.2831853f * (float)fset * r * dt;
//        if (w > 3.0f) break;
//        wt_sincos(w, &sw, &cw);
//        float c = 1.0f, s = 0.0f, re = 0.0f, im = 0.0f;
//        for (uint32_t i = 0U; i < n; i++)
//        {
//            re += x[i] * c;
//            im -= x[i] * s;
//            float cn = c * cw - s * sw;
//            s = s * cw + c * sw;
//            c = cn;
//        }
//        float a1 = 2.0f * __builtin_sqrtf(re * re + im * im) / (float)n;
//        if (a1 > bestA) bestA = a1;
//    }
//    m->swing  = bestA * 1.5707963f;
//    m->purity = (var > 1.0e-9f) ? (bestA * bestA * 0.5f) / var : 0.0f;
//    if (m->purity > 1.0f) m->purity = 1.0f;
//}
//
///* خط رو فقط وقتی متن/رنگش عوض شده دوباره می‌کشه (بدون چشمک) */
//typedef struct { char txt[24]; uint16_t fg; } WtLine_t;
//
//static void wt_row(WtLine_t *l, uint8_t row, uint16_t fg, const char *s)
//{
//    if (l->fg == fg && strncmp(l->txt, s, sizeof l->txt) == 0) return;
//    snprintf(l->txt, sizeof l->txt, "%s", s);
//    l->fg = fg;
//    UI_Row(row, fg, UI_BG, s);
//}
//
///* نوار دامنه: پُری = دامنه‌ی فعلی نسبت به مرجع */
//static void wt_bar(uint16_t w, uint16_t col)
//{
//    LCD_FillRect(4U, 172U, 232U, 14U, UI_DIM);
//    LCD_FillRect(6U, 174U, 228U, 10U, UI_BG);
//    if (w != 0U) LCD_FillRect(6U, 174U, w, 10U, col);
//}
//
//static void wt_buzzer(uint8_t on)
//{
//    HAL_GPIO_WritePin(Buzzer_GPIO_Port, Buzzer_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
//}
//
///* مرجع: ۶ اندازه‌گیری پشت‌سرهم از موج سالم؛ حداقل ۵ تاش باید معتبر باشه */
//static uint8_t wt_learn(uint32_t fset, WtRef_t *ref)
//{
//    float sw = 0.0f, pu = 0.0f; uint8_t good = 0U;
//    for (uint8_t i = 0U; i < 6U; i++)
//    {
//        WtMeas_t m;
//        wt_measure(fset, &m);
//        if (!m.tooFast && m.swing >= WT_MIN_SWING_V && m.purity >= WT_PURITY_MIN)
//        {
//            sw += m.swing; pu += m.purity; good++;
//        }
//    }
//    if (good < 5U) return 0U;
//    ref->valid  = 1U;
//    ref->swing  = sw / (float)good;
//    ref->purity = pu / (float)good;
//    return 1U;
//}
//
//static void App_WiringTest(void)
//{
//    LEDs_AllOff();
//    UI_Begin("WIRING TEST");
//    UI_Hint("OK learn ref L/R freq U/D snd");
//
//    uint8_t   fi = 3U;                               /* 1000 Hz: پاسخ سریع موقع کشیدن پروب */
//    uint32_t  pass = 0U, fail = 0U, drops = 0U;
//    WtState_t prev = WT_OK;
//    uint8_t   restart = 1U, snd = 1U;
//    WtRef_t   ref = {0U, 0.0f, 0.0f};
//    char      msg[20] = "";
//    uint32_t  msgUntil = 0U;
//    uint32_t  last = 0U - 1000U;
//    uint16_t  barW = 0xFFFFU, barCol = 0U;
//
//    WtLine_t ln[8];
//    memset(ln, 0, sizeof ln);
//
//    UI_Row(0U, UI_FG, UI_BG, "P2 PWM > P1 ADC");
//
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK) break;
//        if (in == MENU_INPUT_RIGHT) { fi = (uint8_t)((fi + 1U) % WT_FREQ_COUNT); restart = 1U; ref.valid = 0U; }
//        if (in == MENU_INPUT_LEFT)  { fi = (uint8_t)((fi + WT_FREQ_COUNT - 1U) % WT_FREQ_COUNT); restart = 1U; ref.valid = 0U; }
//        if (in == MENU_INPUT_UP || in == MENU_INPUT_DOWN) snd = (uint8_t)!snd;
//
//        if (restart)
//        {
//            restart = 0U;
//            Probe_PwmStart(PROBE_2, k_wtFreq[fi], WT_DUTY);     /* Probe2 خروجی می‌ده ، Probe1 فقط می‌خونه */
//            HAL_Delay(10);
//            last = 0U - 1000U;
//        }
//        if (in == MENU_INPUT_SELECT)                            /* Probe1 باید روی نقطه‌ی سالم (کنار Probe2) باشه */
//        {
//            wt_buzzer(0U);
//            snprintf(msg, sizeof msg, "%s", wt_learn(k_wtFreq[fi], &ref) ? "Ref saved" : "No signal!");
//            msgUntil = HAL_GetTick() + 1500U;
//            pass = 0U; fail = 0U; drops = 0U; prev = WT_OK;
//            last = 0U - 1000U;
//        }
//        if (!TICK_DUE(last, 40U)) continue;
//        last = HAL_GetTick();
//
//        WtMeas_t m;
//        wt_measure(k_wtFreq[fi], &m);
//        WtState_t st = wt_check(&m, &ref);
//
//        if (st == WT_OK) { if (pass < 99999U) pass++; }
//        else             { if (fail < 99999U) fail++; }
//        if (prev == WT_OK && st != WT_OK && drops < 9999U) drops++;   /* قطع‌ووصل لحظه‌ای */
//        prev = st;
//        LED_Blue_Set(st == WT_OK);
//        wt_buzzer((uint8_t)(snd && st == WT_OK));              /* بوق ممتد = سیگنال می‌رسه (چشم لازم نیست) */
//
//        char b[32], t1[12], t2[12];
//        uint16_t col = (st == WT_OK) ? UI_OK : UI_BAD;
//        float swing = m.swing;
//
//        /* وضعیت بزرگ */
//        UI_Text(4U, 96U, 3U, col, UI_BG, (st == WT_OK) ? "WIRE OK" : "BROKEN");
//
//        if (HAL_GetTick() < msgUntil)
//        {
//            wt_row(&ln[1], 1U, UI_TITLE, msg);
//        }
//        else
//        {
//            snprintf(b, sizeof b, "Out %luHz D%u%%", (unsigned long)k_wtFreq[fi], (unsigned)WT_DUTY);
//            wt_row(&ln[1], 1U, UI_CYAN, b);
//        }
//
//        UI_FmtFixed(t1, sizeof t1, swing, 1U);
//        if (ref.valid) { UI_FmtFixed(t2, sizeof t2, ref.swing, 1U); snprintf(b, sizeof b, "Amp%s Ref%sV", t1, t2); }
//        else           { snprintf(b, sizeof b, "Amp%sV  no ref", t1); }
//        wt_row(&ln[4], 4U, UI_FG, b);
//
//        snprintf(b, sizeof b, "Match %u%%", (unsigned)(m.purity * 100.0f + 0.5f));
//        wt_row(&ln[5], 5U, UI_ORANGE, b);
//
//        /* نوار دامنه نسبت به مرجع (بدون مرجع: نسبت به ۵ ولت) */
//        {
//            float full = ref.valid ? ref.swing : 5.0f;
//            float fr = (full > 0.1f) ? swing / full : 0.0f;
//            if (fr > 1.0f) fr = 1.0f;
//            if (fr < 0.0f) fr = 0.0f;
//            uint16_t w = (uint16_t)(fr * 228.0f);
//            if (w != barW || col != barCol) { barW = w; barCol = col; wt_bar(w, col); }
//        }
//
//        snprintf(b, sizeof b, "Drops %lu Snd:%s", (unsigned long)drops, snd ? "ON" : "OFF");
//        wt_row(&ln[7], 7U, (drops != 0U) ? UI_BAD : UI_OK, b);
//
//        snprintf(b, sizeof b, "Ok%lu Bad%lu %s", (unsigned long)pass, (unsigned long)fail, k_wtReason[st]);
//        UI_Text(4U, 212U, 1U, UI_DIM, UI_BG, b);
//    }
//    wt_buzzer(0U);
//    LED_Blue_Set(0);
//    Probe_PwmStop();
//    LEDs_AllOff();
//}
//
///* ================================================================== */
///*  Coming soon                                                        */
///* ================================================================== */
//static void App_ComingSoon(uint8_t idx)
//{
//    LEDs_AllOff();
//    const UiTheme_t *th = UI_Theme();
//    LCD_FillRect(0U, 24U, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - 24U), th->bg);
//    const PageTitle_t *t = &g_pageTitles[idx];
//    LCD_DrawBitmap1bpp((uint16_t)((LCD_WIDTH - t->w) / 2U), 44U, t->w, t->h, t->bits, th->dark, th->bg);
//    UI_DrawPageIcon((LCD_WIDTH - PAGE_ICON_W) / 2U, 84U, idx);
//    const PageTitle_t *c = &g_pageTitles[TITLE_COMING_SOON];
//    LCD_DrawBitmap1bpp((uint16_t)((LCD_WIDTH - c->w) / 2U), 192U, c->w, c->h, c->bits, th->accent, th->bg);
//    LCD_DrawString(78U, 218U, "Coming soon", th->dark, th->bg, 1U);
//    for (;;)
//    {
//        if (UI_Poll() == MENU_INPUT_BACK) break;
//    }
//}
//
///* ================================================================== */
///*  Sub menus (UP / DOWN = move, ENTER = open, BACK = leave)           */
///* ================================================================== */
//static void RunSubMenu(const MenuScreen_t *scr)
//{
//    Menu_Init(scr);
//    for (;;)
//    {
//        MenuInput_t in = UI_Poll();
//        if (in == MENU_INPUT_BACK) break;
//        if (in != MENU_INPUT_NONE) Menu_HandleInput(in);
//        Menu_Draw();
//    }
//}
//
//static const MenuItem_t s_testItems[] = {
//    { "GPIO Test", App_GpioTest },
//    { "Key Test",  App_KeyTest  },
//    { "LED Test",  App_LedTest  },
//    { "Video",     App_Video    },
//};
//static const MenuScreen_t s_testScreen = {
//    .title = "TESTS", .items = s_testItems,
//    .count = (uint8_t)(sizeof(s_testItems) / sizeof(s_testItems[0])),
//};
//static void App_Tests(void) { RunSubMenu(&s_testScreen); }
//
//static const MenuItem_t s_simItems[] = {
//    { "PWM Gen/Read", App_Pwm },
//    { "DAC Out",      App_Dac },
//};
//static const MenuScreen_t s_simScreen = {
//    .title = "SENSOR SIM", .items = s_simItems,
//    .count = (uint8_t)(sizeof(s_simItems) / sizeof(s_simItems[0])),
//};
//static void App_SensorSim(void) { RunSubMenu(&s_simScreen); }
//
///* ================================================================== */
///*  Home pages -> functions                                            */
///*   0 Dashboard  1 Tests  2 Logic analyzer  3 Voltmeter  4 Simulator  */
///*   5 Scope  6 Ohmmeter  7 Buzzer  8 Wiring test  9 ECU flash                      */
///* ================================================================== */
//void App_RunPage(uint8_t idx)
//{
//    UI_SetTheme(idx);          /* every screen opened from this page uses its color */
//    switch (idx)
//    {
//    case 1: App_Tests();     break;
//    case 3: App_Voltmeter(); break;
//    case 4: App_SensorSim(); break;
//    case 5: App_Scope();     break;
//    case 6: App_Ohmmeter();  break;
//    case 7: App_Buzzer();    break;
//    case 8: App_WiringTest(); break;
//    default: App_ComingSoon(idx); break;     /* 0, 2, 9 */
//    }
//}
#include "app.h"
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

static uint16_t s_vmBuf[VM_N];

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

    float dt = ((float)cyc / (float)SystemCoreClock) / (float)VM_N;
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

/* Rx (قبل از کم‌کردن مقاومت سری) برحسب اهم؛ منفی = OPEN. vpin = ولتاژ واقعی Probe1.
 * ⚠ PB1 (ADC1_IN5) از طریق C46 خازنی کوپل شده و DC رو نمی‌بینه (فقط برای سیگنال AC) ؛
 *   پس ولتاژ DC پروب از PB0 (قبل از OPAMP) خونده می‌شه. */
static float ohm_raw(uint8_t r, float *vpin)
{
    Probe_OhmSelect(r);                             /* Probe2 = 0V ، پول‌آپ 3.3V با Rref روی Probe1 */
    HAL_Delay(8);                                   /* صبر برای نشستن سوئیچ‌ها */
    float v = Adc_GetProbe1Dc();
    if (v < 0.0f) v = 0.0f;
    *vpin = v;
    if (v >= OHM_VREF * OHM_OPEN_FRACTION) return -1.0f;
    return k_ohmRanges[r].rref * v / (OHM_VREF - v);
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
    else if (r < 1.0e6f)     { UI_FmtFixed(t, sizeof t, r / 1000.0f, 2); snprintf(dst, n, "%s k", t); }
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

        uint8_t r = (mode == 0U) ? 0U : (uint8_t)(mode - 1U);
        float vp, rx = ohm_measure(r, &vp);

        if (mode == 0U)
        {
            /* رنجی رو انتخاب کن که Rref ش به Rx نزدیک‌تره (بهترین دقت) */
            uint8_t best = (rx < 0.0f) ? (uint8_t)(OHM_RANGES - 1U) : r;
            if (rx >= 0.0f)
            {
                float bs = 1.0e30f;
                for (uint8_t k = 0U; k < OHM_RANGES; k++)
                {
                    float q = (rx > 1.0f) ? rx / k_ohmRanges[k].rref : 1.0f / k_ohmRanges[k].rref;
                    if (q < 1.0f) q = 1.0f / q;
                    if (q < bs) { bs = q; best = k; }
                }
            }
            if (best != r) { r = best; rx = ohm_measure(r, &vp); }
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
        snprintf(b, sizeof b, "SEL active %s", Probe_SwActiveHigh() ? "HIGH" : "LOW");
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
    UI_Begin("DAC OUT");
    UI_Hint("U/D volt L/R ch OK on/off");

    uint8_t ch = 1U, on[3] = { 0U, 0U, 0U };
    float   v[3] = { 0.0f, 0.0f, 0.0f };
    uint8_t redraw = 1U;
    uint32_t last = 0U - 1000U;

    Probe_DacSet(1U, 0.0f);
    Probe_DacSet(2U, 0.0f);

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;

        if (in == MENU_INPUT_LEFT || in == MENU_INPUT_RIGHT) { ch = (ch == 1U) ? 2U : 1U; redraw = 1U; }
        if (in == MENU_INPUT_UP)   { v[ch] += 0.1f; if (v[ch] > 3.3f) v[ch] = 3.3f; Probe_DacSet(ch, v[ch]); redraw = 1U; }
        if (in == MENU_INPUT_DOWN) { v[ch] -= 0.1f; if (v[ch] < 0.0f) v[ch] = 0.0f; Probe_DacSet(ch, v[ch]); redraw = 1U; }
        if (in == MENU_INPUT_SELECT) { on[ch] = !on[ch]; Probe_DacSwitch(ch, on[ch]); redraw = 1U; }

        if (redraw || TICK_DUE(last, 300U))
        {
            redraw = 0U;
            last = HAL_GetTick();
            char b[32], t[16];
            snprintf(b, sizeof b, "Channel DAC%u", (unsigned)ch);            UI_Row(0U, UI_FG, UI_BG, b);
            UI_FmtFixed(t, sizeof t, v[ch], 2);
            snprintf(b, sizeof b, "Pin %s V", t);                            UI_Row(1U, UI_FG, UI_BG, b);
            UI_FmtFixed(t, sizeof t, v[ch] * DAC_GAIN, 2);
            snprintf(b, sizeof b, "Out %s V", t);                            UI_Row(2U, UI_FG, UI_BG, b);
            snprintf(b, sizeof b, "Switch %s", on[ch] ? "ON" : "OFF");       UI_Row(3U, on[ch] ? UI_OK : UI_FG, UI_BG, b);
            UI_FmtFixed(t, sizeof t, Probe_ReadVolts(RD_PB1), 2);
            snprintf(b, sizeof b, "PB1 %s V", t);                            UI_Row(5U, UI_CYAN, UI_BG, b);
            UI_FmtFixed(t, sizeof t, Probe_ReadVolts(RD_PA6), 2);
            snprintf(b, sizeof b, "PA6 %s V", t);                            UI_Row(6U, UI_ORANGE, UI_BG, b);
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
    static const char *const names[4] = { "yellow", "blue", "green", "red" };
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
#define SCOPE_N      4096U
#define SCOPE_Y0     52U
#define SCOPE_H      150U
#define SCOPE_GRID   0x2104U
#define SCOPE_FILT_W 6                      /* نیم‌پنجره‌ی فیلتر: ±6 نمونه */
static uint16_t s_scopeBuf[SCOPE_N];
static uint16_t s_scopeFilt[SCOPE_N];

static uint32_t sc_abs_diff(uint32_t a, uint32_t b) { return (a > b) ? (a - b) : (b - a); }

/* میزان نویز: میانگین اختلاف نمونه‌های پشت‌سرهم (لبه‌های بزرگ حساب نمی‌شن) */
static uint32_t sc_noise_level(const uint16_t *b, uint32_t n, uint32_t span)
{
    uint32_t lim = span / 3U + 8U, cnt = 0U;
    uint64_t sum = 0U;
    for (uint32_t i = 1U; i < n; i++)
    {
        uint32_t d = sc_abs_diff(b[i], b[i - 1U]);
        if (d <= lim) { sum += d; cnt++; }
    }
    return (cnt != 0U) ? (uint32_t)(sum / cnt) : 0U;
}

/* median ۵ تایی: مرجع بدون نویز که لبه‌ها رو نگه می‌داره */
static uint32_t sc_med5(const uint16_t *b, uint32_t i, uint32_t n)
{
    uint32_t v[5];
    for (int k = 0; k < 5; k++)
    {
        int32_t j = (int32_t)i + k - 2;
        if (j < 0) j = 0;
        if (j >= (int32_t)n) j = (int32_t)n - 1;
        v[k] = b[j];
    }
    for (int x = 1; x < 5; x++)                     /* مرتب‌سازی درجی */
    {
        uint32_t t = v[x]; int y = x - 1;
        while (y >= 0 && v[y] > t) { v[y + 1] = v[y]; y--; }
        v[y + 1] = t;
    }
    return v[2];
}

/* فیلتر حفظ‌کننده‌ی لبه:
 *   ref = median-5 اطراف نمونه (حذف spike، لبه‌ها سالم می‌مونن)
 *   خروجی = میانگین همسایه‌های ±W که از ref بیشتر از T دور نیستن
 *   -> نویز روی سطح‌ها و شیب‌ها میانگین‌گیری می‌شه، لبه‌ی پالس گرد نمی‌شه،
 *      نمونه‌ی پرت (spike) به‌طور خودکار کنار گذاشته می‌شه. */
static void sc_filter(const uint16_t *in, uint16_t *out, uint32_t n, uint32_t span)
{
    uint32_t nd = sc_noise_level(in, n, span);
    uint32_t T  = 4U * nd + 16U;
    if (T < span / 16U) T = span / 16U;                       /* شیب‌های نرم (سینوس) هم میانگین‌گیری بشن */
    uint32_t Tmax = (span / 3U > 16U) ? span / 3U : 16U;     /* لبه‌ها رو نخوره */
    if (T > Tmax) T = Tmax;

    for (uint32_t i = 0U; i < n; i++)
    {
        uint32_t ref = sc_med5(in, i, n);
        uint32_t sum = 0U, cnt = 0U;
        uint32_t j0 = (i >= (uint32_t)SCOPE_FILT_W) ? i - (uint32_t)SCOPE_FILT_W : 0U;
        uint32_t j1 = i + (uint32_t)SCOPE_FILT_W;
        if (j1 >= n) j1 = n - 1U;
        for (uint32_t j = j0; j <= j1; j++)
        {
            uint32_t v = in[j];
            if (sc_abs_diff(v, ref) <= T) { sum += v; cnt++; }
        }
        out[i] = (cnt != 0U) ? (uint16_t)(sum / cnt) : (uint16_t)ref;
    }
}

/* پس‌زمینه‌ی [ya, yb) از ستون x با خط‌چین شبکه (بدون پاک‌کردن کل صفحه = بدون پرش) */
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

static void App_Scope(void)
{
    LEDs_AllOff();
    UI_Begin("SCOPE");
    UI_Hint("U/D src L/R zoom OK filter");
    LCD_FillRect(0U, SCOPE_Y0, LCD_WIDTH, SCOPE_H, 0x0000U);

    static const uint8_t zoom[] = { 1U, 2U, 4U, 8U, 16U };
    uint8_t zi = 1U;
    uint8_t filt = 1U;
    ReadSrc_t src = RD_PB1;
    uint32_t last = 0U - 1000U;

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_RIGHT) zi = (uint8_t)((zi + 1U) % sizeof(zoom));
        if (in == MENU_INPUT_LEFT)  zi = (uint8_t)((zi + sizeof(zoom) - 1U) % sizeof(zoom));
        if (in == MENU_INPUT_SELECT) filt = (uint8_t)!filt;
        if (in == MENU_INPUT_UP || in == MENU_INPUT_DOWN) src = (src == RD_PB1) ? RD_PA6 : RD_PB1;
        if (!TICK_DUE(last, 120U)) continue;
        last = HAL_GetTick();

        uint32_t cyc = Probe_Capture(src, s_scopeBuf, SCOPE_N);
        if (cyc == 0U) continue;

        /* بازه‌ی خام برای تخمین نویز */
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
            mn = 65535U; mx = 0U;                         /* min/max/ماشه از داده‌ی فیلترشده */
            for (uint32_t i = 0U; i < SCOPE_N; i++)
            {
                if (d[i] < mn) mn = d[i];
                if (d[i] > mx) mx = d[i];
            }
        }

        /* ماشه: اولین لبه‌ی بالارونده‌ی وسط سطح، با هیسترزیس (۴۰٪ -> ۶۰٪) */
        uint32_t span = mx - mn, start = 0U;
        if (span > 2000U)
        {
            uint32_t lo = mn + (span * 4U) / 10U, hi = mn + (span * 6U) / 10U;
            uint8_t armed = 0U;
            for (uint32_t i = 0U; i < SCOPE_N / 2U; i++)
            {
                if (d[i] <= lo) armed = 1U;
                else if (armed && d[i] >= hi) { start = i; break; }
            }
        }

        uint32_t top = (mx > 6000U) ? mx + mx / 8U : 6000U;   /* مقیاس عمودی: 0 .. top */
        const uint16_t yMin = SCOPE_Y0, yMax = (uint16_t)(SCOPE_Y0 + SCOPE_H - 1U);
        uint16_t prevY = 0xFFFFU;

        for (uint16_t x = 0U; x < LCD_WIDTH; x++)
        {
            uint32_t idx = start + (uint32_t)x * zoom[zi];
            if (idx >= SCOPE_N)                           /* داده تموم شد: فقط پس‌زمینه */
            {
                sc_col_bg(x, yMin, (uint16_t)(yMax + 1U));
                continue;
            }
            /* هر پیکسل = میانگین zoom نمونه (نویز کمتر، بدون aliasing) */
            uint32_t sum = 0U, cnt = 0U;
            for (uint32_t k = idx; k < idx + zoom[zi] && k < SCOPE_N; k++) { sum += d[k]; cnt++; }
            uint32_t v = sum / cnt;
            uint16_t y = (uint16_t)(yMax - (v * (SCOPE_H - 1U)) / top);
            if (y < yMin) y = yMin;

            uint16_t y0 = (prevY == 0xFFFFU) ? y : ((y < prevY) ? y : prevY);
            uint16_t y1 = (prevY == 0xFFFFU) ? y : ((y < prevY) ? prevY : y);
            if (y1 <= y0) { if (y1 < yMax) y1++; else y0--; }      /* ضخامت حداقل ۲ پیکسل */
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
        LED_Blue_Set(span > 2000U);
    }
    LEDs_AllOff();
}

/* ================================================================== */
/*  Wiring test (تست سیم‌کشی)                                           */
/*                                                                      */
/*  سناریو: یه سر سیم به ECU ماشین وصله، سر دیگه بیرونه.                    */
/*   - Probe2 رو به سر آزاد سیم وصل کن: یه PWM مشخص توی سیم می‌فرسته.        */
/*   - Probe1 رو (سوزنی / back-probe) روی نقطه‌های مختلف سیم بذار و بکش جلو.  */
/*   - تا جایی که Probe1 همون PWM رو بگیره = سیم تا اونجا سالمه (WIRE OK ،    */
/*     بوق ممتد). از هر جایی که نگرفت = سیم بین اون نقطه و Probe2 قطعه.        */
/*                                                                      */
/*  چون سر دیگه به ECU وصله و بار می‌ذاره، دامنه/دیوتی موج ممکنه با حالت       */
/*  ایده‌آل فرق کنه. پس اول Probe1 رو بذار روی همون نقطه‌ی تزریق (کنار Probe2)  */
/*  و OK بزن تا شکل موج «سالم» به‌عنوان مرجع یاد گرفته بشه؛ بعد هر جا شبیه       */
/*  مرجع بود = سالم.                                                       */
/* ================================================================== */
typedef enum { WT_OK = 0, WT_NO_SIGNAL, WT_WEAK, WT_NOISY, WT_TOO_FAST } WtState_t;

static const char *const k_wtReason[] = { "match", "no signal", "weak", "irregular", "too fast" };
static const uint32_t    k_wtFreq[] = { 100U, 200U, 500U, 1000U, 2000U, 10000U };
#define WT_FREQ_COUNT      ((uint8_t)(sizeof(k_wtFreq) / sizeof(k_wtFreq[0])))
#define WT_DUTY            50U
#define WT_MIN_SWING_V     0.3f     /* بدون مرجع: دامنه‌ی کمتر از این = ضعیف */
#define WT_NOISE_V         0.1f     /* دامنه‌ی کمتر از این = اصلاً سیگنالی نیست */
#define WT_REF_FRACTION    0.35f    /* با مرجع: دامنه باید حداقل این کسر از مرجع باشه */
#define WT_PURITY_MIN      0.25f    /* سهم انرژی موج که دقیقاً روی فرکانس ارسالی ماست (نویز/سیگنال دیگه = کم) */

/* نتیجه‌ی یک اندازه‌گیری روی Probe1.
 * روش: تشخیص هم‌فرکانس (DFT فقط روی فرکانس خودمون) ؛ به لبه‌ها و دقت نمونه‌برداری وابسته نیست. */
typedef struct {
    float   swing;      /* دامنه‌ی معادل موج مربعی (پیک تا پیک، ولت) */
    float   purity;     /* 0..1 : چقدر از موج همون فرکانس ارسالی است */
    uint8_t tooFast;    /* 1 = نمونه‌برداری برای این فرکانس کافی نیست */
    float   freq;       /* فرکانسی که واقعاً پیدا شد (Hz) */
    float   ratio;      /* قله / زمینه */
    float   pp;         /* پیک‌تاپیک خام (ولت) */
} WtMeas_t;

/* شکل موج «سالم» که کاربر یاد داده */
typedef struct { uint8_t valid; float swing; float purity; } WtRef_t;

static WtState_t wt_check(const WtMeas_t *m, const WtRef_t *ref)
{
    if (m->tooFast) return WT_TOO_FAST;
    float minSw = ref->valid ? (ref->swing * WT_REF_FRACTION) : WT_MIN_SWING_V;
    if (minSw < WT_NOISE_V) minSw = WT_NOISE_V;
    float minPu = ref->valid ? (ref->purity * 0.5f) : WT_PURITY_MIN;
    if (minPu < WT_PURITY_MIN) minPu = WT_PURITY_MIN;

    if (m->swing < WT_NOISE_V) return WT_NO_SIGNAL;          /* هیچ نوسانی نیومده */
    if (m->purity < minPu)     return WT_NOISY;              /* نوسان هست ولی همون موج ما نیست */
    if (m->swing < minSw)      return WT_WEAK;
    return WT_OK;
}

/* sin/cos بدون libm: برای 0 <= w <= ~3 rad ، تیلور روی w/4 و بعد دو بار زاویه‌ی دوبرابر */
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

/* Probe1 رو از مسیر DC خودش (PC4 ، همون مسیر ولت‌متر) می‌خونه، نه PB1 که خازنی کوپل شده.
 * پنجره ≈ ۴ پریود PWM (یا سریع‌ترین نمونه‌برداری ممکن). */
#define WT_MAX_BINS 420U
static void wt_measure(uint32_t fset, WtMeas_t *m)
{
    memset(m, 0, sizeof *m);
    uint32_t per = (uint32_t)(((uint64_t)SystemCoreClock * 4U) / fset) / VM_N;
    uint32_t gap = (per > 300U) ? (per - 300U) : 0U;
    uint32_t cyc = Adc_CapturePC4(s_vmBuf, VM_N, gap);
    if (cyc == 0U) return;

    float dt  = ((float)cyc / (float)SystemCoreClock) / (float)VM_N;   /* فاصله‌ی واقعی نمونه‌ها */
    float spp = 1.0f / ((float)fset * dt);                              /* نمونه در هر پریود */
    if (spp < 2.5f) { m->tooFast = 1U; return; }
    uint32_t n = VM_N;
    if ((float)n > 40.0f * spp) n = (uint32_t)(40.0f * spp);           /* حداکثر ~۴۰ پریود (سرعت محاسبه) */
    float periods = (float)n / spp;
    if (n < 16U || periods < 2.0f) return;

    static float x[VM_N];
    float mean = 0.0f, mn = 1.0e9f, mx = -1.0e9f;
    for (uint32_t i = 0U; i < n; i++)
    {
        x[i] = vm_counts_to_vin(s_vmBuf[i]); mean += x[i];
        if (x[i] < mn) mn = x[i];
        if (x[i] > mx) mx = x[i];
    }
    mean /= (float)n;
    for (uint32_t i = 0U; i < n; i++) x[i] -= mean;
    m->pp = mx - mn;

    /* جستجوی فرکانس: ۰.۳× تا ۲× فرکانس ارسالی؛ قله نسبت به «زمینه» (میانگین بقیه‌ی فرکانس‌ها)
     * سنجیده می‌شه، پس نویز پهن‌باند و کمی اختلاف فرکانس مشکلی ایجاد نمی‌کنه. */
    static float amp[WT_MAX_BINS];
    float step = 0.2f / periods;
    uint32_t nb = 0U, pk = 0U;
    float r = 0.3f;
    for (; nb < WT_MAX_BINS && r <= 2.0f; nb++, r += step)
    {
        float sw, cw;
        float w = 6.2831853f * (float)fset * r * dt;
        if (w > 3.0f) break;
        wt_sincos(w, &sw, &cw);
        float c = 1.0f, s = 0.0f, re = 0.0f, im = 0.0f;
        for (uint32_t i = 0U; i < n; i++)
        {
            re += x[i] * c;
            im -= x[i] * s;
            float cn = c * cw - s * sw;
            s = s * cw + c * sw;
            c = cn;
        }
        amp[nb] = 2.0f * __builtin_sqrtf(re * re + im * im) / (float)n;
        if (amp[nb] > amp[pk]) pk = nb;
    }
    if (nb < 8U) return;
    float bg = 0.0f; uint32_t nbg = 0U;
    for (uint32_t i = 0U; i < nb; i++)
    {
        uint32_t d = (i > pk) ? (i - pk) : (pk - i);
        if (d > 3U) { bg += amp[i]; nbg++; }
    }
    bg = (nbg != 0U) ? bg / (float)nbg : 0.0f;
    float ratio = amp[pk] / (bg + 1.0e-6f);
    m->swing = amp[pk] * 1.5707963f;
    m->freq  = (float)fset * (0.3f + step * (float)pk);
    m->ratio = ratio;
    float p = 1.0f - 3.0f / ratio;                      /* ratio ≈ 3 یعنی فقط نویز */
    m->purity = (p < 0.0f) ? 0.0f : ((p > 1.0f) ? 1.0f : p);
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
static uint8_t wt_learn(uint32_t fset, WtRef_t *ref)
{
    float sw = 0.0f, pu = 0.0f; uint8_t good = 0U;
    for (uint8_t i = 0U; i < 6U; i++)
    {
        WtMeas_t m;
        wt_measure(fset, &m);
        if (!m.tooFast && m.swing >= WT_MIN_SWING_V && m.purity >= WT_PURITY_MIN)
        {
            sw += m.swing; pu += m.purity; good++;
        }
    }
    if (good < 5U) return 0U;
    ref->valid  = 1U;
    ref->swing  = sw / (float)good;
    ref->purity = pu / (float)good;
    return 1U;
}

static void App_WiringTest(void)
{
    LEDs_AllOff();
    UI_Begin("WIRING TEST");
    UI_Hint("OK learn ref L/R freq U/D snd");

    uint8_t   fi = 3U;                               /* 1000 Hz: پاسخ سریع موقع کشیدن پروب */
    uint32_t  pass = 0U, fail = 0U, drops = 0U;
    WtState_t prev = WT_OK;
    uint8_t   restart = 1U, snd = 1U;
    WtRef_t   ref = {0U, 0.0f, 0.0f};
    char      msg[20] = "";
    uint32_t  msgUntil = 0U;
    uint32_t  last = 0U - 1000U;
    uint16_t  barW = 0xFFFFU, barCol = 0U;

    WtLine_t ln[8];
    memset(ln, 0, sizeof ln);

    UI_Row(0U, UI_FG, UI_BG, "P2 PWM > P1 ADC");

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_RIGHT) { fi = (uint8_t)((fi + 1U) % WT_FREQ_COUNT); restart = 1U; ref.valid = 0U; }
        if (in == MENU_INPUT_LEFT)  { fi = (uint8_t)((fi + WT_FREQ_COUNT - 1U) % WT_FREQ_COUNT); restart = 1U; ref.valid = 0U; }
        if (in == MENU_INPUT_UP || in == MENU_INPUT_DOWN) snd = (uint8_t)!snd;

        if (restart)
        {
            restart = 0U;
            Probe_PwmStart(PROBE_2, k_wtFreq[fi], WT_DUTY);     /* Probe2 خروجی می‌ده ، Probe1 فقط می‌خونه */
            HAL_Delay(10);
            last = 0U - 1000U;
        }
        if (in == MENU_INPUT_SELECT)                            /* Probe1 باید روی نقطه‌ی سالم (کنار Probe2) باشه */
        {
            wt_buzzer(0U);
            snprintf(msg, sizeof msg, "%s", wt_learn(k_wtFreq[fi], &ref) ? "Ref saved" : "No signal!");
            msgUntil = HAL_GetTick() + 1500U;
            pass = 0U; fail = 0U; drops = 0U; prev = WT_OK;
            last = 0U - 1000U;
        }
        if (!TICK_DUE(last, 40U)) continue;
        last = HAL_GetTick();

        WtMeas_t m;
        wt_measure(k_wtFreq[fi], &m);
        WtState_t st = wt_check(&m, &ref);

        if (st == WT_OK) { if (pass < 99999U) pass++; }
        else             { if (fail < 99999U) fail++; }
        if (prev == WT_OK && st != WT_OK && drops < 9999U) drops++;   /* قطع‌ووصل لحظه‌ای */
        prev = st;
        LED_Blue_Set(st == WT_OK);
        wt_buzzer((uint8_t)(snd && st == WT_OK));              /* بوق ممتد = سیگنال می‌رسه (چشم لازم نیست) */

        char b[32], t1[12], t2[12];
        uint16_t col = (st == WT_OK) ? UI_OK : UI_BAD;
        float swing = m.swing;

        /* وضعیت بزرگ */
        UI_Text(4U, 96U, 3U, col, UI_BG, (st == WT_OK) ? "WIRE OK" : "BROKEN");

        if (HAL_GetTick() < msgUntil)
        {
            wt_row(&ln[1], 1U, UI_TITLE, msg);
        }
        else
        {
            snprintf(b, sizeof b, "Out %luHz D%u%%", (unsigned long)k_wtFreq[fi], (unsigned)WT_DUTY);
            wt_row(&ln[1], 1U, UI_CYAN, b);
        }

        UI_FmtFixed(t1, sizeof t1, swing, 1U);
        if (ref.valid) { UI_FmtFixed(t2, sizeof t2, ref.swing, 1U); snprintf(b, sizeof b, "Amp%s Ref%sV", t1, t2); }
        else           { snprintf(b, sizeof b, "Amp%sV  no ref", t1); }
        wt_row(&ln[4], 4U, UI_FG, b);

        snprintf(b, sizeof b, "Match %u%%", (unsigned)(m.purity * 100.0f + 0.5f));
        wt_row(&ln[5], 5U, UI_ORANGE, b);

        {
            char fs[12], ps[10];
            if (m.freq < 1000.0f) { UI_FmtFixed(fs, sizeof fs, m.freq, 0U); strcat(fs, "Hz"); }
            else { UI_FmtFixed(fs, sizeof fs, m.freq / 1000.0f, 1U); strcat(fs, "kHz"); }
            UI_FmtFixed(ps, sizeof ps, m.pp, 1U);
            snprintf(b, sizeof b, "pp%sV %s x%u", ps, fs, (unsigned)(m.ratio + 0.5f));
            wt_row(&ln[6], 6U, UI_DIM, b);
        }

        /* نوار دامنه نسبت به مرجع (بدون مرجع: نسبت به ۵ ولت) */
        {
            float full = ref.valid ? ref.swing : 5.0f;
            float fr = (full > 0.1f) ? swing / full : 0.0f;
            if (fr > 1.0f) fr = 1.0f;
            if (fr < 0.0f) fr = 0.0f;
            uint16_t w = (uint16_t)(fr * 228.0f);
            if (w != barW || col != barCol) { barW = w; barCol = col; wt_bar(w, col); }
        }

        snprintf(b, sizeof b, "Drops %lu Snd:%s", (unsigned long)drops, snd ? "ON" : "OFF");
        wt_row(&ln[7], 7U, (drops != 0U) ? UI_BAD : UI_OK, b);

        snprintf(b, sizeof b, "Ok%lu Bad%lu %s", (unsigned long)pass, (unsigned long)fail, k_wtReason[st]);
        UI_Text(4U, 212U, 1U, UI_DIM, UI_BG, b);
    }
    wt_buzzer(0U);
    LED_Blue_Set(0);
    Probe_PwmStop();
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
};
static const MenuScreen_t s_testScreen = {
    .title = "TESTS", .items = s_testItems,
    .count = (uint8_t)(sizeof(s_testItems) / sizeof(s_testItems[0])),
};
static void App_Tests(void) { RunSubMenu(&s_testScreen); }

static const MenuItem_t s_simItems[] = {
    { "PWM Gen/Read", App_Pwm },
    { "DAC Out",      App_Dac },
};
static const MenuScreen_t s_simScreen = {
    .title = "SENSOR SIM", .items = s_simItems,
    .count = (uint8_t)(sizeof(s_simItems) / sizeof(s_simItems[0])),
};
static void App_SensorSim(void) { RunSubMenu(&s_simScreen); }

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
    case 3: App_Voltmeter(); break;
    case 4: App_SensorSim(); break;
    case 5: App_Scope();     break;
    case 6: App_Ohmmeter();  break;
    case 7: App_Buzzer();    break;
    case 8: App_WiringTest(); break;
    default: App_ComingSoon(idx); break;     /* 0, 2, 9 */
    }
}

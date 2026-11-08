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

static void vm_draw_wave(uint32_t start, float vtop)
{
    const uint16_t yMin = VM_IY, yMax = (uint16_t)(VM_IY + VM_IH - 1U);
    uint16_t prevY = 0xFFFFU;

    for (uint16_t cx = 0U; cx < VM_IW; cx++)
    {
        uint32_t a = start + ((uint32_t)cx * VM_SHOW) / VM_IW;
        uint32_t b = start + ((uint32_t)(cx + 1U) * VM_SHOW) / VM_IW;
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
static uint8_t vm_analyze(uint16_t mn, uint16_t mx, uint32_t cyc, float *hz, float *duty)
{
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
    if (period_samples < 4.0f) return 0U;                 /* خیلی سریع: قابل اعتماد نیست */
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

static void App_Voltmeter(void)
{
    UI_Begin("VOLTMETER");
    UI_Hint("OK=zero L=reset U/D=time BACK");
    LCD_FillRect(VM_BOX_X, VM_BOX_Y, VM_BOX_W, VM_BOX_H, UI_Theme()->accent);
    LCD_FillRect(VM_IX, VM_IY, VM_IW, VM_IH, VM_BG);

    /* فاصله‌ی اضافه بین نمونه‌ها (µs). ردیف آخر صفحه پهنای زمانی کل نمودار رو نشون می‌ده (~ms) */
    static const uint16_t k_tbUs[] = { 0U, 2U, 5U, 10U, 20U, 50U, 100U, 200U };
    const uint8_t TB_N = (uint8_t)(sizeof(k_tbUs) / sizeof(k_tbUs[0]));
    uint8_t tb = 0U;

    char msg[24] = "";
    uint32_t msgUntil = 0U;
    uint32_t last = 0U - 1000U;
    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_SELECT)
        {
            /* پروب‌ها باید روی GND باشن */
            snprintf(msg, sizeof msg, "%s", Adc_CalibrateZero() ? "Zero set" : "Not zero!");
            msgUntil = HAL_GetTick() + 1500U;
            last = 0U - 1000U;
        }
        if (in == MENU_INPUT_LEFT)
        {
            Adc_ResetZero();
            snprintf(msg, sizeof msg, "Zero reset");
            msgUntil = HAL_GetTick() + 1500U;
            last = 0U - 1000U;
        }
        /* بالا = بازه‌ی زمانی بزرگ‌تر (نمونه‌ها کندتر) ، پایین = کوچک‌تر (سریع‌تر) */
        if (in == MENU_INPUT_UP   && tb + 1U < TB_N) { tb++; last = 0U - 1000U; }
        if (in == MENU_INPUT_DOWN && tb > 0U)        { tb--; last = 0U - 1000U; }
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
        uint32_t gap = (SystemCoreClock / 1000000U) * k_tbUs[tb];
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
            vm_draw_wave(start, vtop);
            LED_Blue_Set(sig);

            /* --- Hz / Duty (بالا سمت راست) --- */
            float hz = 0.0f, duty = 0.0f;
            uint8_t fok = sig ? vm_analyze((uint16_t)mn, (uint16_t)mx, cyc, &hz, &duty) : 0U;
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

            /* --- خط اطلاعات ریز: PC4 ، صفر ، مقیاس ، بازه‌ی زمانی --- */
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
            vm_fmt_time(b, sizeof b, ((float)cyc / (float)SystemCoreClock) / (float)VM_N * (float)VM_SHOW);
            snprintf(s, sizeof s, "%-8s", b);
            LCD_DrawString(184U, 196U, s, UI_MAGENTA, UI_BG, 1U);
        }

        UI_Text(4U, 207U, 2U, UI_TITLE, UI_BG, (HAL_GetTick() < msgUntil) ? msg : "");
    }
    LED_Blue_Set(0);
    LEDs_AllOff();
}

/* ================================================================== */
/*  Ohmmeter                                                           */
/* ================================================================== */
static const OhmRange_t k_ohmRanges[OHM_RANGES] = OHM_RANGE_TABLE;

/* Rx برحسب اهم؛ منفی = OPEN. vpin = ولتاژ پایه‌ی ADC */
static float ohm_measure(uint8_t r, float *vpin)
{
    Probe_OhmSelect(r);
    HAL_Delay(4);                                   /* صبر برای نشستن سوئیچ‌ها */
    float v = Adc_ReadPinVolts(&hadc1, OHM_ADC_CH);
    *vpin = v;
    if (v >= OHM_VREF * OHM_OPEN_FRACTION) return -1.0f;
    return k_ohmRanges[r].rref * v / (OHM_VREF - v);
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
    UI_Hint("L/R range  BACK exit");

    uint8_t mode = 0U;                              /* 0 = AUTO ، 1.. = رنج ثابت */
    uint32_t last = 0U - 1000U;
    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
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
        snprintf(b, sizeof b, "Vpin %s V", t);
        UI_Row(5U, UI_CYAN, UI_BG, b);
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
/*  Oscilloscope                                                       */
/* ================================================================== */
#define SCOPE_N      4096U
#define SCOPE_Y0     52U
#define SCOPE_H      150U
static uint16_t s_scopeBuf[SCOPE_N];

static void App_Scope(void)
{
    LEDs_AllOff();
    UI_Begin("SCOPE");
    UI_Hint("U/D src  L/R zoom  BACK");

    static const uint8_t zoom[] = { 1U, 2U, 4U, 8U, 16U };
    uint8_t zi = 1U;
    ReadSrc_t src = RD_PB1;
    uint32_t last = 0U - 1000U;

    for (;;)
    {
        MenuInput_t in = UI_Poll();
        if (in == MENU_INPUT_BACK) break;
        if (in == MENU_INPUT_RIGHT) zi = (uint8_t)((zi + 1U) % sizeof(zoom));
        if (in == MENU_INPUT_LEFT)  zi = (uint8_t)((zi + sizeof(zoom) - 1U) % sizeof(zoom));
        if (in == MENU_INPUT_UP || in == MENU_INPUT_DOWN) src = (src == RD_PB1) ? RD_PA6 : RD_PB1;
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
        /* trigger: first rising crossing of the mid level in the first half */
        uint32_t mid = (mn + mx) / 2U, start = 0U;
        uint32_t span = mx - mn;
        if (span > 2000U)
        {
            for (uint32_t i = 1U; i < SCOPE_N / 2U; i++)
            {
                if (s_scopeBuf[i - 1U] < mid && s_scopeBuf[i] >= mid) { start = i; break; }
            }
        }

        uint32_t top = (mx > 6000U) ? mx + mx / 8U : 6000U;   /* vertical scale: 0 .. top */
        LCD_FillRect(0U, SCOPE_Y0, LCD_WIDTH, SCOPE_H, 0x0000U);
        for (uint16_t g = 1U; g < 4U; g++)                     /* grid */
            LCD_FillRect(0U, (uint16_t)(SCOPE_Y0 + (SCOPE_H * g) / 4U), LCD_WIDTH, 1U, 0x2104U);

        uint16_t prevY = 0U;
        for (uint16_t x = 0U; x < LCD_WIDTH; x++)
        {
            uint32_t idx = start + (uint32_t)x * zoom[zi];
            if (idx >= SCOPE_N) break;
            uint32_t v = s_scopeBuf[idx];
            uint16_t y = (uint16_t)(SCOPE_Y0 + SCOPE_H - 1U - (v * (SCOPE_H - 1U)) / top);
            uint16_t y0 = (x == 0U) ? y : ((y < prevY) ? y : prevY);
            uint16_t y1 = (x == 0U) ? y : ((y < prevY) ? prevY : y);
            LCD_FillRect(x, y0, 1U, (uint16_t)(y1 - y0 + 1U), UI_OK);
            prevY = y;
        }

        char b[40], t1[12], t2[12];
        float dt_us = ((float)cyc / (float)SystemCoreClock) * 1.0e6f / (float)SCOPE_N;
        float win_ms = dt_us * (float)zoom[zi] * (float)LCD_WIDTH / 1000.0f;
        UI_FmtFixed(t1, sizeof t1, Probe_CountsToVolts(src, mx), 2);
        UI_FmtFixed(t2, sizeof t2, win_ms, 2);
        snprintf(b, sizeof b, "%s Vmax %s  %s ms", Probe_ReadName(src), t1, t2);
        UI_Text(2U, 204U, 1U, UI_DIM, UI_BG, b);
        LED_Blue_Set(span > 2000U);
    }
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
/*   5 Scope  6 Ohmmeter  7 Buzzer  8 ECU flash                        */
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
    default: App_ComingSoon(idx); break;     /* 0, 2, 8 */
    }
}

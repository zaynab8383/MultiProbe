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
/*  Voltmeter                                                          */
/* ================================================================== */
static void App_Voltmeter(void)
{
    UI_Begin("VOLTMETER");
    UI_Hint("OK=zero  LEFT=reset  BACK=exit");

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
        if (!TICK_DUE(last, 250U)) continue;
        last = HAL_GetTick();

        char v[16], b[32];
        float vin = Adc_GetVin();
        LEDs_UpdateVoltage(vin);

        UI_FmtFixed(v, sizeof v, vin, 2);
        snprintf(b, sizeof b, "%s V", v);
        UI_Text(4U, 56U, 3U, UI_OK, UI_BG, b);

        UI_FmtFixed(v, sizeof v, Adc_GetPC4(), 3);
        snprintf(b, sizeof b, "PC4  %s V", v);
        UI_Row(3U, UI_DIM, UI_BG, b);
        UI_FmtFixed(v, sizeof v, Adc_GetZero(), 3);
        snprintf(b, sizeof b, "Zero %s V", v);
        UI_Row(4U, UI_DIM, UI_BG, b);
        UI_FmtFixed(v, sizeof v, g_vdda, 2);
        snprintf(b, sizeof b, "VDDA %s V", v);
        UI_Row(5U, UI_DIM, UI_BG, b);

        UI_Row(7U, UI_TITLE, UI_BG, (HAL_GetTick() < msgUntil) ? msg : "");
    }
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
        UI_Row(5U, UI_DIM, UI_BG, b);
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
                UI_Row(6U, UI_FG, UI_BG, b);
                if (s.freq_ok)       snprintf(b, sizeof b, "Freq %lu Hz", (unsigned long)s.freq_hz);
                else if (s.too_fast) snprintf(b, sizeof b, "Freq too high");
                else                 snprintf(b, sizeof b, "Freq ---");
                UI_Row(7U, UI_FG, UI_BG, b);
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
            snprintf(b, sizeof b, "PB1 %s V", t);                            UI_Row(5U, UI_DIM, UI_BG, b);
            UI_FmtFixed(t, sizeof t, Probe_ReadVolts(RD_PA6), 2);
            snprintf(b, sizeof b, "PA6 %s V", t);                            UI_Row(6U, UI_DIM, UI_BG, b);
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
                UI_Row(i, UI_FG, (i == sel) ? UI_SEL_BG : UI_BG, b);
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
    UI_Row(0U, UI_FG, UI_BG, "yellow blue green red");
    for (uint8_t i = 0U; i < 4U; i++)
    {
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
    LCD_FillRect(0U, 24U, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - 24U), 0xFFFFU);
    const PageTitle_t *t = &g_pageTitles[idx];
    LCD_DrawBitmap1bpp((uint16_t)((LCD_WIDTH - t->w) / 2U), 44U, t->w, t->h, t->bits, 0x7945U, 0xFFFFU);
    LCD_DrawBitmap1bpp((LCD_WIDTH - PAGE_ICON_W) / 2U, 84U, PAGE_ICON_W, PAGE_ICON_H, g_pageIcons[idx].bits, 0xA828U, 0xFFFFU);
    const PageTitle_t *c = &g_pageTitles[TITLE_COMING_SOON];
    LCD_DrawBitmap1bpp((uint16_t)((LCD_WIDTH - c->w) / 2U), 192U, c->w, c->h, c->bits, 0xA828U, 0xFFFFU);
    LCD_DrawString(78U, 218U, "Coming soon", 0x7BEFU, 0xFFFFU, 1U);
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

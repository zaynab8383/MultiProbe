#include "app.h"
#include "ui.h"
#include "lcd.h"
#include "adc_probe.h"
#include "probe_hw.h"
#include "board_map.h"
#include "statusbar.h"
#include "video_player.h"
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
    UI_Hint("BACK = exit");

    uint32_t last = 0U - 1000U;
    for (;;)
    {
        if (UI_Poll() == MENU_INPUT_BACK) break;
        if (!TICK_DUE(last, 250U)) continue;
        last = HAL_GetTick();

        char v[16], b[32];
        UI_FmtFixed(v, sizeof v, Adc_GetVin(), 2);
        snprintf(b, sizeof b, "VIN       %s V", v);   UI_Row(0U, UI_FG, UI_BG, b);
        UI_FmtFixed(v, sizeof v, Adc_GetPB1(), 2);
        snprintf(b, sizeof b, "P1 (PB1)  %s V", v);   UI_Row(1U, UI_FG, UI_BG, b);
        UI_FmtFixed(v, sizeof v, Adc_GetPA6(), 2);
        snprintf(b, sizeof b, "PA6       %s V", v);   UI_Row(2U, UI_FG, UI_BG, b);
        UI_FmtFixed(v, sizeof v, Adc_GetPA2(), 2);
        snprintf(b, sizeof b, "PA2       %s V", v);   UI_Row(3U, UI_FG, UI_BG, b);
        UI_FmtFixed(v, sizeof v, Adc_GetPC1(), 2);
        snprintf(b, sizeof b, "PC1       %s V", v);   UI_Row(4U, UI_FG, UI_BG, b);

        UI_FmtFixed(v, sizeof v, g_vdda, 2);
        snprintf(b, sizeof b, "VDDA      %s V", v);   UI_Row(6U, UI_DIM, UI_BG, b);
    }
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
    Probe_PwmStop();
}

/* ================================================================== */
/*  DAC out                                                            */
/* ================================================================== */
static void App_Dac(void)
{
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
/*  Root menu                                                          */
/* ================================================================== */
static const MenuItem_t s_rootItems[] = {
    { "Voltmeter",  App_Voltmeter },
    { "Ohmmeter",   App_Ohmmeter  },
    { "PWM Gen/Read", App_Pwm     },
    { "DAC Out",    App_Dac       },
    { "Buzzer",     App_Buzzer    },
    { "Video",      App_Video     },
    { "GPIO Test",  App_GpioTest  },
};

static const MenuScreen_t s_rootScreen = {
    .title = "MAIN MENU",
    .items = s_rootItems,
    .count = (uint8_t)(sizeof(s_rootItems) / sizeof(s_rootItems[0])),
};

const MenuScreen_t *App_RootScreen(void) { return &s_rootScreen; }

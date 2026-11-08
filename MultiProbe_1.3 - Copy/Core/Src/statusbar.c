#include "statusbar.h"
#include "adc_probe.h"
#include "lcd.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

/* منبع ولتاژی که نشون داده می‌شه:
 *   0 = ولتاژ ورودی/تغذیه‌ی اندازه‌گیری‌شده روی پروب (Adc_GetVin، همونی که LED ها نشون می‌دن)
 *   1 = VDDA یعنی ریل ۳.۳ ولت خود برد (از VREFINT) */
#define STATUSBAR_SHOW_VDDA   0

#define STATUS_REFRESH_MS     250U
#define STATUS_TEXT_SCALE     2U
#define STATUS_TEXT_Y         2U
#define STATUS_VOLT_CHARS     5U                                   /* "12.3V" / "3.30V" */
#define STATUS_VOLT_W         (STATUS_VOLT_CHARS * 7U * STATUS_TEXT_SCALE)
#define STATUS_VOLT_X         (LCD_WIDTH - STATUS_VOLT_W - 6U)

#define STATUS_TEXT           LCD_COLOR_WHITE
#define STATUS_VOLT_COLOR     LCD_COLOR_YELLOW

static uint16_t s_bg       = LCD_COLOR_BLACK;
static uint8_t  s_needDraw = 1U;
static uint32_t s_nextMs   = 0U;
static char     s_last[STATUS_VOLT_CHARS + 2U] = "";

void StatusBar_Init(void)
{
    LCD_FillRect(0, 0, LCD_WIDTH, STATUSBAR_H_PX, s_bg);
    s_needDraw = 1U;
}

void StatusBar_SetColor(uint16_t bg)
{
    if (bg != s_bg)
    {
        s_bg = bg;
        s_needDraw = 1U;
    }
}

static float read_supply(void)
{
#if STATUSBAR_SHOW_VDDA
    Adc_SelfTest();
    return g_vdda;
#else
    return Adc_GetVin();
#endif
}

static void fmt_volt(char *dst, size_t n, float v)
{
    char t[12];
    if (v < 0.0f)   v = 0.0f;
    if (v > 99.9f)  v = 99.9f;
    UI_FmtFixed(t, sizeof t, v, (v < 9.995f) ? 2U : 1U);          /* همیشه ۴ کاراکتر + V */
    snprintf(dst, n, "%sV", t);
}

void StatusBar_Task(void)
{
    uint32_t now = HAL_GetTick();

    if (s_needDraw)
    {
        s_needDraw = 0U;
        LCD_FillRect(0, 0, LCD_WIDTH, STATUSBAR_H_PX, s_bg);
        LCD_DrawString(8, STATUS_TEXT_Y, "MULTIPROBE", STATUS_TEXT, s_bg, STATUS_TEXT_SCALE);
        s_last[0] = '\0';                                         /* ولتاژ دوباره کشیده بشه */
        s_nextMs  = now;
    }

    if ((int32_t)(now - s_nextMs) < 0) return;
    s_nextMs = now + STATUS_REFRESH_MS;

    char txt[STATUS_VOLT_CHARS + 2U];
    fmt_volt(txt, sizeof txt, read_supply());

    if (strcmp(txt, s_last) != 0)                                 /* فقط وقتی متن عوض شده */
    {
        LCD_DrawString(STATUS_VOLT_X, STATUS_TEXT_Y, txt, STATUS_VOLT_COLOR, s_bg, STATUS_TEXT_SCALE);
        strcpy(s_last, txt);
    }
}

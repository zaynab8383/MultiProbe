#include "ui.h"
#include "lcd.h"
#include "keypad.h"
#include "statusbar.h"
#include <stdio.h>
#include <string.h>

#define FONT_CELL_W   7U
#define FONT_CELL_H   10U
#define ROW_Y0        50U
#define ROW_PITCH     20U

void UI_Begin(const char *title)
{
    LCD_FillRect(0U, STATUSBAR_H_PX, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - STATUSBAR_H_PX), UI_BG);
    UI_Text(4U, (uint16_t)(STATUSBAR_H_PX + 3U), 2U, UI_TITLE, UI_BG, title);
}

MenuInput_t UI_Poll(void)
{
    StatusBar_Task();
    return Keypad_Poll();
}

void UI_Text(uint16_t x, uint16_t y, uint8_t scale, uint16_t fg, uint16_t bg, const char *s)
{
    char buf[40];
    uint16_t cols = (uint16_t)((LCD_WIDTH - x) / (FONT_CELL_W * scale));
    if (cols >= sizeof(buf)) cols = (uint16_t)(sizeof(buf) - 1U);

    uint16_t i = 0U;
    while (i < cols && s[i] != '\0') { buf[i] = s[i]; i++; }
    while (i < cols) buf[i++] = ' ';                 /* بقیه‌ی خط رو با پس‌زمینه پر کن */
    buf[i] = '\0';
    LCD_DrawString(x, y, buf, fg, bg, scale);
}

void UI_Row(uint8_t row, uint16_t fg, uint16_t bg, const char *s)
{
    uint16_t y = (uint16_t)(ROW_Y0 + (uint16_t)row * ROW_PITCH);
    LCD_FillRect(0U, y, LCD_WIDTH, ROW_PITCH, bg);
    UI_Text(4U, (uint16_t)(y + 1U), 2U, fg, bg, s);
}

void UI_Hint(const char *s)
{
    UI_Text(2U, (uint16_t)(LCD_HEIGHT - FONT_CELL_H - 1U), 1U, UI_DIM, UI_BG, s);
}

void UI_FmtFixed(char *dst, size_t n, float v, uint8_t dec)
{
    static const uint32_t p10[] = { 1U, 10U, 100U, 1000U, 10000U };
    if (dec > 4U) dec = 4U;
    uint8_t neg = 0U;
    if (v < 0.0f) { neg = 1U; v = -v; }
    if (v > 4.0e6f) v = 4.0e6f;
    uint32_t s = p10[dec];
    uint32_t x = (uint32_t)(v * (float)s + 0.5f);
    uint32_t ip = x / s, fp = x % s;
    const char *sign = (neg && x != 0U) ? "-" : "";
    if (dec == 0U)
        snprintf(dst, n, "%s%lu", sign, (unsigned long)ip);
    else
        snprintf(dst, n, "%s%lu.%0*lu", sign, (unsigned long)ip, (int)dec, (unsigned long)fp);
}

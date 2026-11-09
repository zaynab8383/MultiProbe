#include "ui.h"
#include "lcd.h"
#include "keypad.h"
#include "statusbar.h"
#include "pages_assets.h"
#include <stdio.h>
#include <string.h>

#define FONT_CELL_W   7U
#define FONT_CELL_H   10U
#define ROW_Y0        50U
#define ROW_PITCH     20U
#define TITLE_BAND_H  23U

/* ---- themes: one per home page (same order as the carousel) -------- */
#define C565(r,g,b)  ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define MIXW(v,p)    ((v) + ((255 - (v)) * (p)) / 100)      /* toward white */
#define MIXK(v,p)    (((v) * (100 - (p))) / 100)            /* toward black */
#define THEME(r,g,b) { C565(r,g,b),                                              \
                       C565(MIXW(r,88), MIXW(g,88), MIXW(b,88)),                 \
                       C565(MIXK(r,55), MIXK(g,55), MIXK(b,55)),                 \
                       C565(MIXW(r,60), MIXW(g,60), MIXW(b,60)) }

static const UiTheme_t k_themes[UI_THEME_COUNT] = {
    THEME( 30, 110, 230),   /* 0 Dashboard       blue    */
    THEME(240, 130,  20),   /* 1 Tests           orange  */
    THEME(150,  70, 210),   /* 2 Logic Analyzer  purple  */
    THEME( 30, 170,  80),   /* 3 Voltmeter       green   */
    THEME(  0, 160, 170),   /* 4 Sensor Sim      teal    */
    THEME(225,  50,  60),   /* 5 Oscilloscope    red     */
    THEME(220,  60, 160),   /* 6 Ohmmeter        pink    */
    THEME(210, 150,   0),   /* 7 Buzzer          amber   */
    THEME(120, 170,  20),   /* 8 Wiring Test     lime    */
    THEME( 70,  80, 200),   /* 9 ECU Flash       indigo  */
};

static uint8_t s_theme = 0U;

const UiTheme_t *UI_ThemeOf(uint8_t idx)
{
    return &k_themes[(idx < UI_THEME_COUNT) ? idx : 0U];
}

const UiTheme_t *UI_Theme(void)
{
    return &k_themes[s_theme];
}

void UI_DrawPageIcon(uint16_t x, uint16_t y, uint8_t idx)
{
    uint16_t pal[16];
    const UiTheme_t *t = UI_ThemeOf(idx);
    for (uint8_t i = 0U; i < 16U; i++) pal[i] = g_iconPalette[i];
    pal[0] = t->bg; pal[1] = t->accent; pal[2] = t->dark;
    LCD_DrawRLE4(x, y, PAGE_ICON_W, PAGE_ICON_H, g_pageIcons[idx].rle, g_pageIcons[idx].len, pal);
}

void UI_SetTheme(uint8_t idx)
{
    s_theme = (idx < UI_THEME_COUNT) ? idx : 0U;
    StatusBar_SetColor(k_themes[s_theme].dark);   /* redraws only if the color changed */
}

void UI_Begin(const char *title)
{
    const UiTheme_t *t = UI_Theme();
    LCD_FillRect(0U, STATUSBAR_H_PX, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - STATUSBAR_H_PX), UI_BG);
    LCD_FillRect(0U, STATUSBAR_H_PX, LCD_WIDTH, TITLE_BAND_H, t->accent);
    UI_Text(6U, (uint16_t)(STATUSBAR_H_PX + 2U), 2U, UI_WHITE, t->accent, title);
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

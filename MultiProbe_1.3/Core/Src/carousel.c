#include "carousel.h"
#include "pages_assets.h"
#include "lcd.h"
#include "statusbar.h"

#define COL_BG       0xFFFFU
#define COL_LINE     0xA828U     /* red-brown, like the reference photos */
#define COL_TITLE    0x7945U
#define COL_DOT_OFF  0xC618U
#define COL_SUB      0x7BEFU

#define Y_TOP        STATUSBAR_H_PX

static const char *const k_sub[CAROUSEL_PAGES] = {
    "Dashboard", "Tests", "Logic Analyzer", "Voltmeter", "Sensor Simulator",
    "Oscilloscope", "Ohmmeter", "Buzzer", "ECU Flash"
};

static uint8_t s_cur   = 0U;
static uint8_t s_dirty = 1U;

void Carousel_Init(void)
{
    s_cur   = 0U;
    s_dirty = 1U;
}

void Carousel_Redraw(void) { s_dirty = 1U; }

static void tri_left(uint16_t x, uint16_t yc)       /* tip at x, base at x+12 */
{
    for (int r = -8; r <= 8; r++)
    {
        int a = (r < 0) ? -r : r;
        uint16_t w = (uint16_t)(12 - (a * 12) / 8 + 1);
        LCD_FillRect((uint16_t)(x + 13U - w), (uint16_t)(yc + r), w, 1U, COL_LINE);
    }
}

static void tri_right(uint16_t x, uint16_t yc)      /* base at x, tip at x+12 */
{
    for (int r = -8; r <= 8; r++)
    {
        int a = (r < 0) ? -r : r;
        uint16_t w = (uint16_t)(12 - (a * 12) / 8 + 1);
        LCD_FillRect(x, (uint16_t)(yc + r), w, 1U, COL_LINE);
    }
}

static void draw_text_center(uint16_t y, const char *s, uint16_t fg)
{
    uint16_t n = 0U;
    while (s[n] != '\0') n++;
    uint16_t w = (uint16_t)(n * 7U);
    uint16_t x = (uint16_t)((LCD_WIDTH - w) / 2U);
    LCD_DrawString(x, y, s, fg, COL_BG, 1U);
}

static void draw_frame(void)
{
    /* solid outer line (2 px) */
    LCD_FillRect(3U, 28U, 234U, 2U, COL_LINE);
    LCD_FillRect(3U, 234U, 234U, 2U, COL_LINE);
    LCD_FillRect(3U, 28U, 2U, 208U, COL_LINE);
    LCD_FillRect(235U, 28U, 2U, 208U, COL_LINE);
    /* dotted inner line */
    for (uint16_t x = 9U; x < 232U; x = (uint16_t)(x + 4U))
    {
        LCD_FillRect(x, 33U, 2U, 2U, COL_LINE);
        LCD_FillRect(x, 229U, 2U, 2U, COL_LINE);
    }
    for (uint16_t y = 37U; y < 229U; y = (uint16_t)(y + 4U))
    {
        LCD_FillRect(8U, y, 2U, 2U, COL_LINE);
        LCD_FillRect(230U, y, 2U, 2U, COL_LINE);
    }
}

void Carousel_DrawPage(uint8_t idx);
void Carousel_DrawPage(uint8_t idx)
{
    LCD_FillRect(0U, Y_TOP, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - Y_TOP), COL_BG);
    draw_frame();

    const PageTitle_t *t = &g_pageTitles[idx];
    LCD_DrawBitmap1bpp((uint16_t)((LCD_WIDTH - t->w) / 2U),
                       (uint16_t)(40U + (32U - t->h) / 2U),
                       t->w, t->h, t->bits, COL_TITLE, COL_BG);

    LCD_DrawBitmap1bpp((LCD_WIDTH - PAGE_ICON_W) / 2U, 84U, PAGE_ICON_W, PAGE_ICON_H,
                       g_pageIcons[idx].bits, COL_LINE, COL_BG);

    draw_text_center(190U, k_sub[idx], COL_SUB);
}

void Carousel_Draw(void)
{
    if (!s_dirty) return;
    s_dirty = 0U;

    Carousel_DrawPage(s_cur);

    tri_left(12U, 132U);
    tri_right(215U, 132U);

    /* page dots */
    uint16_t x0 = (uint16_t)((LCD_WIDTH - (CAROUSEL_PAGES * 14U - 8U)) / 2U);
    for (uint8_t i = 0U; i < CAROUSEL_PAGES; i++)
    {
        uint16_t x = (uint16_t)(x0 + i * 14U);
        if (i == s_cur) LCD_FillRect(x, 211U, 8U, 8U, COL_LINE);
        else            LCD_FillRect((uint16_t)(x + 1U), 212U, 6U, 6U, COL_DOT_OFF);
    }
}

void Carousel_HandleInput(MenuInput_t in)
{
    switch (in)
    {
    case MENU_INPUT_RIGHT:
        s_cur = (uint8_t)((s_cur + 1U) % CAROUSEL_PAGES);
        s_dirty = 1U;
        break;
    case MENU_INPUT_LEFT:
        s_cur = (s_cur == 0U) ? (uint8_t)(CAROUSEL_PAGES - 1U) : (uint8_t)(s_cur - 1U);
        s_dirty = 1U;
        break;
    case MENU_INPUT_SELECT:
        App_RunPage(s_cur);
        s_dirty = 1U;
        break;
    default:
        break;          /* UP / DOWN / BACK: nothing on the home screen */
    }
}

#include "menu.h"
#include "lcd.h"
#include "statusbar.h"
#include "ui.h"
#include <string.h>

#define MENU_MAX_DEPTH   6U
#define ROW_HEIGHT       24U
#define TEXT_SCALE       2U

#define COLOR_BG         LCD_COLOR_BLACK
#define COLOR_TEXT       LCD_COLOR_WHITE

/* هر آیتم یه رنگ خودش رو داره */
static const uint16_t k_itemColors[] = {
    0x07FFU,   /* cyan    */
    0xFFE0U,   /* yellow  */
    0x07E0U,   /* green   */
    0xFD20U,   /* orange  */
    0xF81FU,   /* magenta */
    0xFFFFU,   /* white   */
};
#define ITEM_COLORS ((uint8_t)(sizeof(k_itemColors) / sizeof(k_itemColors[0])))

static const MenuScreen_t *s_stack[MENU_MAX_DEPTH];
static uint8_t s_depth = 0U;
static uint8_t s_cursor = 0U;
static uint8_t s_dirty = 1U;

static const MenuScreen_t *Current(void)
{
    return (s_depth > 0U) ? s_stack[s_depth - 1U] : NULL;
}

void Menu_Init(const MenuScreen_t *rootScreen)
{
    s_depth = 0U;
    s_cursor = 0U;
    s_stack[s_depth++] = rootScreen;
    s_dirty = 1U;
}

void Menu_Push(const MenuScreen_t *screen)
{
    if (s_depth < MENU_MAX_DEPTH)
    {
        s_stack[s_depth++] = screen;
        s_cursor = 0U;
        s_dirty = 1U;
    }
}

void Menu_Pop(void)
{
    if (s_depth > 1U)
    {
        s_depth--;
        s_cursor = 0U;
        s_dirty = 1U;
    }
}

void Menu_HandleInput(MenuInput_t input)
{
    const MenuScreen_t *scr = Current();
    if (scr == NULL || scr->count == 0U) return;

    switch (input)
    {
        case MENU_INPUT_UP:
            s_cursor = (s_cursor == 0U) ? (uint8_t)(scr->count - 1U) : (uint8_t)(s_cursor - 1U);
            s_dirty = 1U;
            break;

        case MENU_INPUT_DOWN:
            s_cursor = (uint8_t)((s_cursor + 1U) % scr->count);
            s_dirty = 1U;
            break;

        case MENU_INPUT_SELECT:
            if (scr->items[s_cursor].onSelect != NULL)
                scr->items[s_cursor].onSelect();
            s_dirty = 1U;
            break;

        case MENU_INPUT_BACK:
            Menu_Pop();
            break;

        default:
            break;
    }
}

void Menu_Draw(void)
{
    if (!s_dirty) return;
    s_dirty = 0U;

    const MenuScreen_t *scr = Current();
    if (scr == NULL) return;

    const UiTheme_t *th = UI_Theme();

    /* پاک کردن ناحیه زیر نوار وضعیت (نه کل صفحه) */
    LCD_FillRect(0U, MENU_Y0, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - MENU_Y0), COLOR_BG);

    /* عنوان روی نوار رنگی */
    LCD_FillRect(0U, MENU_Y0, LCD_WIDTH, ROW_HEIGHT - 1U, th->accent);
    LCD_DrawString(8U, (uint16_t)(MENU_Y0 + 2U), scr->title, COLOR_TEXT, th->accent, TEXT_SCALE);

    uint16_t y = (uint16_t)(MENU_Y0 + ROW_HEIGHT);
    for (uint8_t i = 0U; i < scr->count; i++)
    {
        uint8_t  sel = (i == s_cursor);
        uint16_t bg  = sel ? th->dark : COLOR_BG;
        uint16_t fg  = sel ? COLOR_TEXT : k_itemColors[i % ITEM_COLORS];

        LCD_FillRect(0U, y, LCD_WIDTH, ROW_HEIGHT, bg);
        if (sel) LCD_FillRect(0U, y, 5U, ROW_HEIGHT, th->accent);          /* نشانه‌ی کنار آیتم انتخابی */
        LCD_DrawString(16U, (uint16_t)(y + 4U), scr->items[i].name, fg, bg, TEXT_SCALE);
        y = (uint16_t)(y + ROW_HEIGHT);
        if (y >= LCD_HEIGHT) break;   /* صفحه پر شد */
    }
}

void Menu_Redraw(void)
{
    s_dirty = 1U;
}

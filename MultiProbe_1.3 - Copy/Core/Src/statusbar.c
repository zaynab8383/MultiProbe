#include "statusbar.h"
#include "adc_probe.h"
#include "lcd.h"
#include "ui.h"
#include <stdio.h>

/* رنگ ثابت نوار وضعیت */
#define STATUS_BG   LCD_COLOR_BLACK
#define STATUS_TEXT LCD_COLOR_WHITE

static uint8_t s_needDraw = 1U;

void StatusBar_Init(void)
{
    LCD_FillRect(
        0,
        0,
        LCD_WIDTH,
        STATUSBAR_H_PX,
        STATUS_BG
    );
    s_needDraw = 1U;
}

void StatusBar_Task(void)
{
    /* فقط یک بار (یا بعد از StatusBar_Init) کشیده می‌شه؛ دیگه هر ۵۰۰ms چشمک نمی‌زنه */
    if (!s_needDraw) return;
    s_needDraw = 0U;

    LCD_FillRect(0, 0, LCD_WIDTH, STATUSBAR_H_PX, STATUS_BG);
    LCD_DrawString(8, 2, "MULTIPROBE", STATUS_TEXT, STATUS_BG, 2);
}

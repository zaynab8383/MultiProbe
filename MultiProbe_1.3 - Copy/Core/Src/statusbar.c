#include "statusbar.h"
#include "adc_probe.h"
#include "lcd.h"
#include "ui.h"
#include <stdio.h>

/* رنگ ثابت نوار وضعیت */
#define STATUS_BG   LCD_COLOR_BLACK
#define STATUS_TEXT LCD_COLOR_WHITE

void StatusBar_Init(void)
{
    LCD_FillRect(
        0,
        0,
        LCD_WIDTH,
        STATUSBAR_H_PX,
        STATUS_BG
    );
}

void StatusBar_Task(void)
{
    static uint32_t last = 0U;

    if ((HAL_GetTick() - last) < 500U)
        return;

    last = HAL_GetTick();

    /*
     * نوار بالایی دیگر بر اساس ولتاژ تغییر رنگ نمی‌دهد.
     * فقط اطلاعات ثابت یا عمومی نمایش می‌دهد.
     */
    LCD_FillRect(
        0,
        0,
        LCD_WIDTH,
        STATUSBAR_H_PX,
        STATUS_BG
    );

    LCD_DrawString(
        8,
        2,
        "MULTIPROBE",
        STATUS_TEXT,
        STATUS_BG,
        2
    );
}

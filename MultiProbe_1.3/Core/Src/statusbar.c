//#include "statusbar.h"
//#include "adc_probe.h"
//#include "lcd.h"
//#include <stdio.h>
//#include <math.h>
//
//#define COLOR_BG_OK    0x0320U   /* سبز تیره */
//#define COLOR_BG_WARN  0xFD20U   /* نارنجی */
//#define COLOR_BG_BAD   0xF800U   /* قرمز */
//#define COLOR_TEXT     0xFFFFU   /* سفید */
//
//static float s_shown = -100.0f;
//
///* ⚠ موقتاً PC1، با مهندس چک کن کدوم پایه ولتاژ تغذیه است */
//static float Supply_Volts(void) { return Adc_GetPA2(); }
//void StatusBar_Init(void)
//{
//    s_shown = -100.0f;       /* اجبار به کشیدن اولین بار */
//}
//
//void StatusBar_Task(void)
//{
//    static uint32_t last = 0;
//    if ((HAL_GetTick() - last) < 500U) return;
//    last = HAL_GetTick();
//
//    float v = Supply_Volts();
//    if (fabsf(v - s_shown) < 0.1f) return;   /* تغییری نکرده */
//    s_shown = v;
//
//    uint16_t bg = (v < 10.5f || v > 15.0f) ? COLOR_BG_BAD
//                : (v < 11.5f)              ? COLOR_BG_WARN
//                :                            COLOR_BG_OK;
//
//    char buf[16];
//    snprintf(buf, sizeof(buf), "%4.1fV", v);
//
//    LCD_FillRect(0, 0, LCD_WIDTH, STATUSBAR_H_PX, bg);
//    LCD_DrawString(8, 8, buf, COLOR_TEXT, bg, 2);   /* scale=2 برای خوانایی بهتر */
//}
#include "statusbar.h"
#include "adc_probe.h"
#include "lcd.h"
#include "ui.h"
#include <stdio.h>
#include <math.h>

#define COLOR_BG_OK    0x0320U   /* سبز تیره */
#define COLOR_BG_WARN  0xFD20U   /* نارنجی */
#define COLOR_BG_BAD   0xF800U   /* قرمز */
#define COLOR_TEXT     0xFFFFU   /* سفید */

static float s_shown = -100.0f;

/* ولتاژ ورودی دستگاه: PB0 -> تقسیم‌کننده (R8 1M + R6 100K) -> OPAMP1 -> PC4 (طبق نت‌لیست) */
static float Supply_Volts(void) { return Adc_GetVin(); }
void StatusBar_Init(void)
{
    s_shown = -100.0f;       /* اجبار به کشیدن اولین بار */
}

void StatusBar_Task(void)
{
    static uint32_t last = 0;
    if ((HAL_GetTick() - last) < 500U) return;
    last = HAL_GetTick();

    float v = Supply_Volts();
    if (fabsf(v - s_shown) < 0.1f) return;   /* تغییری نکرده */
    s_shown = v;

    uint16_t bg = (v < 10.5f || v > 15.0f) ? COLOR_BG_BAD
                : (v < 11.5f)              ? COLOR_BG_WARN
                :                            COLOR_BG_OK;

    char buf[32];
    char num[12];
    UI_FmtFixed(num, sizeof(num), v, 1);
    snprintf(buf, sizeof(buf), "VIN %sV", num);

    LCD_FillRect(0, 0, LCD_WIDTH, STATUSBAR_H_PX, bg);
    LCD_DrawString(8, 2, buf, COLOR_TEXT, bg, 2);   /* scale=2 برای خوانایی بهتر */
}

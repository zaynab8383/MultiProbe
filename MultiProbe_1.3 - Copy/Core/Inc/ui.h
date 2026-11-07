#ifndef UI_H
#define UI_H
#include "main.h"
#include "menu.h"
#include <stddef.h>

#define UI_BG        0x0000U
#define UI_FG        0xFFFFU
#define UI_TITLE     0xFFE0U
#define UI_DIM       0x7BEFU
#define UI_SEL_BG    0x001FU
#define UI_OK        0x07E0U
#define UI_BAD       0xF800U

void        UI_Begin(const char *title);                 /* زیر نوار وضعیت رو پاک می‌کنه و عنوان می‌ذاره */
MenuInput_t UI_Poll(void);                               /* کی‌پد + نوار وضعیت */
void        UI_Text(uint16_t x, uint16_t y, uint8_t scale, uint16_t fg, uint16_t bg, const char *s);
void        UI_Row(uint8_t row, uint16_t fg, uint16_t bg, const char *s);   /* ردیف‌های scale=2 با فاصله‌ی ۲۰ پیکسل */
void        UI_Hint(const char *s);                      /* خط راهنمای پایین صفحه */
void        UI_FmtFixed(char *dst, size_t n, float v, uint8_t decimals);

#endif

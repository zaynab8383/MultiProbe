#ifndef UI_H
#define UI_H
#include "main.h"
#include "menu.h"
#include <stddef.h>

/* ---- Per-page color theme ----------------------------------------- */
typedef struct {
    uint16_t accent;   /* strong color: title band, frame, icon */
    uint16_t bg;       /* very light tint: home-carousel background */
    uint16_t dark;     /* dark shade: status bar, selected row */
    uint16_t light;    /* light tint: normal text on black */
} UiTheme_t;

#define UI_THEME_COUNT  9U

void             UI_SetTheme(uint8_t idx);        /* also recolors the status bar */
const UiTheme_t *UI_Theme(void);                  /* current theme */
const UiTheme_t *UI_ThemeOf(uint8_t idx);

#define UI_BG        0x0000U
#define UI_FG        (UI_Theme()->light)          /* normal text = tint of the page color */
#define UI_SEL_BG    (UI_Theme()->dark)           /* selected row */
#define UI_TITLE     0xFFE0U
#define UI_DIM       0x7BEFU
#define UI_OK        0x07E0U
#define UI_BAD       0xF800U
#define UI_CYAN      0x07FFU
#define UI_ORANGE    0xFD20U
#define UI_MAGENTA   0xF81FU
#define UI_WHITE     0xFFFFU

void        UI_Begin(const char *title);                 /* زیر نوار وضعیت رو پاک می‌کنه و عنوان رنگی می‌ذاره */
MenuInput_t UI_Poll(void);                               /* کی‌پد + نوار وضعیت */
void        UI_Text(uint16_t x, uint16_t y, uint8_t scale, uint16_t fg, uint16_t bg, const char *s);
void        UI_Row(uint8_t row, uint16_t fg, uint16_t bg, const char *s);   /* ردیف‌های scale=2 با فاصله‌ی ۲۰ پیکسل */
void        UI_Hint(const char *s);                      /* خط راهنمای پایین صفحه */
void        UI_FmtFixed(char *dst, size_t n, float v, uint8_t decimals);

#endif

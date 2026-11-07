#ifndef MENU_H
#define MENU_H
#include "main.h"

typedef enum {
    MENU_INPUT_NONE = 0,
    MENU_INPUT_UP,
    MENU_INPUT_DOWN,
    MENU_INPUT_LEFT,
    MENU_INPUT_RIGHT,
    MENU_INPUT_SELECT,
    MENU_INPUT_BACK,
} MenuInput_t;

typedef void (*MenuAction_t)(void);

typedef struct {
    const char   *name;      /* عنوان نمایشی */
    MenuAction_t  onSelect;  /* وقتی انتخاب شد صدا زده می‌شه، می‌تونه زیرمنو رو باز کنه */
} MenuItem_t;

typedef struct {
    const char       *title;
    const MenuItem_t *items;
    uint8_t            count;
} MenuScreen_t;

void Menu_Init(const MenuScreen_t *rootScreen);
void Menu_HandleInput(MenuInput_t input);
void Menu_Draw(void);          /* فقط وقتی چیزی تغییر کرده دوباره می‌کشه */
void Menu_Push(const MenuScreen_t *screen);  /* رفتن به زیرمنو */
void Menu_Pop(void);                          /* برگشت به منوی قبلی */
void Menu_Redraw(void);                       /* اجبار به کشیدن دوباره منو (بعد از برگشت از یک برنامه) */

#endif

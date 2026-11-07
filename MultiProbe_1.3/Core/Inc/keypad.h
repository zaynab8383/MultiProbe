#ifndef KEYPAD_H
#define KEYPAD_H
#include "main.h"
#include "menu.h"

void Keypad_Init(void);
MenuInput_t Keypad_Poll(void);   /* اگه دکمه‌ای فشرده نشده، MENU_INPUT_NONE برمی‌گردونه */

uint8_t Keypad_RawPressed(MenuInput_t key);

#endif

#ifndef STATUSBAR_H
#define STATUSBAR_H
#include "main.h"

#define STATUSBAR_H_PX   24U
#define MENU_Y0          STATUSBAR_H_PX   /* منوها از اینجا به پایین می‌کشن */

void StatusBar_Init(void);
void StatusBar_Task(void);                 /* باید توی همه‌ی حلقه‌ها صدا زده بشه (UI_Poll این کار رو می‌کنه) */
void StatusBar_SetColor(uint16_t bg);      /* رنگ پس‌زمینه‌ی نوار؛ فقط اگه عوض شده باشه دوباره می‌کشه */

#endif

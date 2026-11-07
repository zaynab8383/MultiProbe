#ifndef STATUSBAR_H
#define STATUSBAR_H
#include "main.h"

#define STATUSBAR_H_PX   24U
#define MENU_Y0          STATUSBAR_H_PX   /* منوها از اینجا به پایین می‌کشن */

void StatusBar_Init(void);
void StatusBar_Task(void);

#endif

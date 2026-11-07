#ifndef CAROUSEL_H
#define CAROUSEL_H
#include "menu.h"

/* Home screen: one full-screen page per feature (icon + Persian title).
 * LEFT / RIGHT = previous / next page, ENTER = open the page. */
#define CAROUSEL_PAGES  9U

void Carousel_Init(void);
void Carousel_HandleInput(MenuInput_t in);
void Carousel_Draw(void);        /* draws only when something changed */
void Carousel_Redraw(void);      /* force a full redraw (after returning from a page) */

/* Implemented in app.c: runs page number idx (blocks until the user presses BACK). */
void App_RunPage(uint8_t idx);

#endif

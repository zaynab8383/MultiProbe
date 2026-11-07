#ifndef LEDS_H
#define LEDS_H

#include "main.h"
#include <stdint.h>

void LEDs_Init(void);
void LEDs_AllOff(void);

void LEDs_UpdateVoltage(float voltage);

/* Blue LED: signal detected */
void LED_Blue_Set(uint8_t on);

void LEDs_Set(uint8_t idx, uint8_t on);   /* 0 yellow, 1 blue, 2 green, 3 red */

#endif

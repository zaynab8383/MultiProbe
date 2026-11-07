#include "leds.h"

#define LED_YELLOW_PORT GPIOC
#define LED_YELLOW_PIN  GPIO_PIN_6

#define LED_RED_PORT    GPIOD
#define LED_RED_PIN     GPIO_PIN_8

#define LED_BLUE_PORT   GPIOD
#define LED_BLUE_PIN    GPIO_PIN_9

#define LED_GREEN_PORT  GPIOD
#define LED_GREEN_PIN   GPIO_PIN_10

void LEDs_Init(void)
{
    LEDs_AllOff();
}

void LEDs_AllOff(void)
{
    HAL_GPIO_WritePin(
        LED_YELLOW_PORT,
        LED_YELLOW_PIN,
        GPIO_PIN_RESET
    );

    HAL_GPIO_WritePin(
        GPIOD,
        LED_RED_PIN | LED_BLUE_PIN | LED_GREEN_PIN,
        GPIO_PIN_RESET
    );
}

void LED_Blue_Set(uint8_t on)
{
    HAL_GPIO_WritePin(
        LED_BLUE_PORT,
        LED_BLUE_PIN,
        on ? GPIO_PIN_SET : GPIO_PIN_RESET
    );
}

void LEDs_UpdateVoltage(float voltage)
{
    /* فقط LEDهای ولتاژ را خاموش کن؛ آبی مربوط به سیگنال است */
    HAL_GPIO_WritePin(
        LED_YELLOW_PORT,
        LED_YELLOW_PIN,
        GPIO_PIN_RESET
    );

    HAL_GPIO_WritePin(
        LED_RED_PORT,
        LED_RED_PIN | LED_GREEN_PIN,
        GPIO_PIN_RESET
    );

    /* حدود تشخیص اولیه */
    if (voltage >= 3.0f && voltage <= 3.6f)
    {
        HAL_GPIO_WritePin(
            LED_YELLOW_PORT,
            LED_YELLOW_PIN,
            GPIO_PIN_SET
        );
    }
    else if (voltage >= 4.5f && voltage <= 5.3f)
    {
        HAL_GPIO_WritePin(
            LED_GREEN_PORT,
            LED_GREEN_PIN,
            GPIO_PIN_SET
        );
    }
    else if (voltage >= 10.5f && voltage <= 13.5f)
    {
        HAL_GPIO_WritePin(
            LED_RED_PORT,
            LED_RED_PIN,
            GPIO_PIN_SET
        );
    }
}

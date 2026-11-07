#include "keypad.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    MenuInput_t   input;
} KeyDef_t;

static const KeyDef_t s_keys[] = {
    { GPIOD, GPIO_PIN_6, MENU_INPUT_SELECT },  /* PD6 → Enter */
    { GPIOD, GPIO_PIN_7, MENU_INPUT_BACK   },  /* PD7 → Back  */
    { GPIOB, GPIO_PIN_3, MENU_INPUT_UP     },  /* PB3 → Up    */
    { GPIOB, GPIO_PIN_4, MENU_INPUT_RIGHT  },  /* PB4 → Right */
    { GPIOB, GPIO_PIN_5, MENU_INPUT_LEFT   },  /* PB5 → Left  */
    { GPIOB, GPIO_PIN_8, MENU_INPUT_DOWN   },  /* PB8 → Down  */
};
#define KEY_COUNT  (sizeof(s_keys) / sizeof(s_keys[0]))

static uint8_t s_lastHigh[KEY_COUNT];

void Keypad_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_INPUT;
    gi.Pull  = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_LOW;

    for (uint8_t i = 0U; i < KEY_COUNT; i++)
    {
        gi.Pin = s_keys[i].pin;
        HAL_GPIO_Init(s_keys[i].port, &gi);
        s_lastHigh[i] = 1U;
    }
}

MenuInput_t Keypad_Poll(void)
{
    static uint32_t lastPoll = 0U;
    if ((HAL_GetTick() - lastPoll) < 20U) return MENU_INPUT_NONE;
    lastPoll = HAL_GetTick();

    for (uint8_t i = 0U; i < KEY_COUNT; i++)
    {
        GPIO_PinState st = HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin);

        if (s_lastHigh[i] && st == GPIO_PIN_RESET)
        {
            s_lastHigh[i] = 0U;
            return s_keys[i].input;
        }
        if (st == GPIO_PIN_SET)
        {
            s_lastHigh[i] = 1U;
        }
    }
    return MENU_INPUT_NONE;
}

/* 1 while the given key is held down (active low) -- used by the key test page */
uint8_t Keypad_RawPressed(MenuInput_t key)
{
    for (uint8_t i = 0U; i < KEY_COUNT; i++)
    {
        if (s_keys[i].input == key)
            return (HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin) == GPIO_PIN_RESET) ? 1U : 0U;
    }
    return 0U;
}

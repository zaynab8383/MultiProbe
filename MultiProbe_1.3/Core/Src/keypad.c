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

/* صف کوچک رویدادها: دکمه توی SysTick (هر ۱ms) اسکن می‌شه، پس وسط کارهای طولانی
 * (مثل رسم نمودار) هم فشردن دکمه گم نمی‌شه */
#define KQ_SIZE 8U
static volatile MenuInput_t s_q[KQ_SIZE];
static volatile uint8_t     s_qh = 0U, s_qt = 0U;
static volatile uint8_t     s_ready = 0U;

static void kq_push(MenuInput_t in)
{
    uint8_t n = (uint8_t)((s_qh + 1U) % KQ_SIZE);
    if (n == s_qt) return;
    s_q[s_qh] = in;
    s_qh = n;
}

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
    s_ready = 1U;
}

/* اگه دکمه‌ی Up (PB3) روی برد کار نکنه: Down رو >۶۰۰ms نگه دار = Up
 * (فشردن کوتاه Down = Down، موقع رها کردن شناخته می‌شه) */
#define LONG_DOWN_MS 600U

void Keypad_Tick(void)          /* از SysTick_Handler صدا زده می‌شه */
{
    static uint32_t lastPoll = 0U;
    static uint32_t downAt = 0U;
    if (!s_ready) return;
    if ((HAL_GetTick() - lastPoll) < 20U) return;
    lastPoll = HAL_GetTick();

    for (uint8_t i = 0U; i < KEY_COUNT; i++)
    {
        GPIO_PinState st = HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin);
        uint8_t isDown = (s_keys[i].input == MENU_INPUT_DOWN);

        if (s_lastHigh[i] && st == GPIO_PIN_RESET)
        {
            s_lastHigh[i] = 0U;
            if (isDown) downAt = lastPoll;
            else        kq_push(s_keys[i].input);
        }
        else if (!s_lastHigh[i] && st == GPIO_PIN_SET)
        {
            s_lastHigh[i] = 1U;
            if (isDown) kq_push(((lastPoll - downAt) >= LONG_DOWN_MS) ? MENU_INPUT_UP : MENU_INPUT_DOWN);
        }
    }
}

MenuInput_t Keypad_Poll(void)
{
    if (s_qt == s_qh) return MENU_INPUT_NONE;
    MenuInput_t in = s_q[s_qt];
    s_qt = (uint8_t)((s_qt + 1U) % KQ_SIZE);
    return in;
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

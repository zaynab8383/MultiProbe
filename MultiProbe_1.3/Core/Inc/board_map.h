#ifndef BOARD_MAP_H
#define BOARD_MAP_H
/*
 * نقشه‌ی پایه‌های برد MultiProbe — همه‌ی فرض‌ها اینجا جمع شده تا مهندس برد
 * فقط همین فایل رو چک/اصلاح کنه.   ⚠ = مطمئن نیستم، باید با شماتیک تأیید بشه.
 */
#include "main.h"

/* ------------------------------------------------------------------ */
/* ۱) خروجی PWM روی پروب‌ها (از روی نت‌لیست PDF)                         */
/*    TIM15_CH1  (PE5) -> 74HC125 A4 -> Y4 -> R31 -> Probe1   OE4 = PC0   */
/*    TIM15_CH1N (PE4) -> 74HC125 A3 -> Y3 -> R35 -> Probe2   OE3 = PA3   */
/*    OE فعال-پایین است و روی برد با R24..R27 به 5V کشیده شده (یعنی HIGH = خاموش) */
/*    TIM1_CH1 (PA8) / CH1N (PA7) -> A1/A2 ، OE1 = PE6 ، OE2 = PC13 (استفاده نشده) */
/*    ⚠ توی لیست شما PC3 بود؛ طبق نت‌لیست PC3 به فلش وصله و OE2، پایه‌ی PC13 است. */
/* ------------------------------------------------------------------ */
#define OE_P1_PORT   GPIOC
#define OE_P1_PIN    GPIO_PIN_0
#define OE_P2_PORT   GPIOA
#define OE_P2_PIN    GPIO_PIN_3
#define OE_X1_PORT   GPIOE
#define OE_X1_PIN    GPIO_PIN_6
#define OE_X2_PORT   GPIOC
#define OE_X2_PIN    GPIO_PIN_13

/* ------------------------------------------------------------------ */
/* ۲) سوئیچ‌های آنالوگ (PE11, PB10 = DAC ؛ PE15, PE14 = اهم‌متر)         */
/*    ⚠ سطح فعال معلوم نیست. حالت بیکار = همون سطحی که کد قبلی از بوت    */
/*    می‌ذاشت (HIGH). اگه برعکس بود فقط این دو خط رو عوض کن.            */
/*    صفحه‌ی "GPIO Test" توی منو برای تست همین موضوع است.               */
/* ------------------------------------------------------------------ */
#define SW_IDLE_LEVEL    GPIO_PIN_SET
#define SW_ACTIVE_LEVEL  GPIO_PIN_RESET

#define SW_DAC1_PORT GPIOE   /* PE11 -> R23 -> Q1,Q2,Q3  (⚠ فرض: مسیر DAC کانال ۱) */
#define SW_DAC1_PIN  GPIO_PIN_11
#define SW_DAC2_PORT GPIOB   /* PB10 -> R46 -> Q4,Q5,Q6  (⚠ فرض: مسیر DAC کانال ۲) */
#define SW_DAC2_PIN  GPIO_PIN_10

#define DAC_GAIN     1.0f    /* ⚠ ضریب تقویت بعد از DAC تا پروب (برای نمایش) */

/* ------------------------------------------------------------------ */
/* ۳) اهم‌متر: تقسیم ولتاژ  Vref --Rref--(پین ADC)--Rx-- GND            */
/*    ⚠ مقادیر Rref و اینکه کدوم سوئیچ کدوم مقاومته رو مهندس تأیید کنه.  */
/*    (از نت‌لیست: R33=1K و R50=10K روی Probe1، Vref=3.3V)              */
/*    ADC: PB1 = ADC1_IN5 (خروجی U7A، همون Probe1)                      */
/* ------------------------------------------------------------------ */
#define OHM_VREF       3.3f
#define OHM_ADC_CH     ADC_CHANNEL_5
#define OHM_RANGES     2U

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    float         rref;      /* اهم */
    const char   *name;
} OhmRange_t;

#define OHM_RANGE_TABLE  {                                   \
    { GPIOE, GPIO_PIN_15, 1000.0f,  "PE15 1k"  },            \
    { GPIOE, GPIO_PIN_14, 10000.0f, "PE14 10k" },            \
}
/* ولتاژ پایه‌ی ADC با Rx = ∞ نباید بالاتر از این بشه؛ بالاتر = "OPEN" */
#define OHM_OPEN_FRACTION  0.985f

/* ------------------------------------------------------------------ */
/* ۴) خوندن PWM: کدوم پایه‌ی ADC سیگنال پروب رو می‌بینه                 */
/*    ⚠ فقط PB1 قطعاً Probe1 است؛ بقیه رو با دکمه‌ی چپ/راست امتحان کن.   */
/* ------------------------------------------------------------------ */
typedef enum { RD_PB1 = 0, RD_PA6, RD_PA2, RD_PC1, RD_COUNT } ReadSrc_t;
#define RD_DEFAULT_FOR_PROBE1   RD_PB1
#define RD_DEFAULT_FOR_PROBE2   RD_PA6

#endif /* BOARD_MAP_H */

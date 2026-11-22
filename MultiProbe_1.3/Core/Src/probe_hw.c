#include "probe_hw.h"
#include "adc_probe.h"
#include <string.h>

extern TIM_HandleTypeDef  htim1;
extern TIM_HandleTypeDef  htim15;
extern ADC_HandleTypeDef  hadc1;
extern ADC_HandleTypeDef  hadc2;
extern DAC_HandleTypeDef  hdac1;

static const OhmRange_t k_ohm[OHM_RANGES] = OHM_RANGE_TABLE;

/* ضریب‌ها همون‌هایی که توی adc_probe.c فرض شده */
#define RATIO_RD_PB1  1.0f
#define RATIO_RD_PA6  6.1f
#define RATIO_RD_PA2  6.1f
#define RATIO_RD_PC1  6.1f
#define RATIO_RD_PC4  13.6466f

#define BURST_N   4096U
static uint16_t s_buf[BURST_N];

static uint8_t s_pwmOn = 0U;
static uint8_t s_ohmGnd = 0U;      /* 1 = Probe2 برای اهم‌متر روی 0V نگه داشته شده */

static void ohm_gnd_off(void);

static void oe(GPIO_TypeDef *port, uint16_t pin, uint8_t enable)
{
    HAL_GPIO_WritePin(port, pin, enable ? GPIO_PIN_RESET : GPIO_PIN_SET);   /* فعال-پایین */
}

/* سطح فعال سوئیچ‌های SEL (PE11, PB10, PE15, PE14). پیش‌فرض از board_map.h ؛ اهم‌متر می‌تونه عوضش کنه */
static GPIO_PinState s_swActive = SW_ACTIVE_LEVEL;

static void sw(GPIO_TypeDef *port, uint16_t pin, uint8_t on)
{
    GPIO_PinState idle = (s_swActive == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(port, pin, on ? s_swActive : idle);
}

uint8_t Probe_SwActiveHigh(void) { return (s_swActive == GPIO_PIN_SET) ? 1U : 0U; }

void Probe_SwFlipPolarity(void)
{
    s_swActive = (s_swActive == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    Probe_DacSwitch(1U, 0U);                        /* سوئیچ‌های DAC هم با سطح جدید خاموش بشن */
    Probe_DacSwitch(2U, 0U);
}


void Probe_RawWrite(GPIO_TypeDef *port, uint16_t pin, uint8_t high)
{
    HAL_GPIO_WritePin(port, pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Probe_Init(void)
{
    /* PC13 توی CubeMX خروجی نشده بود */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(OE_X2_PORT, OE_X2_PIN, GPIO_PIN_SET);
    GPIO_InitTypeDef gi = {0};
    gi.Pin   = OE_X2_PIN;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(OE_X2_PORT, &gi);

    /* شمارنده‌ی سیکل برای زمان‌سنجی نمونه‌برداری */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    Probe_AllOff();
}

/* ---- سیگنال میل‌لنگ ۶۰-۲ : روی Probe1 موج PWM و روی Probe2 معکوسش (PWM not) ---- */
#define CRANK_TEETH      60U
#define CRANK_MISSING    2U
static volatile uint8_t  s_crank = 0U;
static volatile uint8_t  s_crankIdx = 0U;
static volatile uint32_t s_crankCcr = 0U;

/* شروع هر پریود: CCR (شدوی preload) برای پریود بعدی تنظیم می‌شه؛ دو دندانه‌ی آخر CCR=0 -> دندانه‌ی خالی */
void TIM15_IRQHandler(void)
{
    if (TIM15->SR & TIM_SR_UIF)
    {
        TIM15->SR = ~TIM_SR_UIF;
        if (s_crank)
        {
            uint8_t n = (uint8_t)((s_crankIdx + 1U) % CRANK_TEETH);
            s_crankIdx = n;
            TIM15->CCR1 = (n >= (CRANK_TEETH - CRANK_MISSING)) ? 0U : s_crankCcr;
        }
    }
}

static void crank_disable(void)
{
    s_crank = 0U;
    TIM15->DIER &= ~TIM_DIER_UIE;
    TIM15->CCMR1 &= ~TIM_CCMR1_OC1PE;
    HAL_NVIC_DisableIRQ(TIM15_IRQn);
}

void Probe_PwmStartCrank(uint32_t tooth_hz)
{
    ohm_gnd_off();
    if (tooth_hz < 1U)      tooth_hz = 1U;
    if (tooth_hz > 20000U)  tooth_hz = 20000U;

    oe(OE_P1_PORT, OE_P1_PIN, 0U);
    oe(OE_P2_PORT, OE_P2_PIN, 0U);
    crank_disable();
    if (s_pwmOn)
    {
        (void)HAL_TIM_PWM_Stop(&htim15, TIM_CHANNEL_1);
        (void)HAL_TIMEx_PWMN_Stop(&htim15, TIM_CHANNEL_1);
    }

    uint32_t tclk = HAL_RCC_GetPCLK2Freq() * 2U;
    uint32_t psc  = tclk / (tooth_hz * 65000UL);
    uint32_t arr  = tclk / ((psc + 1U) * tooth_hz) - 1U;
    if (arr < 3U) arr = 3U;
    s_crankCcr = (arr + 1U) / 2U;                              /* دندانه با duty 50٪ */
    s_crankIdx = 0U;

    __HAL_TIM_SET_PRESCALER(&htim15, psc);
    __HAL_TIM_SET_AUTORELOAD(&htim15, arr);
    TIM15->CCMR1 |= TIM_CCMR1_OC1PE;                           /* CCR فقط روی لبه‌ی پریود بعدی اعمال بشه */
    __HAL_TIM_SET_COMPARE(&htim15, TIM_CHANNEL_1, s_crankCcr);
    __HAL_TIM_SET_COUNTER(&htim15, 0U);
    TIM15->EGR = TIM_EGR_UG;
    TIM15->SR  = ~TIM_SR_UIF;

    s_crank = 1U;
    TIM15->DIER |= TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM15_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM15_IRQn);

    (void)HAL_TIM_PWM_Start(&htim15, TIM_CHANNEL_1);
    (void)HAL_TIMEx_PWMN_Start(&htim15, TIM_CHANNEL_1);        /* CH1N = مکمل CH1 */
    s_pwmOn = 1U;
    oe(OE_P1_PORT, OE_P1_PIN, 1U);                             /* هر دو پروب فعال */
    oe(OE_P2_PORT, OE_P2_PIN, 1U);
}

void Probe_PwmStop(void)
{
    oe(OE_P1_PORT, OE_P1_PIN, 0U);
    oe(OE_P2_PORT, OE_P2_PIN, 0U);
    crank_disable();
    if (s_pwmOn)
    {
        (void)HAL_TIM_PWM_Stop(&htim15, TIM_CHANNEL_1);
        (void)HAL_TIMEx_PWMN_Stop(&htim15, TIM_CHANNEL_1);
        s_pwmOn = 0U;
    }
}

void Probe_AllOff(void)
{
    Probe_PwmStop();
    oe(OE_X1_PORT, OE_X1_PIN, 0U);
    oe(OE_X2_PORT, OE_X2_PIN, 0U);
    Probe_OhmOff();
    Probe_DacOff();
}

void Probe_PwmStart(ProbeId_t p, uint32_t f, uint8_t duty)
{
    ohm_gnd_off();                                  /* PE4 باید پایه‌ی تایمر باشه */
    if (f < 1U)      f = 1U;
    if (f > 24000000U) f = 24000000U;           /* سقف نرم‌افزاری فعلی؛ نه سقف خود تایمر MCU */
    if (duty > 100U) duty = 100U;

    /* تا وقتی تنظیمات عوض می‌شه هر دو خروجی غیرفعال */
    oe(OE_P1_PORT, OE_P1_PIN, 0U);
    oe(OE_P2_PORT, OE_P2_PIN, 0U);
    crank_disable();
    if (s_pwmOn)
    {
        (void)HAL_TIM_PWM_Stop(&htim15, TIM_CHANNEL_1);
        (void)HAL_TIMEx_PWMN_Stop(&htim15, TIM_CHANNEL_1);
    }

    const uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    const uint32_t tclk = pclk2 * 2U;                       /* APB2 prescaler=2؛ کلاک TIM15 دو برابر PCLK2 */

    /* اگر کلاک تایمر آماده نیست، خروجی‌ها خاموش بمانند؛ هیچ تقسیم نامعتبری انجام نشود. */
    if (tclk == 0U)
    {
        Probe_PwmStop();
        return;
    }

    const uint64_t pscDen = (uint64_t)f * 65000ULL;
    if (pscDen == 0ULL)
    {
        Probe_PwmStop();
        return;
    }

    uint64_t psc64 = (uint64_t)tclk / pscDen;
    if (psc64 > 65535ULL) psc64 = 65535ULL;                /* PSC تایمر ۱۶ بیتی است */
    const uint32_t psc = (uint32_t)psc64;

    const uint64_t periodDen = ((uint64_t)psc + 1ULL) * (uint64_t)f;
    if (periodDen == 0ULL)
    {
        Probe_PwmStop();
        return;
    }

    uint64_t ticks = ((uint64_t)tclk + (periodDen / 2ULL)) / periodDen; /* گرد کردن برای کمینه‌کردن خطای فرکانس */
    if (ticks < 2ULL) ticks = 2ULL;                        /* حداقل دو شمارش برای PWM با دو سطح */
    if (ticks > 65536ULL) ticks = 65536ULL;                /* ARR تایمر ۱۶ بیتی است */
    const uint32_t arr = (uint32_t)(ticks - 1ULL);

    /* CH1N مکمل CH1 است؛ برای Probe2 دیوتی رو برعکس می‌کنیم تا همون دیوتی دلخواه دیده بشه */
    uint32_t d   = (p == PROBE_1) ? duty : (100U - duty);
    uint32_t ccr = (uint32_t)((ticks * (uint64_t)d) / 100ULL);

    __HAL_TIM_SET_PRESCALER(&htim15, psc);
    __HAL_TIM_SET_AUTORELOAD(&htim15, arr);
    __HAL_TIM_SET_COMPARE(&htim15, TIM_CHANNEL_1, ccr);
    __HAL_TIM_SET_COUNTER(&htim15, 0U);
    htim15.Instance->EGR = TIM_EGR_UG;                       /* مقادیر جدید فوراً اعمال بشه */

    (void)HAL_TIM_PWM_Start(&htim15, TIM_CHANNEL_1);
    (void)HAL_TIMEx_PWMN_Start(&htim15, TIM_CHANNEL_1);
    s_pwmOn = 1U;

    if (p == PROBE_1) oe(OE_P1_PORT, OE_P1_PIN, 1U);
    else              oe(OE_P2_PORT, OE_P2_PIN, 1U);
}

/* ---------------- خواندن ---------------- */

static void src_info(ReadSrc_t s, ADC_HandleTypeDef **h, uint32_t *ch, float *ratio)
{
    switch (s)
    {
    default:
    case RD_PB1: *h = &hadc1; *ch = ADC_CHANNEL_5;  *ratio = RATIO_RD_PB1; break;
    case RD_PA6: *h = &hadc1; *ch = ADC_CHANNEL_3;  *ratio = RATIO_RD_PA6; break;
    case RD_PA2: *h = &hadc1; *ch = ADC_CHANNEL_14; *ratio = RATIO_RD_PA2; break;
    case RD_PC1: *h = &hadc2; *ch = ADC_CHANNEL_11; *ratio = RATIO_RD_PC1; break;
    case RD_PC4: *h = &hadc1; *ch = ADC_CHANNEL_4; *ratio = RATIO_RD_PC4; break;
    }
}

const char *Probe_ReadName(ReadSrc_t s)
{
    static const char *const n[RD_COUNT] = { "PB1", "PA6", "PA2", "PC1", "PC4" };
    return (s < RD_COUNT) ? n[s] : "?";
}

float Probe_ReadVolts(ReadSrc_t s)
{
    ADC_HandleTypeDef *h; uint32_t ch; float ratio;
    src_info(s, &h, &ch, &ratio);
    return Adc_ReadPinVolts(h, ch) * ratio;
}

/* n نمونه‌ی پشت‌سرهم از یک کانال؛ برمی‌گردونه چند سیکل CPU طول کشید (0 = خطا) */
uint32_t Probe_Capture(ReadSrc_t src, uint16_t *dst, uint32_t n)
{
    ADC_HandleTypeDef *h; uint32_t ch; float ratio;
    src_info(src, &h, &ch, &ratio);

    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = ch;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    /* 8.5 سیکل خیلی کوتاهه برای امپدانس منبع این پروب‌ها (تقسیم‌کننده‌های
     * ۱۰۰ک/۱M روی PA6/PA2/PC1/PC4): خازن S&H داخلی ADC کامل شارژ نمی‌شه و
     * چند نمونه‌ی بعد از هر لبه به‌صورت یک "رمپ" RC به سمت مقدار واقعی
     * می‌رسه -> دقیقاً همون چیزی که توی اسیلسکوپ به‌جای تخت‌بودن بالای موج
     * مربعی دیده می‌شه. همون 64.5 سیکلی که بقیه‌ی adc_probe.c برای همین
     * کانال‌ها استفاده می‌کنه رو این‌جا هم می‌ذاریم؛ سرعت کلی burst چون با
     * overhead نرم‌افزاری HAL_ADC_Start/Poll غالب می‌شه تقریباً تغییری
     * نمی‌کنه. */
    /* PC4 = خروجی OPAMP (امپدانس پایین): همون 8.5 سیکلی که ولت‌متر (Adc_CapturePC4) استفاده می‌کنه
       تا اسکوپ و ولت‌متر یک شکل موج ببینن. بقیه‌ی کانال‌ها (تقسیم‌کننده‌ی پر‌امپدانس) 64.5 می‌مونن. */
    cfg.SamplingTime = (src == RD_PC4) ? ADC_SAMPLETIME_8CYCLES_5 : ADC_SAMPLETIME_64CYCLES_5;
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    if (HAL_ADC_ConfigChannel(h, &cfg) != HAL_OK) return 0U;

    uint32_t t0 = DWT->CYCCNT;
    for (uint32_t i = 0U; i < n; i++)
    {
        HAL_ADC_Start(h);
        if (HAL_ADC_PollForConversion(h, 2U) == HAL_OK)
            dst[i] = (uint16_t)HAL_ADC_GetValue(h);
        else
            dst[i] = (i > 0U) ? dst[i - 1U] : 0U;
    }
    uint32_t cyc = DWT->CYCCNT - t0;
    HAL_ADC_Stop(h);
    return cyc;
}

float Probe_CountsToVolts(ReadSrc_t src, uint32_t counts)
{
    ADC_HandleTypeDef *h; uint32_t ch; float ratio;
    src_info(src, &h, &ch, &ratio);
    (void)h; (void)ch;
    return (float)counts * 3.3f / 65535.0f * ratio;
}

void Probe_MeasureGap(ReadSrc_t src, SigStat_t *o, uint32_t gap_cycles)
{
    ADC_HandleTypeDef *h; uint32_t ch; float ratio;
    src_info(src, &h, &ch, &ratio);
    memset(o, 0, sizeof(*o));

    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = ch;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;   /* همون دلیل بالا در Probe_Capture */
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    if (HAL_ADC_ConfigChannel(h, &cfg) != HAL_OK) return;

    uint32_t t0 = DWT->CYCCNT;
    for (uint32_t i = 0U; i < BURST_N; i++)
    {
        HAL_ADC_Start(h);
        if (HAL_ADC_PollForConversion(h, 2U) == HAL_OK)
            s_buf[i] = (uint16_t)HAL_ADC_GetValue(h);
        else
            s_buf[i] = (i > 0U) ? s_buf[i - 1U] : 0U;
        if (gap_cycles != 0U)
        {
            uint32_t tg = DWT->CYCCNT;
            while ((DWT->CYCCNT - tg) < gap_cycles) { }
        }
    }
    uint32_t cyc = DWT->CYCCNT - t0;
    HAL_ADC_Stop(h);
    if (cyc == 0U) return;

    uint32_t mn = 65535U, mx = 0U; uint64_t sum = 0U;
    for (uint32_t i = 0U; i < BURST_N; i++)
    {
        uint32_t v = s_buf[i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
    }
    const float k = 3.3f / 65535.0f * ratio;
    o->vmin = (float)mn * k;
    o->vmax = (float)mx * k;
    o->vavg = ((float)sum / (float)BURST_N) * k;

    uint32_t span = mx - mn;
    /* ~۰٫۲ ولت روی پایه */
    if (span < (uint32_t)(0.2f / 3.3f * 65535.0f)) return;       /* DC است، سیگنال نیست */
    o->signal = 1U;

    uint32_t thr  = (mx + mn) / 2U;
    uint32_t hyst = span / 8U;
    uint32_t high = 0U, rises = 0U, firstIdx = 0U, lastIdx = 0U;
    uint8_t  st   = (s_buf[0] > thr) ? 1U : 0U;
    for (uint32_t i = 0U; i < BURST_N; i++)
    {
        uint32_t v = s_buf[i];
        if (v > thr) high++;
        if (!st && v > thr + hyst)       { st = 1U; if (rises++ == 0U) firstIdx = i; lastIdx = i; }
        else if (st && v + hyst < thr)   { st = 0U; }
    }
    o->duty_pct = 100.0f * (float)high / (float)BURST_N;

    if (rises >= 3U)
    {
        uint32_t periods = rises - 1U;
        uint32_t spp_x10 = ((lastIdx - firstIdx) * 10U) / periods;   /* نمونه در هر پریود ×۱۰ */
        if (spp_x10 < 60U)
        {
            o->too_fast = 1U;                                   /* کمتر از ۶ نمونه در پریود */
        }
        else
        {
            uint64_t num = (uint64_t)periods * SystemCoreClock * BURST_N;
            uint64_t den = (uint64_t)cyc * (lastIdx - firstIdx);
            o->freq_hz = (uint32_t)(num / den);
            o->freq_ok = 1U;
        }
    }
}

void Probe_Measure(ReadSrc_t src, SigStat_t *o)
{
    Probe_MeasureGap(src, o, 0U);
}

/* ---------------- اهم‌متر ---------------- */

/* اهم‌متر: Probe2 باید 0V باشه (مرجع GND).
 *   PE4 (TIM15_CH1N) موقتاً GPIO خروجی LOW می‌شه  -> ورودی A3 از 74HC125 = 0
 *   PA3 (OE3) = LOW                                -> بافر Y3 فعال، Probe2 = 0V
 * موقع خروج هر دو برمی‌گردن به حالت عادی (PE4 دوباره پایه‌ی PWM، PA3 = HIGH = خاموش). */
static void ohm_gnd_on(void)
{
    if (s_ohmGnd) return;
    Probe_PwmStop();                               /* تایمر قطع + هر دو OE خاموش */

    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_4, GPIO_PIN_RESET);   /* اول LOW بشه بعد خروجی */
    gi.Pin   = GPIO_PIN_4;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &gi);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_4, GPIO_PIN_RESET);

    oe(OE_P2_PORT, OE_P2_PIN, 1U);                 /* PA3 = LOW */
    s_ohmGnd = 1U;
}

static void ohm_gnd_off(void)
{
    if (!s_ohmGnd) return;
    oe(OE_P2_PORT, OE_P2_PIN, 0U);                 /* PA3 = HIGH */

    GPIO_InitTypeDef gi = {0};
    gi.Pin       = GPIO_PIN_4;                     /* PE4 دوباره TIM15_CH1N */
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_NOPULL;
    gi.Speed     = GPIO_SPEED_FREQ_LOW;
    gi.Alternate = GPIO_AF4_TIM15;
    HAL_GPIO_Init(GPIOE, &gi);
    s_ohmGnd = 0U;
}

void Probe_OhmOff(void)
{
    for (uint8_t i = 0U; i < OHM_RANGES; i++)
        sw(k_ohm[i].port, k_ohm[i].pin, 0U);
    ohm_gnd_off();
}

void Probe_P1PullUp(uint8_t on)
{
    sw(k_ohm[0].port, k_ohm[0].pin, 0U);            /* رنج 1k خاموش */
    sw(k_ohm[OHM_RANGES - 1U].port, k_ohm[OHM_RANGES - 1U].pin, on ? 1U : 0U);   /* رنج 10k */
}

void Probe_OhmSelect(uint8_t range)
{
    ohm_gnd_on();                                  /* فقط اهم‌متر: Probe2 = 0V */
    for (uint8_t i = 0U; i < OHM_RANGES; i++)
        sw(k_ohm[i].port, k_ohm[i].pin, (i == range) ? 1U : 0U);
}


/* ---------------- DAC sine generator (TIM1 update interrupt) ----------------
 * TIM1 در نسخه فعلی برای خروجی پروب استفاده نشده؛ TIM15 همچنان PWM پروب‌هاست.
 * 64 نمونه در هر پریود؛ TIM1 فقط به‌عنوان timebase/interrupt استفاده می‌شود.
 */
#define DAC_SINE_N 64U
static const uint16_t s_sineLut[DAC_SINE_N] = {
    2048, 2248, 2447, 2642, 2831, 3013, 3185, 3346, 3495, 3630, 3750, 3853, 3939, 4007, 4056, 4085, 4095, 4085, 4056, 4007, 3939, 3853, 3750, 3630, 3495, 3346, 3185, 3013, 2831, 2642, 2447, 2248, 2048, 1847, 1648, 1453, 1264, 1082, 910, 749, 600, 465, 345, 242, 156, 88, 39, 10, 0, 10, 39, 88, 156, 242, 345, 465, 600, 749, 910, 1082, 1264, 1453, 1648, 1847
};

static volatile uint8_t s_sineRunning = 0U;
static volatile uint8_t s_sineIndex = 0U;
static volatile uint16_t s_sineMid = 2048U;
static volatile uint16_t s_sineAmp = 0U;

uint8_t Probe_DacSineStart(uint32_t frequency_hz, float amplitude_v, float offset_v)
{
    if (frequency_hz < 1U || frequency_hz > 200U) return 0U;

    float vdda = (g_vdda > 3.0f && g_vdda < 3.6f) ? g_vdda : 3.3f;
    if (amplitude_v < 0.0f || offset_v < 0.0f) return 0U;
    if ((offset_v - amplitude_v) < 0.05f) return 0U;
    if ((offset_v + amplitude_v) > (vdda - 0.05f)) return 0U;

    Probe_DacSineStop();
    Probe_DacSwitch(1U, 0U);
    Probe_DacSet(2U, offset_v);

    s_sineMid = (uint16_t)((offset_v / vdda) * 4095.0f + 0.5f);
    s_sineAmp = (uint16_t)((amplitude_v / vdda) * 4095.0f + 0.5f);
    s_sineIndex = 0U;

    uint32_t timclk = HAL_RCC_GetPCLK2Freq() * 2U;
    uint32_t update_hz = frequency_hz * DAC_SINE_N;
    uint32_t arr = timclk / update_hz;
    if (arr < 2U) return 0U;
    arr -= 1U;

    __HAL_TIM_DISABLE(&htim1);
    __HAL_TIM_SET_PRESCALER(&htim1, 0U);
    __HAL_TIM_SET_AUTORELOAD(&htim1, arr);
    __HAL_TIM_SET_COUNTER(&htim1, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);

    HAL_NVIC_SetPriority(TIM1_UP_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);

    if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_2) != HAL_OK) return 0U;
    Probe_DacSwitch(2U, 1U);
    s_sineRunning = 1U;
    if (HAL_TIM_Base_Start_IT(&htim1) != HAL_OK)
    {
        s_sineRunning = 0U;
        Probe_DacSwitch(2U, 0U);
        return 0U;
    }
    return 1U;
}

void Probe_DacSineStop(void)
{
    s_sineRunning = 0U;
    (void)HAL_TIM_Base_Stop_IT(&htim1);
    HAL_NVIC_DisableIRQ(TIM1_UP_IRQn);
    Probe_DacSwitch(2U, 0U);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if ((htim->Instance == TIM1) && s_sineRunning)
    {
        uint32_t lut = s_sineLut[s_sineIndex];
        int32_t centered = (int32_t)lut - 2048;
        int32_t code = (int32_t)s_sineMid + (centered * (int32_t)s_sineAmp) / 2047;
        if (code < 0) code = 0;
        if (code > 4095) code = 4095;
        (void)HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, (uint32_t)code);
        s_sineIndex = (uint8_t)((s_sineIndex + 1U) & (DAC_SINE_N - 1U));
    }
}

/* ---------------- DAC ---------------- */

void Probe_DacSwitch(uint8_t ch, uint8_t on)
{
    if (ch == 1U) sw(SW_DAC1_PORT, SW_DAC1_PIN, on);
    else          sw(SW_DAC2_PORT, SW_DAC2_PIN, on);
}

void Probe_DacSet(uint8_t ch, float volts)
{
    uint32_t chan = (ch == 1U) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
    if (volts < 0.0f) volts = 0.0f;
    float vdda = (g_vdda > 3.0f && g_vdda < 3.6f) ? g_vdda : 3.3f;
    if (volts > vdda) volts = vdda;
    uint32_t code = (uint32_t)(volts / vdda * 4095.0f + 0.5f);
    if (code > 4095U) code = 4095U;
    (void)HAL_DAC_SetValue(&hdac1, chan, DAC_ALIGN_12B_R, code);
    (void)HAL_DAC_Start(&hdac1, chan);
}

void Probe_DacOff(void)
{
    Probe_DacSineStop();
    Probe_DacSwitch(1U, 0U);
    Probe_DacSwitch(2U, 0U);
    (void)HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 0U);
    (void)HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0U);
}

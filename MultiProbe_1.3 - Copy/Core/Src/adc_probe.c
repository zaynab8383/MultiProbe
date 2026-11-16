#include "adc_probe.h"

extern ADC_HandleTypeDef  hadc1;
extern ADC_HandleTypeDef  hadc2;
extern OPAMP_HandleTypeDef hopamp1;

#define VREF_V         3.3f
#define ADC_MAX        65535.0f
#define AVG_SAMPLES    16

#define DIVIDER_VIN    13.6466f   /* (R8 + R6) / R6 */
#define BIAS_PB1       1.66f
#define RATIO_PB1      1.0f    /* ⚠ از مهندس بگیر */
#define RATIO_PA6      6.1f    /* ⚠ فرض: 5.1K / 1K */
#define RATIO_PA2      6.1f    /* ⚠ فرض */
#define RATIO_PC1      6.1f    /* ⚠ فرض */
/* ولتاژ تغذیه‌ی برد (VCC) از پایه‌ی PA2:  VCC = Vpin × SUPPLY_DIVIDER.
 * اندازه‌گیری روی برد: ورودی 12V با ضریب 11 حدود 9V نشون می‌داد  =>  11 × 12 / 9 = 14.67
 * اگه عدد روی نوار بالا با مولتی‌متر فرق داشت، فقط همین عدد رو درست کن
 * (یا موقتاً با بالا/پایین توی صفحه‌ی اصلی؛ اون با ریست از بین می‌ره). */
#define SUPPLY_DIVIDER 14.67f
static float s_supplyScale = SUPPLY_DIVIDER;   /* با دکمه‌های بالا/پایین توی صفحه‌ی اصلی قابل تنظیمه */

static uint32_t read_raw(ADC_HandleTypeDef *hadc, uint32_t channel, uint32_t sampling)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = sampling;
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;

    if (HAL_ADC_ConfigChannel(hadc, &cfg) != HAL_OK) return 0;

    uint32_t sum = 0;
    for (int i = 0; i < AVG_SAMPLES; i++) {
        HAL_ADC_Start(hadc);
        if (HAL_ADC_PollForConversion(hadc, 10) == HAL_OK)
            sum += HAL_ADC_GetValue(hadc);
        HAL_ADC_Stop(hadc);
    }
    return sum / AVG_SAMPLES;
}

static float to_volt(uint32_t raw) { return (float)raw * VREF_V / ADC_MAX; }
/* ولتاژ خود پایه/کانال، بدون هیچ ضریب تقسیمی (برای تست) */
float Adc_ReadPinVolts(ADC_HandleTypeDef *hadc, uint32_t channel)
{
    return to_volt(read_raw(hadc, channel, ADC_SAMPLETIME_64CYCLES_5));
}

void Adc_Init(void)
{
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
    HAL_OPAMP_Start(&hopamp1);
}

/* PB0 = تقسیم‌کننده‌ی ورودی Probe1 قبل از OPAMP ؛ برخلاف PC4 کف ۰٫۰۹۵ ولت نداره و برای اهم‌متر مناسب‌تره. */
float Adc_GetProbe1Dc(void) { return to_volt(read_raw(&hadc1, ADC_CHANNEL_9, ADC_SAMPLETIME_810CYCLES_5)) * DIVIDER_VIN; }
float Adc_GetPC4(void) { return to_volt(read_raw(&hadc1, ADC_CHANNEL_4,  ADC_SAMPLETIME_64CYCLES_5)); }
float Adc_GetPB0(void) { return to_volt(read_raw(&hadc1, ADC_CHANNEL_9,  ADC_SAMPLETIME_810CYCLES_5)); }

/* ---- تصحیح صفر ----
 * وقتی ورودی 0 ولته، PC4 حدود 0.095 ولت می‌خونه (کف خروجی OPAMP / آفست)، یعنی ~1.3 ولت اضافه روی نمایش.
 * زیر "زانو" (PC4_KNEE_V) به‌صورت خطی کشیده می‌شه که 0 -> 0 بشه و بالای زانو هیچ تغییری نکنه.
 * با دکمه‌ی ورود توی صفحه‌ی ولت‌متر (پروب‌ها روی GND) مقدار صفر دوباره کالیبره می‌شه. */
#define PC4_ZERO_DEFAULT_V  0.0953f
#define PC4_KNEE_V          0.25f
#define PC4_ZERO_MAX_V      0.20f     /* بیشتر از این به‌عنوان "صفر" قبول نمی‌شه */

static float s_pc4Zero = PC4_ZERO_DEFAULT_V;

float Adc_GetZero(void) { return s_pc4Zero; }
void  Adc_ResetZero(void) { s_pc4Zero = PC4_ZERO_DEFAULT_V; }

uint8_t Adc_CalibrateZero(void)
{
    float sum = 0.0f;
    for (int i = 0; i < 8; i++) sum += Adc_GetPC4();
    float z = sum / 8.0f;
    if (z > PC4_ZERO_MAX_V) return 0U;
    s_pc4Zero = z;
    return 1U;
}

/* بدون کلمپ/خم زانو - فقط ضریب تقسیم خطی. برای تست سیم‌کشی (DFT تک‌فرکانسی روی
 * یک موج کوچیک): هر کلمپ‌کردنی به صفر، حتی چند صدم ولت، شکل موج رو غیرخطی
 * می‌کنه و «خلوص» (match%) رو به‌خصوص در دامنه‌ی کم (سیم واقعی با بار ECU، یا
 * تغذیه‌ی پایین) به‌شدت پایین می‌آره. */
float Adc_PinToVinLinear(float p)
{
    return p * DIVIDER_VIN;
}

float Adc_PinToVin(float p)
{
    float z = s_pc4Zero;
    float c;
    if (p <= z)                c = 0.0f;
    else if (p >= PC4_KNEE_V)  c = p;
    else                       c = (p - z) * PC4_KNEE_V / (PC4_KNEE_V - z);
    float v = c * DIVIDER_VIN;
    return (v < 0.05f) ? 0.0f : v;
}

float Adc_GetVin(void)
{
    return Adc_PinToVin(Adc_GetPC4());
}

/* ------------------------------------------------------------------------------------------------
 * نمونه‌برداری سریع با زمان‌بندی «قطعی» (deterministic)
 *
 * چرا؟ کل کد برنامه از فلش خارجی (XIP + OTFDEC) اجرا می‌شه و فقط I-cache داره. هر cache-miss و هر
 * وقفه (SysTick / USB / ...) چند ده میکروثانیه فاصله‌ی بین نمونه‌ها رو می‌پرونه. قبلاً هم هر نمونه
 * HAL_ADC_Start / Poll / Stop می‌شد (روشن/خاموش‌کردن ADC برای هر نمونه). نتیجه: فاصله‌ی نمونه‌ها
 * یکنواخت نبود ولی نمایش فرض می‌کرد یکنواخته -> پالس مربعی با عرض‌های نامنظم و لرزان روی صفحه.
 *
 * حالا:
 *   - حلقه‌ی نمونه‌برداری داخل RAM داخلی (.RamFunc) اجرا می‌شه، نه فلش خارجی
 *   - ADC فقط یک‌بار روشن می‌شه؛ هر نمونه با رجیستر مستقیم شروع می‌شه
 *   - شروع هر نمونه روی یک شبکه‌ی ثابت DWT->CYCCNT قفل است (t0 + k*step) نه «بعد از نمونه‌ی قبل»
 *   - اگه کل بازه کوتاه (<= ~10ms) باشه وقفه‌ها حین نمونه‌برداری مسدود می‌شن
 * ------------------------------------------------------------------------------------------------ */
#define CAP_IRQ_MASK_MAX_MS  10U       /* بیشتر از این وقفه‌ها رو مسدود نمی‌کنیم (SysTick/USB) */

__attribute__((section(".RamFunc"), noinline, optimize("O2")))
static uint32_t adc_burst_ram(ADC_TypeDef *a, uint16_t *dst, uint32_t n, uint32_t step, uint32_t mask_irq)
{
    uint32_t primask = __get_PRIMASK();
    if (mask_irq) __disable_irq();

    /* اولین تبدیل رو HAL_ADC_Start شروع کرده؛ فقط تموم شدنش رو صبر می‌کنیم و دورش می‌ندازیم */
    uint32_t t = DWT->CYCCNT;
    while ((a->ISR & ADC_ISR_EOC) == 0U)
    {
        if ((DWT->CYCCNT - t) > 280000U) { if (mask_irq && !primask) __enable_irq(); return 0U; }
    }
    (void)a->DR;

    uint32_t tgt = DWT->CYCCNT + 400U;
    for (uint32_t i = 0U; i < n; i++)
    {
        while ((int32_t)(DWT->CYCCNT - tgt) < 0) { }
        uint32_t ts = DWT->CYCCNT;
        MODIFY_REG(a->CR, ADC_CR_BITS_PROPERTY_RS, ADC_CR_ADSTART);
        while ((a->ISR & ADC_ISR_EOC) == 0U)
        {
            if ((DWT->CYCCNT - ts) > 280000U) { if (mask_irq && !primask) __enable_irq(); return 0U; }
        }
        dst[i] = (uint16_t)a->DR;                   /* خوندن DR پرچم EOC رو پاک می‌کنه */
        if (i == 0U)
        {
            uint32_t conv = DWT->CYCCNT - ts;       /* زمان واقعی یک تبدیل */
            if (step < conv + 40U) step = conv + 40U;
        }
        tgt += step;
    }
    if (mask_irq && !primask) __enable_irq();
    return step;                                    /* گام واقعی (سیکل CPU) */
}

uint32_t Adc_CapturePC4(uint16_t *dst, uint32_t n, uint32_t gap_cycles)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = ADC_CHANNEL_4;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_8CYCLES_5;
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK) return 0U;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    /* گام = حداقل گام + فاصله‌ی اضافه‌ی درخواستی (gap) ؛ همه‌ی صفحات (ولت‌متر، اسکوپ، لاجیک،
     * تست سیم‌کشی) همون معنی قبلی gap رو دارن. */
    uint32_t step = ADC_CAP_MIN_STEP_CYC + gap_cycles;
    uint32_t mask = ((uint64_t)step * n <= (uint64_t)(SystemCoreClock / 1000U) * CAP_IRQ_MASK_MAX_MS) ? 1U : 0U;

    if (HAL_ADC_Start(&hadc1) != HAL_OK) return 0U;
    uint32_t stepEff = adc_burst_ram(ADC1, dst, n, step, mask);
    (void)HAL_ADC_Stop(&hadc1);
    if (stepEff == 0U) return 0U;
    return stepEff * n;                              /* کل سیکل‌ها: dt = برگشتی / n دقیقاً یکنواخته */
}

float Adc_GetPB1(void) { return (to_volt(read_raw(&hadc1, ADC_CHANNEL_5,  ADC_SAMPLETIME_64CYCLES_5)) - BIAS_PB1) * RATIO_PB1; }
float Adc_GetPA6(void) { return to_volt(read_raw(&hadc1, ADC_CHANNEL_3,  ADC_SAMPLETIME_64CYCLES_5)) * RATIO_PA6; }
float Adc_GetPA2(void) { return to_volt(read_raw(&hadc1, ADC_CHANNEL_14, ADC_SAMPLETIME_64CYCLES_5)) * RATIO_PA2; }
float Adc_GetSupply(void) { return to_volt(read_raw(&hadc1, ADC_CHANNEL_14, ADC_SAMPLETIME_64CYCLES_5)) * s_supplyScale; }
float Adc_GetSupplyScale(void) { return s_supplyScale; }
void  Adc_AdjustSupply(float dv)
{
    float v = Adc_GetSupply();
    if (v < 1.0f) return;
    float t = v + dv;
    if (t < 1.0f) return;
    s_supplyScale *= t / v;
}
float Adc_GetPC1(void) { return to_volt(read_raw(&hadc2, ADC_CHANNEL_11, ADC_SAMPLETIME_64CYCLES_5)) * RATIO_PC1; }
volatile uint32_t g_vrefint_raw;
volatile float    g_vdda;

void Adc_SelfTest(void)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = ADC_CHANNEL_VREFINT;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_810CYCLES_5;
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;

    HAL_ADC_ConfigChannel(&hadc2, &cfg);
    HAL_ADC_Start(&hadc2);
    HAL_ADC_PollForConversion(&hadc2, 10);
    g_vrefint_raw = HAL_ADC_GetValue(&hadc2);
    HAL_ADC_Stop(&hadc2);

    g_vdda = __LL_ADC_CALC_VREFANALOG_VOLTAGE(g_vrefint_raw, ADC_RESOLUTION_16B) / 1000.0f;
}

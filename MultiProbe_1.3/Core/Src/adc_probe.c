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
/* ولتاژ تغذیه‌ی برد (VCC) از پایه‌ی PA2: تقسیم‌کننده‌ی 10K + 1K  =>  VCC = Vpin × 11.
 * اگه عدد روی نوار بالا با مولتی‌متر فرق داشت، فقط همین عدد رو درست کن. */
#define SUPPLY_DIVIDER 11.0f
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

    uint32_t t0 = DWT->CYCCNT;
    for (uint32_t i = 0U; i < n; i++)
    {
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 2U) == HAL_OK)
            dst[i] = (uint16_t)HAL_ADC_GetValue(&hadc1);
        else
            dst[i] = (i > 0U) ? dst[i - 1U] : 0U;

        if (gap_cycles != 0U)
        {
            uint32_t t = DWT->CYCCNT;
            while ((DWT->CYCCNT - t) < gap_cycles) { }
        }
    }
    uint32_t cyc = DWT->CYCCNT - t0;
    HAL_ADC_Stop(&hadc1);
    return cyc;
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

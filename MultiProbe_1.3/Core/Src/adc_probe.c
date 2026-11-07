#include "adc_probe.h"

extern ADC_HandleTypeDef  hadc1;
extern ADC_HandleTypeDef  hadc2;
extern OPAMP_HandleTypeDef hopamp1;

#define VREF_V         3.3f
#define ADC_MAX        65535.0f
#define AVG_SAMPLES    16

#define DIVIDER_VIN    11.0f   /* (R8 + R6) / R6 */
#define BIAS_PB1       1.66f
#define RATIO_PB1      1.0f    /* ⚠ از مهندس بگیر */
#define RATIO_PA6      6.1f    /* ⚠ فرض: 5.1K / 1K */
#define RATIO_PA2      6.1f    /* ⚠ فرض */
#define RATIO_PC1      6.1f    /* ⚠ فرض */

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
float Adc_GetVin(void) { return Adc_GetPC4() * DIVIDER_VIN; }

float Adc_GetPB1(void) { return (to_volt(read_raw(&hadc1, ADC_CHANNEL_5,  ADC_SAMPLETIME_64CYCLES_5)) - BIAS_PB1) * RATIO_PB1; }
float Adc_GetPA6(void) { return to_volt(read_raw(&hadc1, ADC_CHANNEL_3,  ADC_SAMPLETIME_64CYCLES_5)) * RATIO_PA6; }
float Adc_GetPA2(void) { return to_volt(read_raw(&hadc1, ADC_CHANNEL_14, ADC_SAMPLETIME_64CYCLES_5)) * RATIO_PA2; }
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

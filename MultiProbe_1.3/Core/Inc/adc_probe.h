#ifndef ADC_PROBE_H
#define ADC_PROBE_H

#include "main.h"

void  Adc_Init(void);

float Adc_PinToVin(float pin_volts);   /* ولتاژ پایه‌ی PC4 -> ولتاژ ورودی (با تصحیح صفر و ضریب تقسیم) */
/* n نمونه‌ی سریع از PC4 (بدون میانگین). gap_cycles = فاصله‌ی اضافه بین نمونه‌ها (سیکل CPU).
 * خروجی: تعداد سیکل CPU کل، 0 = خطا */
uint32_t Adc_CapturePC4(uint16_t *dst, uint32_t n, uint32_t gap_cycles);
float Adc_GetVin(void);   /* ولتاژ ورودی با تصحیح صفر */
float Adc_GetZero(void);
void  Adc_ResetZero(void);
uint8_t Adc_CalibrateZero(void);   /* ولتاژ واقعی ورودی دستگاه (از PC4، خروجی OPAMP) */
float Adc_GetPB0(void);   /* ولتاژ تقسیم‌شده قبل از بافر، ADC1_IN9 */
float Adc_GetPC4(void);   /* خروجی OPAMP1، ADC1_IN4 */
float Adc_GetPB1(void);   /* ADC1_IN5، خروجی U7A، بعد از کم کردن آفست */
float Adc_GetPA6(void);   /* ADC1_IN3 */
float Adc_GetSupply(void);   /* ولتاژ تغذیه‌ی برد (VCC) از PA2 × 11 */
float Adc_GetSupplyScale(void);   /* ضریب فعلی PA2 -> VCC */
void  Adc_AdjustSupply(float dv); /* ولتاژ نمایشی رو به اندازه‌ی dv کم/زیاد می‌کنه (کالیبراسیون) */
float Adc_GetPA2(void);   /* ADC1_IN14 */
float Adc_GetPC1(void);   /* ADC2_IN11 */
float Adc_ReadPinVolts(ADC_HandleTypeDef *hadc, uint32_t channel);



void Adc_SelfTest(void);
extern volatile uint32_t g_vrefint_raw;
extern volatile float    g_vdda;

#endif

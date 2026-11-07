#ifndef ADC_PROBE_H
#define ADC_PROBE_H

#include "main.h"

void  Adc_Init(void);

float Adc_GetVin(void);   /* ولتاژ ورودی با تصحیح صفر */
float Adc_GetZero(void);
void  Adc_ResetZero(void);
uint8_t Adc_CalibrateZero(void);   /* ولتاژ واقعی ورودی دستگاه (از PC4، خروجی OPAMP) */
float Adc_GetPB0(void);   /* ولتاژ تقسیم‌شده قبل از بافر، ADC1_IN9 */
float Adc_GetPC4(void);   /* خروجی OPAMP1، ADC1_IN4 */
float Adc_GetPB1(void);   /* ADC1_IN5، خروجی U7A، بعد از کم کردن آفست */
float Adc_GetPA6(void);   /* ADC1_IN3 */
float Adc_GetPA2(void);   /* ADC1_IN14 */
float Adc_GetPC1(void);   /* ADC2_IN11 */
float Adc_ReadPinVolts(ADC_HandleTypeDef *hadc, uint32_t channel);



void Adc_SelfTest(void);
extern volatile uint32_t g_vrefint_raw;
extern volatile float    g_vdda;

#endif

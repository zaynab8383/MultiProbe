#ifndef PROBE_HW_H
#define PROBE_HW_H
#include "main.h"
#include "board_map.h"

typedef enum { PROBE_1 = 0, PROBE_2 = 1 } ProbeId_t;

typedef struct {
    float    vmin, vmax, vavg;   /* ولت (با ضریب تقسیم پایه) */
    float    duty_pct;
    uint32_t freq_hz;            /* 0 اگه قابل اندازه‌گیری نبود */
    uint8_t  signal;             /* 1 = نوسان واضح دیده شد */
    uint8_t  freq_ok;            /* 1 = فرکانس قابل اعتماد است */
    uint8_t  too_fast;           /* 1 = فرکانس بالاتر از توان نمونه‌برداری */
} SigStat_t;

void  Probe_Init(void);                       /* همه‌چیز خاموش/بیکار */
void  Probe_AllOff(void);                     /* PWM قطع، OE ها HIGH، سوئیچ‌ها بیکار */

/* PWM: probe خروجی می‌ده (OE اون پایه LOW)، پروب دیگه Hi-Z می‌مونه */
void  Probe_PwmStart(ProbeId_t p, uint32_t freq_hz, uint8_t duty_pct);
void  Probe_PwmStop(void);

/* نمونه‌برداری سریع و استخراج مشخصات سیگنال (~۲۰ms بلاک می‌شه) */
void  Probe_Measure(ReadSrc_t src, SigStat_t *out);
void  Probe_MeasureGap(ReadSrc_t src, SigStat_t *out, uint32_t gap_cycles);   /* با فاصله‌ی اضافه بین نمونه‌ها (برای فرکانس‌های پایین) */
const char *Probe_ReadName(ReadSrc_t src);
uint32_t Probe_Capture(ReadSrc_t src, uint16_t *dst, uint32_t n);   /* CPU cycles taken, 0 = error */
float Probe_CountsToVolts(ReadSrc_t src, uint32_t counts);
float Probe_ReadVolts(ReadSrc_t src);         /* ولتاژ DC (میانگین)، با ضریب تقسیم */

/* اهم‌متر */
void  Probe_OhmSelect(uint8_t range);         /* فقط سوئیچ range فعال */
void  Probe_OhmOff(void);
void  Probe_P1PullUp(uint8_t on);             /* پول‌آپ 10k روی Probe1 (3.3V) ، بدون دست‌زدن به Probe2 (برای تست سیم‌کشی) */
void  Probe_SwFlipPolarity(void);             /* سطح فعال SEL ها رو برعکس می‌کنه */
uint8_t Probe_SwActiveHigh(void);             /* 1 = سوئیچ‌ها با HIGH روشن می‌شن */

/* DAC */
void  Probe_DacSwitch(uint8_t ch, uint8_t on);   /* ch = 1 یا 2 */
void  Probe_DacSet(uint8_t ch, float volts);     /* ولتاژ پایه‌ی DAC (۰..VDDA) */
void  Probe_DacOff(void);

/* تست دستی پایه‌ها */
void  Probe_RawWrite(GPIO_TypeDef *port, uint16_t pin, uint8_t high);

#endif

#ifndef USB_CDC_H
#define USB_CDC_H

#include "main.h"
#include <stdint.h>

/*
 * Minimal USB CDC-ACM ("virtual COM port") device on top of the HAL PCD driver.
 * No ST USB middleware / CubeMX USB_DEVICE needed.
 *
 *   PC  <--USB-->  PA11 (DM) / PA12 (DP)   USB_OTG_HS core with its embedded FS PHY
 *
 * Needs: 48 MHz USB clock (PLL3Q), MX_USB_OTG_HS_PCD_Init() already called (HAL_PCD_Init).
 * VBUS sensing is NOT used (the board has no VBUS pin): the device simply connects.
 */

/* Configure FIFOs + interrupt and connect to the host. Call once. */
void USBCDC_Start(void);

/* 1 once the host has enumerated us (SET_CONFIGURATION received). */
uint8_t USBCDC_Configured(void);

/* Number of received bytes waiting. */
uint32_t USBCDC_Available(void);

/* Read exactly len bytes. HAL_TIMEOUT if they do not arrive within timeout_ms. */
HAL_StatusTypeDef USBCDC_Read(uint8_t *buf, uint32_t len, uint32_t timeout_ms);

/* Read one byte if available, else -1 (never blocks). */
int USBCDC_ReadByteNow(void);

/* Send len bytes to the host. HAL_TIMEOUT / HAL_ERROR if the host does not take them. */
HAL_StatusTypeDef USBCDC_Write(const uint8_t *buf, uint32_t len, uint32_t timeout_ms);

/* Throw received data away until the line has been quiet for quiet_ms. */
void USBCDC_Flush(uint32_t quiet_ms);

#endif /* USB_CDC_H */

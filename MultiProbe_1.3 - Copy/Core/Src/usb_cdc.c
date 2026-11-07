/*
 * Minimal USB CDC-ACM device (virtual COM port) on the HAL PCD driver.
 * See usb_cdc.h.  Windows 10/11 binds its built-in "usbser" driver automatically,
 * Linux/macOS use cdc_acm / AppleUSBCDC - no driver install needed.
 *
 * Endpoints:  EP0 control (64 B)
 *             EP1 OUT bulk 64 B   (PC -> board)
 *             EP1 IN  bulk 64 B   (board -> PC)
 *             EP2 IN  interrupt 8 B (CDC notification, never used but required)
 */

#include "usb_cdc.h"
#include <string.h>

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;      /* created in main.c (MX_USB_OTG_HS_PCD_Init) */
#define H (&hpcd_USB_OTG_HS)

#define EP0_MPS        64U
#define EP_OUT         0x01U
#define EP_IN          0x81U
#define EP_NOTIFY      0x82U
#define BULK_MPS       64U
#define NOTIFY_MPS     8U

#define RX_RING_SIZE   16384U                  /* power of two */
#define RX_RING_MASK   (RX_RING_SIZE - 1U)

/* ------------------------------------------------------------------ */
/* Descriptors                                                         */
/* ------------------------------------------------------------------ */

#define USB_VID        0x0483U                 /* STMicroelectronics (same as the ST VCP examples) */
#define USB_PID        0x5740U

static const uint8_t k_dev_desc[18] =
{
    18, 0x01,                 /* bLength, DEVICE */
    0x00, 0x02,               /* bcdUSB 2.00 */
    0x02, 0x00, 0x00,         /* class CDC, subclass, protocol */
    EP0_MPS,                  /* bMaxPacketSize0 */
    USB_VID & 0xFF, USB_VID >> 8,
    USB_PID & 0xFF, USB_PID >> 8,
    0x00, 0x02,               /* bcdDevice 2.00 */
    1, 2, 3,                  /* iManufacturer, iProduct, iSerialNumber */
    1                         /* bNumConfigurations */
};

#define CFG_DESC_LEN   67U
static const uint8_t k_cfg_desc[CFG_DESC_LEN] =
{
    /* configuration */
    9, 0x02, CFG_DESC_LEN, 0x00, 2 /*interfaces*/, 1 /*cfg value*/, 0, 0xC0 /*self powered*/, 50 /*100 mA*/,

    /* interface 0: CDC communication, 1 endpoint (notification) */
    9, 0x04, 0, 0, 1, 0x02, 0x02, 0x01, 0,
    /* CDC header */            5, 0x24, 0x00, 0x10, 0x01,
    /* call management */       5, 0x24, 0x01, 0x00, 0x01,
    /* ACM */                   4, 0x24, 0x02, 0x02,
    /* union master=0 slave=1 */5, 0x24, 0x06, 0x00, 0x01,
    /* EP2 IN interrupt */      7, 0x05, EP_NOTIFY, 0x03, NOTIFY_MPS, 0x00, 16,

    /* interface 1: CDC data, 2 bulk endpoints */
    9, 0x04, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    /* EP1 OUT bulk */          7, 0x05, EP_OUT, 0x02, BULK_MPS, 0x00, 0,
    /* EP1 IN bulk */           7, 0x05, EP_IN,  0x02, BULK_MPS, 0x00, 0
};

static uint8_t  s_strbuf[64];

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

enum { EP0_IDLE = 0, EP0_DATA_IN, EP0_STATUS_OUT, EP0_DATA_OUT, EP0_STATUS_IN };

static volatile uint8_t  s_configured;
static uint8_t           s_cfgValue;
static uint8_t           s_ep0State;
static const uint8_t    *s_c0Ptr;
static uint16_t          s_c0Rem;
static uint8_t           s_c0Zlp;
static uint8_t           s_ctrlBuf[8];

static uint8_t           s_lineCoding[7] = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08 };  /* 115200 8N1 */
static uint8_t           s_pending_ep0_cmd;                                                /* 0x20 = SET_LINE_CODING */

static uint8_t           s_rxPkt[BULK_MPS];
static uint8_t           s_rxRing[RX_RING_SIZE];
static volatile uint32_t s_rxHead;            /* written by the ISR   */
static volatile uint32_t s_rxTail;            /* written by main code */
static volatile uint8_t  s_rxArmed;

static uint8_t           s_txBuf[BULK_MPS];
static volatile uint8_t  s_txBusy;
static volatile uint8_t  s_txZlp;

static uint8_t           s_started;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void rx_arm(void)                        /* ISR or IRQ-disabled context only */
{
    s_rxArmed = 1U;
    (void)HAL_PCD_EP_Receive(H, EP_OUT, s_rxPkt, BULK_MPS);
}

static void rx_rearm_if_needed(void)
{
    if (s_rxArmed || !s_configured)
        return;

    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
    if (!s_rxArmed && s_configured &&
        (RX_RING_SIZE - (s_rxHead - s_rxTail)) >= BULK_MPS)
    {
        rx_arm();
    }
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}

static void ep0_stall(void)
{
    s_ep0State = EP0_IDLE;
    (void)HAL_PCD_EP_SetStall(H, 0x80U);
    (void)HAL_PCD_EP_SetStall(H, 0x00U);
}

static void ep0_status_in(void)                 /* zero-length IN = "request done" */
{
    s_ep0State = EP0_STATUS_IN;
    (void)HAL_PCD_EP_Transmit(H, 0x80U, NULL, 0U);
}

static void ep0_send(const uint8_t *p, uint16_t len, uint16_t wLength)
{
    if (len > wLength)
        len = wLength;

    /* A transfer that is an exact multiple of 64 B but shorter than the host asked for
     * must be closed with a zero-length packet. */
    s_c0Zlp   = (len != 0U && len < wLength && (len % EP0_MPS) == 0U) ? 1U : 0U;
    s_c0Ptr   = p;
    s_c0Rem   = len;
    s_ep0State = EP0_DATA_IN;
    (void)HAL_PCD_EP_Transmit(H, 0x80U, (uint8_t *)(uintptr_t)p, len);
}

/* UTF-16 string descriptor built into s_strbuf, returns its length. */
static uint16_t build_string(uint8_t idx)
{
    char tmp[32];
    const char *s = NULL;

    if (idx == 0U)                              /* LANGID: English (US) */
    {
        s_strbuf[0] = 4U; s_strbuf[1] = 0x03U; s_strbuf[2] = 0x09U; s_strbuf[3] = 0x04U;
        return 4U;
    }
    if      (idx == 1U) s = "MultiProbe";
    else if (idx == 2U) s = "MultiProbe Video Loader";
    else if (idx == 3U)
    {
        static const char hex[] = "0123456789ABCDEF";
        uint32_t w = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
        for (uint32_t i = 0U; i < 8U; i++)
            tmp[i] = hex[(w >> (28U - 4U * i)) & 0xFU];
        tmp[8] = 0;
        s = tmp;
    }
    else
        return 0U;

    uint16_t n = 0U;
    while (s[n] != 0 && n < 30U) n++;
    s_strbuf[0] = (uint8_t)(2U + 2U * n);
    s_strbuf[1] = 0x03U;
    for (uint16_t i = 0U; i < n; i++)
    {
        s_strbuf[2U + 2U * i] = (uint8_t)s[i];
        s_strbuf[3U + 2U * i] = 0U;
    }
    return (uint16_t)(2U + 2U * n);
}

static void set_configuration(uint8_t cfg)
{
    if (cfg == 1U)
    {
        (void)HAL_PCD_EP_Close(H, EP_OUT);
        (void)HAL_PCD_EP_Close(H, EP_IN);
        (void)HAL_PCD_EP_Close(H, EP_NOTIFY);
        (void)HAL_PCD_EP_Open(H, EP_OUT,    BULK_MPS,   EP_TYPE_BULK);
        (void)HAL_PCD_EP_Open(H, EP_IN,     BULK_MPS,   EP_TYPE_BULK);
        (void)HAL_PCD_EP_Open(H, EP_NOTIFY, NOTIFY_MPS, EP_TYPE_INTR);

        s_cfgValue   = 1U;
        s_txBusy     = 0U;
        s_txZlp      = 0U;
        s_configured = 1U;
        rx_arm();
    }
    else
    {
        (void)HAL_PCD_EP_Close(H, EP_OUT);
        (void)HAL_PCD_EP_Close(H, EP_IN);
        (void)HAL_PCD_EP_Close(H, EP_NOTIFY);
        s_cfgValue   = 0U;
        s_configured = 0U;
        s_rxArmed    = 0U;
    }
}

/* ------------------------------------------------------------------ */
/* Control requests (called from the USB interrupt)                    */
/* ------------------------------------------------------------------ */

static void handle_setup(const uint8_t *s)
{
    const uint8_t  bmReq   = s[0];
    const uint8_t  bReq    = s[1];
    const uint16_t wValue  = (uint16_t)(s[2] | (s[3] << 8));
    const uint16_t wIndex  = (uint16_t)(s[4] | (s[5] << 8));
    const uint16_t wLength = (uint16_t)(s[6] | (s[7] << 8));
    const uint8_t  isIn    = (bmReq & 0x80U) ? 1U : 0U;
    const uint8_t  recip   = bmReq & 0x1FU;

    s_ep0State = EP0_IDLE;

    switch (bmReq & 0x60U)
    {
    /* ---------------- standard requests ---------------- */
    case 0x00U:
        if (isIn)
        {
            switch (bReq)
            {
            case 0x00U:                         /* GET_STATUS */
            {
                static uint8_t st[2];
                st[0] = 0U; st[1] = 0U;
                if (recip == 0U)      st[0] = 1U;                    /* self powered */
                else if (recip == 2U) st[0] = 0U;                    /* endpoint: never halted */
                ep0_send(st, 2U, wLength);
                return;
            }
            case 0x06U:                         /* GET_DESCRIPTOR */
            {
                const uint8_t type = (uint8_t)(wValue >> 8);
                const uint8_t idx  = (uint8_t)(wValue & 0xFFU);

                if (type == 0x01U)
                {
                    ep0_send(k_dev_desc, sizeof(k_dev_desc), wLength);
                    return;
                }
                if (type == 0x02U)
                {
                    ep0_send(k_cfg_desc, CFG_DESC_LEN, wLength);
                    return;
                }
                if (type == 0x03U)
                {
                    uint16_t n = build_string(idx);
                    if (n != 0U)
                    {
                        ep0_send(s_strbuf, n, wLength);
                        return;
                    }
                }
                break;                          /* qualifier, BOS, ... -> stall */
            }
            case 0x08U:                         /* GET_CONFIGURATION */
                ep0_send(&s_cfgValue, 1U, wLength);
                return;
            case 0x0AU:                         /* GET_INTERFACE */
            {
                static const uint8_t alt0 = 0U;
                ep0_send(&alt0, 1U, wLength);
                return;
            }
            default:
                break;
            }
        }
        else
        {
            switch (bReq)
            {
            case 0x05U:                         /* SET_ADDRESS */
                (void)HAL_PCD_SetAddress(H, (uint8_t)(wValue & 0x7FU));
                ep0_status_in();
                return;
            case 0x09U:                         /* SET_CONFIGURATION */
                if ((wValue & 0xFFU) <= 1U)
                {
                    set_configuration((uint8_t)(wValue & 0xFFU));
                    ep0_status_in();
                    return;
                }
                break;
            case 0x0BU:                         /* SET_INTERFACE (only alt 0 exists) */
                if (wValue == 0U)
                {
                    ep0_status_in();
                    return;
                }
                break;
            case 0x01U:                         /* CLEAR_FEATURE */
                if (recip == 2U && wValue == 0U)            /* ENDPOINT_HALT */
                {
                    (void)HAL_PCD_EP_ClrStall(H, (uint8_t)wIndex);
                    ep0_status_in();
                    return;
                }
                if (recip == 0U)                            /* DEVICE_REMOTE_WAKEUP: ignore */
                {
                    ep0_status_in();
                    return;
                }
                break;
            case 0x03U:                         /* SET_FEATURE */
                if (recip == 2U && wValue == 0U)
                {
                    (void)HAL_PCD_EP_SetStall(H, (uint8_t)wIndex);
                    ep0_status_in();
                    return;
                }
                break;
            default:
                break;
            }
        }
        break;

    /* ---------------- CDC class requests ---------------- */
    case 0x20U:
        switch (bReq)
        {
        case 0x20U:                             /* SET_LINE_CODING (7 bytes follow) */
            if (!isIn && wLength == 7U)
            {
                s_pending_ep0_cmd = 0x20U;
                s_ep0State = EP0_DATA_OUT;
                (void)HAL_PCD_EP_Receive(H, 0x00U, s_ctrlBuf, 7U);
                return;
            }
            break;
        case 0x21U:                             /* GET_LINE_CODING */
            if (isIn)
            {
                ep0_send(s_lineCoding, 7U, wLength);
                return;
            }
            break;
        case 0x22U:                             /* SET_CONTROL_LINE_STATE (DTR/RTS in wValue) */
        case 0x23U:                             /* SEND_BREAK */
            if (!isIn && wLength == 0U)
            {
                ep0_status_in();
                return;
            }
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }

    ep0_stall();
}

/* ------------------------------------------------------------------ */
/* HAL PCD callbacks (override the weak ones)                          */
/* ------------------------------------------------------------------ */

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
    (void)hpcd;
    s_configured = 0U;
    s_cfgValue   = 0U;
    s_rxArmed    = 0U;
    s_txBusy     = 0U;
    s_txZlp      = 0U;
    s_ep0State   = EP0_IDLE;

    (void)HAL_PCD_EP_Open(H, 0x00U, EP0_MPS, EP_TYPE_CTRL);
    (void)HAL_PCD_EP_Open(H, 0x80U, EP0_MPS, EP_TYPE_CTRL);
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
    handle_setup((const uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    (void)hpcd;

    if (epnum == 0U)
    {
        if (s_ep0State == EP0_DATA_IN)
        {
            uint16_t sent = (s_c0Rem > EP0_MPS) ? EP0_MPS : s_c0Rem;
            s_c0Ptr += sent;
            s_c0Rem  = (uint16_t)(s_c0Rem - sent);

            if (s_c0Rem != 0U)
            {
                (void)HAL_PCD_EP_Transmit(H, 0x80U, (uint8_t *)(uintptr_t)s_c0Ptr, s_c0Rem);
            }
            else if (s_c0Zlp)
            {
                s_c0Zlp = 0U;
                (void)HAL_PCD_EP_Transmit(H, 0x80U, NULL, 0U);
            }
            else
            {
                s_ep0State = EP0_STATUS_OUT;     /* host now sends a zero-length OUT */
                (void)HAL_PCD_EP_Receive(H, 0x00U, NULL, 0U);
            }
        }
        else if (s_ep0State == EP0_STATUS_IN)
        {
            s_ep0State = EP0_IDLE;
        }
    }
    else if (epnum == (EP_IN & 0x7FU))
    {
        if (s_txZlp)
        {
            s_txZlp = 0U;
            (void)HAL_PCD_EP_Transmit(H, EP_IN, NULL, 0U);
        }
        else
        {
            s_txBusy = 0U;
        }
    }
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    (void)hpcd;

    if (epnum == 0U)
    {
        if (s_ep0State == EP0_DATA_OUT)
        {
            if (s_pending_ep0_cmd == 0x20U)
                memcpy(s_lineCoding, s_ctrlBuf, 7U);
            s_pending_ep0_cmd = 0U;
            ep0_status_in();
        }
        else if (s_ep0State == EP0_STATUS_OUT)
        {
            s_ep0State = EP0_IDLE;
        }
    }
    else if (epnum == (EP_OUT & 0x7FU))
    {
        uint32_t n = HAL_PCD_EP_GetRxCount(H, EP_OUT);
        uint32_t head = s_rxHead;

        if (n > BULK_MPS) n = BULK_MPS;
        for (uint32_t i = 0U; i < n; i++)
            s_rxRing[(head + i) & RX_RING_MASK] = s_rxPkt[i];
        s_rxHead = head + n;

        /* Back-pressure: if the ring cannot take another full packet, stay un-armed
         * (the host gets NAKs) until the reader has made room. */
        if ((RX_RING_SIZE - (s_rxHead - s_rxTail)) >= BULK_MPS)
            rx_arm();
        else
            s_rxArmed = 0U;
    }
}

/* ------------------------------------------------------------------ */
/* Interrupt vector                                                    */
/* ------------------------------------------------------------------ */

void OTG_HS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(H);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void USBCDC_Start(void)
{
    if (s_started)
        return;
    s_started = 1U;

    /* FIFO RAM (words): RX shared 128, EP0 TX 64, EP1 TX 64, EP2 TX 16 */
    (void)HAL_PCDEx_SetRxFiFo(H, 0x80U);
    (void)HAL_PCDEx_SetTxFiFo(H, 0U, 0x40U);
    (void)HAL_PCDEx_SetTxFiFo(H, 1U, 0x40U);
    (void)HAL_PCDEx_SetTxFiFo(H, 2U, 0x10U);

    HAL_NVIC_SetPriority(OTG_HS_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

    (void)HAL_PCD_Start(H);                     /* connects the D+ pull-up: the PC sees the device */
}

uint8_t USBCDC_Configured(void)
{
    return s_configured;
}

uint32_t USBCDC_Available(void)
{
    return s_rxHead - s_rxTail;
}

int USBCDC_ReadByteNow(void)
{
    if (s_rxHead == s_rxTail)
        return -1;

    uint8_t b = s_rxRing[s_rxTail & RX_RING_MASK];
    s_rxTail = s_rxTail + 1U;
    rx_rearm_if_needed();
    return b;
}

HAL_StatusTypeDef USBCDC_Read(uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    const uint32_t t0 = HAL_GetTick();
    uint32_t got = 0U;

    while (got < len)
    {
        uint32_t avail = s_rxHead - s_rxTail;

        if (avail != 0U)
        {
            if (avail > (len - got))
                avail = len - got;

            uint32_t tail = s_rxTail;
            for (uint32_t i = 0U; i < avail; i++)
                buf[got + i] = s_rxRing[(tail + i) & RX_RING_MASK];
            s_rxTail = tail + avail;
            got += avail;
            rx_rearm_if_needed();
            continue;
        }

        rx_rearm_if_needed();                   /* in case a reset left us un-armed */
        if ((HAL_GetTick() - t0) >= timeout_ms)
            return HAL_TIMEOUT;
    }
    return HAL_OK;
}

HAL_StatusTypeDef USBCDC_Write(const uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    const uint32_t t0 = HAL_GetTick();

    while (len != 0U)
    {
        while (s_txBusy)
        {
            if (!s_configured)
                return HAL_ERROR;
            if ((HAL_GetTick() - t0) >= timeout_ms)
                return HAL_TIMEOUT;
        }
        if (!s_configured)
            return HAL_ERROR;

        uint32_t n = (len > BULK_MPS) ? BULK_MPS : len;
        memcpy(s_txBuf, buf, n);
        s_txZlp  = (n == BULK_MPS && len == BULK_MPS) ? 1U : 0U;   /* exact multiple at the end */
        s_txBusy = 1U;
        (void)HAL_PCD_EP_Transmit(H, EP_IN, s_txBuf, n);

        buf += n;
        len -= n;
    }

    while (s_txBusy)                            /* wait until the last packet (and ZLP) is gone */
    {
        if (!s_configured)
            return HAL_ERROR;
        if ((HAL_GetTick() - t0) >= timeout_ms)
            return HAL_TIMEOUT;
    }
    return HAL_OK;
}

void USBCDC_Flush(uint32_t quiet_ms)
{
    uint32_t last = HAL_GetTick();

    for (;;)
    {
        if (USBCDC_ReadByteNow() >= 0)
        {
            while (USBCDC_ReadByteNow() >= 0) { }
            last = HAL_GetTick();
        }
        else if ((HAL_GetTick() - last) >= quiet_ms)
        {
            return;
        }
    }
}

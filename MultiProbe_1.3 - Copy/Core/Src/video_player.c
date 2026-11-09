#include "video_player.h"
#include "lcd.h"
#include "w25q64.h"
#include "image_store.h"      /* Image_Crc32() */
#include <string.h>
#include "usb_cdc.h"
#include "keypad.h"
#include "statusbar.h"

extern UART_HandleTypeDef huart4;

/* ------------------------------------------------------------------ */
/* Stored header                                                       */
/* ------------------------------------------------------------------ */

#define VIDEO_MAGIC        0x4456504DUL   /* bytes "MPVD" */
#define VIDEO_VERSION      1U
#define BLOCK_SIZE         4096U          /* = flash sector size */
#define BLOCK_RETRIES      8U

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t width;          /* stored frame size in pixels */
    uint16_t height;
    uint8_t  scale;          /* each stored pixel is drawn scale x scale on the LCD */
    uint8_t  reserved0;
    uint16_t fps_x10;        /* playback speed * 10 */
    uint16_t reserved1;
    uint32_t frame_count;
    uint32_t frame_bytes;    /* width * height * 2 */
    uint32_t data_bytes;     /* frame_count * frame_bytes */
    uint32_t data_crc32;     /* CRC-32 (zlib) of all frame bytes */
    uint32_t header_crc32;   /* CRC-32 of the 32 bytes above */
} VideoHeader;

_Static_assert(sizeof(VideoHeader) == 36U, "VideoHeader must be 36 bytes");

volatile uint32_t g_video_fps_x10 = 0U;

static VideoHeader s_hdr;
static uint8_t     s_hdrValid = 0U;

static uint8_t header_ok(const VideoHeader *h)
{
    if (h->magic != VIDEO_MAGIC || h->version != VIDEO_VERSION)
        return 0U;
    if (h->width == 0U || h->height == 0U || h->scale == 0U)
        return 0U;
    if (((uint32_t)h->width  * h->scale) > LCD_WIDTH ||
        ((uint32_t)h->height * h->scale) > LCD_HEIGHT)
        return 0U;
    if (h->fps_x10 < 10U || h->fps_x10 > 600U)
        return 0U;
    if (h->frame_count == 0U ||
        h->frame_bytes != ((uint32_t)h->width * h->height * 2U) ||
        h->data_bytes  != (h->frame_count * h->frame_bytes) ||
        h->data_bytes  >  VIDEO_MAX_BYTES)
        return 0U;
    if (h->header_crc32 != Image_Crc32(0U, (const uint8_t *)h, sizeof(VideoHeader) - 4U))
        return 0U;
    return 1U;
}

/* ------------------------------------------------------------------ */
/* UART upload                                                         */
/*                                                                     */
/* PC -> MCU                       MCU -> PC                           */
/*   "MPVUPLD1" (repeated)           'R'  ready                        */
/*   VideoHeader (36 B)              'K' ok / 'E' rejected             */
/*   per 4096-byte block:                                              */
/*     4096 B data + CRC32 (4 B LE)  'K' stored / 'N' resend / 'E' err */
/*   (after the last block)          'D' done + CRC ok / 'F' CRC bad   */
/* ------------------------------------------------------------------ */

static uint8_t s_block[BLOCK_SIZE + 4U];
static uint8_t s_usbUploadReq = 0U;      /* the PC sent the handshake while a clip was playing */
static volatile uint8_t s_exitReq = 0U;      /* user pressed BACK */
static uint8_t s_booted = 0U;

#if VIDEO_TRANSPORT_USB
/* ---- USB CDC (virtual COM port) transport ------------------------- */

static void tp_send_byte(uint8_t c)
{
    (void)USBCDC_Write(&c, 1U, 200U);
}

/* Throw away whatever is in flight until the line has been quiet for ~20 ms. */
static void tp_drain(void)
{
    USBCDC_Flush(20U);
}

static HAL_StatusTypeDef tp_receive(uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    return USBCDC_Read(buf, len, timeout_ms);
}

/* Non-blocking: run the pending bytes through an 8-byte window.
 * Returns 1 as soon as the handshake "MPVUPLD1" has been seen. */
static uint8_t tp_poll_magic(void)
{
    static const uint8_t magic[8] = { 'M', 'P', 'V', 'U', 'P', 'L', 'D', '1' };
    static uint8_t win[8];
    int b;

    while ((b = USBCDC_ReadByteNow()) >= 0)
    {
        memmove(win, win + 1, 7U);
        win[7] = (uint8_t)b;
        if (memcmp(win, magic, 8U) == 0)
        {
            memset(win, 0, sizeof(win));
            return 1U;
        }
    }
    return 0U;
}

/* Boot window:
 *   - no PC enumerated us within VIDEO_USB_ENUM_WAIT_MS -> do not wait, start playing;
 *   - PC present -> wait window_ms for the handshake. (If it comes later, the play loop
 *     notices it too, so no reset is needed.) */
static uint8_t wait_for_magic(uint32_t window_ms)
{
    USBCDC_Start();

    uint32_t t0 = HAL_GetTick();
    while (!USBCDC_Configured())
    {
        if ((HAL_GetTick() - t0) >= VIDEO_USB_ENUM_WAIT_MS)
            return 0U;
    }

    t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < window_ms)
    {
        if (tp_poll_magic())
            return 1U;
    }
    return 0U;
}

#else
/* ---- UART4 transport ------------------------------------------------ */

static void uart_set_baud(uint32_t baud)
{
    huart4.Init.BaudRate = baud;
    (void)HAL_UART_Init(&huart4);
}

static void tp_send_byte(uint8_t c)
{
    (void)HAL_UART_Transmit(&huart4, &c, 1U, 100U);
}

static void tp_drain(void)
{
    uint8_t b;
    __HAL_UART_CLEAR_OREFLAG(&huart4);
    while (HAL_UART_Receive(&huart4, &b, 1U, 20U) == HAL_OK)
    {
    }
    __HAL_UART_CLEAR_OREFLAG(&huart4);
}

static HAL_StatusTypeDef tp_receive(uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    return HAL_UART_Receive(&huart4, buf, (uint16_t)len, timeout_ms);
}

static uint8_t tp_poll_magic(void)
{
    return 0U;                               /* UART: only during the boot window */
}

static uint8_t wait_for_magic(uint32_t window_ms)
{
    static const uint8_t magic[8] = { 'M', 'P', 'V', 'U', 'P', 'L', 'D', '1' };
    uint8_t  win[8] = {0};
    uint8_t  b;
    uint32_t t0 = HAL_GetTick();

    __HAL_UART_CLEAR_OREFLAG(&huart4);

    while ((HAL_GetTick() - t0) < window_ms)
    {
        __HAL_UART_CLEAR_FLAG(&huart4, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
        if (HAL_UART_Receive(&huart4, &b, 1U, 2U) == HAL_OK)
        {
            memmove(win, win + 1, 7U);
            win[7] = b;
            if (memcmp(win, magic, 8U) == 0)
                return 1U;
        }
    }
    return 0U;
}
#endif /* VIDEO_TRANSPORT_USB */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static HAL_StatusTypeDef store_block(uint32_t index)
{
    const uint32_t addr = VIDEO_DATA_ADDR + (index * BLOCK_SIZE);

    if (W25Q64_SectorErase(addr) != HAL_OK)
        return HAL_ERROR;

    for (uint32_t off = 0U; off < BLOCK_SIZE; off += 256U)
    {
        if (W25Q64_PageProgram(addr + off, &s_block[off], 256U) != HAL_OK)
            return HAL_ERROR;
    }
    return HAL_OK;
}

static uint8_t verify_flash_crc(const VideoHeader *h)
{
    uint32_t crc = 0U;

    for (uint32_t off = 0U; off < h->data_bytes; off += BLOCK_SIZE)
    {
        uint32_t len = h->data_bytes - off;
        if (len > BLOCK_SIZE)
            len = BLOCK_SIZE;

        if (W25Q64_Read(VIDEO_DATA_ADDR + off, s_block, len) != HAL_OK)
            return 0U;
        crc = Image_Crc32(crc, s_block, len);
    }
    return (crc == h->data_crc32) ? 1U : 0U;
}

static VideoUploadResult do_upload(void)
{
    VideoHeader hdr;

    /* Tell the PC we are ready, THEN do the slow things (LCD fill, drain):
     * the PC waits ~300 ms after seeing 'R' before it sends the header. */
    tp_send_byte('R');
    LCD_FillColor(LCD_COLOR_BLUE);          /* blue = receiving */
    tp_drain();

    if (tp_receive((uint8_t *)&hdr, sizeof(hdr), 3000U) != HAL_OK ||
        !header_ok(&hdr))
    {
        tp_send_byte('E');
        return VIDEO_UPLOAD_FAILED;
    }

    /* Invalidate the old clip first so a half-written one is never played. */
    s_hdrValid = 0U;
    if (W25Q64_SectorErase(VIDEO_FLASH_ADDR) != HAL_OK)
    {
        tp_send_byte('E');
        return VIDEO_UPLOAD_FAILED;
    }
    tp_send_byte('K');

    const uint32_t blocks = (hdr.data_bytes + BLOCK_SIZE - 1U) / BLOCK_SIZE;

    for (uint32_t i = 0U; i < blocks; i++)
    {
        uint32_t tries = 0U;

        for (;;)
        {
            if (tp_receive(s_block, BLOCK_SIZE + 4U, 2000U) == HAL_OK &&
                Image_Crc32(0U, s_block, BLOCK_SIZE) == rd32(&s_block[BLOCK_SIZE]))
            {
                break;                       /* good block */
            }

            if (++tries > BLOCK_RETRIES)
            {
                tp_send_byte('E');
                return VIDEO_UPLOAD_FAILED;
            }
            tp_drain();
            tp_send_byte('N');             /* ask the PC to resend */
        }

        if (store_block(i) != HAL_OK)
        {
            tp_send_byte('E');
            return VIDEO_UPLOAD_FAILED;
        }
        tp_send_byte('K');
    }

    if (!verify_flash_crc(&hdr))
    {
        tp_send_byte('F');
        return VIDEO_UPLOAD_FAILED;
    }

    /* Header goes in last: the clip only becomes valid when everything is stored. */
    if (W25Q64_PageProgram(VIDEO_FLASH_ADDR, (const uint8_t *)&hdr, sizeof(hdr)) != HAL_OK)
    {
        tp_send_byte('E');
        return VIDEO_UPLOAD_FAILED;
    }

    tp_send_byte('D');
    return VIDEO_UPLOAD_OK;
}

VideoUploadResult Video_UploadListen(uint32_t window_ms)
{
#if !VIDEO_TRANSPORT_USB
    uart_set_baud(VIDEO_UPLOAD_BAUD);
#endif

    if (!wait_for_magic(window_ms))
        return VIDEO_NO_UPLOAD;

    VideoUploadResult r = do_upload();

    LCD_FillColor((r == VIDEO_UPLOAD_OK) ? LCD_COLOR_GREEN : LCD_COLOR_RED);
    HAL_Delay(700);
    return r;
}

/* ------------------------------------------------------------------ */
/* ST-LINK (SWD) upload - no UART adapter needed                       */
/*                                                                     */
/* tools/swd_upload.py writes into this RAM mailbox through the        */
/* debugger (STM32_Programmer_CLI, hot-plug, CPU keeps running) and    */
/* the firmware stores the data in the W25Q64.                         */
/*                                                                     */
/*   0x24080000  mailbox (SwdMailbox)                                  */
/*   0x24081000  data buffer, 256 KB (one chunk = up to 64 blocks)     */
/*                                                                     */
/* Commands (PC writes cmd/offset/length/crc, then seq LAST):          */
/*   BEGIN  : header in mailbox -> validate, erase header sector       */
/*   CHUNK  : buffer -> flash at VIDEO_DATA_ADDR + offset (CRC checked)*/
/*   FINISH : verify whole-clip CRC, write header, start playing       */
/* The firmware answers with status, then ack_seq = seq.               */
/* ------------------------------------------------------------------ */

#ifndef SWD_MB_ADDR
#define SWD_MB_ADDR    0x24080000UL
#endif
#ifndef SWD_BUF_ADDR
#define SWD_BUF_ADDR   0x24081000UL
#endif
#define SWD_BUF_SIZE   0x00040000UL
#define SWD_FW_MAGIC   0x3158424DUL      /* "MBX1" */

enum { SWD_CMD_BEGIN = 1, SWD_CMD_CHUNK = 2, SWD_CMD_FINISH = 3 };
enum { SWD_ST_OK = 0, SWD_ST_BAD_HEADER = 1, SWD_ST_FLASH = 2, SWD_ST_BAD_ARG = 3,
       SWD_ST_CRC = 4, SWD_ST_VERIFY = 5, SWD_ST_NO_BEGIN = 6, SWD_ST_BAD_CMD = 7 };

typedef struct
{
    volatile uint32_t fw_magic;    /* 0x00 FW  : set when the mailbox is ready            */
    volatile uint32_t cmd;         /* 0x04 PC                                             */
    volatile uint32_t offset;      /* 0x08 PC  : byte offset inside the clip data         */
    volatile uint32_t length;      /* 0x0C PC  : chunk length (multiple of 4096)          */
    volatile uint32_t crc;         /* 0x10 PC  : CRC-32 of the chunk                      */
    volatile uint32_t seq;         /* 0x14 PC  : written LAST = "command is complete"     */
    volatile uint32_t ack_seq;     /* 0x18 FW  : = seq when the command has been handled  */
    volatile uint32_t status;      /* 0x1C FW  : SWD_ST_*                                 */
    volatile uint32_t state;       /* 0x20 FW  : 0 = idle/playing, 1 = receiving          */
    uint32_t          reserved[7]; /* 0x24..0x3F                                          */
    volatile uint8_t  header[36];  /* 0x40 PC  : VideoHeader for BEGIN                    */
} SwdMailbox;

_Static_assert(__builtin_offsetof(SwdMailbox, header) == 0x40U, "SwdMailbox layout");

#define SWD_MB   ((SwdMailbox *)(uintptr_t)(SWD_MB_ADDR))

extern uint8_t _ebss;                  /* end of .bss (linker script) */

static VideoHeader s_swdHdr;
static uint8_t     s_swdHaveHdr = 0U;
static uint8_t     s_swdReload  = 0U;  /* a complete new clip is in flash -> (re)load it */

static void swd_init(void)
{
    SwdMailbox *mb = SWD_MB;

    /* The mailbox must lie above everything the linker placed in RAM. */
    if ((uintptr_t)&_ebss > (uintptr_t)SWD_MB_ADDR)
        return;

    mb->fw_magic = 0U;
    mb->cmd      = 0U;
    mb->seq      = 0U;
    mb->ack_seq  = 0U;
    mb->status   = 0U;
    mb->state    = 0U;
    __DMB();
    mb->fw_magic = SWD_FW_MAGIC;
}

static HAL_StatusTypeDef flash_store_block(uint32_t addr, const uint8_t *src)
{
    if (W25Q64_SectorErase(addr) != HAL_OK)
        return HAL_ERROR;

    for (uint32_t off = 0U; off < BLOCK_SIZE; off += 256U)
    {
        if (W25Q64_PageProgram(addr + off, &src[off], 256U) != HAL_OK)
            return HAL_ERROR;
    }
    return HAL_OK;
}

/* Handle one pending command. Returns 1 if the stored clip changed
 * (playback must stop / be reloaded), 0 otherwise. */
static uint8_t swd_service(void)
{
    SwdMailbox *mb = SWD_MB;
    const uint32_t seq = mb->seq;

    if (mb->fw_magic != SWD_FW_MAGIC || seq == mb->ack_seq)
        return 0U;

    __DMB();
    const uint32_t cmd = mb->cmd;
    const uint32_t off = mb->offset;
    const uint32_t len = mb->length;
    const uint32_t crc = mb->crc;

    uint32_t st   = SWD_ST_OK;
    uint8_t  stop = 0U;

    switch (cmd)
    {
    case SWD_CMD_BEGIN:
    {
        VideoHeader h;
        for (uint32_t i = 0U; i < sizeof(h); i++)
            ((uint8_t *)&h)[i] = mb->header[i];

        if (!header_ok(&h))
        {
            st = SWD_ST_BAD_HEADER;
            break;
        }

        s_hdrValid   = 0U;               /* old clip is going away */
        s_swdHaveHdr = 0U;
        stop         = 1U;
        LCD_FillColor(LCD_COLOR_BLUE);   /* blue = receiving */
        mb->state    = 1U;

        if (W25Q64_SectorErase(VIDEO_FLASH_ADDR) != HAL_OK)
        {
            st = SWD_ST_FLASH;
            break;
        }
        s_swdHdr     = h;
        s_swdHaveHdr = 1U;
        break;
    }

    case SWD_CMD_CHUNK:
    {
        if (!s_swdHaveHdr)
        {
            st = SWD_ST_NO_BEGIN;
            break;
        }

        const uint32_t total = ((s_swdHdr.data_bytes + BLOCK_SIZE - 1U) / BLOCK_SIZE) * BLOCK_SIZE;

        if (len == 0U || len > SWD_BUF_SIZE || (len % BLOCK_SIZE) != 0U ||
            (off % BLOCK_SIZE) != 0U || off > total || len > (total - off))
        {
            st = SWD_ST_BAD_ARG;
            break;
        }

        if (Image_Crc32(0U, (const uint8_t *)(uintptr_t)SWD_BUF_ADDR, len) != crc)
        {
            st = SWD_ST_CRC;             /* damaged in RAM: the PC simply resends */
            break;
        }

        for (uint32_t b = 0U; b < (len / BLOCK_SIZE); b++)
        {
            if (flash_store_block(VIDEO_DATA_ADDR + off + (b * BLOCK_SIZE),
                                  (const uint8_t *)(uintptr_t)(SWD_BUF_ADDR + (b * BLOCK_SIZE))) != HAL_OK)
            {
                st = SWD_ST_FLASH;
                LCD_FillColor(LCD_COLOR_RED);
                break;
            }
        }
        break;
    }

    case SWD_CMD_FINISH:
    {
        if (!s_swdHaveHdr)
        {
            st = SWD_ST_NO_BEGIN;
            break;
        }

        if (!verify_flash_crc(&s_swdHdr))
        {
            st = SWD_ST_VERIFY;
            LCD_FillColor(LCD_COLOR_RED);
            break;
        }

        /* Header last: the clip only becomes valid when everything is stored. */
        if (W25Q64_PageProgram(VIDEO_FLASH_ADDR, (const uint8_t *)&s_swdHdr, sizeof(s_swdHdr)) != HAL_OK)
        {
            st = SWD_ST_FLASH;
            LCD_FillColor(LCD_COLOR_RED);
            break;
        }

        LCD_FillColor(LCD_COLOR_GREEN);  /* green = stored + verified */
        HAL_Delay(700);
        s_swdHaveHdr = 0U;
        s_swdReload  = 1U;
        mb->state    = 0U;
        stop         = 1U;
        break;
    }

    default:
        st = SWD_ST_BAD_CMD;
        break;
    }

    mb->status = st;
    __DMB();
    mb->ack_seq = seq;                   /* answer: the PC polls this word */
    return stop;
}

/* ------------------------------------------------------------------ */
/* Playback                                                            */
/* ------------------------------------------------------------------ */

static uint8_t s_frame[LCD_WIDTH * LCD_HEIGHT * 2U];

HAL_StatusTypeDef Video_Load(void)
{
    VideoHeader h;

    s_hdrValid = 0U;

    if (W25Q64_Read(VIDEO_FLASH_ADDR, (uint8_t *)&h, sizeof(h)) != HAL_OK)
        return HAL_ERROR;
    if (!header_ok(&h))
        return HAL_ERROR;

    s_hdr      = h;
    s_hdrValid = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef Video_PlayLoop(void)
{
    if (!s_hdrValid)
        return HAL_ERROR;

    /* Black background (matters when the clip is smaller than the screen). */
    LCD_FillColor(LCD_COLOR_BLACK);

    /* Free-running cycle counter used for frame pacing. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    const uint32_t period = (uint32_t)(((uint64_t)SystemCoreClock * 10U) / s_hdr.fps_x10);
    uint32_t next         = DWT->CYCCNT;

    uint32_t fps_t0     = HAL_GetTick();
    uint32_t fps_frames = 0U;

    for (;;)
    {
        for (uint32_t i = 0U; i < s_hdr.frame_count; i++)
        {
            if (W25Q64_Read(VIDEO_DATA_ADDR + (i * s_hdr.frame_bytes),
                            s_frame, s_hdr.frame_bytes) != HAL_OK)
            {
                return HAL_ERROR;
            }

            LCD_DrawBitmap(s_frame, s_hdr.width, s_hdr.height, s_hdr.scale);

            if (swd_service())               /* new upload started -> stop playing */
                return HAL_OK;

            if (tp_poll_magic())             /* PC started a USB upload -> stop playing */
            {
                s_usbUploadReq = 1U;
                return HAL_OK;
            }

            if (Keypad_Poll() == MENU_INPUT_BACK)   /* BACK -> back to the menu */
            {
                s_exitReq = 1U;
                return HAL_OK;
            }

            /* Pace to the clip's frame rate. If we are slower than that, don't wait. */
            next += period;
            if ((int32_t)(DWT->CYCCNT - next) >= 0)
            {
                next = DWT->CYCCNT;
            }
            else
            {
                while ((int32_t)(DWT->CYCCNT - next) < 0)
                {
                }
            }

            fps_frames++;
            uint32_t now = HAL_GetTick();
            if ((now - fps_t0) >= 1000U)
            {
                g_video_fps_x10 = (fps_frames * 10000U) / (now - fps_t0);
                fps_t0     = now;
                fps_frames = 0U;
            }
        }
    }
}

static void video_show_no_clip(void)
{
    LCD_FillColor(LCD_COLOR_BLACK);
    LCD_DrawString(40U, 100U, "NO VIDEO", LCD_COLOR_WHITE, LCD_COLOR_BLACK, 2U);
    LCD_DrawString(30U, 130U, "send it from the PC", LCD_COLOR_YELLOW, LCD_COLOR_BLACK, 1U);
    LCD_DrawString(30U, 146U, "BACK = menu", LCD_COLOR_YELLOW, LCD_COLOR_BLACK, 1U);
}

static void video_loop(void)
{
    for (;;)
    {
        s_swdReload    = 0U;
        s_usbUploadReq = 0U;

        if (Video_Load() == HAL_OK)
        {
            if (Video_PlayLoop() != HAL_OK)
                LCD_FillColor(LCD_COLOR_CYAN);   /* flash read error while playing */
        }
        else
        {
            video_show_no_clip();
        }

        if (s_exitReq) { s_exitReq = 0U; return; }

        /* Idle: keep answering the PC until a complete new clip has arrived (or BACK). */
        while (!s_swdReload && !s_usbUploadReq && !s_exitReq)
        {
            (void)swd_service();
            if (tp_poll_magic())
                s_usbUploadReq = 1U;
            if (Keypad_Poll() == MENU_INPUT_BACK)
                s_exitReq = 1U;
            HAL_Delay(2);
        }

        if (s_exitReq) { s_exitReq = 0U; return; }

        if (s_usbUploadReq)                  /* PC sent "MPVUPLD1" over USB */
        {
            s_usbUploadReq = 0U;
            VideoUploadResult r = do_upload();
            LCD_FillColor((r == VIDEO_UPLOAD_OK) ? LCD_COLOR_GREEN : LCD_COLOR_RED);
            HAL_Delay(700);
        }
    }
}

void Video_Boot(void)
{
    swd_init();                              /* mailbox is usable from now on */
    s_booted = 1U;
    (void)Video_UploadListen(VIDEO_UPLOAD_WINDOW_MS);
}

void Video_Menu(void)
{
    if (!s_booted)
    {
        swd_init();
        s_booted = 1U;
    }
    s_exitReq = 0U;
    video_loop();
}

void Video_Run(void)
{
    Video_Boot();
    video_loop();
}

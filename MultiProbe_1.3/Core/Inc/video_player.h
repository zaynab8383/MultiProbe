//#ifndef VIDEO_PLAYER_H
//#define VIDEO_PLAYER_H
//
//#include "main.h"
//#include "w25q64.h"
//#include <stdint.h>
//
///*
// * Video (GIF) player for the 240x240 ST7789 LCD.
// *
// * The clip is stored in the external W25Q64 flash:
// *
// *   0x00200000            header sector (36-byte VideoHeader, rest is 0xFF)
// *   0x00201000 ...        frames, back to back, RGB565 big-endian (hi, lo)
// *
// * (the logo lives at 0x00100000, so nothing overlaps)
// *
// * The clip is loaded into the flash from the PC over UART4 (PA0 = TX, PA1 = RX)
// * with tools/video_upload.py. For VIDEO_UPLOAD_WINDOW_MS after the logo is
// * shown the board listens for the PC; if nothing comes it just plays the clip.
// */
//
//#define VIDEO_FLASH_ADDR        0x00200000U
//#define VIDEO_DATA_ADDR         (VIDEO_FLASH_ADDR + 0x1000U)
//#define VIDEO_MAX_BYTES         (W25Q64_CAPACITY_BYTES - VIDEO_DATA_ADDR)   /* 0x5FF000 */
//
//#define VIDEO_UPLOAD_WINDOW_MS  15000U     /* how long the logo is shown while waiting for the PC */
////#define VIDEO_UPLOAD_BAUD       921600U
//#define VIDEO_UPLOAD_BAUD       115200U
//typedef enum
//{
//    VIDEO_NO_UPLOAD     = 0,   /* nobody talked to us during the window */
//    VIDEO_UPLOAD_OK     = 1,
//    VIDEO_UPLOAD_FAILED = 2
//} VideoUploadResult;
//
///* Measured playback speed, FPS * 10 (e.g. 150 = 15.0 fps). Watch it in the debugger. */
//extern volatile uint32_t g_video_fps_x10;
//
///* Wait for an upload from the PC (window_ms), and store the clip if one arrives. */
//VideoUploadResult Video_UploadListen(uint32_t window_ms);
//
///* Read + validate the header stored in flash. HAL_OK if a valid clip is present. */
//HAL_StatusTypeDef Video_Load(void);
//
///* Play the stored clip in an endless loop. Returns only on a flash read error / no clip. */
//HAL_StatusTypeDef Video_PlayLoop(void);
//
///* Upload window, then play. Returns only if there is no (valid) clip or on error. */
//void Video_Run(void);
//
//#endif /* VIDEO_PLAYER_H */
#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include "main.h"
#include "w25q64.h"
#include <stdint.h>

/*
 * Video (GIF) player for the 240x240 ST7789 LCD.
 *
 * The clip is stored in the external W25Q64 flash:
 *
 *   0x00200000            header sector (36-byte VideoHeader, rest is 0xFF)
 *   0x00201000 ...        frames, back to back, RGB565 big-endian (hi, lo)
 *
 * (the logo lives at 0x00100000, so nothing overlaps)
 *
 * The clip is loaded into the flash from the PC in one of two ways:
 *   - over the ST-LINK (SWD), no extra hardware:  tools/swd_upload.py
 *       (works any time, also while the clip is playing)
 *   - over UART4 (PA0 = TX, PA1 = RX):             tools/video_upload.py
 *       (only during VIDEO_UPLOAD_WINDOW_MS after boot)
 */

#define VIDEO_FLASH_ADDR        0x00200000U
#define VIDEO_DATA_ADDR         (VIDEO_FLASH_ADDR + 0x1000U)
#define VIDEO_MAX_BYTES         (W25Q64_CAPACITY_BYTES - VIDEO_DATA_ADDR)   /* 0x5FF000 */

#define VIDEO_UPLOAD_WINDOW_MS  1000U     /* UART upload window after boot (the SWD upload needs no window) */
#define VIDEO_UPLOAD_BAUD       115200U

typedef enum
{
    VIDEO_NO_UPLOAD     = 0,   /* nobody talked to us during the window */
    VIDEO_UPLOAD_OK     = 1,
    VIDEO_UPLOAD_FAILED = 2
} VideoUploadResult;

/* Measured playback speed, FPS * 10 (e.g. 150 = 15.0 fps). Watch it in the debugger. */
extern volatile uint32_t g_video_fps_x10;

/* Wait for an upload from the PC (window_ms), and store the clip if one arrives. */
VideoUploadResult Video_UploadListen(uint32_t window_ms);

/* Read + validate the header stored in flash. HAL_OK if a valid clip is present. */
HAL_StatusTypeDef Video_Load(void);

/* Play the stored clip in an endless loop. Returns only on a flash read error / no clip. */
HAL_StatusTypeDef Video_PlayLoop(void);

/* Upload window, then play. NEVER returns: it keeps answering SWD uploads. */
void Video_Run(void);

#endif /* VIDEO_PLAYER_H */

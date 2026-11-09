#ifndef PAGES_ASSETS_H
#define PAGES_ASSETS_H
#include <stdint.h>

#define PAGE_ICON_COUNT   10U
#define PAGE_TITLE_COUNT  11U      /* 0..9 = the ten pages, 10 = "coming soon" */
#define PAGE_ICON_W       96U
#define PAGE_ICON_H       96U
#define TITLE_COMING_SOON 10U

typedef struct { const uint8_t *rle; uint16_t len; } PageIcon_t;   /* 4-bit palette index, RLE */
typedef struct { const uint8_t *bits; uint8_t w; uint8_t h; } PageTitle_t;

/* palette[0..2] are filled by the caller from the page theme (bg, accent, dark) */
extern const uint16_t    g_iconPalette[16];
extern const PageIcon_t  g_pageIcons[PAGE_ICON_COUNT];
extern const PageTitle_t g_pageTitles[PAGE_TITLE_COUNT];

#endif

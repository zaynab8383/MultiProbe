#ifndef PAGES_ASSETS_H
#define PAGES_ASSETS_H
#include <stdint.h>

#define PAGE_ICON_COUNT   9U
#define PAGE_TITLE_COUNT  10U      /* 0..8 = the nine pages, 9 = "coming soon" */
#define PAGE_ICON_W       96U
#define PAGE_ICON_H       96U
#define TITLE_COMING_SOON 9U

typedef struct { const uint8_t *bits; } PageIcon_t;
typedef struct { const uint8_t *bits; uint8_t w; uint8_t h; } PageTitle_t;

extern const PageIcon_t  g_pageIcons[PAGE_ICON_COUNT];
extern const PageTitle_t g_pageTitles[PAGE_TITLE_COUNT];

#endif

/*
 * qspi_ll.h - register-level (NO HAL, NO SysTick) driver for the W25Q64JV on OCTOSPI1.
 * Used by:  Core/Src/xip_boot.c  (runs from internal flash right after reset, maps the
 *                                  external flash at 0x90000000 so the whole firmware runs from it)
 *           Loader/loader.c      (STM32CubeProgrammer / CubeIDE external loader)
 *
 * Wiring (single W25Q64 = IC1): IO0 PD11, IO1 PD12, IO2 PE2, IO3 PD13 (AF9), CLK PB2 (AF9), NCS PB6 (AF10).
 * Every function is static so it is compiled into whichever object includes this file.
 */
#ifndef QSPI_LL_H
#define QSPI_LL_H

#include "stm32h7xx.h"
#include <stdint.h>

#define QLL_MAP_BASE     0x90000000UL
#define QLL_SIZE_BYTES   (8UL * 1024UL * 1024UL)
#define QLL_PRESCALER    3U          /* OCTOSPI clock = HCLK / 8 = 35 MHz at HCLK 280 (safe sampling without delay-block tuning) */
#define QLL_DEVSIZE_REG  22U         /* 2^(22+1) = 8 MB */
#define QLL_CSHT_REG     3U          /* CS high time = 4 clocks */
#define QLL_SPIN_LIMIT   30000000UL

static void qll_set_af(GPIO_TypeDef *g, uint32_t pin, uint32_t af, uint32_t pull)
{
    g->MODER   = (g->MODER   & ~(3UL << (pin * 2U))) | (2UL << (pin * 2U));
    g->OSPEEDR |= (3UL << (pin * 2U));
    g->PUPDR   = (g->PUPDR   & ~(3UL << (pin * 2U))) | (pull << (pin * 2U));
    g->AFR[pin >> 3] = (g->AFR[pin >> 3] & ~(0xFUL << ((pin & 7U) * 4U))) | (af << ((pin & 7U) * 4U));
}

static int qll_wait_not_busy(void)
{
    uint32_t t = QLL_SPIN_LIMIT;
    while (OCTOSPI1->SR & OCTOSPI_SR_BUSY) { if (--t == 0U) return 0; }
    return 1;
}

/* Clocks + pins + OCTOSPI1 in indirect mode (also safe to call while memory-mapped mode is active). */
static int qll_hw_init(void)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIODEN | RCC_AHB4ENR_GPIOEEN;
    RCC->AHB3ENR |= RCC_AHB3ENR_OSPI1EN | RCC_AHB3ENR_IOMNGREN;
    (void)RCC->AHB3ENR;

    qll_set_af(GPIOD, 11U, 9U, 0U);
    qll_set_af(GPIOD, 12U, 9U, 0U);
    qll_set_af(GPIOD, 13U, 9U, 0U);
    qll_set_af(GPIOE,  2U, 9U, 0U);
    qll_set_af(GPIOB,  2U, 9U, 0U);
    qll_set_af(GPIOB,  6U, 10U, 1U);           /* NCS: pull-up */

    if (OCTOSPI1->CR & OCTOSPI_CR_EN)
    {
        OCTOSPI1->CR |= OCTOSPI_CR_ABORT;      /* leaves memory-mapped mode */
        uint32_t t = QLL_SPIN_LIMIT;
        while (OCTOSPI1->CR & OCTOSPI_CR_ABORT) { if (--t == 0U) return 0; }
        if (!qll_wait_not_busy()) return 0;
        OCTOSPI1->CR &= ~OCTOSPI_CR_EN;
    }

    /*
     * Route OCTOSPI1 to OSPIM Port 1:
     *   CLK  -> Port 1
     *   NCS  -> Port 1
     *   IO[3:0] -> Port 1 low
     *
     * CubeMX normally does this through HAL_OSPIM_Config(). This project
     * deliberately uses a register-level boot driver, so configure the
     * IO manager here before enabling OCTOSPI1. Without this mapping the
     * peripheral may be configured but the external W25Q64 is not routed.
     */
    OCTOSPIM->CR &= ~OCTOSPIM_CR_MUXEN;
    OCTOSPIM->PCR[0] = OCTOSPIM_PCR_CLKEN
                     | OCTOSPIM_PCR_NCSEN
                     | OCTOSPIM_PCR_IOLEN;
    __DSB();
    __ISB();

    OCTOSPI1->DCR1 = (QLL_DEVSIZE_REG << OCTOSPI_DCR1_DEVSIZE_Pos) | (QLL_CSHT_REG << OCTOSPI_DCR1_CSHT_Pos);
    OCTOSPI1->DCR2 = QLL_PRESCALER;
    OCTOSPI1->DCR3 = 0U;
    OCTOSPI1->DCR4 = 0U;
    OCTOSPI1->CR   = OCTOSPI_CR_EN;            /* FMODE = indirect write, FTHRES = 1 byte */
    return 1;
}

/* One indirect transaction on 1 line. addr<0 : no address phase. tx/rx : data (n bytes). */
static int qll_cmd(uint8_t ins, int32_t addr, const uint8_t *tx, uint8_t *rx, uint32_t n)
{
    if (!qll_wait_not_busy()) return 0;
    OCTOSPI1->FCR = OCTOSPI_FCR_CTCF;

    uint32_t ccr = OCTOSPI_CCR_IMODE_0;
    if (addr >= 0) ccr |= OCTOSPI_CCR_ADMODE_0 | (2UL << OCTOSPI_CCR_ADSIZE_Pos);   /* 24-bit address */
    if (n)         ccr |= OCTOSPI_CCR_DMODE_0;

    OCTOSPI1->CR  = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE) | (rx ? OCTOSPI_CR_FMODE_0 : 0UL);
    OCTOSPI1->TCR = 0U;
    OCTOSPI1->CCR = ccr;
    if (n) OCTOSPI1->DLR = n - 1U;
    OCTOSPI1->IR = ins;
    if (addr >= 0) OCTOSPI1->AR = (uint32_t)addr;

    for (uint32_t i = 0U; i < n; i++)
    {
        uint32_t t = QLL_SPIN_LIMIT;
        while (!(OCTOSPI1->SR & OCTOSPI_SR_FTF)) { if (--t == 0U) return 0; }
        if (rx) rx[i] = *(volatile uint8_t *)&OCTOSPI1->DR;
        else    *(volatile uint8_t *)&OCTOSPI1->DR = tx[i];
    }
    uint32_t t = QLL_SPIN_LIMIT;
    while (!(OCTOSPI1->SR & OCTOSPI_SR_TCF)) { if (--t == 0U) return 0; }
    OCTOSPI1->FCR = OCTOSPI_FCR_CTCF;
    return 1;
}

static int qll_wait_ready(uint32_t spin)
{
    uint8_t sr = 0xFFU;
    while (spin--)
    {
        if (!qll_cmd(0x05U, -1, 0, &sr, 1U)) return 0;
        if (!(sr & 1U)) return 1;
    }
    return 0;
}

static int qll_write_enable(void) { return qll_cmd(0x06U, -1, 0, 0, 0U); }

/* Make sure the Quad-Enable bit (SR2 bit 1) is set; it is non-volatile so this is a no-op after the first time. */
static int qll_ensure_qe(void)
{
    uint8_t sr2 = 0U;
    if (!qll_cmd(0x35U, -1, 0, &sr2, 1U)) return 0;
    if (sr2 & 0x02U) return 1;
    sr2 |= 0x02U;
    if (!qll_write_enable()) return 0;
    if (!qll_cmd(0x31U, -1, &sr2, 0, 1U)) return 0;
    return qll_wait_ready(QLL_SPIN_LIMIT);
}

/* Fast Read Quad I/O (0xEB): 1-line instruction, 4-line address (24 bit), 4-line mode byte, 4 dummy, 4-line data. */
static int qll_enter_mmap(void)
{
    if (!qll_wait_not_busy()) return 0;
    OCTOSPI1->CR  = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE) | OCTOSPI_CR_FMODE_0;   /* indirect read while configuring */
    OCTOSPI1->CCR = OCTOSPI_CCR_IMODE_0
                  | (3UL << OCTOSPI_CCR_ADMODE_Pos) | (2UL << OCTOSPI_CCR_ADSIZE_Pos)
                  | (3UL << OCTOSPI_CCR_ABMODE_Pos)                      /* 8-bit alternate byte (ABSIZE = 0) */
                  | (3UL << OCTOSPI_CCR_DMODE_Pos);
    OCTOSPI1->TCR = (4UL << OCTOSPI_TCR_DCYC_Pos);
    OCTOSPI1->ABR = 0xFFU;                                                /* != 0xAx : no continuous-read mode */
    OCTOSPI1->IR  = 0xEBU;
    OCTOSPI1->CR |= OCTOSPI_CR_FMODE;                                     /* FMODE = 11 : memory-mapped */
    return 1;
}

#endif /* QSPI_LL_H */

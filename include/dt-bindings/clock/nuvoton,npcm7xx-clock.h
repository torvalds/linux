/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Nuvoton NPCM7xx Clock Generator binding
 * clock binding number for all clocks supported by nuvoton,npcm7xx-clk
 *
 * Copyright (C) 2018 Nuvoton Technologies tali.perry@nuvoton.com
 *
 */

#ifndef __DT_BINDINGS_CLOCK_NPCM7XX_H
#define __DT_BINDINGS_CLOCK_NPCM7XX_H


#define NPCM7XX_CLK_CPU 0
#define NPCM7XX_CLK_GFX_PIXEL 1
#define NPCM7XX_CLK_MC 2
#define NPCM7XX_CLK_ADC 3
#define NPCM7XX_CLK_AHB 4
#define NPCM7XX_CLK_TIMER 5
#define NPCM7XX_CLK_UART 6
#define NPCM7XX_CLK_MMC  7
#define NPCM7XX_CLK_SPI3 8
#define NPCM7XX_CLK_PCI  9
#define NPCM7XX_CLK_AXI 10
#define NPCM7XX_CLK_APB4 11
#define NPCM7XX_CLK_APB3 12
#define NPCM7XX_CLK_APB2 13
#define NPCM7XX_CLK_APB1 14
#define NPCM7XX_CLK_APB5 15
#define NPCM7XX_CLK_CLKOUT 16
#define NPCM7XX_CLK_GFX  17
#define NPCM7XX_CLK_SU   18
#define NPCM7XX_CLK_SU48 19
#define NPCM7XX_CLK_SDHC 20
#define NPCM7XX_CLK_SPI0 21
#define NPCM7XX_CLK_SPIX 22

#define NPCM7XX_CLK_REFCLK 23
#define NPCM7XX_CLK_SYSBYPCK 24
#define NPCM7XX_CLK_MCBYPCK 25

#define NPCM7XX_NUM_CLOCKS	 (NPCM7XX_CLK_MCBYPCK+1)

#endif

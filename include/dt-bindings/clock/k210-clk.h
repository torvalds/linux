/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019-20 Sean Anderson <seanga2@gmail.com>
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */
#ifndef CLOCK_K210_CLK_H
#define CLOCK_K210_CLK_H

/*
 * Kendryte K210 SoC clock identifiers (arbitrary values).
 */
#define K210_CLK_CPU	0
#define K210_CLK_SRAM0	1
#define K210_CLK_SRAM1	2
#define K210_CLK_AI	3
#define K210_CLK_DMA	4
#define K210_CLK_FFT	5
#define K210_CLK_ROM	6
#define K210_CLK_DVP	7
#define K210_CLK_APB0	8
#define K210_CLK_APB1	9
#define K210_CLK_APB2	10
#define K210_CLK_I2S0	11
#define K210_CLK_I2S1	12
#define K210_CLK_I2S2	13
#define K210_CLK_I2S0_M	14
#define K210_CLK_I2S1_M	15
#define K210_CLK_I2S2_M	16
#define K210_CLK_WDT0	17
#define K210_CLK_WDT1	18
#define K210_CLK_SPI0	19
#define K210_CLK_SPI1	20
#define K210_CLK_SPI2	21
#define K210_CLK_I2C0	22
#define K210_CLK_I2C1	23
#define K210_CLK_I2C2	24
#define K210_CLK_SPI3	25
#define K210_CLK_TIMER0	26
#define K210_CLK_TIMER1	27
#define K210_CLK_TIMER2	28
#define K210_CLK_GPIO	29
#define K210_CLK_UART1	30
#define K210_CLK_UART2	31
#define K210_CLK_UART3	32
#define K210_CLK_FPIOA	33
#define K210_CLK_SHA	34
#define K210_CLK_AES	35
#define K210_CLK_OTP	36
#define K210_CLK_RTC	37

#define K210_NUM_CLKS	38

#endif /* CLOCK_K210_CLK_H */

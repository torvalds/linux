/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019 Sean Anderson <seanga2@gmail.com>
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */
#ifndef RESET_K210_SYSCTL_H
#define RESET_K210_SYSCTL_H

/*
 * Kendryte K210 SoC system controller K210_SYSCTL_SOFT_RESET register bits.
 * Taken from Kendryte SDK (kendryte-standalone-sdk).
 */
#define K210_RST_ROM	0
#define K210_RST_DMA	1
#define K210_RST_AI	2
#define K210_RST_DVP	3
#define K210_RST_FFT	4
#define K210_RST_GPIO	5
#define K210_RST_SPI0	6
#define K210_RST_SPI1	7
#define K210_RST_SPI2	8
#define K210_RST_SPI3	9
#define K210_RST_I2S0	10
#define K210_RST_I2S1	11
#define K210_RST_I2S2	12
#define K210_RST_I2C0	13
#define K210_RST_I2C1	14
#define K210_RST_I2C2	15
#define K210_RST_UART1	16
#define K210_RST_UART2	17
#define K210_RST_UART3	18
#define K210_RST_AES	19
#define K210_RST_FPIOA	20
#define K210_RST_TIMER0	21
#define K210_RST_TIMER1	22
#define K210_RST_TIMER2	23
#define K210_RST_WDT0	24
#define K210_RST_WDT1	25
#define K210_RST_SHA	26
#define K210_RST_RTC	29

#endif /* RESET_K210_SYSCTL_H */

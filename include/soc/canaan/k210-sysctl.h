/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019-20 Sean Anderson <seanga2@gmail.com>
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */
#ifndef K210_SYSCTL_H
#define K210_SYSCTL_H

/*
 * Kendryte K210 SoC system controller registers offsets.
 * Taken from Kendryte SDK (kendryte-standalone-sdk).
 */
#define K210_SYSCTL_GIT_ID	0x00 /* Git short commit id */
#define K210_SYSCTL_UART_BAUD	0x04 /* Default UARTHS baud rate */
#define K210_SYSCTL_PLL0	0x08 /* PLL0 controller */
#define K210_SYSCTL_PLL1	0x0C /* PLL1 controller */
#define K210_SYSCTL_PLL2	0x10 /* PLL2 controller */
#define K210_SYSCTL_PLL_LOCK	0x18 /* PLL lock tester */
#define K210_SYSCTL_ROM_ERROR	0x1C /* AXI ROM detector */
#define K210_SYSCTL_SEL0	0x20 /* Clock select controller 0 */
#define K210_SYSCTL_SEL1	0x24 /* Clock select controller 1 */
#define K210_SYSCTL_EN_CENT	0x28 /* Central clock enable */
#define K210_SYSCTL_EN_PERI	0x2C /* Peripheral clock enable */
#define K210_SYSCTL_SOFT_RESET	0x30 /* Soft reset ctrl */
#define K210_SYSCTL_PERI_RESET	0x34 /* Peripheral reset controller */
#define K210_SYSCTL_THR0	0x38 /* Clock threshold controller 0 */
#define K210_SYSCTL_THR1	0x3C /* Clock threshold controller 1 */
#define K210_SYSCTL_THR2	0x40 /* Clock threshold controller 2 */
#define K210_SYSCTL_THR3	0x44 /* Clock threshold controller 3 */
#define K210_SYSCTL_THR4	0x48 /* Clock threshold controller 4 */
#define K210_SYSCTL_THR5	0x4C /* Clock threshold controller 5 */
#define K210_SYSCTL_THR6	0x50 /* Clock threshold controller 6 */
#define K210_SYSCTL_MISC	0x54 /* Miscellaneous controller */
#define K210_SYSCTL_PERI	0x58 /* Peripheral controller */
#define K210_SYSCTL_SPI_SLEEP	0x5C /* SPI sleep controller */
#define K210_SYSCTL_RESET_STAT	0x60 /* Reset source status */
#define K210_SYSCTL_DMA_SEL0	0x64 /* DMA handshake selector 0 */
#define K210_SYSCTL_DMA_SEL1	0x68 /* DMA handshake selector 1 */
#define K210_SYSCTL_POWER_SEL	0x6C /* IO Power Mode Select controller */

void k210_clk_early_init(void __iomem *regs);

#endif

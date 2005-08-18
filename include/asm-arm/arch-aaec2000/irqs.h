/*
 *  linux/include/asm-arm/arch-aaec2000/irqs.h
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H


#define INT_GPIOF0_FIQ	0  /* External GPIO Port F O Fast Interrupt Input */
#define INT_BL_FIQ	1  /* Battery Low Fast Interrupt */
#define INT_WE_FIQ	2  /* Watchdog Expired Fast Interrupt */
#define INT_MV_FIQ	3  /* Media Changed Interrupt */
#define INT_SC		4  /* Sound Codec Interrupt */
#define INT_GPIO1	5  /* GPIO Port F Configurable Int 1 */
#define INT_GPIO2	6  /* GPIO Port F Configurable Int 2 */
#define INT_GPIO3	7  /* GPIO Port F Configurable Int 3 */
#define INT_TMR1_OFL	8  /* Timer 1 Overflow Interrupt */
#define INT_TMR2_OFL	9  /* Timer 2 Overflow Interrupt */
#define INT_RTC_CM	10 /* RTC Compare Match Interrupt */
#define INT_TICK	11 /* 64Hz Tick Interrupt */
#define INT_UART1	12 /* UART1 Interrupt */
#define INT_UART2	13 /* UART2 & Modem State Changed Interrupt */
#define INT_LCD		14 /* LCD Interrupt */
#define INT_SSI		15 /* SSI End of Transfer Interrupt */
#define INT_UART3	16 /* UART3 Interrupt */
#define INT_SCI		17 /* SCI Interrupt */
#define INT_AAC		18 /* Advanced Audio Codec Interrupt */
#define INT_MMC		19 /* MMC Interrupt */
#define INT_USB		20 /* USB Interrupt */
#define INT_DMA		21 /* DMA Interrupt */
#define INT_TMR3_UOFL	22 /* Timer 3 Underflow Interrupt */
#define INT_GPIO4	23 /* GPIO Port F Configurable Int 4 */
#define INT_GPIO5	24 /* GPIO Port F Configurable Int 4 */
#define INT_GPIO6	25 /* GPIO Port F Configurable Int 4 */
#define INT_GPIO7	26 /* GPIO Port F Configurable Int 4 */
#define INT_BMI		27 /* BMI Interrupt */

#define NR_IRQS		(INT_BMI + 1)

#endif /* __ASM_ARCH_IRQS_H */

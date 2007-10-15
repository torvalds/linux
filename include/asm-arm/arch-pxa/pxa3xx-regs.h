/*
 * linux/include/asm-arm/arch-pxa/pxa3xx-regs.h
 *
 * PXA3xx specific register definitions
 *
 * Copyright (C) 2007 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_PXA3XX_REGS_H
#define __ASM_ARCH_PXA3XX_REGS_H

/*
 * Application Subsystem Clock
 */
#define ACCR		__REG(0x41340000)	/* Application Subsystem Clock Configuration Register */
#define ACSR		__REG(0x41340004)	/* Application Subsystem Clock Status Register */
#define AICSR		__REG(0x41340008)	/* Application Subsystem Interrupt Control/Status Register */
#define CKENA		__REG(0x4134000C)	/* A Clock Enable Register */
#define CKENB		__REG(0x41340010)	/* B Clock Enable Register */
#define AC97_DIV	__REG(0x41340014)	/* AC97 clock divisor value register */

/*
 * Clock Enable Bit
 */
#define CKEN_LCD	1	/* < LCD Clock Enable */
#define CKEN_USBH	2	/* < USB host clock enable */
#define CKEN_CAMERA	3	/* < Camera interface clock enable */
#define CKEN_NAND	4	/* < NAND Flash Controller Clock Enable */
#define CKEN_USB2	6	/* < USB 2.0 client clock enable. */
#define CKEN_DMC	8	/* < Dynamic Memory Controller clock enable */
#define CKEN_SMC	9	/* < Static Memory Controller clock enable */
#define CKEN_ISC	10	/* < Internal SRAM Controller clock enable */
#define CKEN_BOOT	11	/* < Boot rom clock enable */
#define CKEN_MMC1	12	/* < MMC1 Clock enable */
#define CKEN_MMC2	13	/* < MMC2 clock enable */
#define CKEN_KEYPAD	14	/* < Keypand Controller Clock Enable */
#define CKEN_CIR	15	/* < Consumer IR Clock Enable */
#define CKEN_USIM0	17	/* < USIM[0] Clock Enable */
#define CKEN_USIM1	18	/* < USIM[1] Clock Enable */
#define CKEN_TPM	19	/* < TPM clock enable */
#define CKEN_UDC	20	/* < UDC clock enable */
#define CKEN_BTUART	21	/* < BTUART clock enable */
#define CKEN_FFUART	22	/* < FFUART clock enable */
#define CKEN_STUART	23	/* < STUART clock enable */
#define CKEN_AC97	24	/* < AC97 clock enable */
#define CKEN_TOUCH	25	/* < Touch screen Interface Clock Enable */
#define CKEN_SSP1	26	/* < SSP1 clock enable */
#define CKEN_SSP2	27	/* < SSP2 clock enable */
#define CKEN_SSP3	28	/* < SSP3 clock enable */
#define CKEN_SSP4	29	/* < SSP4 clock enable */
#define CKEN_MSL0	30	/* < MSL0 clock enable */
#define CKEN_PWM0	32	/* < PWM[0] clock enable */
#define CKEN_PWM1	33	/* < PWM[1] clock enable */
#define CKEN_I2C	36	/* < I2C clock enable */
#define CKEN_INTC	38	/* < Interrupt controller clock enable */
#define CKEN_GPIO	39	/* < GPIO clock enable */
#define CKEN_1WIRE	40	/* < 1-wire clock enable */
#define CKEN_HSIO2	41	/* < HSIO2 clock enable */
#define CKEN_MINI_IM	48	/* < Mini-IM */
#define CKEN_MINI_LCD	49	/* < Mini LCD */

#if defined(CONFIG_CPU_PXA310)
#define CKEN_MMC3	5	/* < MMC3 Clock Enable */
#define CKEN_MVED	43	/* < MVED clock enable */
#endif

/* Note: GCU clock enable bit differs on PXA300/PXA310 and PXA320 */
#define PXA300_CKEN_GRAPHICS	42	/* Graphics controller clock enable */
#define PXA320_CKEN_GRAPHICS	7	/* Graphics controller clock enable */

#endif /* __ASM_ARCH_PXA3XX_REGS_H */

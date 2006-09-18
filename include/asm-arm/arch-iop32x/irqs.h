/*
 * linux/include/asm-arm/arch-iop32x/irqs.h
 *
 * Author:	Rory Bolt <rorybolt@pacbell.net>
 * Copyright:	(C) 2002 Rory Bolt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef _IRQS_H_
#define _IRQS_H_

/*
 * IOP80321 chipset interrupts
 */
#define IRQ_IOP321_DMA0_EOT	0
#define IRQ_IOP321_DMA0_EOC	1
#define IRQ_IOP321_DMA1_EOT	2
#define IRQ_IOP321_DMA1_EOC	3
#define IRQ_IOP321_AA_EOT	6
#define IRQ_IOP321_AA_EOC	7
#define IRQ_IOP321_CORE_PMON	8
#define IRQ_IOP321_TIMER0	9
#define IRQ_IOP321_TIMER1	10
#define IRQ_IOP321_I2C_0	11
#define IRQ_IOP321_I2C_1	12
#define IRQ_IOP321_MESSAGING	13
#define IRQ_IOP321_ATU_BIST	14
#define IRQ_IOP321_PERFMON	15
#define IRQ_IOP321_CORE_PMU	16
#define IRQ_IOP321_BIU_ERR	17
#define IRQ_IOP321_ATU_ERR	18
#define IRQ_IOP321_MCU_ERR	19
#define IRQ_IOP321_DMA0_ERR	20
#define IRQ_IOP321_DMA1_ERR	21
#define IRQ_IOP321_AA_ERR	23
#define IRQ_IOP321_MSG_ERR	24
#define IRQ_IOP321_SSP		25
#define IRQ_IOP321_XINT0	27
#define IRQ_IOP321_XINT1	28
#define IRQ_IOP321_XINT2	29
#define IRQ_IOP321_XINT3	30
#define IRQ_IOP321_HPI		31

#define NR_IRQS			32


#endif // _IRQ_H_

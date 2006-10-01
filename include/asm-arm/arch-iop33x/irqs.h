/*
 * include/asm-arm/arch-iop33x/irqs.h
 *
 * Author:	Dave Jiang (dave.jiang@intel.com)
 * Copyright:	(C) 2003 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __IRQS_H
#define __IRQS_H

/*
 * IOP80331 chipset interrupts
 */
#define IRQ_IOP33X_DMA0_EOT	0
#define IRQ_IOP33X_DMA0_EOC	1
#define IRQ_IOP33X_DMA1_EOT	2
#define IRQ_IOP33X_DMA1_EOC	3
#define IRQ_IOP33X_AA_EOT	6
#define IRQ_IOP33X_AA_EOC	7
#define IRQ_IOP33X_TIMER0	8
#define IRQ_IOP33X_TIMER1	9
#define IRQ_IOP33X_I2C_0	10
#define IRQ_IOP33X_I2C_1	11
#define IRQ_IOP33X_MSG		12
#define IRQ_IOP33X_MSGIBQ	13
#define IRQ_IOP33X_ATU_BIST	14
#define IRQ_IOP33X_PERFMON	15
#define IRQ_IOP33X_CORE_PMU	16
#define IRQ_IOP33X_XINT0	24
#define IRQ_IOP33X_XINT1	25
#define IRQ_IOP33X_XINT2	26
#define IRQ_IOP33X_XINT3	27
#define IRQ_IOP33X_XINT8	32
#define IRQ_IOP33X_XINT9	33
#define IRQ_IOP33X_XINT10	34
#define IRQ_IOP33X_XINT11	35
#define IRQ_IOP33X_XINT12	36
#define IRQ_IOP33X_XINT13	37
#define IRQ_IOP33X_XINT14	38
#define IRQ_IOP33X_XINT15	39
#define IRQ_IOP33X_UART0	51
#define IRQ_IOP33X_UART1	52
#define IRQ_IOP33X_PBIE		53
#define IRQ_IOP33X_ATU_CRW	54
#define IRQ_IOP33X_ATU_ERR	55
#define IRQ_IOP33X_MCU_ERR	56
#define IRQ_IOP33X_DMA0_ERR	57
#define IRQ_IOP33X_DMA1_ERR	58
#define IRQ_IOP33X_AA_ERR	60
#define IRQ_IOP33X_MSG_ERR	62
#define IRQ_IOP33X_HPI		63

#define NR_IRQS			64


#endif

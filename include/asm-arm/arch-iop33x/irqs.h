/*
 * linux/include/asm-arm/arch-iop33x/irqs.h
 *
 * Author:	Dave Jiang (dave.jiang@intel.com)
 * Copyright:	(C) 2003 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef _IRQS_H_
#define _IRQS_H_

/*
 * IOP80331 chipset interrupts
 */
#define IRQ_IOP331_DMA0_EOT	0
#define IRQ_IOP331_DMA0_EOC	1
#define IRQ_IOP331_DMA1_EOT	2
#define IRQ_IOP331_DMA1_EOC	3
#define IRQ_IOP331_AA_EOT	6
#define IRQ_IOP331_AA_EOC	7
#define IRQ_IOP331_TIMER0	8
#define IRQ_IOP331_TIMER1	9
#define IRQ_IOP331_I2C_0	10
#define IRQ_IOP331_I2C_1	11
#define IRQ_IOP331_MSG		12
#define IRQ_IOP331_MSGIBQ	13
#define IRQ_IOP331_ATU_BIST	14
#define IRQ_IOP331_PERFMON	15
#define IRQ_IOP331_CORE_PMU	16
#define IRQ_IOP331_XINT0	24
#define IRQ_IOP331_XINT1	25
#define IRQ_IOP331_XINT2	26
#define IRQ_IOP331_XINT3	27
#define IRQ_IOP331_XINT8	32
#define IRQ_IOP331_XINT9	33
#define IRQ_IOP331_XINT10	34
#define IRQ_IOP331_XINT11	35
#define IRQ_IOP331_XINT12	36
#define IRQ_IOP331_XINT13	37
#define IRQ_IOP331_XINT14	38
#define IRQ_IOP331_XINT15	39
#define IRQ_IOP331_UART0	51
#define IRQ_IOP331_UART1	52
#define IRQ_IOP331_PBIE		53
#define IRQ_IOP331_ATU_CRW	54
#define IRQ_IOP331_ATU_ERR	55
#define IRQ_IOP331_MCU_ERR	56
#define IRQ_IOP331_DMA0_ERR	57
#define IRQ_IOP331_DMA1_ERR	58
#define IRQ_IOP331_AA_ERR	60
#define IRQ_IOP331_MSG_ERR	62
#define IRQ_IOP331_HPI		63

#define NR_IRQS			64


/*
 * Interrupts available on the IQ80331 board
 */

/*
 * On board devices
 */
#define	IRQ_IQ80331_I82544	IRQ_IOP331_XINT0
#define IRQ_IQ80331_UART0	IRQ_IOP331_UART0
#define IRQ_IQ80331_UART1	IRQ_IOP331_UART1

/*
 * PCI interrupts
 */
#define	IRQ_IQ80331_INTA	IRQ_IOP331_XINT0
#define	IRQ_IQ80331_INTB	IRQ_IOP331_XINT1
#define	IRQ_IQ80331_INTC	IRQ_IOP331_XINT2
#define	IRQ_IQ80331_INTD	IRQ_IOP331_XINT3

/*
 * Interrupts available on the IQ80332 board
 */

/*
 * On board devices
 */
#define	IRQ_IQ80332_I82544	IRQ_IOP331_XINT0
#define IRQ_IQ80332_UART0	IRQ_IOP331_UART0
#define IRQ_IQ80332_UART1	IRQ_IOP331_UART1

/*
 * PCI interrupts
 */
#define	IRQ_IQ80332_INTA	IRQ_IOP331_XINT0
#define	IRQ_IQ80332_INTB	IRQ_IOP331_XINT1
#define	IRQ_IQ80332_INTC	IRQ_IOP331_XINT2
#define	IRQ_IQ80332_INTD	IRQ_IOP331_XINT3

#endif // _IRQ_H_

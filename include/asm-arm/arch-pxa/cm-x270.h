/*
 * linux/include/asm/arch-pxa/cm-x270.h
 *
 * Copyright Compulab Ltd., 2003, 2007
 * Mike Rapoport <mike@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


/* CM-x270 device physical addresses */
#define CMX270_CS1_PHYS		(PXA_CS1_PHYS)
#define MARATHON_PHYS		(PXA_CS2_PHYS)
#define CMX270_IDE104_PHYS	(PXA_CS3_PHYS)
#define CMX270_IT8152_PHYS	(PXA_CS4_PHYS)

/* Statically mapped regions */
#define CMX270_VIRT_BASE		(0xe8000000)
#define CMX270_IT8152_VIRT		(CMX270_VIRT_BASE)
#define CMX270_IDE104_VIRT		(CMX270_IT8152_VIRT + SZ_64M)

/* GPIO related definitions */
#define GPIO_IT8152_IRQ			(22)

#define IRQ_GPIO_IT8152_IRQ	IRQ_GPIO(GPIO_IT8152_IRQ)
#define PME_IRQ			IRQ_GPIO(0)
#define CMX270_IDE_IRQ		IRQ_GPIO(100)
#define CMX270_GPIRQ1		IRQ_GPIO(101)
#define CMX270_TOUCHIRQ		IRQ_GPIO(96)
#define CMX270_ETHIRQ		IRQ_GPIO(10)
#define CMX270_GFXIRQ		IRQ_GPIO(95)
#define CMX270_NANDIRQ		IRQ_GPIO(89)
#define CMX270_MMC_IRQ		IRQ_GPIO(83)

/* PCMCIA related definitions */
#define PCC_DETECT(x)	(GPLR(84 - (x)) & GPIO_bit(84 - (x)))
#define PCC_READY(x)	(GPLR(82 - (x)) & GPIO_bit(82 - (x)))

#define PCMCIA_S0_CD_VALID		IRQ_GPIO(84)
#define PCMCIA_S0_CD_VALID_EDGE		GPIO_BOTH_EDGES

#define PCMCIA_S1_CD_VALID		IRQ_GPIO(83)
#define PCMCIA_S1_CD_VALID_EDGE		GPIO_BOTH_EDGES

#define PCMCIA_S0_RDYINT		IRQ_GPIO(82)
#define PCMCIA_S1_RDYINT		IRQ_GPIO(81)

#define PCMCIA_RESET_GPIO		53

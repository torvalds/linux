/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASM_GT64120_MOMENCO_OCELOT_GT64120_DEP_H
#define _ASM_GT64120_MOMENCO_OCELOT_GT64120_DEP_H

/*
 * PCI address allocation
 */
#define GT_PCI_MEM_BASE	(0x22000000UL)
#define GT_PCI_MEM_SIZE	GT_DEF_PCI0_MEM0_SIZE
#define GT_PCI_IO_BASE	(0x20000000UL)
#define GT_PCI_IO_SIZE	GT_DEF_PCI0_IO_SIZE

extern unsigned long gt64120_base;

#define GT64120_BASE	(gt64120_base)

/*
 * GT timer irq
 */
#define	GT_TIMER		6

#endif  /* _ASM_GT64120_MOMENCO_OCELOT_GT64120_DEP_H */

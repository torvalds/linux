/*
 * Copyright (C) 2000, 2001, 2002, 2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _ASM_SIBYTE_SB1250_H
#define _ASM_SIBYTE_SB1250_H

/*
 * yymmddpp: year, month, day, patch.
 * should sync with Makefile EXTRAVERSION
 */
#define SIBYTE_RELEASE 0x02111403

#define SB1250_NR_IRQS 64

#define BCM1480_NR_IRQS                 128
#define BCM1480_NR_IRQS_HALF            64

#define SB1250_DUART_MINOR_BASE		64

#ifndef __ASSEMBLY__

#include <asm/addrspace.h>

/* For revision/pass information */
#include <asm/sibyte/sb1250_scd.h>
#include <asm/sibyte/bcm1480_scd.h>
extern unsigned int sb1_pass;
extern unsigned int soc_pass;
extern unsigned int soc_type;
extern unsigned int periph_rev;
extern unsigned int zbbus_mhz;

extern void sb1250_time_init(void);
extern void sb1250_mask_irq(int cpu, int irq);
extern void sb1250_unmask_irq(int cpu, int irq);
extern void sb1250_smp_finish(void);

extern void bcm1480_time_init(void);
extern void bcm1480_mask_irq(int cpu, int irq);
extern void bcm1480_unmask_irq(int cpu, int irq);
extern void bcm1480_smp_finish(void);

#define AT_spin \
	__asm__ __volatile__ (		\
		".set noat\n"		\
		"li $at, 0\n"		\
		"1: beqz $at, 1b\n"	\
		".set at\n"		\
		)

#endif

#define IOADDR(a) ((void __iomem *)(IO_BASE + (a)))

#endif

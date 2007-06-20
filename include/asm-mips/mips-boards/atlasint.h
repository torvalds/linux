/*
 * Copyright (C) 1999, 2006  MIPS Technologies, Inc.  All rights reserved.
 *	Authors: Carsten Langgaard <carstenl@mips.com>
 *		 Maciej W. Rozycki <macro@mips.com>
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Defines for the Atlas interrupt controller.
 *
 */
#ifndef _MIPS_ATLASINT_H
#define _MIPS_ATLASINT_H

#include <irq.h>

/* CPU interrupt offsets */
#define MIPSCPU_INT_SW0		0
#define MIPSCPU_INT_SW1		1
#define MIPSCPU_INT_MB0		2
#define MIPSCPU_INT_ATLAS	MIPSCPU_INT_MB0
#define MIPSCPU_INT_MB1		3
#define MIPSCPU_INT_MB2		4
#define MIPSCPU_INT_MB3		5
#define MIPSCPU_INT_MB4		6

/*
 * Interrupts 8..39 are used for Atlas interrupt controller interrupts
 */
#define ATLAS_INT_BASE		8
#define ATLAS_INT_UART		(ATLAS_INT_BASE + 0)
#define ATLAS_INT_TIM0		(ATLAS_INT_BASE + 1)
#define ATLAS_INT_RES2		(ATLAS_INT_BASE + 2)
#define ATLAS_INT_RES3		(ATLAS_INT_BASE + 3)
#define ATLAS_INT_RTC		(ATLAS_INT_BASE + 4)
#define ATLAS_INT_COREHI	(ATLAS_INT_BASE + 5)
#define ATLAS_INT_CORELO	(ATLAS_INT_BASE + 6)
#define ATLAS_INT_RES7		(ATLAS_INT_BASE + 7)
#define ATLAS_INT_PCIA		(ATLAS_INT_BASE + 8)
#define ATLAS_INT_PCIB		(ATLAS_INT_BASE + 9)
#define ATLAS_INT_PCIC		(ATLAS_INT_BASE + 10)
#define ATLAS_INT_PCID		(ATLAS_INT_BASE + 11)
#define ATLAS_INT_ENUM		(ATLAS_INT_BASE + 12)
#define ATLAS_INT_DEG		(ATLAS_INT_BASE + 13)
#define ATLAS_INT_ATXFAIL	(ATLAS_INT_BASE + 14)
#define ATLAS_INT_INTA		(ATLAS_INT_BASE + 15)
#define ATLAS_INT_INTB		(ATLAS_INT_BASE + 16)
#define ATLAS_INT_ETH		ATLAS_INT_INTB
#define ATLAS_INT_INTC		(ATLAS_INT_BASE + 17)
#define ATLAS_INT_SCSI		ATLAS_INT_INTC
#define ATLAS_INT_INTD		(ATLAS_INT_BASE + 18)
#define ATLAS_INT_SERR		(ATLAS_INT_BASE + 19)
#define ATLAS_INT_RES20		(ATLAS_INT_BASE + 20)
#define ATLAS_INT_RES21		(ATLAS_INT_BASE + 21)
#define ATLAS_INT_RES22		(ATLAS_INT_BASE + 22)
#define ATLAS_INT_RES23		(ATLAS_INT_BASE + 23)
#define ATLAS_INT_RES24		(ATLAS_INT_BASE + 24)
#define ATLAS_INT_RES25		(ATLAS_INT_BASE + 25)
#define ATLAS_INT_RES26		(ATLAS_INT_BASE + 26)
#define ATLAS_INT_RES27		(ATLAS_INT_BASE + 27)
#define ATLAS_INT_RES28		(ATLAS_INT_BASE + 28)
#define ATLAS_INT_RES29		(ATLAS_INT_BASE + 29)
#define ATLAS_INT_RES30		(ATLAS_INT_BASE + 30)
#define ATLAS_INT_RES31		(ATLAS_INT_BASE + 31)
#define ATLAS_INT_END		(ATLAS_INT_BASE + 31)

/*
 * Interrupts 64..127 are used for Soc-it Classic interrupts
 */
#define MSC01C_INT_BASE		64

/* SOC-it Classic interrupt offsets */
#define MSC01C_INT_TMR		0
#define MSC01C_INT_PCI		1

/*
 * Interrupts 64..127 are used for Soc-it EIC interrupts
 */
#define MSC01E_INT_BASE		64

/* SOC-it EIC interrupt offsets */
#define	MSC01E_INT_SW0		1
#define	MSC01E_INT_SW1		2
#define	MSC01E_INT_MB0		3
#define	MSC01E_INT_ATLAS	MSC01E_INT_MB0
#define	MSC01E_INT_MB1		4
#define	MSC01E_INT_MB2		5
#define	MSC01E_INT_MB3		6
#define	MSC01E_INT_MB4		7
#define	MSC01E_INT_TMR		8
#define	MSC01E_INT_PCI		9
#define	MSC01E_INT_PERFCTR	10
#define	MSC01E_INT_CPUCTR	11

#endif /* !(_MIPS_ATLASINT_H) */

/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999 MIPS Technologies, Inc.  All rights reserved.
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

#define ATLASINT_BASE		1
#define ATLASINT_UART		(ATLASINT_BASE+0)
#define ATLASINT_TIM0		(ATLASINT_BASE+1)
#define ATLASINT_RES2		(ATLASINT_BASE+2)
#define ATLASINT_RES3		(ATLASINT_BASE+3)
#define ATLASINT_RTC		(ATLASINT_BASE+4)
#define ATLASINT_COREHI		(ATLASINT_BASE+5)
#define ATLASINT_CORELO		(ATLASINT_BASE+6)
#define ATLASINT_RES7		(ATLASINT_BASE+7)
#define ATLASINT_PCIA		(ATLASINT_BASE+8)
#define ATLASINT_PCIB		(ATLASINT_BASE+9)
#define ATLASINT_PCIC		(ATLASINT_BASE+10)
#define ATLASINT_PCID		(ATLASINT_BASE+11)
#define ATLASINT_ENUM		(ATLASINT_BASE+12)
#define ATLASINT_DEG		(ATLASINT_BASE+13)
#define ATLASINT_ATXFAIL	(ATLASINT_BASE+14)
#define ATLASINT_INTA		(ATLASINT_BASE+15)
#define ATLASINT_INTB		(ATLASINT_BASE+16)
#define ATLASINT_ETH		ATLASINT_INTB
#define ATLASINT_INTC		(ATLASINT_BASE+17)
#define ATLASINT_SCSI		ATLASINT_INTC
#define ATLASINT_INTD		(ATLASINT_BASE+18)
#define ATLASINT_SERR		(ATLASINT_BASE+19)
#define ATLASINT_RES20		(ATLASINT_BASE+20)
#define ATLASINT_RES21		(ATLASINT_BASE+21)
#define ATLASINT_RES22		(ATLASINT_BASE+22)
#define ATLASINT_RES23		(ATLASINT_BASE+23)
#define ATLASINT_RES24		(ATLASINT_BASE+24)
#define ATLASINT_RES25		(ATLASINT_BASE+25)
#define ATLASINT_RES26		(ATLASINT_BASE+26)
#define ATLASINT_RES27		(ATLASINT_BASE+27)
#define ATLASINT_RES28		(ATLASINT_BASE+28)
#define ATLASINT_RES29		(ATLASINT_BASE+29)
#define ATLASINT_RES30		(ATLASINT_BASE+30)
#define ATLASINT_RES31		(ATLASINT_BASE+31)
#define ATLASINT_END		(ATLASINT_BASE+31)

/*
 * Atlas registers are memory mapped on 64-bit aligned boundaries and
 * only word access are allowed.
 */
struct atlas_ictrl_regs {
        volatile unsigned int intraw;
        int dummy1;
        volatile unsigned int intseten;
        int dummy2;
        volatile unsigned int intrsten;
        int dummy3;
        volatile unsigned int intenable;
        int dummy4;
        volatile unsigned int intstatus;
        int dummy5;
};

extern void atlasint_init(void);

#endif /* !(_MIPS_ATLASINT_H) */

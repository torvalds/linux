/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
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
 * Defines of the Atlas board specific address-MAP, registers, etc.
 *
 */
#ifndef _MIPS_ATLAS_H
#define _MIPS_ATLAS_H

#include <asm/addrspace.h>

/*
 * Atlas RTC-device indirect register access.
 */
#define ATLAS_RTC_ADR_REG       0x1f000800
#define ATLAS_RTC_DAT_REG       0x1f000808

/*
 * Atlas interrupt controller register base.
 */
#define ATLAS_ICTRL_REGS_BASE   0x1f000000

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

/*
 * Atlas UART register base.
 */
#define ATLAS_UART_REGS_BASE    0x1f000900
#define ATLAS_BASE_BAUD ( 3686400 / 16 )

/*
 * Atlas PSU standby register.
 */
#define ATLAS_PSUSTBY_REG       0x1f000600
#define ATLAS_GOSTBY            0x4d

/*
 * We make a universal assumption about the way the bootloader (YAMON)
 * have located the Philips SAA9730 chip.
 * This is not ideal, but is needed for setting up remote debugging as
 * soon as possible.
 */
#define ATLAS_SAA9730_REG	0x10800000

#define ATLAS_SAA9730_BAUDCLOCK	3692300

#endif /* !(_MIPS_ATLAS_H) */

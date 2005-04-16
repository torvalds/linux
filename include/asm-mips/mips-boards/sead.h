/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2002 MIPS Technologies, Inc.  All rights reserved.
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
 * Defines of the SEAD board specific address-MAP, registers, etc.
 *
 */
#ifndef _MIPS_SEAD_H
#define _MIPS_SEAD_H

#include <asm/addrspace.h>

/*
 * SEAD UART register base.
 */
#define SEAD_UART0_REGS_BASE    (0x1f000800)
#define SEAD_BASE_BAUD ( 3686400 / 16 )

#endif /* !(_MIPS_SEAD_H) */

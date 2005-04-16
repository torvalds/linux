/*
 *  linux/include/asm-arm/hardware/ep7211.h
 *
 *  This file contains the hardware definitions of the EP7211 internal
 *  registers.
 *
 *  Copyright (C) 2001 Blue Mug, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_HARDWARE_EP7211_H
#define __ASM_HARDWARE_EP7211_H

#include <asm/hardware/clps7111.h>

/*
 * define EP7211_BASE to be the base address of the region
 * you want to access.
 */

#define EP7211_PHYS_BASE	(0x80000000)

/*
 * XXX miket@bluemug.com: need to introduce EP7211 registers (those not
 * present in 7212) here.
 */

#endif /* __ASM_HARDWARE_EP7211_H */

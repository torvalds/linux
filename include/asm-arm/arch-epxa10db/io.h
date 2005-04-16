/*
 *  linux/include/asm-arm/arch-epxa10db/io.h
 *
 *  Copyright (C) 1999 ARM Limited
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
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffff


/*
 * Generic virtual read/write
 */
/*#define outsw   __arch_writesw
#define outsl   __arch_writesl
#define outsb   __arch_writesb
#define insb    __arch_readsb
#define insw    __arch_readsw
#define insl    __arch_readsl*/

#define __io(a)			((void __iomem *)(a))
#define __mem_pci(a)            (a) 

#endif

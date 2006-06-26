/*
 *  linux/include/asm-arm/arch-integrator/io.h
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
 * WARNING: this has to mirror definitions in platform.h
 */
#define PCI_MEMORY_VADDR        0xe8000000
#define PCI_CONFIG_VADDR        0xec000000
#define PCI_V3_VADDR            0xed000000
#define PCI_IO_VADDR            0xee000000

#define __io(a)			((void __iomem *)(PCI_IO_VADDR + (a)))
#define __mem_pci(a)		(a)

#endif

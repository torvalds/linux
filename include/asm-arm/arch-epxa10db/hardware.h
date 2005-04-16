/*
 *  linux/include/asm-arm/arch-epxa10/hardware.h
 *
 *  This file contains the hardware definitions of the Integrator.
 *
 *  Copyright (C) 1999 ARM Limited.
 *  Copyright (C) 2001 Altera Corporation
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
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/arch/platform.h>

/*
 * Where in virtual memory the IO devices (timers, system controllers
 * and so on)
 */
#define IO_BASE			0xf0000000                 // VA of IO 
#define IO_SIZE			0x10000000                 // How much?
#define IO_START		EXC_REGISTERS_BASE              // PA of IO
/* macro to get at IO space when running virtually */
#define IO_ADDRESS(x) ((x) | 0xf0000000) 

#define FLASH_VBASE             0xFE000000
#define FLASH_SIZE              0x01000000
#define FLASH_START             EXC_EBI_BLOCK0_BASE
#define FLASH_VADDR(x) ((x)|0xFE000000)
/*
 * Similar to above, but for PCI addresses (memory, IO, Config and the
 * V3 chip itself).  WARNING: this has to mirror definitions in platform.h
 */
#if 0
#define PCI_MEMORY_VADDR        0xe8000000
#define PCI_CONFIG_VADDR        0xec000000
#define PCI_V3_VADDR            0xed000000
#define PCI_IO_VADDR            0xee000000

#define PCIO_BASE		PCI_IO_VADDR
#define PCIMEM_BASE		PCI_MEMORY_VADDR


#define pcibios_assign_all_busses()	1

#define PCIBIOS_MIN_IO		0x6000
#define PCIBIOS_MIN_MEM 	0x00100000
#endif


#endif


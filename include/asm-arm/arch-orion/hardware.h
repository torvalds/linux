/*
 * include/asm-arm/arch-orion/hardware.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#define __ASM_ARCH_HARDWARE_H__

#include "orion.h"

#define PCI_MEMORY_VADDR        ORION_PCI_SYS_MEM_BASE
#define PCI_IO_VADDR            ORION_PCI_SYS_IO_BASE

#define pcibios_assign_all_busses()  1

#define PCIBIOS_MIN_IO  0x1000
#define PCIBIOS_MIN_MEM 0x01000000
#define PCIMEM_BASE     PCI_MEMORY_VADDR /* mem base for VGA */

#endif  /* _ASM_ARCH_HARDWARE_H */

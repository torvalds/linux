/*
 * include/asm-arm/arch-orion/io.h
 *
 * Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include "orion.h"

#define IO_SPACE_LIMIT		0xffffffff
#define IO_SPACE_REMAP		ORION_PCI_SYS_IO_BASE

static inline void __iomem *__io(unsigned long addr)
{
	return (void __iomem *)addr;
}

#define __io(a)			__io(a)
#define __mem_pci(a)		(a)

#endif

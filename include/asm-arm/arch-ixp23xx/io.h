/*
 * include/asm-arm/arch-ixp23xx/io.h
 *
 * Original Author: Naeem M Afzal <naeem.m.afzal@intel.com>
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (C) 2003-2005 Intel Corp.
 * Copyright (C) 2005 MontaVista Software, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_IO_H
#define __ASM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

#define __io(p)		((void __iomem*)((p) + IXP23XX_PCI_IO_VIRT))
#define __mem_pci(a)	(a)

#include <linux/kernel.h>	/* For BUG */

static inline void __iomem *
ixp23xx_ioremap(unsigned long addr, unsigned long size, unsigned int mtype)
{
	if (addr >= IXP23XX_PCI_MEM_START &&
		addr <= IXP23XX_PCI_MEM_START + IXP23XX_PCI_MEM_SIZE) {
		if (addr + size > IXP23XX_PCI_MEM_START + IXP23XX_PCI_MEM_SIZE)
			return NULL;

		return (void __iomem *)
 			((addr - IXP23XX_PCI_MEM_START) + IXP23XX_PCI_MEM_VIRT);
	}

	return __arm_ioremap(addr, size, mtype);
}

static inline void
ixp23xx_iounmap(void __iomem *addr)
{
	if ((((u32)addr) >= IXP23XX_PCI_MEM_VIRT) &&
	    (((u32)addr) < IXP23XX_PCI_MEM_VIRT + IXP23XX_PCI_MEM_SIZE))
		return;

	__iounmap(addr);
}

#define __arch_ioremap(a,s,f)	ixp23xx_ioremap(a,s,f)
#define __arch_iounmap(a)	ixp23xx_iounmap(a)


#endif

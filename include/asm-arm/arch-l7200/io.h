/*
 * linux/include/asm-arm/arch-l7200/io.h
 *
 * Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *
 * Changelog:
 *  03-21-2000	SJH	Created from linux/include/asm-arm/arch-nexuspci/io.h
 *  08-31-2000	SJH	Added in IO functions necessary for new drivers
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include <asm/hardware.h>

#define IO_SPACE_LIMIT 0xffffffff

/*
 * There are not real ISA nor PCI buses, so we fake it.
 */
static inline void __iomem *__io(unsigned long addr)
{
	return (void __iomem *)addr;
}
#define __io(a)	__io(a)
#define __mem_pci(a)		(a)

#endif

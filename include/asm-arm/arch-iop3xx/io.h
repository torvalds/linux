/*
 * linux/include/asm-arm/arch-iop3xx/io.h
 *
 *  Copyright (C) 2001  MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include <asm/hardware.h>

#define IO_SPACE_LIMIT 0xffffffff

#define __io(p)			((void __iomem *)(p))
#define __mem_pci(a)		(a)
#define __mem_isa(a)		(a)

#endif

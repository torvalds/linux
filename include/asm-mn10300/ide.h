/* MN10300 Arch-specific IDE code
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from include/asm-i386/ide.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_IDE_H
#define _ASM_IDE_H

#ifdef __KERNEL__

#include <asm/intctl-regs.h>

#undef SUPPORT_SLOW_DATA_PORTS
#define SUPPORT_SLOW_DATA_PORTS 0

#undef SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC 0

/*
 * some bits needed for parts of the IDE subsystem to compile
 */
#define __ide_mm_insw(port, addr, n) \
	insw((unsigned long) (port), (addr), (n))
#define __ide_mm_insl(port, addr, n) \
	insl((unsigned long) (port), (addr), (n))
#define __ide_mm_outsw(port, addr, n) \
	outsw((unsigned long) (port), (addr), (n))
#define __ide_mm_outsl(port, addr, n) \
	outsl((unsigned long) (port), (addr), (n))

#endif /* __KERNEL__ */
#endif /* _ASM_IDE_H */

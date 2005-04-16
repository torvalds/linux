/* ide.h: FRV IDE declarations
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_IDE_H
#define _ASM_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/irq.h>

#undef SUPPORT_SLOW_DATA_PORTS
#define SUPPORT_SLOW_DATA_PORTS 0

#undef SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC 0

#ifndef MAX_HWIFS
#define MAX_HWIFS 8
#endif

/****************************************************************************/
/*
 * some bits needed for parts of the IDE subsystem to compile
 */
#define __ide_mm_insw(port, addr, n)	insw(port, addr, n)
#define __ide_mm_insl(port, addr, n)	insl(port, addr, n)
#define __ide_mm_outsw(port, addr, n)	outsw(port, addr, n)
#define __ide_mm_outsl(port, addr, n)	outsl(port, addr, n)


#endif /* __KERNEL__ */
#endif /* _ASM_IDE_H */

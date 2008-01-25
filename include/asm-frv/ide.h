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

#include <asm/setup.h>
#include <asm/io.h>
#include <asm/irq.h>

#ifndef MAX_HWIFS
#define MAX_HWIFS 8
#endif

/****************************************************************************/
/*
 * some bits needed for parts of the IDE subsystem to compile
 */
#define __ide_mm_insw(port, addr, n)	insw((unsigned long) (port), addr, n)
#define __ide_mm_insl(port, addr, n)	insl((unsigned long) (port), addr, n)
#define __ide_mm_outsw(port, addr, n)	outsw((unsigned long) (port), addr, n)
#define __ide_mm_outsl(port, addr, n)	outsl((unsigned long) (port), addr, n)


#endif /* __KERNEL__ */
#endif /* _ASM_IDE_H */

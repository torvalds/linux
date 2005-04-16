/*
 *  linux/include/asm-arm/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 */

#ifndef __ASMARM_IDE_H
#define __ASMARM_IDE_H

#ifdef __KERNEL__

#ifndef MAX_HWIFS
#define MAX_HWIFS	4
#endif

#include <asm/irq.h>
#include <asm/mach-types.h>

/* JMA 18.05.03 these will never be needed, but the kernel needs them to compile */
#define __ide_mm_insw(port,addr,len)    readsw(port,addr,len)
#define __ide_mm_insl(port,addr,len)    readsl(port,addr,len)
#define __ide_mm_outsw(port,addr,len)   writesw(port,addr,len)
#define __ide_mm_outsl(port,addr,len)   writesl(port,addr,len)

#define IDE_ARCH_OBSOLETE_INIT
#define ide_default_io_ctl(base)	(0)

#endif /* __KERNEL__ */

#endif /* __ASMARM_IDE_H */

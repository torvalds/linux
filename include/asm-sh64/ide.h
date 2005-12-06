/*
 *  linux/include/asm-sh64/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 *
 *  sh64 version by Richard Curnow & Paul Mundt
 */

/*
 *  This file contains the sh64 architecture specific IDE code.
 */

#ifndef __ASM_SH64_IDE_H
#define __ASM_SH64_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

/* Without this, the initialisation of PCI IDE cards end up calling
 * ide_init_hwif_ports, which won't work. */
#ifdef CONFIG_BLK_DEV_IDEPCI
#define IDE_ARCH_OBSOLETE_INIT 1
#define ide_default_io_ctl(base)	(0)
#endif

#include <asm-generic/ide_iops.h>

#endif /* __KERNEL__ */

#endif /* __ASM_SH64_IDE_H */

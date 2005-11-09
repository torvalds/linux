/*
 *  linux/include/asm-sh/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 *  In future, SuperH code.
 */

#ifndef __ASM_SH_IDE_H
#define __ASM_SH_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#define ide_default_io_ctl(base)	(0)

#include <asm-generic/ide_iops.h>

#endif /* __KERNEL__ */

#endif /* __ASM_SH_IDE_H */

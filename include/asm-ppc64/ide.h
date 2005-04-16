/*
 *  linux/include/asm-ppc/ide.h
 *
 *  Copyright (C) 1994-1996 Linus Torvalds & authors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 *  This file contains the ppc64 architecture specific IDE code.
 */

#ifndef __ASMPPC64_IDE_H
#define __ASMPPC64_IDE_H

#ifdef __KERNEL__

#ifndef MAX_HWIFS
# define MAX_HWIFS	10
#endif

#define IDE_ARCH_OBSOLETE_INIT
#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */

#endif /* __KERNEL__ */

#endif /* __ASMPPC64_IDE_H */

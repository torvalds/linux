/*
 * include/asm-xtensa/ide.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1996  Linus Torvalds & authors
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_IDE_H
#define _XTENSA_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#ifndef MAX_HWIFS
# define MAX_HWIFS	1
#endif

static __inline__ int ide_default_irq(unsigned long base)
{
	/* Unsupported! */
  	return 0;
}

static __inline__ unsigned long ide_default_io_base(int index)
{
	/* Unsupported! */
  	return 0;
}

#endif	/* __KERNEL__ */
#endif	/* _XTENSA_IDE_H */

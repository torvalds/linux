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


#ifndef MAX_HWIFS
# define MAX_HWIFS	1
#endif

#include <asm-generic/ide_iops.h>

#endif	/* __KERNEL__ */

#endif	/* _XTENSA_IDE_H */

/****************************************************************************/

/*
 *  linux/include/asm-blackfin/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 *  Copyright (C) 2001       Lineo Inc., davidm@snapgear.com
 *  Copyright (C) 2002       Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 2002       Yoshinori Sato (ysato@users.sourceforge.jp)
 *  Copyright (C) 2005       Hennerich Michael (hennerich@blackfin.uclinux.org)
 */

/****************************************************************************/
#ifndef _BLACKFIN_IDE_H
#define _BLACKFIN_IDE_H
/****************************************************************************/
#ifdef __KERNEL__
/****************************************************************************/

#define MAX_HWIFS	1

/* Legacy ... BLK_DEV_IDECS */
#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */


#include <asm-generic/ide_iops.h>

/****************************************************************************/
#endif				/* __KERNEL__ */
#endif				/* _BLACKFIN_IDE_H */
/****************************************************************************/

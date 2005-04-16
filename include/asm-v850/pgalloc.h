/*
 * include/asm-v850/pgalloc.h
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_PGALLOC_H__
#define __V850_PGALLOC_H__

#include <linux/mm.h>  /* some crap code expects this */

/* ... and then, there was one.  */
#define check_pgt_cache()	((void)0)

#endif /* __V850_PGALLOC_H__ */

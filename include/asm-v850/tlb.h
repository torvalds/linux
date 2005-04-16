/*
 * include/asm-v850/tlb.h
 *
 *  Copyright (C) 2002  NEC Corporation
 *  Copyright (C) 2002  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_TLB_H__
#define __V850_TLB_H__

#define tlb_flush(tlb)	((void)0)

#include <asm-generic/tlb.h>

#endif /* __V850_TLB_H__ */

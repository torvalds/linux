/*
 * include/asm-v850/macrology.h -- Various useful CPP macros
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#define macrology_paste(arg1, arg2)	macrology_paste_1(arg1, arg2)
#define macrology_paste_1(arg1, arg2)	arg1 ## arg2
#define macrology_stringify(sym)	macrology_stringify_1(sym)
#define macrology_stringify_1(sym)	#sym

/*
 * include/asm-v850/clinkage.h -- Macros to reflect C symbol-naming conventions
 *
 *  Copyright (C) 2001,02  NEC Corporatione
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __CLINKAGE_H__
#define __V850_CLINKAGE_H__

#include <asm/macrology.h>
#include <asm/asm.h>

#define C_SYMBOL_NAME(name) 	macrology_paste(_, name)
#define C_SYMBOL_STRING(name)	macrology_stringify(C_SYMBOL_NAME(name))
#define C_ENTRY(name)		G_ENTRY(C_SYMBOL_NAME(name))
#define C_DATA(name)		G_DATA(C_SYMBOL_NAME(name))
#define C_END(name)		END(C_SYMBOL_NAME(name))

#endif /* __V850_CLINKAGE_H__ */

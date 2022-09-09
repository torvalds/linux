/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOLS_ASM_GENERIC_BITOPS_H
#define __TOOLS_ASM_GENERIC_BITOPS_H

/*
 * tools/ copied this from include/asm-generic/bitops.h, bit by bit as it needed
 * some functions.
 *
 * For the benefit of those who are trying to port Linux to another
 * architecture, here are some C-language equivalents.  You should
 * recode these in the native assembly language, if at all possible.
 *
 * C language equivalents written by Theodore Ts'o, 9/26/92
 */

#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/__ffz.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>

#ifndef _TOOLS_LINUX_BITOPS_H_
#error only <linux/bitops.h> can be included directly
#endif

#include <asm-generic/bitops/hweight.h>

#include <asm-generic/bitops/atomic.h>
#include <asm-generic/bitops/non-atomic.h>

#endif /* __TOOLS_ASM_GENERIC_BITOPS_H */

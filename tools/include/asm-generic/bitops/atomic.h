/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_ASM_GENERIC_BITOPS_ATOMIC_H_
#define _TOOLS_LINUX_ASM_GENERIC_BITOPS_ATOMIC_H_

#include <asm/types.h>
#include <asm/bitsperlong.h>

/*
 * Just alias the test versions, all of the compiler built-in atomics "fetch",
 * and optimizing compile-time constants on x86 isn't worth the complexity.
 */
#define set_bit test_and_set_bit
#define clear_bit test_and_clear_bit

#endif /* _TOOLS_LINUX_ASM_GENERIC_BITOPS_ATOMIC_H_ */

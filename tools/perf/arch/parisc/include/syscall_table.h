/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/bitsperlong.h>

#if __BITS_PER_LONG == 64
#include <asm/syscalls_64.h>
#else
#include <asm/syscalls_32.h>
#endif

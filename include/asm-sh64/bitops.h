#ifndef __ASM_SH64_BITOPS_H
#define __ASM_SH64_BITOPS_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/bitops.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 */

#ifdef __KERNEL__
#include <linux/compiler.h>
#include <asm/system.h>
/* For __swab32 */
#include <asm/byteorder.h>

static __inline__ void set_bit(int nr, volatile void * addr)
{
	int	mask;
	volatile unsigned int *a = addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	*a |= mask;
	local_irq_restore(flags);
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()
static inline void clear_bit(int nr, volatile unsigned long *a)
{
	int	mask;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	*a &= ~mask;
	local_irq_restore(flags);
}

static __inline__ void change_bit(int nr, volatile void * addr)
{
	int	mask;
	volatile unsigned int *a = addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	*a ^= mask;
	local_irq_restore(flags);
}

static __inline__ int test_and_set_bit(int nr, volatile void * addr)
{
	int	mask, retval;
	volatile unsigned int *a = addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	retval = (mask & *a) != 0;
	*a |= mask;
	local_irq_restore(flags);

	return retval;
}

static __inline__ int test_and_clear_bit(int nr, volatile void * addr)
{
	int	mask, retval;
	volatile unsigned int *a = addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	retval = (mask & *a) != 0;
	*a &= ~mask;
	local_irq_restore(flags);

	return retval;
}

static __inline__ int test_and_change_bit(int nr, volatile void * addr)
{
	int	mask, retval;
	volatile unsigned int *a = addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	retval = (mask & *a) != 0;
	*a ^= mask;
	local_irq_restore(flags);

	return retval;
}

#include <asm-generic/bitops/non-atomic.h>

static __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result, __d2, __d3;

        __asm__("gettr  tr0, %2\n\t"
                "pta    $+32, tr0\n\t"
                "andi   %1, 1, %3\n\t"
                "beq    %3, r63, tr0\n\t"
                "pta    $+4, tr0\n"
                "0:\n\t"
                "shlri.l        %1, 1, %1\n\t"
                "addi   %0, 1, %0\n\t"
                "andi   %1, 1, %3\n\t"
                "beqi   %3, 1, tr0\n"
                "1:\n\t"
                "ptabs  %2, tr0\n\t"
                : "=r" (result), "=r" (word), "=r" (__d2), "=r" (__d3)
                : "0" (0L), "1" (word));

	return result;
}

#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/ext2-non-atomic.h>
#include <asm-generic/bitops/ext2-atomic.h>
#include <asm-generic/bitops/minix.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/fls64.h>

#endif /* __KERNEL__ */

#endif /* __ASM_SH64_BITOPS_H */

/* $Id: bitops.h,v 1.67 2001/11/19 18:36:34 davem Exp $
 * bitops.h: Bit string operations on the Sparc.
 *
 * Copyright 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright 1996 Eddie C. Dost   (ecd@skynet.be)
 * Copyright 2001 Anton Blanchard (anton@samba.org)
 */

#ifndef _SPARC_BITOPS_H
#define _SPARC_BITOPS_H

#include <linux/compiler.h>
#include <asm/byteorder.h>

#ifdef __KERNEL__

extern unsigned long ___set_bit(unsigned long *addr, unsigned long mask);
extern unsigned long ___clear_bit(unsigned long *addr, unsigned long mask);
extern unsigned long ___change_bit(unsigned long *addr, unsigned long mask);

/*
 * Set bit 'nr' in 32-bit quantity at address 'addr' where bit '0'
 * is in the highest of the four bytes and bit '31' is the high bit
 * within the first byte. Sparc is BIG-Endian. Unless noted otherwise
 * all bit-ops return 0 if bit was previously clear and != 0 otherwise.
 */
static inline int test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *ADDR, mask;

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	return ___set_bit(ADDR, mask) != 0;
}

static inline void set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *ADDR, mask;

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	(void) ___set_bit(ADDR, mask);
}

static inline int test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *ADDR, mask;

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	return ___clear_bit(ADDR, mask) != 0;
}

static inline void clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *ADDR, mask;

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	(void) ___clear_bit(ADDR, mask);
}

static inline int test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *ADDR, mask;

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	return ___change_bit(ADDR, mask) != 0;
}

static inline void change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *ADDR, mask;

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	(void) ___change_bit(ADDR, mask);
}

#include <asm-generic/bitops/non-atomic.h>

#define smp_mb__before_clear_bit()	do { } while(0)
#define smp_mb__after_clear_bit()	do { } while(0)

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/ext2-non-atomic.h>
#include <asm-generic/bitops/ext2-atomic.h>
#include <asm-generic/bitops/minix.h>

#endif /* __KERNEL__ */

#endif /* defined(_SPARC_BITOPS_H) */

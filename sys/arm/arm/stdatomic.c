/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stdatomic.h>
#include <sys/types.h>

#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/sysarch.h>

/*
 * Executing statements with interrupts disabled.
 */

#if defined(_KERNEL) && !defined(SMP)
#define	WITHOUT_INTERRUPTS(s) do {					\
	register_t regs;						\
									\
	regs = intr_disable();						\
	do s while (0);							\
	intr_restore(regs);						\
} while (0)
#endif /* _KERNEL && !SMP */

/*
 * Memory barriers.
 *
 * It turns out __sync_synchronize() does not emit any code when used
 * with GCC 4.2. Implement our own version that does work reliably.
 *
 * Although __sync_lock_test_and_set() should only perform an acquire
 * barrier, make it do a full barrier like the other functions. This
 * should make <stdatomic.h>'s atomic_exchange_explicit() work reliably.
 */

#if defined(_KERNEL) && !defined(SMP)
static inline void
do_sync(void)
{

	__asm volatile ("" : : : "memory");
}
#elif __ARM_ARCH >= 6
static inline void
do_sync(void)
{

	dmb();
}
#endif

#if defined(__CLANG_ATOMICS) || defined(__GNUC_ATOMICS)

/*
 * New C11 __atomic_* API.
 */

/* ARMv6+ systems should be supported by the compiler. */
#if __ARM_ARCH <= 5

/* Clang doesn't allow us to reimplement builtins without this. */
#ifdef __clang__
#pragma redefine_extname __sync_synchronize_ext __sync_synchronize
#define __sync_synchronize __sync_synchronize_ext
#endif

void
__sync_synchronize(void)
{
}

#ifdef _KERNEL

#ifdef SMP
#error "On SMP systems we should have proper atomic operations."
#endif

/*
 * On uniprocessor systems, we can perform the atomic operations by
 * disabling interrupts.
 */

#define	EMIT_LOAD_N(N, uintN_t)						\
uintN_t									\
__atomic_load_##N(uintN_t *mem, int model __unused)			\
{									\
	uintN_t ret;							\
									\
	WITHOUT_INTERRUPTS({						\
		ret = *mem;						\
	});								\
	return (ret);							\
}

#define	EMIT_STORE_N(N, uintN_t)					\
void									\
__atomic_store_##N(uintN_t *mem, uintN_t val, int model __unused)	\
{									\
									\
	WITHOUT_INTERRUPTS({						\
		*mem = val;						\
	});								\
}

#define	EMIT_COMPARE_EXCHANGE_N(N, uintN_t)				\
_Bool									\
__atomic_compare_exchange_##N(uintN_t *mem, uintN_t *expected,		\
    uintN_t desired, int success __unused, int failure __unused)	\
{									\
	_Bool ret;							\
									\
	WITHOUT_INTERRUPTS({						\
		if (*mem == *expected) {				\
			*mem = desired;					\
			ret = 1;					\
		} else {						\
			*expected = *mem;				\
			ret = 0;					\
		}							\
	});								\
	return (ret);							\
}

#define	EMIT_FETCH_OP_N(N, uintN_t, name, op)				\
uintN_t									\
__atomic_##name##_##N(uintN_t *mem, uintN_t val, int model __unused)	\
{									\
	uintN_t ret;							\
									\
	WITHOUT_INTERRUPTS({						\
		ret = *mem;						\
		*mem op val;						\
	});								\
	return (ret);							\
}

#define	EMIT_ALL_OPS_N(N, uintN_t)					\
EMIT_LOAD_N(N, uintN_t)							\
EMIT_STORE_N(N, uintN_t)						\
EMIT_COMPARE_EXCHANGE_N(N, uintN_t)					\
EMIT_FETCH_OP_N(N, uintN_t, exchange, =)				\
EMIT_FETCH_OP_N(N, uintN_t, fetch_add, +=)				\
EMIT_FETCH_OP_N(N, uintN_t, fetch_and, &=)				\
EMIT_FETCH_OP_N(N, uintN_t, fetch_or, |=)				\
EMIT_FETCH_OP_N(N, uintN_t, fetch_sub, -=)				\
EMIT_FETCH_OP_N(N, uintN_t, fetch_xor, ^=)

EMIT_ALL_OPS_N(1, uint8_t)
EMIT_ALL_OPS_N(2, uint16_t)
EMIT_ALL_OPS_N(4, uint32_t)
EMIT_ALL_OPS_N(8, uint64_t)
#undef	EMIT_ALL_OPS_N

#else /* !_KERNEL */

/*
 * For userspace on uniprocessor systems, we can implement the atomic
 * operations by using a Restartable Atomic Sequence. This makes the
 * kernel restart the code from the beginning when interrupted.
 */

#define	EMIT_LOAD_N(N, uintN_t)						\
uintN_t									\
__atomic_load_##N(uintN_t *mem, int model __unused)			\
{									\
									\
	return (*mem);							\
}

#define	EMIT_STORE_N(N, uintN_t)					\
void									\
__atomic_store_##N(uintN_t *mem, uintN_t val, int model __unused)	\
{									\
									\
	*mem = val;							\
}

#define	EMIT_EXCHANGE_N(N, uintN_t, ldr, str)				\
uintN_t									\
__atomic_exchange_##N(uintN_t *mem, uintN_t val, int model __unused)	\
{									\
	uint32_t old, temp, ras_start;					\
									\
	ras_start = ARM_RAS_START;					\
	__asm volatile (						\
		/* Set up Restartable Atomic Sequence. */		\
		"1:"							\
		"\tadr   %2, 1b\n"					\
		"\tstr   %2, [%5]\n"					\
		"\tadr   %2, 2f\n"					\
		"\tstr   %2, [%5, #4]\n"				\
									\
		"\t"ldr" %0, %4\n"	/* Load old value. */		\
		"\t"str" %3, %1\n"	/* Store new value. */		\
									\
		/* Tear down Restartable Atomic Sequence. */		\
		"2:"							\
		"\tmov   %2, #0x00000000\n"				\
		"\tstr   %2, [%5]\n"					\
		"\tmov   %2, #0xffffffff\n"				\
		"\tstr   %2, [%5, #4]\n"				\
		: "=&r" (old), "=m" (*mem), "=&r" (temp)		\
		: "r" (val), "m" (*mem), "r" (ras_start));		\
	return (old);							\
}

#define	EMIT_COMPARE_EXCHANGE_N(N, uintN_t, ldr, streq)			\
_Bool									\
__atomic_compare_exchange_##N(uintN_t *mem, uintN_t *pexpected,		\
    uintN_t desired, int success __unused, int failure __unused)	\
{									\
	uint32_t expected, old, temp, ras_start;			\
									\
	expected = *pexpected;						\
	ras_start = ARM_RAS_START;					\
	__asm volatile (						\
		/* Set up Restartable Atomic Sequence. */		\
		"1:"							\
		"\tadr   %2, 1b\n"					\
		"\tstr   %2, [%6]\n"					\
		"\tadr   %2, 2f\n"					\
		"\tstr   %2, [%6, #4]\n"				\
									\
		"\t"ldr" %0, %5\n"	/* Load old value. */		\
		"\tcmp   %0, %3\n"	/* Compare to expected value. */\
		"\t"streq" %4, %1\n"	/* Store new value. */		\
									\
		/* Tear down Restartable Atomic Sequence. */		\
		"2:"							\
		"\tmov   %2, #0x00000000\n"				\
		"\tstr   %2, [%6]\n"					\
		"\tmov   %2, #0xffffffff\n"				\
		"\tstr   %2, [%6, #4]\n"				\
		: "=&r" (old), "=m" (*mem), "=&r" (temp)		\
		: "r" (expected), "r" (desired), "m" (*mem),		\
		  "r" (ras_start));					\
	if (old == expected) {						\
		return (1);						\
	} else {							\
		*pexpected = old;					\
		return (0);						\
	}								\
}

#define	EMIT_FETCH_OP_N(N, uintN_t, ldr, str, name, op, ret)		\
uintN_t									\
__atomic_##name##_##N(uintN_t *mem, uintN_t val, int model __unused)	\
{									\
	uint32_t old, new, ras_start;					\
									\
	ras_start = ARM_RAS_START;					\
	__asm volatile (						\
		/* Set up Restartable Atomic Sequence. */		\
		"1:"							\
		"\tadr   %2, 1b\n"					\
		"\tstr   %2, [%5]\n"					\
		"\tadr   %2, 2f\n"					\
		"\tstr   %2, [%5, #4]\n"				\
									\
		"\t"ldr" %0, %4\n"	/* Load old value. */		\
		"\t"op"  %2, %0, %3\n"	/* Calculate new value. */	\
		"\t"str" %2, %1\n"	/* Store new value. */		\
									\
		/* Tear down Restartable Atomic Sequence. */		\
		"2:"							\
		"\tmov   %2, #0x00000000\n"				\
		"\tstr   %2, [%5]\n"					\
		"\tmov   %2, #0xffffffff\n"				\
		"\tstr   %2, [%5, #4]\n"				\
		: "=&r" (old), "=m" (*mem), "=&r" (new)			\
		: "r" (val), "m" (*mem), "r" (ras_start));		\
	return (ret);							\
}

#define	EMIT_ALL_OPS_N(N, uintN_t, ldr, str, streq)			\
EMIT_LOAD_N(N, uintN_t)							\
EMIT_STORE_N(N, uintN_t)						\
EMIT_EXCHANGE_N(N, uintN_t, ldr, str)					\
EMIT_COMPARE_EXCHANGE_N(N, uintN_t, ldr, streq)				\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, fetch_add, "add", old)		\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, fetch_and, "and", old)		\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, fetch_or,  "orr", old)		\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, fetch_sub, "sub", old)		\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, fetch_xor, "eor", old)		\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, add_fetch, "add", new)		\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, and_fetch, "and", new)		\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, or_fetch,  "orr", new)		\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, sub_fetch, "sub", new)		\
EMIT_FETCH_OP_N(N, uintN_t, ldr, str, xor_fetch, "eor", new)

EMIT_ALL_OPS_N(1, uint8_t, "ldrb", "strb", "strbeq")
EMIT_ALL_OPS_N(2, uint16_t, "ldrh", "strh", "strheq")
EMIT_ALL_OPS_N(4, uint32_t, "ldr", "str", "streq")
#undef	EMIT_ALL_OPS_N

#endif /* _KERNEL */

#endif /* __ARM_ARCH */

#endif /* __CLANG_ATOMICS || __GNUC_ATOMICS */

#if defined(__SYNC_ATOMICS) || defined(EMIT_SYNC_ATOMICS)

#ifdef __clang__
#pragma redefine_extname __sync_lock_test_and_set_1_c __sync_lock_test_and_set_1
#pragma redefine_extname __sync_lock_test_and_set_2_c __sync_lock_test_and_set_2
#pragma	redefine_extname __sync_lock_test_and_set_4_c __sync_lock_test_and_set_4
#pragma	redefine_extname __sync_val_compare_and_swap_1_c __sync_val_compare_and_swap_1
#pragma	redefine_extname __sync_val_compare_and_swap_2_c __sync_val_compare_and_swap_2
#pragma	redefine_extname __sync_val_compare_and_swap_4_c __sync_val_compare_and_swap_4
#pragma	redefine_extname __sync_fetch_and_add_1_c __sync_fetch_and_add_1
#pragma	redefine_extname __sync_fetch_and_add_2_c __sync_fetch_and_add_2
#pragma	redefine_extname __sync_fetch_and_add_4_c __sync_fetch_and_add_4
#pragma	redefine_extname __sync_fetch_and_and_1_c __sync_fetch_and_and_1
#pragma	redefine_extname __sync_fetch_and_and_2_c __sync_fetch_and_and_2
#pragma	redefine_extname __sync_fetch_and_and_4_c __sync_fetch_and_and_4
#pragma	redefine_extname __sync_fetch_and_or_1_c __sync_fetch_and_or_1
#pragma	redefine_extname __sync_fetch_and_or_2_c __sync_fetch_and_or_2
#pragma	redefine_extname __sync_fetch_and_or_4_c __sync_fetch_and_or_4
#pragma	redefine_extname __sync_fetch_and_xor_1_c __sync_fetch_and_xor_1
#pragma	redefine_extname __sync_fetch_and_xor_2_c __sync_fetch_and_xor_2
#pragma	redefine_extname __sync_fetch_and_xor_4_c __sync_fetch_and_xor_4
#pragma	redefine_extname __sync_fetch_and_sub_1_c __sync_fetch_and_sub_1
#pragma	redefine_extname __sync_fetch_and_sub_2_c __sync_fetch_and_sub_2
#pragma	redefine_extname __sync_fetch_and_sub_4_c __sync_fetch_and_sub_4
#endif

/*
 * Old __sync_* API.
 */

#if __ARM_ARCH >= 6

/* Implementations for old GCC versions, lacking support for atomics. */

typedef union {
	uint8_t		v8[4];
	uint32_t	v32;
} reg_t;

/*
 * Given a memory address pointing to an 8-bit or 16-bit integer, return
 * the address of the 32-bit word containing it.
 */

static inline uint32_t *
round_to_word(void *ptr)
{

	return ((uint32_t *)((intptr_t)ptr & ~3));
}

/*
 * Utility functions for loading and storing 8-bit and 16-bit integers
 * in 32-bit words at an offset corresponding with the location of the
 * atomic variable.
 */

static inline void
put_1(reg_t *r, const uint8_t *offset_ptr, uint8_t val)
{
	size_t offset;

	offset = (intptr_t)offset_ptr & 3;
	r->v8[offset] = val;
}

static inline uint8_t
get_1(const reg_t *r, const uint8_t *offset_ptr)
{
	size_t offset;

	offset = (intptr_t)offset_ptr & 3;
	return (r->v8[offset]);
}

static inline void
put_2(reg_t *r, const uint16_t *offset_ptr, uint16_t val)
{
	size_t offset;
	union {
		uint16_t in;
		uint8_t out[2];
	} bytes;

	offset = (intptr_t)offset_ptr & 3;
	bytes.in = val;
	r->v8[offset] = bytes.out[0];
	r->v8[offset + 1] = bytes.out[1];
}

static inline uint16_t
get_2(const reg_t *r, const uint16_t *offset_ptr)
{
	size_t offset;
	union {
		uint8_t in[2];
		uint16_t out;
	} bytes;

	offset = (intptr_t)offset_ptr & 3;
	bytes.in[0] = r->v8[offset];
	bytes.in[1] = r->v8[offset + 1];
	return (bytes.out);
}

/*
 * 8-bit and 16-bit routines.
 *
 * These operations are not natively supported by the CPU, so we use
 * some shifting and bitmasking on top of the 32-bit instructions.
 */

#define	EMIT_LOCK_TEST_AND_SET_N(N, uintN_t)				\
uintN_t									\
__sync_lock_test_and_set_##N##_c(uintN_t *mem, uintN_t val)			\
{									\
	uint32_t *mem32;						\
	reg_t val32, negmask, old;					\
	uint32_t temp1, temp2;						\
									\
	mem32 = round_to_word(mem);					\
	val32.v32 = 0x00000000;						\
	put_##N(&val32, mem, val);					\
	negmask.v32 = 0xffffffff;					\
	put_##N(&negmask, mem, 0);					\
									\
	do_sync();							\
	__asm volatile (						\
		"1:"							\
		"\tldrex %0, %6\n"	/* Load old value. */		\
		"\tand   %2, %5, %0\n"	/* Remove the old value. */	\
		"\torr   %2, %2, %4\n"	/* Put in the new value. */	\
		"\tstrex %3, %2, %1\n"	/* Attempt to store. */		\
		"\tcmp   %3, #0\n"	/* Did it succeed? */		\
		"\tbne   1b\n"		/* Spin if failed. */		\
		: "=&r" (old.v32), "=m" (*mem32), "=&r" (temp1),	\
		  "=&r" (temp2)						\
		: "r" (val32.v32), "r" (negmask.v32), "m" (*mem32));	\
	return (get_##N(&old, mem));					\
}

EMIT_LOCK_TEST_AND_SET_N(1, uint8_t)
EMIT_LOCK_TEST_AND_SET_N(2, uint16_t)

#define	EMIT_VAL_COMPARE_AND_SWAP_N(N, uintN_t)				\
uintN_t									\
__sync_val_compare_and_swap_##N##_c(uintN_t *mem, uintN_t expected,		\
    uintN_t desired)							\
{									\
	uint32_t *mem32;						\
	reg_t expected32, desired32, posmask, old;			\
	uint32_t negmask, temp1, temp2;					\
									\
	mem32 = round_to_word(mem);					\
	expected32.v32 = 0x00000000;					\
	put_##N(&expected32, mem, expected);				\
	desired32.v32 = 0x00000000;					\
	put_##N(&desired32, mem, desired);				\
	posmask.v32 = 0x00000000;					\
	put_##N(&posmask, mem, ~0);					\
	negmask = ~posmask.v32;						\
									\
	do_sync();							\
	__asm volatile (						\
		"1:"							\
		"\tldrex %0, %8\n"	/* Load old value. */		\
		"\tand   %2, %6, %0\n"	/* Isolate the old value. */	\
		"\tcmp   %2, %4\n"	/* Compare to expected value. */\
		"\tbne   2f\n"		/* Values are unequal. */	\
		"\tand   %2, %7, %0\n"	/* Remove the old value. */	\
		"\torr   %2, %5\n"	/* Put in the new value. */	\
		"\tstrex %3, %2, %1\n"	/* Attempt to store. */		\
		"\tcmp   %3, #0\n"	/* Did it succeed? */		\
		"\tbne   1b\n"		/* Spin if failed. */		\
		"2:"							\
		: "=&r" (old), "=m" (*mem32), "=&r" (temp1),		\
		  "=&r" (temp2)						\
		: "r" (expected32.v32), "r" (desired32.v32),		\
		  "r" (posmask.v32), "r" (negmask), "m" (*mem32));	\
	return (get_##N(&old, mem));					\
}

EMIT_VAL_COMPARE_AND_SWAP_N(1, uint8_t)
EMIT_VAL_COMPARE_AND_SWAP_N(2, uint16_t)

#define	EMIT_ARITHMETIC_FETCH_AND_OP_N(N, uintN_t, name, op)		\
uintN_t									\
__sync_##name##_##N##_c(uintN_t *mem, uintN_t val)				\
{									\
	uint32_t *mem32;						\
	reg_t val32, posmask, old;					\
	uint32_t negmask, temp1, temp2;					\
									\
	mem32 = round_to_word(mem);					\
	val32.v32 = 0x00000000;						\
	put_##N(&val32, mem, val);					\
	posmask.v32 = 0x00000000;					\
	put_##N(&posmask, mem, ~0);					\
	negmask = ~posmask.v32;						\
									\
	do_sync();							\
	__asm volatile (						\
		"1:"							\
		"\tldrex %0, %7\n"	/* Load old value. */		\
		"\t"op"  %2, %0, %4\n"	/* Calculate new value. */	\
		"\tand   %2, %5\n"	/* Isolate the new value. */	\
		"\tand   %3, %6, %0\n"	/* Remove the old value. */	\
		"\torr   %2, %2, %3\n"	/* Put in the new value. */	\
		"\tstrex %3, %2, %1\n"	/* Attempt to store. */		\
		"\tcmp   %3, #0\n"	/* Did it succeed? */		\
		"\tbne   1b\n"		/* Spin if failed. */		\
		: "=&r" (old.v32), "=m" (*mem32), "=&r" (temp1),	\
		  "=&r" (temp2)						\
		: "r" (val32.v32), "r" (posmask.v32), "r" (negmask),	\
		  "m" (*mem32));					\
	return (get_##N(&old, mem));					\
}

EMIT_ARITHMETIC_FETCH_AND_OP_N(1, uint8_t, fetch_and_add, "add")
EMIT_ARITHMETIC_FETCH_AND_OP_N(1, uint8_t, fetch_and_sub, "sub")
EMIT_ARITHMETIC_FETCH_AND_OP_N(2, uint16_t, fetch_and_add, "add")
EMIT_ARITHMETIC_FETCH_AND_OP_N(2, uint16_t, fetch_and_sub, "sub")

#define	EMIT_BITWISE_FETCH_AND_OP_N(N, uintN_t, name, op, idempotence)	\
uintN_t									\
__sync_##name##_##N##_c(uintN_t *mem, uintN_t val)				\
{									\
	uint32_t *mem32;						\
	reg_t val32, old;						\
	uint32_t temp1, temp2;						\
									\
	mem32 = round_to_word(mem);					\
	val32.v32 = idempotence ? 0xffffffff : 0x00000000;		\
	put_##N(&val32, mem, val);					\
									\
	do_sync();							\
	__asm volatile (						\
		"1:"							\
		"\tldrex %0, %5\n"	/* Load old value. */		\
		"\t"op"  %2, %4, %0\n"	/* Calculate new value. */	\
		"\tstrex %3, %2, %1\n"	/* Attempt to store. */		\
		"\tcmp   %3, #0\n"	/* Did it succeed? */		\
		"\tbne   1b\n"		/* Spin if failed. */		\
		: "=&r" (old.v32), "=m" (*mem32), "=&r" (temp1),	\
		  "=&r" (temp2)						\
		: "r" (val32.v32), "m" (*mem32));			\
	return (get_##N(&old, mem));					\
}

EMIT_BITWISE_FETCH_AND_OP_N(1, uint8_t, fetch_and_and, "and", 1)
EMIT_BITWISE_FETCH_AND_OP_N(1, uint8_t, fetch_and_or, "orr", 0)
EMIT_BITWISE_FETCH_AND_OP_N(1, uint8_t, fetch_and_xor, "eor", 0)
EMIT_BITWISE_FETCH_AND_OP_N(2, uint16_t, fetch_and_and, "and", 1)
EMIT_BITWISE_FETCH_AND_OP_N(2, uint16_t, fetch_and_or, "orr", 0)
EMIT_BITWISE_FETCH_AND_OP_N(2, uint16_t, fetch_and_xor, "eor", 0)

/*
 * 32-bit routines.
 */

uint32_t
__sync_lock_test_and_set_4_c(uint32_t *mem, uint32_t val)
{
	uint32_t old, temp;

	do_sync();
	__asm volatile (
		"1:"
		"\tldrex %0, %4\n"	/* Load old value. */
		"\tstrex %2, %3, %1\n"	/* Attempt to store. */
		"\tcmp   %2, #0\n"	/* Did it succeed? */
		"\tbne   1b\n"		/* Spin if failed. */
		: "=&r" (old), "=m" (*mem), "=&r" (temp)
		: "r" (val), "m" (*mem));
	return (old);
}

uint32_t
__sync_val_compare_and_swap_4_c(uint32_t *mem, uint32_t expected,
    uint32_t desired)
{
	uint32_t old, temp;

	do_sync();
	__asm volatile (
		"1:"
		"\tldrex %0, %5\n"	/* Load old value. */
		"\tcmp   %0, %3\n"	/* Compare to expected value. */
		"\tbne   2f\n"		/* Values are unequal. */
		"\tstrex %2, %4, %1\n"	/* Attempt to store. */
		"\tcmp   %2, #0\n"	/* Did it succeed? */
		"\tbne   1b\n"		/* Spin if failed. */
		"2:"
		: "=&r" (old), "=m" (*mem), "=&r" (temp)
		: "r" (expected), "r" (desired), "m" (*mem));
	return (old);
}

#define	EMIT_FETCH_AND_OP_4(name, op)					\
uint32_t								\
__sync_##name##_4##_c(uint32_t *mem, uint32_t val)				\
{									\
	uint32_t old, temp1, temp2;					\
									\
	do_sync();							\
	__asm volatile (						\
		"1:"							\
		"\tldrex %0, %5\n"	/* Load old value. */		\
		"\t"op"  %2, %0, %4\n"	/* Calculate new value. */	\
		"\tstrex %3, %2, %1\n"	/* Attempt to store. */		\
		"\tcmp   %3, #0\n"	/* Did it succeed? */		\
		"\tbne   1b\n"		/* Spin if failed. */		\
		: "=&r" (old), "=m" (*mem), "=&r" (temp1),		\
		  "=&r" (temp2)						\
		: "r" (val), "m" (*mem));				\
	return (old);							\
}

EMIT_FETCH_AND_OP_4(fetch_and_add, "add")
EMIT_FETCH_AND_OP_4(fetch_and_and, "and")
EMIT_FETCH_AND_OP_4(fetch_and_or, "orr")
EMIT_FETCH_AND_OP_4(fetch_and_sub, "sub")
EMIT_FETCH_AND_OP_4(fetch_and_xor, "eor")

#ifndef __clang__
__strong_reference(__sync_lock_test_and_set_1_c, __sync_lock_test_and_set_1);
__strong_reference(__sync_lock_test_and_set_2_c, __sync_lock_test_and_set_2);
__strong_reference(__sync_lock_test_and_set_4_c, __sync_lock_test_and_set_4);
__strong_reference(__sync_val_compare_and_swap_1_c, __sync_val_compare_and_swap_1);
__strong_reference(__sync_val_compare_and_swap_2_c, __sync_val_compare_and_swap_2);
__strong_reference(__sync_val_compare_and_swap_4_c, __sync_val_compare_and_swap_4);
__strong_reference(__sync_fetch_and_add_1_c, __sync_fetch_and_add_1);
__strong_reference(__sync_fetch_and_add_2_c, __sync_fetch_and_add_2);
__strong_reference(__sync_fetch_and_add_4_c, __sync_fetch_and_add_4);
__strong_reference(__sync_fetch_and_and_1_c, __sync_fetch_and_and_1);
__strong_reference(__sync_fetch_and_and_2_c, __sync_fetch_and_and_2);
__strong_reference(__sync_fetch_and_and_4_c, __sync_fetch_and_and_4);
__strong_reference(__sync_fetch_and_sub_1_c, __sync_fetch_and_sub_1);
__strong_reference(__sync_fetch_and_sub_2_c, __sync_fetch_and_sub_2);
__strong_reference(__sync_fetch_and_sub_4_c, __sync_fetch_and_sub_4);
__strong_reference(__sync_fetch_and_or_1_c, __sync_fetch_and_or_1);
__strong_reference(__sync_fetch_and_or_2_c, __sync_fetch_and_or_2);
__strong_reference(__sync_fetch_and_or_4_c, __sync_fetch_and_or_4);
__strong_reference(__sync_fetch_and_xor_1_c, __sync_fetch_and_xor_1);
__strong_reference(__sync_fetch_and_xor_2_c, __sync_fetch_and_xor_2);
__strong_reference(__sync_fetch_and_xor_4_c, __sync_fetch_and_xor_4);
#endif

#else /* __ARM_ARCH < 6 */

#ifdef _KERNEL

#ifdef SMP
#error "On SMP systems we should have proper atomic operations."
#endif

/*
 * On uniprocessor systems, we can perform the atomic operations by
 * disabling interrupts.
 */

#define	EMIT_VAL_COMPARE_AND_SWAP_N(N, uintN_t)				\
uintN_t									\
__sync_val_compare_and_swap_##N(uintN_t *mem, uintN_t expected,		\
    uintN_t desired)							\
{									\
	uintN_t ret;							\
									\
	WITHOUT_INTERRUPTS({						\
		ret = *mem;						\
		if (*mem == expected)					\
			*mem = desired;					\
	});								\
	return (ret);							\
}

#define	EMIT_FETCH_AND_OP_N(N, uintN_t, name, op)			\
uintN_t									\
__sync_##name##_##N(uintN_t *mem, uintN_t val)				\
{									\
	uintN_t ret;							\
									\
	WITHOUT_INTERRUPTS({						\
		ret = *mem;						\
		*mem op val;						\
	});								\
	return (ret);							\
}

#define	EMIT_ALL_OPS_N(N, uintN_t)					\
EMIT_VAL_COMPARE_AND_SWAP_N(N, uintN_t)					\
EMIT_FETCH_AND_OP_N(N, uintN_t, lock_test_and_set, =)			\
EMIT_FETCH_AND_OP_N(N, uintN_t, fetch_and_add, +=)			\
EMIT_FETCH_AND_OP_N(N, uintN_t, fetch_and_and, &=)			\
EMIT_FETCH_AND_OP_N(N, uintN_t, fetch_and_or, |=)			\
EMIT_FETCH_AND_OP_N(N, uintN_t, fetch_and_sub, -=)			\
EMIT_FETCH_AND_OP_N(N, uintN_t, fetch_and_xor, ^=)

EMIT_ALL_OPS_N(1, uint8_t)
EMIT_ALL_OPS_N(2, uint16_t)
EMIT_ALL_OPS_N(4, uint32_t)
EMIT_ALL_OPS_N(8, uint64_t)
#undef	EMIT_ALL_OPS_N

#else /* !_KERNEL */

/*
 * For userspace on uniprocessor systems, we can implement the atomic
 * operations by using a Restartable Atomic Sequence. This makes the
 * kernel restart the code from the beginning when interrupted.
 */

#define	EMIT_LOCK_TEST_AND_SET_N(N, uintN_t, ldr, str)			\
uintN_t									\
__sync_lock_test_and_set_##N##_c(uintN_t *mem, uintN_t val)			\
{									\
	uint32_t old, temp, ras_start;					\
									\
	ras_start = ARM_RAS_START;					\
	__asm volatile (						\
		/* Set up Restartable Atomic Sequence. */		\
		"1:"							\
		"\tadr   %2, 1b\n"					\
		"\tstr   %2, [%5]\n"					\
		"\tadr   %2, 2f\n"					\
		"\tstr   %2, [%5, #4]\n"				\
									\
		"\t"ldr" %0, %4\n"	/* Load old value. */		\
		"\t"str" %3, %1\n"	/* Store new value. */		\
									\
		/* Tear down Restartable Atomic Sequence. */		\
		"2:"							\
		"\tmov   %2, #0x00000000\n"				\
		"\tstr   %2, [%5]\n"					\
		"\tmov   %2, #0xffffffff\n"				\
		"\tstr   %2, [%5, #4]\n"				\
		: "=&r" (old), "=m" (*mem), "=&r" (temp)		\
		: "r" (val), "m" (*mem), "r" (ras_start));		\
	return (old);							\
}

#define	EMIT_VAL_COMPARE_AND_SWAP_N(N, uintN_t, ldr, streq)		\
uintN_t									\
__sync_val_compare_and_swap_##N##_c(uintN_t *mem, uintN_t expected,		\
    uintN_t desired)							\
{									\
	uint32_t old, temp, ras_start;					\
									\
	ras_start = ARM_RAS_START;					\
	__asm volatile (						\
		/* Set up Restartable Atomic Sequence. */		\
		"1:"							\
		"\tadr   %2, 1b\n"					\
		"\tstr   %2, [%6]\n"					\
		"\tadr   %2, 2f\n"					\
		"\tstr   %2, [%6, #4]\n"				\
									\
		"\t"ldr" %0, %5\n"	/* Load old value. */		\
		"\tcmp   %0, %3\n"	/* Compare to expected value. */\
		"\t"streq" %4, %1\n"	/* Store new value. */		\
									\
		/* Tear down Restartable Atomic Sequence. */		\
		"2:"							\
		"\tmov   %2, #0x00000000\n"				\
		"\tstr   %2, [%6]\n"					\
		"\tmov   %2, #0xffffffff\n"				\
		"\tstr   %2, [%6, #4]\n"				\
		: "=&r" (old), "=m" (*mem), "=&r" (temp)		\
		: "r" (expected), "r" (desired), "m" (*mem),		\
		  "r" (ras_start));					\
	return (old);							\
}

#define	EMIT_FETCH_AND_OP_N(N, uintN_t, ldr, str, name, op)		\
uintN_t									\
__sync_##name##_##N##_c(uintN_t *mem, uintN_t val)				\
{									\
	uint32_t old, temp, ras_start;					\
									\
	ras_start = ARM_RAS_START;					\
	__asm volatile (						\
		/* Set up Restartable Atomic Sequence. */		\
		"1:"							\
		"\tadr   %2, 1b\n"					\
		"\tstr   %2, [%5]\n"					\
		"\tadr   %2, 2f\n"					\
		"\tstr   %2, [%5, #4]\n"				\
									\
		"\t"ldr" %0, %4\n"	/* Load old value. */		\
		"\t"op"  %2, %0, %3\n"	/* Calculate new value. */	\
		"\t"str" %2, %1\n"	/* Store new value. */		\
									\
		/* Tear down Restartable Atomic Sequence. */		\
		"2:"							\
		"\tmov   %2, #0x00000000\n"				\
		"\tstr   %2, [%5]\n"					\
		"\tmov   %2, #0xffffffff\n"				\
		"\tstr   %2, [%5, #4]\n"				\
		: "=&r" (old), "=m" (*mem), "=&r" (temp)		\
		: "r" (val), "m" (*mem), "r" (ras_start));		\
	return (old);							\
}

#define	EMIT_ALL_OPS_N(N, uintN_t, ldr, str, streq)			\
EMIT_LOCK_TEST_AND_SET_N(N, uintN_t, ldr, str)				\
EMIT_VAL_COMPARE_AND_SWAP_N(N, uintN_t, ldr, streq)			\
EMIT_FETCH_AND_OP_N(N, uintN_t, ldr, str, fetch_and_add, "add")		\
EMIT_FETCH_AND_OP_N(N, uintN_t, ldr, str, fetch_and_and, "and")		\
EMIT_FETCH_AND_OP_N(N, uintN_t, ldr, str, fetch_and_or, "orr")		\
EMIT_FETCH_AND_OP_N(N, uintN_t, ldr, str, fetch_and_sub, "sub")		\
EMIT_FETCH_AND_OP_N(N, uintN_t, ldr, str, fetch_and_xor, "eor")

#ifdef __clang__
EMIT_ALL_OPS_N(1, uint8_t, "ldrb", "strb", "strbeq")
EMIT_ALL_OPS_N(2, uint16_t, "ldrh", "strh", "strheq")
#else
EMIT_ALL_OPS_N(1, uint8_t, "ldrb", "strb", "streqb")
EMIT_ALL_OPS_N(2, uint16_t, "ldrh", "strh", "streqh")
#endif
EMIT_ALL_OPS_N(4, uint32_t, "ldr", "str", "streq")

#ifndef __clang__
__strong_reference(__sync_lock_test_and_set_1_c, __sync_lock_test_and_set_1);
__strong_reference(__sync_lock_test_and_set_2_c, __sync_lock_test_and_set_2);
__strong_reference(__sync_lock_test_and_set_4_c, __sync_lock_test_and_set_4);
__strong_reference(__sync_val_compare_and_swap_1_c, __sync_val_compare_and_swap_1);
__strong_reference(__sync_val_compare_and_swap_2_c, __sync_val_compare_and_swap_2);
__strong_reference(__sync_val_compare_and_swap_4_c, __sync_val_compare_and_swap_4);
__strong_reference(__sync_fetch_and_add_1_c, __sync_fetch_and_add_1);
__strong_reference(__sync_fetch_and_add_2_c, __sync_fetch_and_add_2);
__strong_reference(__sync_fetch_and_add_4_c, __sync_fetch_and_add_4);
__strong_reference(__sync_fetch_and_and_1_c, __sync_fetch_and_and_1);
__strong_reference(__sync_fetch_and_and_2_c, __sync_fetch_and_and_2);
__strong_reference(__sync_fetch_and_and_4_c, __sync_fetch_and_and_4);
__strong_reference(__sync_fetch_and_sub_1_c, __sync_fetch_and_sub_1);
__strong_reference(__sync_fetch_and_sub_2_c, __sync_fetch_and_sub_2);
__strong_reference(__sync_fetch_and_sub_4_c, __sync_fetch_and_sub_4);
__strong_reference(__sync_fetch_and_or_1_c, __sync_fetch_and_or_1);
__strong_reference(__sync_fetch_and_or_2_c, __sync_fetch_and_or_2);
__strong_reference(__sync_fetch_and_or_4_c, __sync_fetch_and_or_4);
__strong_reference(__sync_fetch_and_xor_1_c, __sync_fetch_and_xor_1);
__strong_reference(__sync_fetch_and_xor_2_c, __sync_fetch_and_xor_2);
__strong_reference(__sync_fetch_and_xor_4_c, __sync_fetch_and_xor_4);
#endif /* __ARM_ARCH */

#endif /* _KERNEL */

#endif

#endif /* __SYNC_ATOMICS */

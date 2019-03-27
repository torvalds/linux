/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Copyright (c) 1998 Doug Rabson
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

#include <sys/stdatomic.h>
#include <sys/types.h>

#ifndef _KERNEL
#include <stdbool.h>
#endif /* _KERNEL */

#if defined(__SYNC_ATOMICS)

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

static inline void
do_sync(void)
{

	__asm volatile (
#if !defined(_KERNEL) || defined(SMP)
		".set noreorder\n"
		"\tsync\n"
		"\tnop\n"
		"\tnop\n"
		"\tnop\n"
		"\tnop\n"
		"\tnop\n"
		"\tnop\n"
		"\tnop\n"
		"\tnop\n"
		".set reorder\n"
#else /* _KERNEL && !SMP */
		""
#endif /* !KERNEL || SMP */
		: : : "memory");
}

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
__sync_lock_test_and_set_##N(uintN_t *mem, uintN_t val)			\
{									\
	uint32_t *mem32;						\
	reg_t val32, negmask, old;					\
	uint32_t temp;							\
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
		"\tll	%0, %5\n"	/* Load old value. */		\
		"\tand	%2, %4, %0\n"	/* Remove the old value. */	\
		"\tor	%2, %3\n"	/* Put in the new value. */	\
		"\tsc	%2, %1\n"	/* Attempt to store. */		\
		"\tbeqz	%2, 1b\n"	/* Spin if failed. */		\
		: "=&r" (old.v32), "=m" (*mem32), "=&r" (temp)		\
		: "r" (val32.v32), "r" (negmask.v32), "m" (*mem32));	\
	return (get_##N(&old, mem));					\
}

EMIT_LOCK_TEST_AND_SET_N(1, uint8_t)
EMIT_LOCK_TEST_AND_SET_N(2, uint16_t)

#define	EMIT_VAL_COMPARE_AND_SWAP_N(N, uintN_t)				\
uintN_t									\
__sync_val_compare_and_swap_##N(uintN_t *mem, uintN_t expected,		\
    uintN_t desired)							\
{									\
	uint32_t *mem32;						\
	reg_t expected32, desired32, posmask, old;			\
	uint32_t negmask, temp;						\
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
		"\tll	%0, %7\n"	/* Load old value. */		\
		"\tand	%2, %5, %0\n"	/* Isolate the old value. */	\
		"\tbne	%2, %3, 2f\n"	/* Compare to expected value. */\
		"\tand	%2, %6, %0\n"	/* Remove the old value. */	\
		"\tor	%2, %4\n"	/* Put in the new value. */	\
		"\tsc	%2, %1\n"	/* Attempt to store. */		\
		"\tbeqz	%2, 1b\n"	/* Spin if failed. */		\
		"2:"							\
		: "=&r" (old), "=m" (*mem32), "=&r" (temp)		\
		: "r" (expected32.v32), "r" (desired32.v32),		\
		  "r" (posmask.v32), "r" (negmask), "m" (*mem32));	\
	return (get_##N(&old, mem));					\
}

EMIT_VAL_COMPARE_AND_SWAP_N(1, uint8_t)
EMIT_VAL_COMPARE_AND_SWAP_N(2, uint16_t)

#define	EMIT_ARITHMETIC_FETCH_AND_OP_N(N, uintN_t, name, op)		\
uintN_t									\
__sync_##name##_##N(uintN_t *mem, uintN_t val)				\
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
		"\tll	%0, %7\n"	/* Load old value. */		\
		"\t"op"	%2, %0, %4\n"	/* Calculate new value. */	\
		"\tand	%2, %5\n"	/* Isolate the new value. */	\
		"\tand	%3, %6, %0\n"	/* Remove the old value. */	\
		"\tor	%2, %3\n"	/* Put in the new value. */	\
		"\tsc	%2, %1\n"	/* Attempt to store. */		\
		"\tbeqz	%2, 1b\n"	/* Spin if failed. */		\
		: "=&r" (old.v32), "=m" (*mem32), "=&r" (temp1),	\
		  "=&r" (temp2)						\
		: "r" (val32.v32), "r" (posmask.v32), "r" (negmask),	\
		  "m" (*mem32));					\
	return (get_##N(&old, mem));					\
}

EMIT_ARITHMETIC_FETCH_AND_OP_N(1, uint8_t, fetch_and_add, "addu")
EMIT_ARITHMETIC_FETCH_AND_OP_N(1, uint8_t, fetch_and_sub, "subu")
EMIT_ARITHMETIC_FETCH_AND_OP_N(2, uint16_t, fetch_and_add, "addu")
EMIT_ARITHMETIC_FETCH_AND_OP_N(2, uint16_t, fetch_and_sub, "subu")

#define	EMIT_BITWISE_FETCH_AND_OP_N(N, uintN_t, name, op, idempotence)	\
uintN_t									\
__sync_##name##_##N(uintN_t *mem, uintN_t val)				\
{									\
	uint32_t *mem32;						\
	reg_t val32, old;						\
	uint32_t temp;							\
									\
	mem32 = round_to_word(mem);					\
	val32.v32 = idempotence ? 0xffffffff : 0x00000000;		\
	put_##N(&val32, mem, val);					\
									\
	do_sync();							\
	__asm volatile (						\
		"1:"							\
		"\tll	%0, %4\n"	/* Load old value. */		\
		"\t"op"	%2, %3, %0\n"	/* Calculate new value. */	\
		"\tsc	%2, %1\n"	/* Attempt to store. */		\
		"\tbeqz	%2, 1b\n"	/* Spin if failed. */		\
		: "=&r" (old.v32), "=m" (*mem32), "=&r" (temp)		\
		: "r" (val32.v32), "m" (*mem32));			\
	return (get_##N(&old, mem));					\
}

EMIT_BITWISE_FETCH_AND_OP_N(1, uint8_t, fetch_and_and, "and", 1)
EMIT_BITWISE_FETCH_AND_OP_N(1, uint8_t, fetch_and_or, "or", 0)
EMIT_BITWISE_FETCH_AND_OP_N(1, uint8_t, fetch_and_xor, "xor", 0)
EMIT_BITWISE_FETCH_AND_OP_N(2, uint16_t, fetch_and_and, "and", 1)
EMIT_BITWISE_FETCH_AND_OP_N(2, uint16_t, fetch_and_or, "or", 0)
EMIT_BITWISE_FETCH_AND_OP_N(2, uint16_t, fetch_and_xor, "xor", 0)

/*
 * 32-bit routines.
 */

static __inline uint32_t
do_compare_and_swap_4(uint32_t *mem, uint32_t expected,
    uint32_t desired)
{
	uint32_t old, temp;

	do_sync();
	__asm volatile (
		"1:"
		"\tll	%0, %5\n"	/* Load old value. */
		"\tbne	%0, %3, 2f\n"	/* Compare to expected value. */
		"\tmove	%2, %4\n"	/* Value to store. */
		"\tsc	%2, %1\n"	/* Attempt to store. */
		"\tbeqz	%2, 1b\n"	/* Spin if failed. */
		"2:"
		: "=&r" (old), "=m" (*mem), "=&r" (temp)
		: "r" (expected), "r" (desired), "m" (*mem));
	return (old);
}

uint32_t
__sync_val_compare_and_swap_4(uint32_t *mem, uint32_t expected,
    uint32_t desired)
{

	return (do_compare_and_swap_4(mem, expected, desired));
}

bool
__sync_bool_compare_and_swap_4(uint32_t *mem, uint32_t expected,
    uint32_t desired)
{

	return (do_compare_and_swap_4(mem, expected, desired) ==
	    expected);
}

#define	EMIT_FETCH_AND_OP_4(name, op)					\
uint32_t								\
__sync_##name##_4(uint32_t *mem, uint32_t val)				\
{									\
	uint32_t old, temp;						\
									\
	do_sync();							\
	__asm volatile (						\
		"1:"							\
		"\tll	%0, %4\n"	/* Load old value. */		\
		"\t"op"\n"		/* Calculate new value. */	\
		"\tsc	%2, %1\n"	/* Attempt to store. */		\
		"\tbeqz	%2, 1b\n"	/* Spin if failed. */		\
		: "=&r" (old), "=m" (*mem), "=&r" (temp)		\
		: "r" (val), "m" (*mem));				\
	return (old);							\
}

EMIT_FETCH_AND_OP_4(lock_test_and_set, "move %2, %3")
EMIT_FETCH_AND_OP_4(fetch_and_add, "addu %2, %0, %3")
EMIT_FETCH_AND_OP_4(fetch_and_and, "and %2, %0, %3")
EMIT_FETCH_AND_OP_4(fetch_and_or, "or %2, %0, %3")
EMIT_FETCH_AND_OP_4(fetch_and_sub, "subu %2, %0, %3")
EMIT_FETCH_AND_OP_4(fetch_and_xor, "xor %2, %0, %3")

/*
 * 64-bit routines.
 *
 * Note: All the 64-bit atomic operations are only atomic when running
 * in 64-bit mode. It is assumed that code compiled for n32 and n64 fits
 * into this definition and no further safeties are needed.
 */

#if defined(__mips_n32) || defined(__mips_n64)

uint64_t
__sync_val_compare_and_swap_8(uint64_t *mem, uint64_t expected,
    uint64_t desired)
{
	uint64_t old, temp;

	do_sync();
	__asm volatile (
		"1:"
		"\tlld	%0, %5\n"	/* Load old value. */
		"\tbne	%0, %3, 2f\n"	/* Compare to expected value. */
		"\tmove	%2, %4\n"	/* Value to store. */
		"\tscd	%2, %1\n"	/* Attempt to store. */
		"\tbeqz	%2, 1b\n"	/* Spin if failed. */
		"2:"
		: "=&r" (old), "=m" (*mem), "=&r" (temp)
		: "r" (expected), "r" (desired), "m" (*mem));
	return (old);
}

#define	EMIT_FETCH_AND_OP_8(name, op)					\
uint64_t								\
__sync_##name##_8(uint64_t *mem, uint64_t val)				\
{									\
	uint64_t old, temp;						\
									\
	do_sync();							\
	__asm volatile (						\
		"1:"							\
		"\tlld	%0, %4\n"	/* Load old value. */		\
		"\t"op"\n"		/* Calculate new value. */	\
		"\tscd	%2, %1\n"	/* Attempt to store. */		\
		"\tbeqz	%2, 1b\n"	/* Spin if failed. */		\
		: "=&r" (old), "=m" (*mem), "=&r" (temp)		\
		: "r" (val), "m" (*mem));				\
	return (old);							\
}

EMIT_FETCH_AND_OP_8(lock_test_and_set, "move %2, %3")
EMIT_FETCH_AND_OP_8(fetch_and_add, "daddu %2, %0, %3")
EMIT_FETCH_AND_OP_8(fetch_and_and, "and %2, %0, %3")
EMIT_FETCH_AND_OP_8(fetch_and_or, "or %2, %0, %3")
EMIT_FETCH_AND_OP_8(fetch_and_sub, "dsubu %2, %0, %3")
EMIT_FETCH_AND_OP_8(fetch_and_xor, "xor %2, %0, %3")

#endif /* __mips_n32 || __mips_n64 */

#endif /* __SYNC_ATOMICS */

/*-
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

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Tool for testing the logical behaviour of operations on atomic
 * integer types. These tests make no attempt to actually test whether
 * the functions are atomic or provide the right barrier semantics.
 *
 * For every type, we create an array of 16 elements and repeat the test
 * on every element in the array. This allows us to test whether the
 * atomic operations have no effect on surrounding values. This is
 * especially useful for the smaller integer types, as it may be the
 * case that these operations are implemented by processing entire words
 * (e.g. on MIPS).
 */

static inline intmax_t
rndnum(void)
{
	intmax_t v;

	arc4random_buf(&v, sizeof(v));
	return (v);
}

#define	DO_FETCH_TEST(T, a, name, result) do {				\
	T v1 = atomic_load(a);						\
	T v2 = rndnum();						\
	assert(atomic_##name(a, v2) == v1); 				\
	assert(atomic_load(a) == (T)(result)); 				\
} while (0)

#define	DO_COMPARE_EXCHANGE_TEST(T, a, name) do {			\
	T v1 = atomic_load(a);						\
	T v2 = rndnum();						\
	T v3 = rndnum();						\
	if (atomic_compare_exchange_##name(a, &v2, v3))			\
		assert(v1 == v2);					\
	else								\
		assert(atomic_compare_exchange_##name(a, &v2, v3));	\
	assert(atomic_load(a) == v3);					\
} while (0)

#define	DO_ALL_TESTS(T, a) do {						\
	{								\
		T v1 = rndnum();					\
		atomic_init(a, v1);					\
		assert(atomic_load(a) == v1);				\
	}								\
	{								\
		T v1 = rndnum();					\
		atomic_store(a, v1);					\
		assert(atomic_load(a) == v1);				\
	}								\
									\
	DO_FETCH_TEST(T, a, exchange, v2);				\
	DO_FETCH_TEST(T, a, fetch_add, v1 + v2);			\
	DO_FETCH_TEST(T, a, fetch_and, v1 & v2);			\
	DO_FETCH_TEST(T, a, fetch_or, v1 | v2);				\
	DO_FETCH_TEST(T, a, fetch_sub, v1 - v2);			\
	DO_FETCH_TEST(T, a, fetch_xor, v1 ^ v2);			\
									\
	DO_COMPARE_EXCHANGE_TEST(T, a, weak);				\
	DO_COMPARE_EXCHANGE_TEST(T, a, strong);				\
} while (0)

#define	TEST_TYPE(T) do {						\
	int j;								\
	struct { _Atomic(T) v[16]; } list, cmp;				\
	arc4random_buf(&cmp, sizeof(cmp));				\
	for (j = 0; j < 16; j++) {					\
		list = cmp;						\
		DO_ALL_TESTS(T, &list.v[j]);				\
		list.v[j] = cmp.v[j];					\
		assert(memcmp(&list, &cmp, sizeof(list)) == 0);		\
	}								\
} while (0)

int
main(void)
{
	int i;

	for (i = 0; i < 1000; i++) {
		TEST_TYPE(int8_t);
		TEST_TYPE(uint8_t);
		TEST_TYPE(int16_t);
		TEST_TYPE(uint16_t);
		TEST_TYPE(int32_t);
		TEST_TYPE(uint32_t);
		TEST_TYPE(int64_t);
		TEST_TYPE(uint64_t);
	}

	return (0);
}

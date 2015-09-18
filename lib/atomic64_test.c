/*
 * Testsuite for atomic64_t functions
 *
 * Copyright © 2010  Luca Barbieri
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/atomic.h>

#define TEST(bit, op, c_op, val)				\
do {								\
	atomic##bit##_set(&v, v0);				\
	r = v0;							\
	atomic##bit##_##op(val, &v);				\
	r c_op val;						\
	WARN(atomic##bit##_read(&v) != r, "%Lx != %Lx\n",	\
		(unsigned long long)atomic##bit##_read(&v),	\
		(unsigned long long)r);				\
} while (0)

static __init void test_atomic(void)
{
	int v0 = 0xaaa31337;
	int v1 = 0xdeadbeef;
	int onestwos = 0x11112222;
	int one = 1;

	atomic_t v;
	int r;

	TEST(, add, +=, onestwos);
	TEST(, add, +=, -one);
	TEST(, sub, -=, onestwos);
	TEST(, sub, -=, -one);
	TEST(, or, |=, v1);
	TEST(, and, &=, v1);
	TEST(, xor, ^=, v1);
	TEST(, andnot, &= ~, v1);
}

#define INIT(c) do { atomic64_set(&v, c); r = c; } while (0)
static __init void test_atomic64(void)
{
	long long v0 = 0xaaa31337c001d00dLL;
	long long v1 = 0xdeadbeefdeafcafeLL;
	long long v2 = 0xfaceabadf00df001LL;
	long long onestwos = 0x1111111122222222LL;
	long long one = 1LL;

	atomic64_t v = ATOMIC64_INIT(v0);
	long long r = v0;
	BUG_ON(v.counter != r);

	atomic64_set(&v, v1);
	r = v1;
	BUG_ON(v.counter != r);
	BUG_ON(atomic64_read(&v) != r);

	TEST(64, add, +=, onestwos);
	TEST(64, add, +=, -one);
	TEST(64, sub, -=, onestwos);
	TEST(64, sub, -=, -one);
	TEST(64, or, |=, v1);
	TEST(64, and, &=, v1);
	TEST(64, xor, ^=, v1);
	TEST(64, andnot, &= ~, v1);

	INIT(v0);
	r += onestwos;
	BUG_ON(atomic64_add_return(onestwos, &v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	r += -one;
	BUG_ON(atomic64_add_return(-one, &v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	r -= onestwos;
	BUG_ON(atomic64_sub_return(onestwos, &v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	r -= -one;
	BUG_ON(atomic64_sub_return(-one, &v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	atomic64_inc(&v);
	r += one;
	BUG_ON(v.counter != r);

	INIT(v0);
	r += one;
	BUG_ON(atomic64_inc_return(&v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	atomic64_dec(&v);
	r -= one;
	BUG_ON(v.counter != r);

	INIT(v0);
	r -= one;
	BUG_ON(atomic64_dec_return(&v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(atomic64_xchg(&v, v1) != v0);
	r = v1;
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(atomic64_cmpxchg(&v, v0, v1) != v0);
	r = v1;
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(atomic64_cmpxchg(&v, v2, v1) != v0);
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(atomic64_add_unless(&v, one, v0));
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(!atomic64_add_unless(&v, one, v1));
	r += one;
	BUG_ON(v.counter != r);

#ifdef CONFIG_ARCH_HAS_ATOMIC64_DEC_IF_POSITIVE
	INIT(onestwos);
	BUG_ON(atomic64_dec_if_positive(&v) != (onestwos - 1));
	r -= one;
	BUG_ON(v.counter != r);

	INIT(0);
	BUG_ON(atomic64_dec_if_positive(&v) != -one);
	BUG_ON(v.counter != r);

	INIT(-one);
	BUG_ON(atomic64_dec_if_positive(&v) != (-one - one));
	BUG_ON(v.counter != r);
#else
#warning Please implement atomic64_dec_if_positive for your architecture and select the above Kconfig symbol
#endif

	INIT(onestwos);
	BUG_ON(!atomic64_inc_not_zero(&v));
	r += one;
	BUG_ON(v.counter != r);

	INIT(0);
	BUG_ON(atomic64_inc_not_zero(&v));
	BUG_ON(v.counter != r);

	INIT(-one);
	BUG_ON(!atomic64_inc_not_zero(&v));
	r += one;
	BUG_ON(v.counter != r);
}

static __init int test_atomics(void)
{
	test_atomic();
	test_atomic64();

#ifdef CONFIG_X86
	pr_info("passed for %s platform %s CX8 and %s SSE\n",
#ifdef CONFIG_X86_64
		"x86-64",
#elif defined(CONFIG_X86_CMPXCHG64)
		"i586+",
#else
		"i386+",
#endif
	       boot_cpu_has(X86_FEATURE_CX8) ? "with" : "without",
	       boot_cpu_has(X86_FEATURE_XMM) ? "with" : "without");
#else
	pr_info("passed\n");
#endif

	return 0;
}

core_initcall(test_atomics);

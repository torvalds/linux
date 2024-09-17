// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Testsuite for atomic64_t functions
 *
 * Copyright © 2010  Luca Barbieri
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/module.h>

#ifdef CONFIG_X86
#include <asm/cpufeature.h>	/* for boot_cpu_has below */
#endif

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

/*
 * Test for a atomic operation family,
 * @test should be a macro accepting parameters (bit, op, ...)
 */

#define FAMILY_TEST(test, bit, op, args...)	\
do {						\
	test(bit, op, ##args);		\
	test(bit, op##_acquire, ##args);	\
	test(bit, op##_release, ##args);	\
	test(bit, op##_relaxed, ##args);	\
} while (0)

#define TEST_RETURN(bit, op, c_op, val)				\
do {								\
	atomic##bit##_set(&v, v0);				\
	r = v0;							\
	r c_op val;						\
	BUG_ON(atomic##bit##_##op(val, &v) != r);		\
	BUG_ON(atomic##bit##_read(&v) != r);			\
} while (0)

#define TEST_FETCH(bit, op, c_op, val)				\
do {								\
	atomic##bit##_set(&v, v0);				\
	r = v0;							\
	r c_op val;						\
	BUG_ON(atomic##bit##_##op(val, &v) != v0);		\
	BUG_ON(atomic##bit##_read(&v) != r);			\
} while (0)

#define RETURN_FAMILY_TEST(bit, op, c_op, val)			\
do {								\
	FAMILY_TEST(TEST_RETURN, bit, op, c_op, val);		\
} while (0)

#define FETCH_FAMILY_TEST(bit, op, c_op, val)			\
do {								\
	FAMILY_TEST(TEST_FETCH, bit, op, c_op, val);		\
} while (0)

#define TEST_ARGS(bit, op, init, ret, expect, args...)		\
do {								\
	atomic##bit##_set(&v, init);				\
	BUG_ON(atomic##bit##_##op(&v, ##args) != ret);		\
	BUG_ON(atomic##bit##_read(&v) != expect);		\
} while (0)

#define XCHG_FAMILY_TEST(bit, init, new)				\
do {									\
	FAMILY_TEST(TEST_ARGS, bit, xchg, init, init, new, new);	\
} while (0)

#define CMPXCHG_FAMILY_TEST(bit, init, new, wrong)			\
do {									\
	FAMILY_TEST(TEST_ARGS, bit, cmpxchg, 				\
			init, init, new, init, new);			\
	FAMILY_TEST(TEST_ARGS, bit, cmpxchg,				\
			init, init, init, wrong, new);			\
} while (0)

#define INC_RETURN_FAMILY_TEST(bit, i)			\
do {							\
	FAMILY_TEST(TEST_ARGS, bit, inc_return,		\
			i, (i) + one, (i) + one);	\
} while (0)

#define DEC_RETURN_FAMILY_TEST(bit, i)			\
do {							\
	FAMILY_TEST(TEST_ARGS, bit, dec_return,		\
			i, (i) - one, (i) - one);	\
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

	RETURN_FAMILY_TEST(, add_return, +=, onestwos);
	RETURN_FAMILY_TEST(, add_return, +=, -one);
	RETURN_FAMILY_TEST(, sub_return, -=, onestwos);
	RETURN_FAMILY_TEST(, sub_return, -=, -one);

	FETCH_FAMILY_TEST(, fetch_add, +=, onestwos);
	FETCH_FAMILY_TEST(, fetch_add, +=, -one);
	FETCH_FAMILY_TEST(, fetch_sub, -=, onestwos);
	FETCH_FAMILY_TEST(, fetch_sub, -=, -one);

	FETCH_FAMILY_TEST(, fetch_or,  |=, v1);
	FETCH_FAMILY_TEST(, fetch_and, &=, v1);
	FETCH_FAMILY_TEST(, fetch_andnot, &= ~, v1);
	FETCH_FAMILY_TEST(, fetch_xor, ^=, v1);

	INC_RETURN_FAMILY_TEST(, v0);
	DEC_RETURN_FAMILY_TEST(, v0);

	XCHG_FAMILY_TEST(, v0, v1);
	CMPXCHG_FAMILY_TEST(, v0, v1, onestwos);

}

#define INIT(c) do { atomic64_set(&v, c); r = c; } while (0)
static __init void test_atomic64(void)
{
	long long v0 = 0xaaa31337c001d00dLL;
	long long v1 = 0xdeadbeefdeafcafeLL;
	long long v2 = 0xfaceabadf00df001LL;
	long long v3 = 0x8000000000000000LL;
	long long onestwos = 0x1111111122222222LL;
	long long one = 1LL;
	int r_int;

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

	RETURN_FAMILY_TEST(64, add_return, +=, onestwos);
	RETURN_FAMILY_TEST(64, add_return, +=, -one);
	RETURN_FAMILY_TEST(64, sub_return, -=, onestwos);
	RETURN_FAMILY_TEST(64, sub_return, -=, -one);

	FETCH_FAMILY_TEST(64, fetch_add, +=, onestwos);
	FETCH_FAMILY_TEST(64, fetch_add, +=, -one);
	FETCH_FAMILY_TEST(64, fetch_sub, -=, onestwos);
	FETCH_FAMILY_TEST(64, fetch_sub, -=, -one);

	FETCH_FAMILY_TEST(64, fetch_or,  |=, v1);
	FETCH_FAMILY_TEST(64, fetch_and, &=, v1);
	FETCH_FAMILY_TEST(64, fetch_andnot, &= ~, v1);
	FETCH_FAMILY_TEST(64, fetch_xor, ^=, v1);

	INIT(v0);
	atomic64_inc(&v);
	r += one;
	BUG_ON(v.counter != r);

	INIT(v0);
	atomic64_dec(&v);
	r -= one;
	BUG_ON(v.counter != r);

	INC_RETURN_FAMILY_TEST(64, v0);
	DEC_RETURN_FAMILY_TEST(64, v0);

	XCHG_FAMILY_TEST(64, v0, v1);
	CMPXCHG_FAMILY_TEST(64, v0, v1, v2);

	INIT(v0);
	BUG_ON(atomic64_add_unless(&v, one, v0));
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(!atomic64_add_unless(&v, one, v1));
	r += one;
	BUG_ON(v.counter != r);

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

	/* Confirm the return value fits in an int, even if the value doesn't */
	INIT(v3);
	r_int = atomic64_inc_not_zero(&v);
	BUG_ON(!r_int);
}

static __init int test_atomics_init(void)
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

static __exit void test_atomics_exit(void) {}

module_init(test_atomics_init);
module_exit(test_atomics_exit);

MODULE_LICENSE("GPL");

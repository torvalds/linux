// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Test cases for arithmetic overflow checks. See:
 * "Running tests with kunit_tool" at Documentation/dev-tools/kunit/start.rst
 *	./tools/testing/kunit/kunit.py run overflow [--raw_output]
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#define SKIP(cond, reason)		do {			\
	if (cond) {						\
		kunit_skip(test, reason);			\
		return;						\
	}							\
} while (0)

/*
 * Clang 11 and earlier generate unwanted libcalls for signed output
 * on unsigned input.
 */
#if defined(CONFIG_CC_IS_CLANG) && __clang_major__ <= 11
# define SKIP_SIGN_MISMATCH(t)	SKIP(t, "Clang 11 unwanted libcalls")
#else
# define SKIP_SIGN_MISMATCH(t)	do { } while (0)
#endif

/*
 * Clang 13 and earlier generate unwanted libcalls for 64-bit tests on
 * 32-bit hosts.
 */
#if defined(CONFIG_CC_IS_CLANG) && __clang_major__ <= 13 &&	\
    BITS_PER_LONG != 64
# define SKIP_64_ON_32(t)	SKIP(t, "Clang 13 unwanted libcalls")
#else
# define SKIP_64_ON_32(t)	do { } while (0)
#endif

#define DEFINE_TEST_ARRAY_TYPED(t1, t2, t)			\
	static const struct test_ ## t1 ## _ ## t2 ## __ ## t {	\
		t1 a;						\
		t2 b;						\
		t sum, diff, prod;				\
		bool s_of, d_of, p_of;				\
	} t1 ## _ ## t2 ## __ ## t ## _tests[]

#define DEFINE_TEST_ARRAY(t)	DEFINE_TEST_ARRAY_TYPED(t, t, t)

DEFINE_TEST_ARRAY(u8) = {
	{0, 0, 0, 0, 0, false, false, false},
	{1, 1, 2, 0, 1, false, false, false},
	{0, 1, 1, U8_MAX, 0, false, true, false},
	{1, 0, 1, 1, 0, false, false, false},
	{0, U8_MAX, U8_MAX, 1, 0, false, true, false},
	{U8_MAX, 0, U8_MAX, U8_MAX, 0, false, false, false},
	{1, U8_MAX, 0, 2, U8_MAX, true, true, false},
	{U8_MAX, 1, 0, U8_MAX-1, U8_MAX, true, false, false},
	{U8_MAX, U8_MAX, U8_MAX-1, 0, 1, true, false, true},

	{U8_MAX, U8_MAX-1, U8_MAX-2, 1, 2, true, false, true},
	{U8_MAX-1, U8_MAX, U8_MAX-2, U8_MAX, 2, true, true, true},

	{1U << 3, 1U << 3, 1U << 4, 0, 1U << 6, false, false, false},
	{1U << 4, 1U << 4, 1U << 5, 0, 0, false, false, true},
	{1U << 4, 1U << 3, 3*(1U << 3), 1U << 3, 1U << 7, false, false, false},
	{1U << 7, 1U << 7, 0, 0, 0, true, false, true},

	{48, 32, 80, 16, 0, false, false, true},
	{128, 128, 0, 0, 0, true, false, true},
	{123, 234, 101, 145, 110, true, true, true},
};
DEFINE_TEST_ARRAY(u16) = {
	{0, 0, 0, 0, 0, false, false, false},
	{1, 1, 2, 0, 1, false, false, false},
	{0, 1, 1, U16_MAX, 0, false, true, false},
	{1, 0, 1, 1, 0, false, false, false},
	{0, U16_MAX, U16_MAX, 1, 0, false, true, false},
	{U16_MAX, 0, U16_MAX, U16_MAX, 0, false, false, false},
	{1, U16_MAX, 0, 2, U16_MAX, true, true, false},
	{U16_MAX, 1, 0, U16_MAX-1, U16_MAX, true, false, false},
	{U16_MAX, U16_MAX, U16_MAX-1, 0, 1, true, false, true},

	{U16_MAX, U16_MAX-1, U16_MAX-2, 1, 2, true, false, true},
	{U16_MAX-1, U16_MAX, U16_MAX-2, U16_MAX, 2, true, true, true},

	{1U << 7, 1U << 7, 1U << 8, 0, 1U << 14, false, false, false},
	{1U << 8, 1U << 8, 1U << 9, 0, 0, false, false, true},
	{1U << 8, 1U << 7, 3*(1U << 7), 1U << 7, 1U << 15, false, false, false},
	{1U << 15, 1U << 15, 0, 0, 0, true, false, true},

	{123, 234, 357, 65425, 28782, false, true, false},
	{1234, 2345, 3579, 64425, 10146, false, true, true},
};
DEFINE_TEST_ARRAY(u32) = {
	{0, 0, 0, 0, 0, false, false, false},
	{1, 1, 2, 0, 1, false, false, false},
	{0, 1, 1, U32_MAX, 0, false, true, false},
	{1, 0, 1, 1, 0, false, false, false},
	{0, U32_MAX, U32_MAX, 1, 0, false, true, false},
	{U32_MAX, 0, U32_MAX, U32_MAX, 0, false, false, false},
	{1, U32_MAX, 0, 2, U32_MAX, true, true, false},
	{U32_MAX, 1, 0, U32_MAX-1, U32_MAX, true, false, false},
	{U32_MAX, U32_MAX, U32_MAX-1, 0, 1, true, false, true},

	{U32_MAX, U32_MAX-1, U32_MAX-2, 1, 2, true, false, true},
	{U32_MAX-1, U32_MAX, U32_MAX-2, U32_MAX, 2, true, true, true},

	{1U << 15, 1U << 15, 1U << 16, 0, 1U << 30, false, false, false},
	{1U << 16, 1U << 16, 1U << 17, 0, 0, false, false, true},
	{1U << 16, 1U << 15, 3*(1U << 15), 1U << 15, 1U << 31, false, false, false},
	{1U << 31, 1U << 31, 0, 0, 0, true, false, true},

	{-2U, 1U, -1U, -3U, -2U, false, false, false},
	{-4U, 5U, 1U, -9U, -20U, true, false, true},
};

DEFINE_TEST_ARRAY(u64) = {
	{0, 0, 0, 0, 0, false, false, false},
	{1, 1, 2, 0, 1, false, false, false},
	{0, 1, 1, U64_MAX, 0, false, true, false},
	{1, 0, 1, 1, 0, false, false, false},
	{0, U64_MAX, U64_MAX, 1, 0, false, true, false},
	{U64_MAX, 0, U64_MAX, U64_MAX, 0, false, false, false},
	{1, U64_MAX, 0, 2, U64_MAX, true, true, false},
	{U64_MAX, 1, 0, U64_MAX-1, U64_MAX, true, false, false},
	{U64_MAX, U64_MAX, U64_MAX-1, 0, 1, true, false, true},

	{U64_MAX, U64_MAX-1, U64_MAX-2, 1, 2, true, false, true},
	{U64_MAX-1, U64_MAX, U64_MAX-2, U64_MAX, 2, true, true, true},

	{1ULL << 31, 1ULL << 31, 1ULL << 32, 0, 1ULL << 62, false, false, false},
	{1ULL << 32, 1ULL << 32, 1ULL << 33, 0, 0, false, false, true},
	{1ULL << 32, 1ULL << 31, 3*(1ULL << 31), 1ULL << 31, 1ULL << 63, false, false, false},
	{1ULL << 63, 1ULL << 63, 0, 0, 0, true, false, true},
	{1000000000ULL /* 10^9 */, 10000000000ULL /* 10^10 */,
	 11000000000ULL, 18446744064709551616ULL, 10000000000000000000ULL,
	 false, true, false},
	{-15ULL, 10ULL, -5ULL, -25ULL, -150ULL, false, false, true},
};

DEFINE_TEST_ARRAY(s8) = {
	{0, 0, 0, 0, 0, false, false, false},

	{0, S8_MAX, S8_MAX, -S8_MAX, 0, false, false, false},
	{S8_MAX, 0, S8_MAX, S8_MAX, 0, false, false, false},
	{0, S8_MIN, S8_MIN, S8_MIN, 0, false, true, false},
	{S8_MIN, 0, S8_MIN, S8_MIN, 0, false, false, false},

	{-1, S8_MIN, S8_MAX, S8_MAX, S8_MIN, true, false, true},
	{S8_MIN, -1, S8_MAX, -S8_MAX, S8_MIN, true, false, true},
	{-1, S8_MAX, S8_MAX-1, S8_MIN, -S8_MAX, false, false, false},
	{S8_MAX, -1, S8_MAX-1, S8_MIN, -S8_MAX, false, true, false},
	{-1, -S8_MAX, S8_MIN, S8_MAX-1, S8_MAX, false, false, false},
	{-S8_MAX, -1, S8_MIN, S8_MIN+2, S8_MAX, false, false, false},

	{1, S8_MIN, -S8_MAX, -S8_MAX, S8_MIN, false, true, false},
	{S8_MIN, 1, -S8_MAX, S8_MAX, S8_MIN, false, true, false},
	{1, S8_MAX, S8_MIN, S8_MIN+2, S8_MAX, true, false, false},
	{S8_MAX, 1, S8_MIN, S8_MAX-1, S8_MAX, true, false, false},

	{S8_MIN, S8_MIN, 0, 0, 0, true, false, true},
	{S8_MAX, S8_MAX, -2, 0, 1, true, false, true},

	{-4, -32, -36, 28, -128, false, false, true},
	{-4, 32, 28, -36, -128, false, false, false},
};

DEFINE_TEST_ARRAY(s16) = {
	{0, 0, 0, 0, 0, false, false, false},

	{0, S16_MAX, S16_MAX, -S16_MAX, 0, false, false, false},
	{S16_MAX, 0, S16_MAX, S16_MAX, 0, false, false, false},
	{0, S16_MIN, S16_MIN, S16_MIN, 0, false, true, false},
	{S16_MIN, 0, S16_MIN, S16_MIN, 0, false, false, false},

	{-1, S16_MIN, S16_MAX, S16_MAX, S16_MIN, true, false, true},
	{S16_MIN, -1, S16_MAX, -S16_MAX, S16_MIN, true, false, true},
	{-1, S16_MAX, S16_MAX-1, S16_MIN, -S16_MAX, false, false, false},
	{S16_MAX, -1, S16_MAX-1, S16_MIN, -S16_MAX, false, true, false},
	{-1, -S16_MAX, S16_MIN, S16_MAX-1, S16_MAX, false, false, false},
	{-S16_MAX, -1, S16_MIN, S16_MIN+2, S16_MAX, false, false, false},

	{1, S16_MIN, -S16_MAX, -S16_MAX, S16_MIN, false, true, false},
	{S16_MIN, 1, -S16_MAX, S16_MAX, S16_MIN, false, true, false},
	{1, S16_MAX, S16_MIN, S16_MIN+2, S16_MAX, true, false, false},
	{S16_MAX, 1, S16_MIN, S16_MAX-1, S16_MAX, true, false, false},

	{S16_MIN, S16_MIN, 0, 0, 0, true, false, true},
	{S16_MAX, S16_MAX, -2, 0, 1, true, false, true},
};
DEFINE_TEST_ARRAY(s32) = {
	{0, 0, 0, 0, 0, false, false, false},

	{0, S32_MAX, S32_MAX, -S32_MAX, 0, false, false, false},
	{S32_MAX, 0, S32_MAX, S32_MAX, 0, false, false, false},
	{0, S32_MIN, S32_MIN, S32_MIN, 0, false, true, false},
	{S32_MIN, 0, S32_MIN, S32_MIN, 0, false, false, false},

	{-1, S32_MIN, S32_MAX, S32_MAX, S32_MIN, true, false, true},
	{S32_MIN, -1, S32_MAX, -S32_MAX, S32_MIN, true, false, true},
	{-1, S32_MAX, S32_MAX-1, S32_MIN, -S32_MAX, false, false, false},
	{S32_MAX, -1, S32_MAX-1, S32_MIN, -S32_MAX, false, true, false},
	{-1, -S32_MAX, S32_MIN, S32_MAX-1, S32_MAX, false, false, false},
	{-S32_MAX, -1, S32_MIN, S32_MIN+2, S32_MAX, false, false, false},

	{1, S32_MIN, -S32_MAX, -S32_MAX, S32_MIN, false, true, false},
	{S32_MIN, 1, -S32_MAX, S32_MAX, S32_MIN, false, true, false},
	{1, S32_MAX, S32_MIN, S32_MIN+2, S32_MAX, true, false, false},
	{S32_MAX, 1, S32_MIN, S32_MAX-1, S32_MAX, true, false, false},

	{S32_MIN, S32_MIN, 0, 0, 0, true, false, true},
	{S32_MAX, S32_MAX, -2, 0, 1, true, false, true},
};

DEFINE_TEST_ARRAY(s64) = {
	{0, 0, 0, 0, 0, false, false, false},

	{0, S64_MAX, S64_MAX, -S64_MAX, 0, false, false, false},
	{S64_MAX, 0, S64_MAX, S64_MAX, 0, false, false, false},
	{0, S64_MIN, S64_MIN, S64_MIN, 0, false, true, false},
	{S64_MIN, 0, S64_MIN, S64_MIN, 0, false, false, false},

	{-1, S64_MIN, S64_MAX, S64_MAX, S64_MIN, true, false, true},
	{S64_MIN, -1, S64_MAX, -S64_MAX, S64_MIN, true, false, true},
	{-1, S64_MAX, S64_MAX-1, S64_MIN, -S64_MAX, false, false, false},
	{S64_MAX, -1, S64_MAX-1, S64_MIN, -S64_MAX, false, true, false},
	{-1, -S64_MAX, S64_MIN, S64_MAX-1, S64_MAX, false, false, false},
	{-S64_MAX, -1, S64_MIN, S64_MIN+2, S64_MAX, false, false, false},

	{1, S64_MIN, -S64_MAX, -S64_MAX, S64_MIN, false, true, false},
	{S64_MIN, 1, -S64_MAX, S64_MAX, S64_MIN, false, true, false},
	{1, S64_MAX, S64_MIN, S64_MIN+2, S64_MAX, true, false, false},
	{S64_MAX, 1, S64_MIN, S64_MAX-1, S64_MAX, true, false, false},

	{S64_MIN, S64_MIN, 0, 0, 0, true, false, true},
	{S64_MAX, S64_MAX, -2, 0, 1, true, false, true},

	{-1, -1, -2, 0, 1, false, false, false},
	{-1, -128, -129, 127, 128, false, false, false},
	{-128, -1, -129, -127, 128, false, false, false},
	{0, -S64_MAX, -S64_MAX, S64_MAX, 0, false, false, false},
};

#define check_one_op(t, fmt, op, sym, a, b, r, of) do {			\
	int _a_orig = a, _a_bump = a + 1;				\
	int _b_orig = b, _b_bump = b + 1;				\
	bool _of;							\
	t _r;								\
									\
	_of = check_ ## op ## _overflow(a, b, &_r);			\
	KUNIT_EXPECT_EQ_MSG(test, _of, of,				\
		"expected "fmt" "sym" "fmt" to%s overflow (type %s)\n",	\
		a, b, of ? "" : " not", #t);				\
	KUNIT_EXPECT_EQ_MSG(test, _r, r,				\
		"expected "fmt" "sym" "fmt" == "fmt", got "fmt" (type %s)\n", \
		a, b, r, _r, #t);					\
	/* Check for internal macro side-effects. */			\
	_of = check_ ## op ## _overflow(_a_orig++, _b_orig++, &_r);	\
	KUNIT_EXPECT_EQ_MSG(test, _a_orig, _a_bump, "Unexpected " #op " macro side-effect!\n"); \
	KUNIT_EXPECT_EQ_MSG(test, _b_orig, _b_bump, "Unexpected " #op " macro side-effect!\n"); \
} while (0)

#define DEFINE_TEST_FUNC_TYPED(n, t, fmt)				\
static void do_test_ ## n(struct kunit *test, const struct test_ ## n *p) \
{									\
	check_one_op(t, fmt, add, "+", p->a, p->b, p->sum, p->s_of);	\
	check_one_op(t, fmt, add, "+", p->b, p->a, p->sum, p->s_of);	\
	check_one_op(t, fmt, sub, "-", p->a, p->b, p->diff, p->d_of);	\
	check_one_op(t, fmt, mul, "*", p->a, p->b, p->prod, p->p_of);	\
	check_one_op(t, fmt, mul, "*", p->b, p->a, p->prod, p->p_of);	\
}									\
									\
static void n ## _overflow_test(struct kunit *test) {			\
	unsigned i;							\
									\
	SKIP_64_ON_32(__same_type(t, u64));				\
	SKIP_64_ON_32(__same_type(t, s64));				\
	SKIP_SIGN_MISMATCH(__same_type(n ## _tests[0].a, u32) &&	\
			   __same_type(n ## _tests[0].b, u32) &&	\
			   __same_type(n ## _tests[0].sum, int));	\
									\
	for (i = 0; i < ARRAY_SIZE(n ## _tests); ++i)			\
		do_test_ ## n(test, &n ## _tests[i]);			\
	kunit_info(test, "%zu %s arithmetic tests finished\n",		\
		ARRAY_SIZE(n ## _tests), #n);				\
}

#define DEFINE_TEST_FUNC(t, fmt)					\
	DEFINE_TEST_FUNC_TYPED(t ## _ ## t ## __ ## t, t, fmt)

DEFINE_TEST_FUNC(u8, "%d");
DEFINE_TEST_FUNC(s8, "%d");
DEFINE_TEST_FUNC(u16, "%d");
DEFINE_TEST_FUNC(s16, "%d");
DEFINE_TEST_FUNC(u32, "%u");
DEFINE_TEST_FUNC(s32, "%d");
DEFINE_TEST_FUNC(u64, "%llu");
DEFINE_TEST_FUNC(s64, "%lld");

DEFINE_TEST_ARRAY_TYPED(u32, u32, u8) = {
	{0, 0, 0, 0, 0, false, false, false},
	{U8_MAX, 2, 1, U8_MAX - 2, U8_MAX - 1, true, false, true},
	{U8_MAX + 1, 0, 0, 0, 0, true, true, false},
};
DEFINE_TEST_FUNC_TYPED(u32_u32__u8, u8, "%d");

DEFINE_TEST_ARRAY_TYPED(u32, u32, int) = {
	{0, 0, 0, 0, 0, false, false, false},
	{U32_MAX, 0, -1, -1, 0, true, true, false},
};
DEFINE_TEST_FUNC_TYPED(u32_u32__int, int, "%d");

DEFINE_TEST_ARRAY_TYPED(u8, u8, int) = {
	{0, 0, 0, 0, 0, false, false, false},
	{U8_MAX, U8_MAX, 2 * U8_MAX, 0, U8_MAX * U8_MAX, false, false, false},
	{1, 2, 3, -1, 2, false, false, false},
};
DEFINE_TEST_FUNC_TYPED(u8_u8__int, int, "%d");

DEFINE_TEST_ARRAY_TYPED(int, int, u8) = {
	{0, 0, 0, 0, 0, false, false, false},
	{1, 2, 3, U8_MAX, 2, false, true, false},
	{-1, 0, U8_MAX, U8_MAX, 0, true, true, false},
};
DEFINE_TEST_FUNC_TYPED(int_int__u8, u8, "%d");

/* Args are: value, shift, type, expected result, overflow expected */
#define TEST_ONE_SHIFT(a, s, t, expect, of)	do {			\
	typeof(a) __a = (a);						\
	typeof(s) __s = (s);						\
	t __e = (expect);						\
	t __d;								\
	bool __of = check_shl_overflow(__a, __s, &__d);			\
	if (__of != of) {						\
		KUNIT_EXPECT_EQ_MSG(test, __of, of,			\
			"expected (%s)(%s << %s) to%s overflow\n",	\
			#t, #a, #s, of ? "" : " not");			\
	} else if (!__of && __d != __e) {				\
		KUNIT_EXPECT_EQ_MSG(test, __d, __e,			\
			"expected (%s)(%s << %s) == %s\n",		\
			#t, #a, #s, #expect);				\
		if ((t)-1 < 0)						\
			kunit_info(test, "got %lld\n", (s64)__d);	\
		else							\
			kunit_info(test, "got %llu\n", (u64)__d);	\
	}								\
	count++;							\
} while (0)

static void shift_sane_test(struct kunit *test)
{
	int count = 0;

	/* Sane shifts. */
	TEST_ONE_SHIFT(1, 0, u8, 1 << 0, false);
	TEST_ONE_SHIFT(1, 4, u8, 1 << 4, false);
	TEST_ONE_SHIFT(1, 7, u8, 1 << 7, false);
	TEST_ONE_SHIFT(0xF, 4, u8, 0xF << 4, false);
	TEST_ONE_SHIFT(1, 0, u16, 1 << 0, false);
	TEST_ONE_SHIFT(1, 10, u16, 1 << 10, false);
	TEST_ONE_SHIFT(1, 15, u16, 1 << 15, false);
	TEST_ONE_SHIFT(0xFF, 8, u16, 0xFF << 8, false);
	TEST_ONE_SHIFT(1, 0, int, 1 << 0, false);
	TEST_ONE_SHIFT(1, 16, int, 1 << 16, false);
	TEST_ONE_SHIFT(1, 30, int, 1 << 30, false);
	TEST_ONE_SHIFT(1, 0, s32, 1 << 0, false);
	TEST_ONE_SHIFT(1, 16, s32, 1 << 16, false);
	TEST_ONE_SHIFT(1, 30, s32, 1 << 30, false);
	TEST_ONE_SHIFT(1, 0, unsigned int, 1U << 0, false);
	TEST_ONE_SHIFT(1, 20, unsigned int, 1U << 20, false);
	TEST_ONE_SHIFT(1, 31, unsigned int, 1U << 31, false);
	TEST_ONE_SHIFT(0xFFFFU, 16, unsigned int, 0xFFFFU << 16, false);
	TEST_ONE_SHIFT(1, 0, u32, 1U << 0, false);
	TEST_ONE_SHIFT(1, 20, u32, 1U << 20, false);
	TEST_ONE_SHIFT(1, 31, u32, 1U << 31, false);
	TEST_ONE_SHIFT(0xFFFFU, 16, u32, 0xFFFFU << 16, false);
	TEST_ONE_SHIFT(1, 0, u64, 1ULL << 0, false);
	TEST_ONE_SHIFT(1, 40, u64, 1ULL << 40, false);
	TEST_ONE_SHIFT(1, 63, u64, 1ULL << 63, false);
	TEST_ONE_SHIFT(0xFFFFFFFFULL, 32, u64, 0xFFFFFFFFULL << 32, false);

	/* Sane shift: start and end with 0, without a too-wide shift. */
	TEST_ONE_SHIFT(0, 7, u8, 0, false);
	TEST_ONE_SHIFT(0, 15, u16, 0, false);
	TEST_ONE_SHIFT(0, 31, unsigned int, 0, false);
	TEST_ONE_SHIFT(0, 31, u32, 0, false);
	TEST_ONE_SHIFT(0, 63, u64, 0, false);

	/* Sane shift: start and end with 0, without reaching signed bit. */
	TEST_ONE_SHIFT(0, 6, s8, 0, false);
	TEST_ONE_SHIFT(0, 14, s16, 0, false);
	TEST_ONE_SHIFT(0, 30, int, 0, false);
	TEST_ONE_SHIFT(0, 30, s32, 0, false);
	TEST_ONE_SHIFT(0, 62, s64, 0, false);

	kunit_info(test, "%d sane shift tests finished\n", count);
}

static void shift_overflow_test(struct kunit *test)
{
	int count = 0;

	/* Overflow: shifted the bit off the end. */
	TEST_ONE_SHIFT(1, 8, u8, 0, true);
	TEST_ONE_SHIFT(1, 16, u16, 0, true);
	TEST_ONE_SHIFT(1, 32, unsigned int, 0, true);
	TEST_ONE_SHIFT(1, 32, u32, 0, true);
	TEST_ONE_SHIFT(1, 64, u64, 0, true);

	/* Overflow: shifted into the signed bit. */
	TEST_ONE_SHIFT(1, 7, s8, 0, true);
	TEST_ONE_SHIFT(1, 15, s16, 0, true);
	TEST_ONE_SHIFT(1, 31, int, 0, true);
	TEST_ONE_SHIFT(1, 31, s32, 0, true);
	TEST_ONE_SHIFT(1, 63, s64, 0, true);

	/* Overflow: high bit falls off unsigned types. */
	/* 10010110 */
	TEST_ONE_SHIFT(150, 1, u8, 0, true);
	/* 1000100010010110 */
	TEST_ONE_SHIFT(34966, 1, u16, 0, true);
	/* 10000100000010001000100010010110 */
	TEST_ONE_SHIFT(2215151766U, 1, u32, 0, true);
	TEST_ONE_SHIFT(2215151766U, 1, unsigned int, 0, true);
	/* 1000001000010000010000000100000010000100000010001000100010010110 */
	TEST_ONE_SHIFT(9372061470395238550ULL, 1, u64, 0, true);

	/* Overflow: bit shifted into signed bit on signed types. */
	/* 01001011 */
	TEST_ONE_SHIFT(75, 1, s8, 0, true);
	/* 0100010001001011 */
	TEST_ONE_SHIFT(17483, 1, s16, 0, true);
	/* 01000010000001000100010001001011 */
	TEST_ONE_SHIFT(1107575883, 1, s32, 0, true);
	TEST_ONE_SHIFT(1107575883, 1, int, 0, true);
	/* 0100000100001000001000000010000001000010000001000100010001001011 */
	TEST_ONE_SHIFT(4686030735197619275LL, 1, s64, 0, true);

	/* Overflow: bit shifted past signed bit on signed types. */
	/* 01001011 */
	TEST_ONE_SHIFT(75, 2, s8, 0, true);
	/* 0100010001001011 */
	TEST_ONE_SHIFT(17483, 2, s16, 0, true);
	/* 01000010000001000100010001001011 */
	TEST_ONE_SHIFT(1107575883, 2, s32, 0, true);
	TEST_ONE_SHIFT(1107575883, 2, int, 0, true);
	/* 0100000100001000001000000010000001000010000001000100010001001011 */
	TEST_ONE_SHIFT(4686030735197619275LL, 2, s64, 0, true);

	kunit_info(test, "%d overflow shift tests finished\n", count);
}

static void shift_truncate_test(struct kunit *test)
{
	int count = 0;

	/* Overflow: values larger than destination type. */
	TEST_ONE_SHIFT(0x100, 0, u8, 0, true);
	TEST_ONE_SHIFT(0xFF, 0, s8, 0, true);
	TEST_ONE_SHIFT(0x10000U, 0, u16, 0, true);
	TEST_ONE_SHIFT(0xFFFFU, 0, s16, 0, true);
	TEST_ONE_SHIFT(0x100000000ULL, 0, u32, 0, true);
	TEST_ONE_SHIFT(0x100000000ULL, 0, unsigned int, 0, true);
	TEST_ONE_SHIFT(0xFFFFFFFFUL, 0, s32, 0, true);
	TEST_ONE_SHIFT(0xFFFFFFFFUL, 0, int, 0, true);
	TEST_ONE_SHIFT(0xFFFFFFFFFFFFFFFFULL, 0, s64, 0, true);

	/* Overflow: shifted at or beyond entire type's bit width. */
	TEST_ONE_SHIFT(0, 8, u8, 0, true);
	TEST_ONE_SHIFT(0, 9, u8, 0, true);
	TEST_ONE_SHIFT(0, 8, s8, 0, true);
	TEST_ONE_SHIFT(0, 9, s8, 0, true);
	TEST_ONE_SHIFT(0, 16, u16, 0, true);
	TEST_ONE_SHIFT(0, 17, u16, 0, true);
	TEST_ONE_SHIFT(0, 16, s16, 0, true);
	TEST_ONE_SHIFT(0, 17, s16, 0, true);
	TEST_ONE_SHIFT(0, 32, u32, 0, true);
	TEST_ONE_SHIFT(0, 33, u32, 0, true);
	TEST_ONE_SHIFT(0, 32, int, 0, true);
	TEST_ONE_SHIFT(0, 33, int, 0, true);
	TEST_ONE_SHIFT(0, 32, s32, 0, true);
	TEST_ONE_SHIFT(0, 33, s32, 0, true);
	TEST_ONE_SHIFT(0, 64, u64, 0, true);
	TEST_ONE_SHIFT(0, 65, u64, 0, true);
	TEST_ONE_SHIFT(0, 64, s64, 0, true);
	TEST_ONE_SHIFT(0, 65, s64, 0, true);

	kunit_info(test, "%d truncate shift tests finished\n", count);
}

static void shift_nonsense_test(struct kunit *test)
{
	int count = 0;

	/* Nonsense: negative initial value. */
	TEST_ONE_SHIFT(-1, 0, s8, 0, true);
	TEST_ONE_SHIFT(-1, 0, u8, 0, true);
	TEST_ONE_SHIFT(-5, 0, s16, 0, true);
	TEST_ONE_SHIFT(-5, 0, u16, 0, true);
	TEST_ONE_SHIFT(-10, 0, int, 0, true);
	TEST_ONE_SHIFT(-10, 0, unsigned int, 0, true);
	TEST_ONE_SHIFT(-100, 0, s32, 0, true);
	TEST_ONE_SHIFT(-100, 0, u32, 0, true);
	TEST_ONE_SHIFT(-10000, 0, s64, 0, true);
	TEST_ONE_SHIFT(-10000, 0, u64, 0, true);

	/* Nonsense: negative shift values. */
	TEST_ONE_SHIFT(0, -5, s8, 0, true);
	TEST_ONE_SHIFT(0, -5, u8, 0, true);
	TEST_ONE_SHIFT(0, -10, s16, 0, true);
	TEST_ONE_SHIFT(0, -10, u16, 0, true);
	TEST_ONE_SHIFT(0, -15, int, 0, true);
	TEST_ONE_SHIFT(0, -15, unsigned int, 0, true);
	TEST_ONE_SHIFT(0, -20, s32, 0, true);
	TEST_ONE_SHIFT(0, -20, u32, 0, true);
	TEST_ONE_SHIFT(0, -30, s64, 0, true);
	TEST_ONE_SHIFT(0, -30, u64, 0, true);

	/*
	 * Corner case: for unsigned types, we fail when we've shifted
	 * through the entire width of bits. For signed types, we might
	 * want to match this behavior, but that would mean noticing if
	 * we shift through all but the signed bit, and this is not
	 * currently detected (but we'll notice an overflow into the
	 * signed bit). So, for now, we will test this condition but
	 * mark it as not expected to overflow.
	 */
	TEST_ONE_SHIFT(0, 7, s8, 0, false);
	TEST_ONE_SHIFT(0, 15, s16, 0, false);
	TEST_ONE_SHIFT(0, 31, int, 0, false);
	TEST_ONE_SHIFT(0, 31, s32, 0, false);
	TEST_ONE_SHIFT(0, 63, s64, 0, false);

	kunit_info(test, "%d nonsense shift tests finished\n", count);
}
#undef TEST_ONE_SHIFT

/*
 * Deal with the various forms of allocator arguments. See comments above
 * the DEFINE_TEST_ALLOC() instances for mapping of the "bits".
 */
#define alloc_GFP		 (GFP_KERNEL | __GFP_NOWARN)
#define alloc010(alloc, arg, sz) alloc(sz, alloc_GFP)
#define alloc011(alloc, arg, sz) alloc(sz, alloc_GFP, NUMA_NO_NODE)
#define alloc000(alloc, arg, sz) alloc(sz)
#define alloc001(alloc, arg, sz) alloc(sz, NUMA_NO_NODE)
#define alloc110(alloc, arg, sz) alloc(arg, sz, alloc_GFP)
#define free0(free, arg, ptr)	 free(ptr)
#define free1(free, arg, ptr)	 free(arg, ptr)

/* Wrap around to 16K */
#define TEST_SIZE		(5 * 4096)

#define DEFINE_TEST_ALLOC(func, free_func, want_arg, want_gfp, want_node)\
static void test_ ## func (struct kunit *test, void *arg)		\
{									\
	volatile size_t a = TEST_SIZE;					\
	volatile size_t b = (SIZE_MAX / TEST_SIZE) + 1;			\
	void *ptr;							\
									\
	/* Tiny allocation test. */					\
	ptr = alloc ## want_arg ## want_gfp ## want_node (func, arg, 1);\
	KUNIT_ASSERT_NOT_ERR_OR_NULL_MSG(test, ptr,			\
			    #func " failed regular allocation?!\n");	\
	free ## want_arg (free_func, arg, ptr);				\
									\
	/* Wrapped allocation test. */					\
	ptr = alloc ## want_arg ## want_gfp ## want_node (func, arg,	\
							  a * b);	\
	KUNIT_ASSERT_NOT_ERR_OR_NULL_MSG(test, ptr,			\
			    #func " unexpectedly failed bad wrapping?!\n"); \
	free ## want_arg (free_func, arg, ptr);				\
									\
	/* Saturated allocation test. */				\
	ptr = alloc ## want_arg ## want_gfp ## want_node (func, arg,	\
						   array_size(a, b));	\
	if (ptr) {							\
		KUNIT_FAIL(test, #func " missed saturation!\n");	\
		free ## want_arg (free_func, arg, ptr);			\
	}								\
}

/*
 * Allocator uses a trailing node argument --------+  (e.g. kmalloc_node())
 * Allocator uses the gfp_t argument -----------+  |  (e.g. kmalloc())
 * Allocator uses a special leading argument +  |  |  (e.g. devm_kmalloc())
 *                                           |  |  |
 */
DEFINE_TEST_ALLOC(kmalloc,	 kfree,	     0, 1, 0);
DEFINE_TEST_ALLOC(kmalloc_node,	 kfree,	     0, 1, 1);
DEFINE_TEST_ALLOC(kzalloc,	 kfree,	     0, 1, 0);
DEFINE_TEST_ALLOC(kzalloc_node,  kfree,	     0, 1, 1);
DEFINE_TEST_ALLOC(__vmalloc,	 vfree,	     0, 1, 0);
DEFINE_TEST_ALLOC(kvmalloc,	 kvfree,     0, 1, 0);
DEFINE_TEST_ALLOC(kvmalloc_node, kvfree,     0, 1, 1);
DEFINE_TEST_ALLOC(kvzalloc,	 kvfree,     0, 1, 0);
DEFINE_TEST_ALLOC(kvzalloc_node, kvfree,     0, 1, 1);
DEFINE_TEST_ALLOC(devm_kmalloc,  devm_kfree, 1, 1, 0);
DEFINE_TEST_ALLOC(devm_kzalloc,  devm_kfree, 1, 1, 0);

static void overflow_allocation_test(struct kunit *test)
{
	const char device_name[] = "overflow-test";
	struct device *dev;
	int count = 0;

#define check_allocation_overflow(alloc)	do {	\
	count++;					\
	test_ ## alloc(test, dev);			\
} while (0)

	/* Create dummy device for devm_kmalloc()-family tests. */
	dev = root_device_register(device_name);
	KUNIT_ASSERT_FALSE_MSG(test, IS_ERR(dev),
			       "Cannot register test device\n");

	check_allocation_overflow(kmalloc);
	check_allocation_overflow(kmalloc_node);
	check_allocation_overflow(kzalloc);
	check_allocation_overflow(kzalloc_node);
	check_allocation_overflow(__vmalloc);
	check_allocation_overflow(kvmalloc);
	check_allocation_overflow(kvmalloc_node);
	check_allocation_overflow(kvzalloc);
	check_allocation_overflow(kvzalloc_node);
	check_allocation_overflow(devm_kmalloc);
	check_allocation_overflow(devm_kzalloc);

	device_unregister(dev);

	kunit_info(test, "%d allocation overflow tests finished\n", count);
#undef check_allocation_overflow
}

struct __test_flex_array {
	unsigned long flags;
	size_t count;
	unsigned long data[];
};

static void overflow_size_helpers_test(struct kunit *test)
{
	/* Make sure struct_size() can be used in a constant expression. */
	u8 ce_array[struct_size((struct __test_flex_array *)0, data, 55)];
	struct __test_flex_array *obj;
	int count = 0;
	int var;
	volatile int unconst = 0;

	/* Verify constant expression against runtime version. */
	var = 55;
	OPTIMIZER_HIDE_VAR(var);
	KUNIT_EXPECT_EQ(test, sizeof(ce_array), struct_size(obj, data, var));

#define check_one_size_helper(expected, func, args...)	do {	\
	size_t _r = func(args);					\
	KUNIT_EXPECT_EQ_MSG(test, _r, expected,			\
		"expected " #func "(" #args ") to return %zu but got %zu instead\n", \
		(size_t)(expected), _r);			\
	count++;						\
} while (0)

	var = 4;
	check_one_size_helper(20,	size_mul, var++, 5);
	check_one_size_helper(20,	size_mul, 4, var++);
	check_one_size_helper(0,	size_mul, 0, 3);
	check_one_size_helper(0,	size_mul, 3, 0);
	check_one_size_helper(6,	size_mul, 2, 3);
	check_one_size_helper(SIZE_MAX,	size_mul, SIZE_MAX,  1);
	check_one_size_helper(SIZE_MAX,	size_mul, SIZE_MAX,  3);
	check_one_size_helper(SIZE_MAX,	size_mul, SIZE_MAX, -3);

	var = 4;
	check_one_size_helper(9,	size_add, var++, 5);
	check_one_size_helper(9,	size_add, 4, var++);
	check_one_size_helper(9,	size_add, 9, 0);
	check_one_size_helper(9,	size_add, 0, 9);
	check_one_size_helper(5,	size_add, 2, 3);
	check_one_size_helper(SIZE_MAX, size_add, SIZE_MAX,  1);
	check_one_size_helper(SIZE_MAX, size_add, SIZE_MAX,  3);
	check_one_size_helper(SIZE_MAX, size_add, SIZE_MAX, -3);

	var = 4;
	check_one_size_helper(1,	size_sub, var--, 3);
	check_one_size_helper(1,	size_sub, 4, var--);
	check_one_size_helper(1,	size_sub, 3, 2);
	check_one_size_helper(9,	size_sub, 9, 0);
	check_one_size_helper(SIZE_MAX, size_sub, 9, -3);
	check_one_size_helper(SIZE_MAX, size_sub, 0, 9);
	check_one_size_helper(SIZE_MAX, size_sub, 2, 3);
	check_one_size_helper(SIZE_MAX, size_sub, SIZE_MAX,  0);
	check_one_size_helper(SIZE_MAX, size_sub, SIZE_MAX, 10);
	check_one_size_helper(SIZE_MAX, size_sub, 0,  SIZE_MAX);
	check_one_size_helper(SIZE_MAX, size_sub, 14, SIZE_MAX);
	check_one_size_helper(SIZE_MAX - 2, size_sub, SIZE_MAX - 1,  1);
	check_one_size_helper(SIZE_MAX - 4, size_sub, SIZE_MAX - 1,  3);
	check_one_size_helper(1,		size_sub, SIZE_MAX - 1, -3);

	var = 4;
	check_one_size_helper(4 * sizeof(*obj->data),
			      flex_array_size, obj, data, var++);
	check_one_size_helper(5 * sizeof(*obj->data),
			      flex_array_size, obj, data, var++);
	check_one_size_helper(0, flex_array_size, obj, data, 0 + unconst);
	check_one_size_helper(sizeof(*obj->data),
			      flex_array_size, obj, data, 1 + unconst);
	check_one_size_helper(7 * sizeof(*obj->data),
			      flex_array_size, obj, data, 7 + unconst);
	check_one_size_helper(SIZE_MAX,
			      flex_array_size, obj, data, -1 + unconst);
	check_one_size_helper(SIZE_MAX,
			      flex_array_size, obj, data, SIZE_MAX - 4 + unconst);

	var = 4;
	check_one_size_helper(sizeof(*obj) + (4 * sizeof(*obj->data)),
			      struct_size, obj, data, var++);
	check_one_size_helper(sizeof(*obj) + (5 * sizeof(*obj->data)),
			      struct_size, obj, data, var++);
	check_one_size_helper(sizeof(*obj), struct_size, obj, data, 0 + unconst);
	check_one_size_helper(sizeof(*obj) + sizeof(*obj->data),
			      struct_size, obj, data, 1 + unconst);
	check_one_size_helper(SIZE_MAX,
			      struct_size, obj, data, -3 + unconst);
	check_one_size_helper(SIZE_MAX,
			      struct_size, obj, data, SIZE_MAX - 3 + unconst);

	kunit_info(test, "%d overflow size helper tests finished\n", count);
#undef check_one_size_helper
}

static struct kunit_case overflow_test_cases[] = {
	KUNIT_CASE(u8_u8__u8_overflow_test),
	KUNIT_CASE(s8_s8__s8_overflow_test),
	KUNIT_CASE(u16_u16__u16_overflow_test),
	KUNIT_CASE(s16_s16__s16_overflow_test),
	KUNIT_CASE(u32_u32__u32_overflow_test),
	KUNIT_CASE(s32_s32__s32_overflow_test),
	KUNIT_CASE(u64_u64__u64_overflow_test),
	KUNIT_CASE(s64_s64__s64_overflow_test),
	KUNIT_CASE(u32_u32__int_overflow_test),
	KUNIT_CASE(u32_u32__u8_overflow_test),
	KUNIT_CASE(u8_u8__int_overflow_test),
	KUNIT_CASE(int_int__u8_overflow_test),
	KUNIT_CASE(shift_sane_test),
	KUNIT_CASE(shift_overflow_test),
	KUNIT_CASE(shift_truncate_test),
	KUNIT_CASE(shift_nonsense_test),
	KUNIT_CASE(overflow_allocation_test),
	KUNIT_CASE(overflow_size_helpers_test),
	{}
};

static struct kunit_suite overflow_test_suite = {
	.name = "overflow",
	.test_cases = overflow_test_cases,
};

kunit_test_suite(overflow_test_suite);

MODULE_LICENSE("Dual MIT/GPL");

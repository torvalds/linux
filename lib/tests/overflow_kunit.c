// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Test cases for arithmetic overflow checks. See:
 * "Running tests with kunit_tool" at Documentation/dev-tools/kunit/start.rst
 *	./tools/testing/kunit/kunit.py run overflow [--raw_output]
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/device.h>
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
		"expected check "fmt" "sym" "fmt" to%s overflow (type %s)\n",	\
		a, b, of ? "" : " not", #t);				\
	KUNIT_EXPECT_EQ_MSG(test, _r, r,				\
		"expected check "fmt" "sym" "fmt" == "fmt", got "fmt" (type %s)\n", \
		a, b, r, _r, #t);					\
	/* Check for internal macro side-effects. */			\
	_of = check_ ## op ## _overflow(_a_orig++, _b_orig++, &_r);	\
	KUNIT_EXPECT_EQ_MSG(test, _a_orig, _a_bump,			\
		"Unexpected check " #op " macro side-effect!\n");	\
	KUNIT_EXPECT_EQ_MSG(test, _b_orig, _b_bump,			\
		"Unexpected check " #op " macro side-effect!\n");	\
									\
	_r = wrapping_ ## op(t, a, b);					\
	KUNIT_EXPECT_TRUE_MSG(test, _r == r,				\
		"expected wrap "fmt" "sym" "fmt" == "fmt", got "fmt" (type %s)\n", \
		a, b, r, _r, #t);					\
	/* Check for internal macro side-effects. */			\
	_a_orig = a;							\
	_b_orig = b;							\
	_r = wrapping_ ## op(t, _a_orig++, _b_orig++);			\
	KUNIT_EXPECT_EQ_MSG(test, _a_orig, _a_bump,			\
		"Unexpected wrap " #op " macro side-effect!\n");	\
	KUNIT_EXPECT_EQ_MSG(test, _b_orig, _b_bump,			\
		"Unexpected wrap " #op " macro side-effect!\n");	\
} while (0)

static int global_counter;
static void bump_counter(void)
{
	global_counter++;
}

static int get_index(void)
{
	volatile int index = 0;
	bump_counter();
	return index;
}

#define check_self_op(fmt, op, sym, a, b) do {				\
	typeof(a + 0) _a = a;						\
	typeof(b + 0) _b = b;						\
	typeof(a + 0) _a_sym = a;					\
	typeof(a + 0) _a_orig[1] = { a };				\
	typeof(b + 0) _b_orig = b;					\
	typeof(b + 0) _b_bump = b + 1;					\
	typeof(a + 0) _r;						\
									\
	_a_sym sym _b;							\
	_r = wrapping_ ## op(_a, _b);					\
	KUNIT_EXPECT_TRUE_MSG(test, _r == _a_sym,			\
		"expected "fmt" "#op" "fmt" == "fmt", got "fmt"\n",	\
		a, b, _a_sym, _r);					\
	KUNIT_EXPECT_TRUE_MSG(test, _a == _a_sym,			\
		"expected "fmt" "#op" "fmt" == "fmt", got "fmt"\n",	\
		a, b, _a_sym, _a);					\
	/* Check for internal macro side-effects. */			\
	global_counter = 0;						\
	wrapping_ ## op(_a_orig[get_index()], _b_orig++);		\
	KUNIT_EXPECT_EQ_MSG(test, global_counter, 1,			\
		"Unexpected wrapping_" #op " macro side-effect on arg1!\n"); \
	KUNIT_EXPECT_EQ_MSG(test, _b_orig, _b_bump,			\
		"Unexpected wrapping_" #op " macro side-effect on arg2!\n"); \
} while (0)

#define DEFINE_TEST_FUNC_TYPED(n, t, fmt)				\
static void do_test_ ## n(struct kunit *test, const struct test_ ## n *p) \
{									\
	/* check_{add,sub,mul}_overflow() and wrapping_{add,sub,mul} */	\
	check_one_op(t, fmt, add, "+", p->a, p->b, p->sum, p->s_of);	\
	check_one_op(t, fmt, add, "+", p->b, p->a, p->sum, p->s_of);	\
	check_one_op(t, fmt, sub, "-", p->a, p->b, p->diff, p->d_of);	\
	check_one_op(t, fmt, mul, "*", p->a, p->b, p->prod, p->p_of);	\
	check_one_op(t, fmt, mul, "*", p->b, p->a, p->prod, p->p_of);	\
	/* wrapping_assign_{add,sub}() */				\
	check_self_op(fmt, assign_add, +=, p->a, p->b);			\
	check_self_op(fmt, assign_add, +=, p->b, p->a);			\
	check_self_op(fmt, assign_sub, -=, p->a, p->b);			\
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
	struct device *dev;
	int count = 0;

#define check_allocation_overflow(alloc)	do {	\
	count++;					\
	test_ ## alloc(test, dev);			\
} while (0)

	/* Create dummy device for devm_kmalloc()-family tests. */
	dev = kunit_device_register(test, "overflow-test");
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
	u8 ce_array[struct_size_t(struct __test_flex_array, data, 55)];
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

static void overflows_type_test(struct kunit *test)
{
	int count = 0;
	unsigned int var;

#define __TEST_OVERFLOWS_TYPE(func, arg1, arg2, of)	do {		\
	bool __of = func(arg1, arg2);					\
	KUNIT_EXPECT_EQ_MSG(test, __of, of,				\
		"expected " #func "(" #arg1 ", " #arg2 " to%s overflow\n",\
		of ? "" : " not");					\
	count++;							\
} while (0)

/* Args are: first type, second type, value, overflow expected */
#define TEST_OVERFLOWS_TYPE(__t1, __t2, v, of) do {			\
	__t1 t1 = (v);							\
	__t2 t2;							\
	__TEST_OVERFLOWS_TYPE(__overflows_type, t1, t2, of);		\
	__TEST_OVERFLOWS_TYPE(__overflows_type, t1, __t2, of);		\
	__TEST_OVERFLOWS_TYPE(__overflows_type_constexpr, t1, t2, of);	\
	__TEST_OVERFLOWS_TYPE(__overflows_type_constexpr, t1, __t2, of);\
} while (0)

	TEST_OVERFLOWS_TYPE(u8, u8, U8_MAX, false);
	TEST_OVERFLOWS_TYPE(u8, u16, U8_MAX, false);
	TEST_OVERFLOWS_TYPE(u8, s8, U8_MAX, true);
	TEST_OVERFLOWS_TYPE(u8, s8, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(u8, s8, (u8)S8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u8, s16, U8_MAX, false);
	TEST_OVERFLOWS_TYPE(s8, u8, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(s8, u8, -1, true);
	TEST_OVERFLOWS_TYPE(s8, u8, S8_MIN, true);
	TEST_OVERFLOWS_TYPE(s8, u16, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(s8, u16, -1, true);
	TEST_OVERFLOWS_TYPE(s8, u16, S8_MIN, true);
	TEST_OVERFLOWS_TYPE(s8, u32, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(s8, u32, -1, true);
	TEST_OVERFLOWS_TYPE(s8, u32, S8_MIN, true);
#if BITS_PER_LONG == 64
	TEST_OVERFLOWS_TYPE(s8, u64, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(s8, u64, -1, true);
	TEST_OVERFLOWS_TYPE(s8, u64, S8_MIN, true);
#endif
	TEST_OVERFLOWS_TYPE(s8, s8, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(s8, s8, S8_MIN, false);
	TEST_OVERFLOWS_TYPE(s8, s16, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(s8, s16, S8_MIN, false);
	TEST_OVERFLOWS_TYPE(u16, u8, U8_MAX, false);
	TEST_OVERFLOWS_TYPE(u16, u8, (u16)U8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u16, u8, U16_MAX, true);
	TEST_OVERFLOWS_TYPE(u16, s8, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(u16, s8, (u16)S8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u16, s8, U16_MAX, true);
	TEST_OVERFLOWS_TYPE(u16, s16, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(u16, s16, (u16)S16_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u16, s16, U16_MAX, true);
	TEST_OVERFLOWS_TYPE(u16, u32, U16_MAX, false);
	TEST_OVERFLOWS_TYPE(u16, s32, U16_MAX, false);
	TEST_OVERFLOWS_TYPE(s16, u8, U8_MAX, false);
	TEST_OVERFLOWS_TYPE(s16, u8, (s16)U8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s16, u8, -1, true);
	TEST_OVERFLOWS_TYPE(s16, u8, S16_MIN, true);
	TEST_OVERFLOWS_TYPE(s16, u16, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(s16, u16, -1, true);
	TEST_OVERFLOWS_TYPE(s16, u16, S16_MIN, true);
	TEST_OVERFLOWS_TYPE(s16, u32, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(s16, u32, -1, true);
	TEST_OVERFLOWS_TYPE(s16, u32, S16_MIN, true);
#if BITS_PER_LONG == 64
	TEST_OVERFLOWS_TYPE(s16, u64, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(s16, u64, -1, true);
	TEST_OVERFLOWS_TYPE(s16, u64, S16_MIN, true);
#endif
	TEST_OVERFLOWS_TYPE(s16, s8, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(s16, s8, S8_MIN, false);
	TEST_OVERFLOWS_TYPE(s16, s8, (s16)S8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s16, s8, (s16)S8_MIN - 1, true);
	TEST_OVERFLOWS_TYPE(s16, s8, S16_MAX, true);
	TEST_OVERFLOWS_TYPE(s16, s8, S16_MIN, true);
	TEST_OVERFLOWS_TYPE(s16, s16, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(s16, s16, S16_MIN, false);
	TEST_OVERFLOWS_TYPE(s16, s32, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(s16, s32, S16_MIN, false);
	TEST_OVERFLOWS_TYPE(u32, u8, U8_MAX, false);
	TEST_OVERFLOWS_TYPE(u32, u8, (u32)U8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u32, u8, U32_MAX, true);
	TEST_OVERFLOWS_TYPE(u32, s8, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(u32, s8, (u32)S8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u32, s8, U32_MAX, true);
	TEST_OVERFLOWS_TYPE(u32, u16, U16_MAX, false);
	TEST_OVERFLOWS_TYPE(u32, u16, U16_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u32, u16, U32_MAX, true);
	TEST_OVERFLOWS_TYPE(u32, s16, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(u32, s16, (u32)S16_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u32, s16, U32_MAX, true);
	TEST_OVERFLOWS_TYPE(u32, u32, U32_MAX, false);
	TEST_OVERFLOWS_TYPE(u32, s32, S32_MAX, false);
	TEST_OVERFLOWS_TYPE(u32, s32, U32_MAX, true);
	TEST_OVERFLOWS_TYPE(u32, s32, (u32)S32_MAX + 1, true);
#if BITS_PER_LONG == 64
	TEST_OVERFLOWS_TYPE(u32, u64, U32_MAX, false);
	TEST_OVERFLOWS_TYPE(u32, s64, U32_MAX, false);
#endif
	TEST_OVERFLOWS_TYPE(s32, u8, U8_MAX, false);
	TEST_OVERFLOWS_TYPE(s32, u8, (s32)U8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s32, u16, S32_MAX, true);
	TEST_OVERFLOWS_TYPE(s32, u8, -1, true);
	TEST_OVERFLOWS_TYPE(s32, u8, S32_MIN, true);
	TEST_OVERFLOWS_TYPE(s32, u16, U16_MAX, false);
	TEST_OVERFLOWS_TYPE(s32, u16, (s32)U16_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s32, u16, S32_MAX, true);
	TEST_OVERFLOWS_TYPE(s32, u16, -1, true);
	TEST_OVERFLOWS_TYPE(s32, u16, S32_MIN, true);
	TEST_OVERFLOWS_TYPE(s32, u32, S32_MAX, false);
	TEST_OVERFLOWS_TYPE(s32, u32, -1, true);
	TEST_OVERFLOWS_TYPE(s32, u32, S32_MIN, true);
#if BITS_PER_LONG == 64
	TEST_OVERFLOWS_TYPE(s32, u64, S32_MAX, false);
	TEST_OVERFLOWS_TYPE(s32, u64, -1, true);
	TEST_OVERFLOWS_TYPE(s32, u64, S32_MIN, true);
#endif
	TEST_OVERFLOWS_TYPE(s32, s8, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(s32, s8, S8_MIN, false);
	TEST_OVERFLOWS_TYPE(s32, s8, (s32)S8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s32, s8, (s32)S8_MIN - 1, true);
	TEST_OVERFLOWS_TYPE(s32, s8, S32_MAX, true);
	TEST_OVERFLOWS_TYPE(s32, s8, S32_MIN, true);
	TEST_OVERFLOWS_TYPE(s32, s16, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(s32, s16, S16_MIN, false);
	TEST_OVERFLOWS_TYPE(s32, s16, (s32)S16_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s32, s16, (s32)S16_MIN - 1, true);
	TEST_OVERFLOWS_TYPE(s32, s16, S32_MAX, true);
	TEST_OVERFLOWS_TYPE(s32, s16, S32_MIN, true);
	TEST_OVERFLOWS_TYPE(s32, s32, S32_MAX, false);
	TEST_OVERFLOWS_TYPE(s32, s32, S32_MIN, false);
#if BITS_PER_LONG == 64
	TEST_OVERFLOWS_TYPE(s32, s64, S32_MAX, false);
	TEST_OVERFLOWS_TYPE(s32, s64, S32_MIN, false);
	TEST_OVERFLOWS_TYPE(u64, u8, U64_MAX, true);
	TEST_OVERFLOWS_TYPE(u64, u8, U8_MAX, false);
	TEST_OVERFLOWS_TYPE(u64, u8, (u64)U8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u64, u16, U64_MAX, true);
	TEST_OVERFLOWS_TYPE(u64, u16, U16_MAX, false);
	TEST_OVERFLOWS_TYPE(u64, u16, (u64)U16_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u64, u32, U64_MAX, true);
	TEST_OVERFLOWS_TYPE(u64, u32, U32_MAX, false);
	TEST_OVERFLOWS_TYPE(u64, u32, (u64)U32_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u64, u64, U64_MAX, false);
	TEST_OVERFLOWS_TYPE(u64, s8, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(u64, s8, (u64)S8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u64, s8, U64_MAX, true);
	TEST_OVERFLOWS_TYPE(u64, s16, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(u64, s16, (u64)S16_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u64, s16, U64_MAX, true);
	TEST_OVERFLOWS_TYPE(u64, s32, S32_MAX, false);
	TEST_OVERFLOWS_TYPE(u64, s32, (u64)S32_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(u64, s32, U64_MAX, true);
	TEST_OVERFLOWS_TYPE(u64, s64, S64_MAX, false);
	TEST_OVERFLOWS_TYPE(u64, s64, U64_MAX, true);
	TEST_OVERFLOWS_TYPE(u64, s64, (u64)S64_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s64, u8, S64_MAX, true);
	TEST_OVERFLOWS_TYPE(s64, u8, S64_MIN, true);
	TEST_OVERFLOWS_TYPE(s64, u8, -1, true);
	TEST_OVERFLOWS_TYPE(s64, u8, U8_MAX, false);
	TEST_OVERFLOWS_TYPE(s64, u8, (s64)U8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s64, u16, S64_MAX, true);
	TEST_OVERFLOWS_TYPE(s64, u16, S64_MIN, true);
	TEST_OVERFLOWS_TYPE(s64, u16, -1, true);
	TEST_OVERFLOWS_TYPE(s64, u16, U16_MAX, false);
	TEST_OVERFLOWS_TYPE(s64, u16, (s64)U16_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s64, u32, S64_MAX, true);
	TEST_OVERFLOWS_TYPE(s64, u32, S64_MIN, true);
	TEST_OVERFLOWS_TYPE(s64, u32, -1, true);
	TEST_OVERFLOWS_TYPE(s64, u32, U32_MAX, false);
	TEST_OVERFLOWS_TYPE(s64, u32, (s64)U32_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s64, u64, S64_MAX, false);
	TEST_OVERFLOWS_TYPE(s64, u64, S64_MIN, true);
	TEST_OVERFLOWS_TYPE(s64, u64, -1, true);
	TEST_OVERFLOWS_TYPE(s64, s8, S8_MAX, false);
	TEST_OVERFLOWS_TYPE(s64, s8, S8_MIN, false);
	TEST_OVERFLOWS_TYPE(s64, s8, (s64)S8_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s64, s8, (s64)S8_MIN - 1, true);
	TEST_OVERFLOWS_TYPE(s64, s8, S64_MAX, true);
	TEST_OVERFLOWS_TYPE(s64, s16, S16_MAX, false);
	TEST_OVERFLOWS_TYPE(s64, s16, S16_MIN, false);
	TEST_OVERFLOWS_TYPE(s64, s16, (s64)S16_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s64, s16, (s64)S16_MIN - 1, true);
	TEST_OVERFLOWS_TYPE(s64, s16, S64_MAX, true);
	TEST_OVERFLOWS_TYPE(s64, s32, S32_MAX, false);
	TEST_OVERFLOWS_TYPE(s64, s32, S32_MIN, false);
	TEST_OVERFLOWS_TYPE(s64, s32, (s64)S32_MAX + 1, true);
	TEST_OVERFLOWS_TYPE(s64, s32, (s64)S32_MIN - 1, true);
	TEST_OVERFLOWS_TYPE(s64, s32, S64_MAX, true);
	TEST_OVERFLOWS_TYPE(s64, s64, S64_MAX, false);
	TEST_OVERFLOWS_TYPE(s64, s64, S64_MIN, false);
#endif

	/* Check for macro side-effects. */
	var = INT_MAX - 1;
	__TEST_OVERFLOWS_TYPE(__overflows_type, var++, int, false);
	__TEST_OVERFLOWS_TYPE(__overflows_type, var++, int, false);
	__TEST_OVERFLOWS_TYPE(__overflows_type, var++, int, true);
	var = INT_MAX - 1;
	__TEST_OVERFLOWS_TYPE(overflows_type, var++, int, false);
	__TEST_OVERFLOWS_TYPE(overflows_type, var++, int, false);
	__TEST_OVERFLOWS_TYPE(overflows_type, var++, int, true);

	kunit_info(test, "%d overflows_type() tests finished\n", count);
#undef TEST_OVERFLOWS_TYPE
#undef __TEST_OVERFLOWS_TYPE
}

static void same_type_test(struct kunit *test)
{
	int count = 0;
	int var;

#define TEST_SAME_TYPE(t1, t2, same)			do {	\
	typeof(t1) __t1h = type_max(t1);			\
	typeof(t1) __t1l = type_min(t1);			\
	typeof(t2) __t2h = type_max(t2);			\
	typeof(t2) __t2l = type_min(t2);			\
	KUNIT_EXPECT_EQ(test, true, __same_type(t1, __t1h));	\
	KUNIT_EXPECT_EQ(test, true, __same_type(t1, __t1l));	\
	KUNIT_EXPECT_EQ(test, true, __same_type(__t1h, t1));	\
	KUNIT_EXPECT_EQ(test, true, __same_type(__t1l, t1));	\
	KUNIT_EXPECT_EQ(test, true, __same_type(t2, __t2h));	\
	KUNIT_EXPECT_EQ(test, true, __same_type(t2, __t2l));	\
	KUNIT_EXPECT_EQ(test, true, __same_type(__t2h, t2));	\
	KUNIT_EXPECT_EQ(test, true, __same_type(__t2l, t2));	\
	KUNIT_EXPECT_EQ(test, same, __same_type(t1, t2));	\
	KUNIT_EXPECT_EQ(test, same, __same_type(t2, __t1h));	\
	KUNIT_EXPECT_EQ(test, same, __same_type(t2, __t1l));	\
	KUNIT_EXPECT_EQ(test, same, __same_type(__t1h, t2));	\
	KUNIT_EXPECT_EQ(test, same, __same_type(__t1l, t2));	\
	KUNIT_EXPECT_EQ(test, same, __same_type(t1, __t2h));	\
	KUNIT_EXPECT_EQ(test, same, __same_type(t1, __t2l));	\
	KUNIT_EXPECT_EQ(test, same, __same_type(__t2h, t1));	\
	KUNIT_EXPECT_EQ(test, same, __same_type(__t2l, t1));	\
} while (0)

#if BITS_PER_LONG == 64
# define TEST_SAME_TYPE64(base, t, m)	TEST_SAME_TYPE(base, t, m)
#else
# define TEST_SAME_TYPE64(base, t, m)	do { } while (0)
#endif

#define TEST_TYPE_SETS(base, mu8, mu16, mu32, ms8, ms16, ms32, mu64, ms64) \
do {									\
	TEST_SAME_TYPE(base,  u8,  mu8);				\
	TEST_SAME_TYPE(base, u16, mu16);				\
	TEST_SAME_TYPE(base, u32, mu32);				\
	TEST_SAME_TYPE(base,  s8,  ms8);				\
	TEST_SAME_TYPE(base, s16, ms16);				\
	TEST_SAME_TYPE(base, s32, ms32);				\
	TEST_SAME_TYPE64(base, u64, mu64);				\
	TEST_SAME_TYPE64(base, s64, ms64);				\
} while (0)

	TEST_TYPE_SETS(u8,   true, false, false, false, false, false, false, false);
	TEST_TYPE_SETS(u16, false,  true, false, false, false, false, false, false);
	TEST_TYPE_SETS(u32, false, false,  true, false, false, false, false, false);
	TEST_TYPE_SETS(s8,  false, false, false,  true, false, false, false, false);
	TEST_TYPE_SETS(s16, false, false, false, false,  true, false, false, false);
	TEST_TYPE_SETS(s32, false, false, false, false, false,  true, false, false);
#if BITS_PER_LONG == 64
	TEST_TYPE_SETS(u64, false, false, false, false, false, false,  true, false);
	TEST_TYPE_SETS(s64, false, false, false, false, false, false, false,  true);
#endif

	/* Check for macro side-effects. */
	var = 4;
	KUNIT_EXPECT_EQ(test, var, 4);
	KUNIT_EXPECT_TRUE(test, __same_type(var++, int));
	KUNIT_EXPECT_EQ(test, var, 4);
	KUNIT_EXPECT_TRUE(test, __same_type(int, var++));
	KUNIT_EXPECT_EQ(test, var, 4);
	KUNIT_EXPECT_TRUE(test, __same_type(var++, var++));
	KUNIT_EXPECT_EQ(test, var, 4);

	kunit_info(test, "%d __same_type() tests finished\n", count);

#undef TEST_TYPE_SETS
#undef TEST_SAME_TYPE64
#undef TEST_SAME_TYPE
}

static void castable_to_type_test(struct kunit *test)
{
	int count = 0;

#define TEST_CASTABLE_TO_TYPE(arg1, arg2, pass)	do {	\
	bool __pass = castable_to_type(arg1, arg2);		\
	KUNIT_EXPECT_EQ_MSG(test, __pass, pass,			\
		"expected castable_to_type(" #arg1 ", " #arg2 ") to%s pass\n",\
		pass ? "" : " not");				\
	count++;						\
} while (0)

	TEST_CASTABLE_TO_TYPE(16, u8, true);
	TEST_CASTABLE_TO_TYPE(16, u16, true);
	TEST_CASTABLE_TO_TYPE(16, u32, true);
	TEST_CASTABLE_TO_TYPE(16, s8, true);
	TEST_CASTABLE_TO_TYPE(16, s16, true);
	TEST_CASTABLE_TO_TYPE(16, s32, true);
	TEST_CASTABLE_TO_TYPE(-16, s8, true);
	TEST_CASTABLE_TO_TYPE(-16, s16, true);
	TEST_CASTABLE_TO_TYPE(-16, s32, true);
#if BITS_PER_LONG == 64
	TEST_CASTABLE_TO_TYPE(16, u64, true);
	TEST_CASTABLE_TO_TYPE(-16, s64, true);
#endif

#define TEST_CASTABLE_TO_TYPE_VAR(width)	do {				\
	u ## width u ## width ## var = 0;					\
	s ## width s ## width ## var = 0;					\
										\
	/* Constant expressions that fit types. */				\
	TEST_CASTABLE_TO_TYPE(type_max(u ## width), u ## width, true);		\
	TEST_CASTABLE_TO_TYPE(type_min(u ## width), u ## width, true);		\
	TEST_CASTABLE_TO_TYPE(type_max(u ## width), u ## width ## var, true);	\
	TEST_CASTABLE_TO_TYPE(type_min(u ## width), u ## width ## var, true);	\
	TEST_CASTABLE_TO_TYPE(type_max(s ## width), s ## width, true);		\
	TEST_CASTABLE_TO_TYPE(type_min(s ## width), s ## width, true);		\
	TEST_CASTABLE_TO_TYPE(type_max(s ## width), s ## width ## var, true);	\
	TEST_CASTABLE_TO_TYPE(type_min(u ## width), s ## width ## var, true);	\
	/* Constant expressions that do not fit types. */			\
	TEST_CASTABLE_TO_TYPE(type_max(u ## width), s ## width, false);		\
	TEST_CASTABLE_TO_TYPE(type_max(u ## width), s ## width ## var, false);	\
	TEST_CASTABLE_TO_TYPE(type_min(s ## width), u ## width, false);		\
	TEST_CASTABLE_TO_TYPE(type_min(s ## width), u ## width ## var, false);	\
	/* Non-constant expression with mismatched type. */			\
	TEST_CASTABLE_TO_TYPE(s ## width ## var, u ## width, false);		\
	TEST_CASTABLE_TO_TYPE(u ## width ## var, s ## width, false);		\
} while (0)

#define TEST_CASTABLE_TO_TYPE_RANGE(width)	do {				\
	unsigned long big = U ## width ## _MAX;					\
	signed long small = S ## width ## _MIN;					\
	u ## width u ## width ## var = 0;					\
	s ## width s ## width ## var = 0;					\
										\
	/* Constant expression in range. */					\
	TEST_CASTABLE_TO_TYPE(U ## width ## _MAX, u ## width, true);		\
	TEST_CASTABLE_TO_TYPE(U ## width ## _MAX, u ## width ## var, true);	\
	TEST_CASTABLE_TO_TYPE(S ## width ## _MIN, s ## width, true);		\
	TEST_CASTABLE_TO_TYPE(S ## width ## _MIN, s ## width ## var, true);	\
	/* Constant expression out of range. */					\
	TEST_CASTABLE_TO_TYPE((unsigned long)U ## width ## _MAX + 1, u ## width, false); \
	TEST_CASTABLE_TO_TYPE((unsigned long)U ## width ## _MAX + 1, u ## width ## var, false); \
	TEST_CASTABLE_TO_TYPE((signed long)S ## width ## _MIN - 1, s ## width, false); \
	TEST_CASTABLE_TO_TYPE((signed long)S ## width ## _MIN - 1, s ## width ## var, false); \
	/* Non-constant expression with mismatched type. */			\
	TEST_CASTABLE_TO_TYPE(big, u ## width, false);				\
	TEST_CASTABLE_TO_TYPE(big, u ## width ## var, false);			\
	TEST_CASTABLE_TO_TYPE(small, s ## width, false);			\
	TEST_CASTABLE_TO_TYPE(small, s ## width ## var, false);			\
} while (0)

	TEST_CASTABLE_TO_TYPE_VAR(8);
	TEST_CASTABLE_TO_TYPE_VAR(16);
	TEST_CASTABLE_TO_TYPE_VAR(32);
#if BITS_PER_LONG == 64
	TEST_CASTABLE_TO_TYPE_VAR(64);
#endif

	TEST_CASTABLE_TO_TYPE_RANGE(8);
	TEST_CASTABLE_TO_TYPE_RANGE(16);
#if BITS_PER_LONG == 64
	TEST_CASTABLE_TO_TYPE_RANGE(32);
#endif
	kunit_info(test, "%d castable_to_type() tests finished\n", count);

#undef TEST_CASTABLE_TO_TYPE_RANGE
#undef TEST_CASTABLE_TO_TYPE_VAR
#undef TEST_CASTABLE_TO_TYPE
}

struct foo {
	int a;
	u32 counter;
	s16 array[] __counted_by(counter);
};

struct bar {
	int a;
	u32 counter;
	s16 array[];
};

static void DEFINE_FLEX_test(struct kunit *test)
{
	DEFINE_RAW_FLEX(struct bar, two, array, 2);
	DEFINE_FLEX(struct foo, eight, array, counter, 8);
	DEFINE_FLEX(struct foo, empty, array, counter, 0);
	/* Using _RAW_ on a __counted_by struct will initialize "counter" to zero */
	DEFINE_RAW_FLEX(struct foo, two_but_zero, array, 2);
	int array_size_override = 0;

	KUNIT_EXPECT_EQ(test, sizeof(*two), sizeof(struct bar));
	KUNIT_EXPECT_EQ(test, __struct_size(two), sizeof(struct bar) + 2 * sizeof(s16));
	KUNIT_EXPECT_EQ(test, __member_size(two), sizeof(struct bar) + 2 * sizeof(s16));
	KUNIT_EXPECT_EQ(test, __struct_size(two->array), 2 * sizeof(s16));
	KUNIT_EXPECT_EQ(test, __member_size(two->array), 2 * sizeof(s16));

	KUNIT_EXPECT_EQ(test, sizeof(*eight), sizeof(struct foo));
	KUNIT_EXPECT_EQ(test, __struct_size(eight), sizeof(struct foo) + 8 * sizeof(s16));
	KUNIT_EXPECT_EQ(test, __member_size(eight), sizeof(struct foo) + 8 * sizeof(s16));
	KUNIT_EXPECT_EQ(test, __struct_size(eight->array), 8 * sizeof(s16));
	KUNIT_EXPECT_EQ(test, __member_size(eight->array), 8 * sizeof(s16));

	KUNIT_EXPECT_EQ(test, sizeof(*empty), sizeof(struct foo));
	KUNIT_EXPECT_EQ(test, __struct_size(empty), sizeof(struct foo));
	KUNIT_EXPECT_EQ(test, __member_size(empty), sizeof(struct foo));
	KUNIT_EXPECT_EQ(test, __struct_size(empty->array), 0);
	KUNIT_EXPECT_EQ(test, __member_size(empty->array), 0);

	/* If __counted_by is not being used, array size will have the on-stack size. */
	if (!IS_ENABLED(CONFIG_CC_HAS_COUNTED_BY))
		array_size_override = 2 * sizeof(s16);

	KUNIT_EXPECT_EQ(test, sizeof(*two_but_zero), sizeof(struct foo));
	KUNIT_EXPECT_EQ(test, __struct_size(two_but_zero), sizeof(struct foo) + 2 * sizeof(s16));
	KUNIT_EXPECT_EQ(test, __member_size(two_but_zero), sizeof(struct foo) + 2 * sizeof(s16));
	KUNIT_EXPECT_EQ(test, __struct_size(two_but_zero->array), array_size_override);
	KUNIT_EXPECT_EQ(test, __member_size(two_but_zero->array), array_size_override);
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
	KUNIT_CASE(overflows_type_test),
	KUNIT_CASE(same_type_test),
	KUNIT_CASE(castable_to_type_test),
	KUNIT_CASE(DEFINE_FLEX_test),
	{}
};

static struct kunit_suite overflow_test_suite = {
	.name = "overflow",
	.test_cases = overflow_test_cases,
};

kunit_test_suite(overflow_test_suite);

MODULE_DESCRIPTION("Test cases for arithmetic overflow checks");
MODULE_LICENSE("Dual MIT/GPL");

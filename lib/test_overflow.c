// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Test cases for arithmetic overflow checks.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#define DEFINE_TEST_ARRAY(t)			\
	static const struct test_ ## t {	\
		t a, b;				\
		t sum, diff, prod;		\
		bool s_of, d_of, p_of;		\
	} t ## _tests[] __initconst

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

#define check_one_op(t, fmt, op, sym, a, b, r, of) do {		\
	t _r;							\
	bool _of;						\
								\
	_of = check_ ## op ## _overflow(a, b, &_r);		\
	if (_of != of) {					\
		pr_warn("expected "fmt" "sym" "fmt		\
			" to%s overflow (type %s)\n",		\
			a, b, of ? "" : " not", #t);		\
		err = 1;					\
	}							\
	if (_r != r) {						\
		pr_warn("expected "fmt" "sym" "fmt" == "	\
			fmt", got "fmt" (type %s)\n",		\
			a, b, r, _r, #t);			\
		err = 1;					\
	}							\
} while (0)

#define DEFINE_TEST_FUNC(t, fmt)					\
static int __init do_test_ ## t(const struct test_ ## t *p)		\
{							   		\
	int err = 0;							\
									\
	check_one_op(t, fmt, add, "+", p->a, p->b, p->sum, p->s_of);	\
	check_one_op(t, fmt, add, "+", p->b, p->a, p->sum, p->s_of);	\
	check_one_op(t, fmt, sub, "-", p->a, p->b, p->diff, p->d_of);	\
	check_one_op(t, fmt, mul, "*", p->a, p->b, p->prod, p->p_of);	\
	check_one_op(t, fmt, mul, "*", p->b, p->a, p->prod, p->p_of);	\
									\
	return err;							\
}									\
									\
static int __init test_ ## t ## _overflow(void) {			\
	int err = 0;							\
	unsigned i;							\
									\
	for (i = 0; i < ARRAY_SIZE(t ## _tests); ++i)			\
		err |= do_test_ ## t(&t ## _tests[i]);			\
	pr_info("%zu %s arithmetic tests finished\n",			\
		ARRAY_SIZE(t ## _tests), #t);				\
	return err;							\
}

DEFINE_TEST_FUNC(u8, "%d");
DEFINE_TEST_FUNC(s8, "%d");
DEFINE_TEST_FUNC(u16, "%d");
DEFINE_TEST_FUNC(s16, "%d");
DEFINE_TEST_FUNC(u32, "%u");
DEFINE_TEST_FUNC(s32, "%d");
#if BITS_PER_LONG == 64
DEFINE_TEST_FUNC(u64, "%llu");
DEFINE_TEST_FUNC(s64, "%lld");
#endif

static int __init test_overflow_calculation(void)
{
	int err = 0;

	err |= test_u8_overflow();
	err |= test_s8_overflow();
	err |= test_u16_overflow();
	err |= test_s16_overflow();
	err |= test_u32_overflow();
	err |= test_s32_overflow();
#if BITS_PER_LONG == 64
	err |= test_u64_overflow();
	err |= test_s64_overflow();
#endif

	return err;
}

static int __init test_overflow_shift(void)
{
	int err = 0;
	int count = 0;

/* Args are: value, shift, type, expected result, overflow expected */
#define TEST_ONE_SHIFT(a, s, t, expect, of) ({				\
	int __failed = 0;						\
	typeof(a) __a = (a);						\
	typeof(s) __s = (s);						\
	t __e = (expect);						\
	t __d;								\
	bool __of = check_shl_overflow(__a, __s, &__d);			\
	if (__of != of) {						\
		pr_warn("expected (%s)(%s << %s) to%s overflow\n",	\
			#t, #a, #s, of ? "" : " not");			\
		__failed = 1;						\
	} else if (!__of && __d != __e) {				\
		pr_warn("expected (%s)(%s << %s) == %s\n",		\
			#t, #a, #s, #expect);				\
		if ((t)-1 < 0)						\
			pr_warn("got %lld\n", (s64)__d);		\
		else							\
			pr_warn("got %llu\n", (u64)__d);		\
		__failed = 1;						\
	}								\
	count++;							\
	__failed;							\
})

	/* Sane shifts. */
	err |= TEST_ONE_SHIFT(1, 0, u8, 1 << 0, false);
	err |= TEST_ONE_SHIFT(1, 4, u8, 1 << 4, false);
	err |= TEST_ONE_SHIFT(1, 7, u8, 1 << 7, false);
	err |= TEST_ONE_SHIFT(0xF, 4, u8, 0xF << 4, false);
	err |= TEST_ONE_SHIFT(1, 0, u16, 1 << 0, false);
	err |= TEST_ONE_SHIFT(1, 10, u16, 1 << 10, false);
	err |= TEST_ONE_SHIFT(1, 15, u16, 1 << 15, false);
	err |= TEST_ONE_SHIFT(0xFF, 8, u16, 0xFF << 8, false);
	err |= TEST_ONE_SHIFT(1, 0, int, 1 << 0, false);
	err |= TEST_ONE_SHIFT(1, 16, int, 1 << 16, false);
	err |= TEST_ONE_SHIFT(1, 30, int, 1 << 30, false);
	err |= TEST_ONE_SHIFT(1, 0, s32, 1 << 0, false);
	err |= TEST_ONE_SHIFT(1, 16, s32, 1 << 16, false);
	err |= TEST_ONE_SHIFT(1, 30, s32, 1 << 30, false);
	err |= TEST_ONE_SHIFT(1, 0, unsigned int, 1U << 0, false);
	err |= TEST_ONE_SHIFT(1, 20, unsigned int, 1U << 20, false);
	err |= TEST_ONE_SHIFT(1, 31, unsigned int, 1U << 31, false);
	err |= TEST_ONE_SHIFT(0xFFFFU, 16, unsigned int, 0xFFFFU << 16, false);
	err |= TEST_ONE_SHIFT(1, 0, u32, 1U << 0, false);
	err |= TEST_ONE_SHIFT(1, 20, u32, 1U << 20, false);
	err |= TEST_ONE_SHIFT(1, 31, u32, 1U << 31, false);
	err |= TEST_ONE_SHIFT(0xFFFFU, 16, u32, 0xFFFFU << 16, false);
	err |= TEST_ONE_SHIFT(1, 0, u64, 1ULL << 0, false);
	err |= TEST_ONE_SHIFT(1, 40, u64, 1ULL << 40, false);
	err |= TEST_ONE_SHIFT(1, 63, u64, 1ULL << 63, false);
	err |= TEST_ONE_SHIFT(0xFFFFFFFFULL, 32, u64,
			      0xFFFFFFFFULL << 32, false);

	/* Sane shift: start and end with 0, without a too-wide shift. */
	err |= TEST_ONE_SHIFT(0, 7, u8, 0, false);
	err |= TEST_ONE_SHIFT(0, 15, u16, 0, false);
	err |= TEST_ONE_SHIFT(0, 31, unsigned int, 0, false);
	err |= TEST_ONE_SHIFT(0, 31, u32, 0, false);
	err |= TEST_ONE_SHIFT(0, 63, u64, 0, false);

	/* Sane shift: start and end with 0, without reaching signed bit. */
	err |= TEST_ONE_SHIFT(0, 6, s8, 0, false);
	err |= TEST_ONE_SHIFT(0, 14, s16, 0, false);
	err |= TEST_ONE_SHIFT(0, 30, int, 0, false);
	err |= TEST_ONE_SHIFT(0, 30, s32, 0, false);
	err |= TEST_ONE_SHIFT(0, 62, s64, 0, false);

	/* Overflow: shifted the bit off the end. */
	err |= TEST_ONE_SHIFT(1, 8, u8, 0, true);
	err |= TEST_ONE_SHIFT(1, 16, u16, 0, true);
	err |= TEST_ONE_SHIFT(1, 32, unsigned int, 0, true);
	err |= TEST_ONE_SHIFT(1, 32, u32, 0, true);
	err |= TEST_ONE_SHIFT(1, 64, u64, 0, true);

	/* Overflow: shifted into the signed bit. */
	err |= TEST_ONE_SHIFT(1, 7, s8, 0, true);
	err |= TEST_ONE_SHIFT(1, 15, s16, 0, true);
	err |= TEST_ONE_SHIFT(1, 31, int, 0, true);
	err |= TEST_ONE_SHIFT(1, 31, s32, 0, true);
	err |= TEST_ONE_SHIFT(1, 63, s64, 0, true);

	/* Overflow: high bit falls off unsigned types. */
	/* 10010110 */
	err |= TEST_ONE_SHIFT(150, 1, u8, 0, true);
	/* 1000100010010110 */
	err |= TEST_ONE_SHIFT(34966, 1, u16, 0, true);
	/* 10000100000010001000100010010110 */
	err |= TEST_ONE_SHIFT(2215151766U, 1, u32, 0, true);
	err |= TEST_ONE_SHIFT(2215151766U, 1, unsigned int, 0, true);
	/* 1000001000010000010000000100000010000100000010001000100010010110 */
	err |= TEST_ONE_SHIFT(9372061470395238550ULL, 1, u64, 0, true);

	/* Overflow: bit shifted into signed bit on signed types. */
	/* 01001011 */
	err |= TEST_ONE_SHIFT(75, 1, s8, 0, true);
	/* 0100010001001011 */
	err |= TEST_ONE_SHIFT(17483, 1, s16, 0, true);
	/* 01000010000001000100010001001011 */
	err |= TEST_ONE_SHIFT(1107575883, 1, s32, 0, true);
	err |= TEST_ONE_SHIFT(1107575883, 1, int, 0, true);
	/* 0100000100001000001000000010000001000010000001000100010001001011 */
	err |= TEST_ONE_SHIFT(4686030735197619275LL, 1, s64, 0, true);

	/* Overflow: bit shifted past signed bit on signed types. */
	/* 01001011 */
	err |= TEST_ONE_SHIFT(75, 2, s8, 0, true);
	/* 0100010001001011 */
	err |= TEST_ONE_SHIFT(17483, 2, s16, 0, true);
	/* 01000010000001000100010001001011 */
	err |= TEST_ONE_SHIFT(1107575883, 2, s32, 0, true);
	err |= TEST_ONE_SHIFT(1107575883, 2, int, 0, true);
	/* 0100000100001000001000000010000001000010000001000100010001001011 */
	err |= TEST_ONE_SHIFT(4686030735197619275LL, 2, s64, 0, true);

	/* Overflow: values larger than destination type. */
	err |= TEST_ONE_SHIFT(0x100, 0, u8, 0, true);
	err |= TEST_ONE_SHIFT(0xFF, 0, s8, 0, true);
	err |= TEST_ONE_SHIFT(0x10000U, 0, u16, 0, true);
	err |= TEST_ONE_SHIFT(0xFFFFU, 0, s16, 0, true);
	err |= TEST_ONE_SHIFT(0x100000000ULL, 0, u32, 0, true);
	err |= TEST_ONE_SHIFT(0x100000000ULL, 0, unsigned int, 0, true);
	err |= TEST_ONE_SHIFT(0xFFFFFFFFUL, 0, s32, 0, true);
	err |= TEST_ONE_SHIFT(0xFFFFFFFFUL, 0, int, 0, true);
	err |= TEST_ONE_SHIFT(0xFFFFFFFFFFFFFFFFULL, 0, s64, 0, true);

	/* Nonsense: negative initial value. */
	err |= TEST_ONE_SHIFT(-1, 0, s8, 0, true);
	err |= TEST_ONE_SHIFT(-1, 0, u8, 0, true);
	err |= TEST_ONE_SHIFT(-5, 0, s16, 0, true);
	err |= TEST_ONE_SHIFT(-5, 0, u16, 0, true);
	err |= TEST_ONE_SHIFT(-10, 0, int, 0, true);
	err |= TEST_ONE_SHIFT(-10, 0, unsigned int, 0, true);
	err |= TEST_ONE_SHIFT(-100, 0, s32, 0, true);
	err |= TEST_ONE_SHIFT(-100, 0, u32, 0, true);
	err |= TEST_ONE_SHIFT(-10000, 0, s64, 0, true);
	err |= TEST_ONE_SHIFT(-10000, 0, u64, 0, true);

	/* Nonsense: negative shift values. */
	err |= TEST_ONE_SHIFT(0, -5, s8, 0, true);
	err |= TEST_ONE_SHIFT(0, -5, u8, 0, true);
	err |= TEST_ONE_SHIFT(0, -10, s16, 0, true);
	err |= TEST_ONE_SHIFT(0, -10, u16, 0, true);
	err |= TEST_ONE_SHIFT(0, -15, int, 0, true);
	err |= TEST_ONE_SHIFT(0, -15, unsigned int, 0, true);
	err |= TEST_ONE_SHIFT(0, -20, s32, 0, true);
	err |= TEST_ONE_SHIFT(0, -20, u32, 0, true);
	err |= TEST_ONE_SHIFT(0, -30, s64, 0, true);
	err |= TEST_ONE_SHIFT(0, -30, u64, 0, true);

	/* Overflow: shifted at or beyond entire type's bit width. */
	err |= TEST_ONE_SHIFT(0, 8, u8, 0, true);
	err |= TEST_ONE_SHIFT(0, 9, u8, 0, true);
	err |= TEST_ONE_SHIFT(0, 8, s8, 0, true);
	err |= TEST_ONE_SHIFT(0, 9, s8, 0, true);
	err |= TEST_ONE_SHIFT(0, 16, u16, 0, true);
	err |= TEST_ONE_SHIFT(0, 17, u16, 0, true);
	err |= TEST_ONE_SHIFT(0, 16, s16, 0, true);
	err |= TEST_ONE_SHIFT(0, 17, s16, 0, true);
	err |= TEST_ONE_SHIFT(0, 32, u32, 0, true);
	err |= TEST_ONE_SHIFT(0, 33, u32, 0, true);
	err |= TEST_ONE_SHIFT(0, 32, int, 0, true);
	err |= TEST_ONE_SHIFT(0, 33, int, 0, true);
	err |= TEST_ONE_SHIFT(0, 32, s32, 0, true);
	err |= TEST_ONE_SHIFT(0, 33, s32, 0, true);
	err |= TEST_ONE_SHIFT(0, 64, u64, 0, true);
	err |= TEST_ONE_SHIFT(0, 65, u64, 0, true);
	err |= TEST_ONE_SHIFT(0, 64, s64, 0, true);
	err |= TEST_ONE_SHIFT(0, 65, s64, 0, true);

	/*
	 * Corner case: for unsigned types, we fail when we've shifted
	 * through the entire width of bits. For signed types, we might
	 * want to match this behavior, but that would mean noticing if
	 * we shift through all but the signed bit, and this is not
	 * currently detected (but we'll notice an overflow into the
	 * signed bit). So, for now, we will test this condition but
	 * mark it as not expected to overflow.
	 */
	err |= TEST_ONE_SHIFT(0, 7, s8, 0, false);
	err |= TEST_ONE_SHIFT(0, 15, s16, 0, false);
	err |= TEST_ONE_SHIFT(0, 31, int, 0, false);
	err |= TEST_ONE_SHIFT(0, 31, s32, 0, false);
	err |= TEST_ONE_SHIFT(0, 63, s64, 0, false);

	pr_info("%d shift tests finished\n", count);

#undef TEST_ONE_SHIFT

	return err;
}

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
static int __init test_ ## func (void *arg)				\
{									\
	volatile size_t a = TEST_SIZE;					\
	volatile size_t b = (SIZE_MAX / TEST_SIZE) + 1;			\
	void *ptr;							\
									\
	/* Tiny allocation test. */					\
	ptr = alloc ## want_arg ## want_gfp ## want_node (func, arg, 1);\
	if (!ptr) {							\
		pr_warn(#func " failed regular allocation?!\n");	\
		return 1;						\
	}								\
	free ## want_arg (free_func, arg, ptr);				\
									\
	/* Wrapped allocation test. */					\
	ptr = alloc ## want_arg ## want_gfp ## want_node (func, arg,	\
							  a * b);	\
	if (!ptr) {							\
		pr_warn(#func " unexpectedly failed bad wrapping?!\n");	\
		return 1;						\
	}								\
	free ## want_arg (free_func, arg, ptr);				\
									\
	/* Saturated allocation test. */				\
	ptr = alloc ## want_arg ## want_gfp ## want_node (func, arg,	\
						   array_size(a, b));	\
	if (ptr) {							\
		pr_warn(#func " missed saturation!\n");			\
		free ## want_arg (free_func, arg, ptr);			\
		return 1;						\
	}								\
	return 0;							\
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

static int __init test_overflow_allocation(void)
{
	const char device_name[] = "overflow-test";
	struct device *dev;
	int count = 0;
	int err = 0;

#define check_allocation_overflow(alloc)	({	\
	count++;					\
	test_ ## alloc(dev);				\
})

	/* Create dummy device for devm_kmalloc()-family tests. */
	dev = root_device_register(device_name);
	if (IS_ERR(dev)) {
		pr_warn("Cannot register test device\n");
		return 1;
	}

	err |= check_allocation_overflow(kmalloc);
	err |= check_allocation_overflow(kmalloc_node);
	err |= check_allocation_overflow(kzalloc);
	err |= check_allocation_overflow(kzalloc_node);
	err |= check_allocation_overflow(__vmalloc);
	err |= check_allocation_overflow(kvmalloc);
	err |= check_allocation_overflow(kvmalloc_node);
	err |= check_allocation_overflow(kvzalloc);
	err |= check_allocation_overflow(kvzalloc_node);
	err |= check_allocation_overflow(devm_kmalloc);
	err |= check_allocation_overflow(devm_kzalloc);

	device_unregister(dev);

	pr_info("%d allocation overflow tests finished\n", count);

#undef check_allocation_overflow

	return err;
}

static int __init test_module_init(void)
{
	int err = 0;

	err |= test_overflow_calculation();
	err |= test_overflow_shift();
	err |= test_overflow_allocation();

	if (err) {
		pr_warn("FAIL!\n");
		err = -EINVAL;
	} else {
		pr_info("all tests passed\n");
	}

	return err;
}

static void __exit test_module_exit(void)
{ }

module_init(test_module_init);
module_exit(test_module_exit);
MODULE_LICENSE("Dual MIT/GPL");

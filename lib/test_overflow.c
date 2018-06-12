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
	pr_info("%-3s: %zu tests\n", #t, ARRAY_SIZE(t ## _tests));	\
	for (i = 0; i < ARRAY_SIZE(t ## _tests); ++i)			\
		err |= do_test_ ## t(&t ## _tests[i]);			\
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

/*
 * Deal with the various forms of allocator arguments. See comments above
 * the DEFINE_TEST_ALLOC() instances for mapping of the "bits".
 */
#define alloc010(alloc, arg, sz) alloc(sz, GFP_KERNEL)
#define alloc011(alloc, arg, sz) alloc(sz, GFP_KERNEL, NUMA_NO_NODE)
#define alloc000(alloc, arg, sz) alloc(sz)
#define alloc001(alloc, arg, sz) alloc(sz, NUMA_NO_NODE)
#define alloc110(alloc, arg, sz) alloc(arg, sz, GFP_KERNEL)
#define free0(free, arg, ptr)	 free(ptr)
#define free1(free, arg, ptr)	 free(arg, ptr)

/* Wrap around to 8K */
#define TEST_SIZE		(9 << PAGE_SHIFT)

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
	pr_info(#func " detected saturation\n");			\
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
DEFINE_TEST_ALLOC(vmalloc,	 vfree,	     0, 0, 0);
DEFINE_TEST_ALLOC(vmalloc_node,  vfree,	     0, 0, 1);
DEFINE_TEST_ALLOC(vzalloc,	 vfree,	     0, 0, 0);
DEFINE_TEST_ALLOC(vzalloc_node,  vfree,	     0, 0, 1);
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
	int err = 0;

	/* Create dummy device for devm_kmalloc()-family tests. */
	dev = root_device_register(device_name);
	if (IS_ERR(dev)) {
		pr_warn("Cannot register test device\n");
		return 1;
	}

	err |= test_kmalloc(NULL);
	err |= test_kmalloc_node(NULL);
	err |= test_kzalloc(NULL);
	err |= test_kzalloc_node(NULL);
	err |= test_kvmalloc(NULL);
	err |= test_kvmalloc_node(NULL);
	err |= test_kvzalloc(NULL);
	err |= test_kvzalloc_node(NULL);
	err |= test_vmalloc(NULL);
	err |= test_vmalloc_node(NULL);
	err |= test_vzalloc(NULL);
	err |= test_vzalloc_node(NULL);
	err |= test_devm_kmalloc(dev);
	err |= test_devm_kzalloc(dev);

	device_unregister(dev);

	return err;
}

static int __init test_module_init(void)
{
	int err = 0;

	err |= test_overflow_calculation();
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

// SPDX-License-Identifier: GPL-2.0
/*
 * Runtime test cases for CONFIG_FORTIFY_SOURCE that aren't expected to
 * Oops the kernel on success. (For those, see drivers/misc/lkdtm/fortify.c)
 *
 * For corner cases with UBSAN, try testing with:
 *
 * ./tools/testing/kunit/kunit.py run --arch=x86_64 \
 *	--kconfig_add CONFIG_FORTIFY_SOURCE=y \
 *	--kconfig_add CONFIG_UBSAN=y \
 *	--kconfig_add CONFIG_UBSAN_TRAP=y \
 *	--kconfig_add CONFIG_UBSAN_BOUNDS=y \
 *	--kconfig_add CONFIG_UBSAN_LOCAL_BOUNDS=y \
 *	--make_options LLVM=1 fortify
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/string.h>

static const char array_of_10[] = "this is 10";
static const char *ptr_of_11 = "this is 11!";
static char array_unknown[] = "compiler thinks I might change";

static void known_sizes_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, __compiletime_strlen("88888888"), 8);
	KUNIT_EXPECT_EQ(test, __compiletime_strlen(array_of_10), 10);
	KUNIT_EXPECT_EQ(test, __compiletime_strlen(ptr_of_11), 11);

	KUNIT_EXPECT_EQ(test, __compiletime_strlen(array_unknown), SIZE_MAX);
	/* Externally defined and dynamically sized string pointer: */
	KUNIT_EXPECT_EQ(test, __compiletime_strlen(test->name), SIZE_MAX);
}

/* This is volatile so the optimizer can't perform DCE below. */
static volatile int pick;

/* Not inline to keep optimizer from figuring out which string we want. */
static noinline size_t want_minus_one(int pick)
{
	const char *str;

	switch (pick) {
	case 1:
		str = "4444";
		break;
	case 2:
		str = "333";
		break;
	default:
		str = "1";
		break;
	}
	return __compiletime_strlen(str);
}

static void control_flow_split_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, want_minus_one(pick), SIZE_MAX);
}

static struct kunit_case fortify_test_cases[] = {
	KUNIT_CASE(known_sizes_test),
	KUNIT_CASE(control_flow_split_test),
	{}
};

static struct kunit_suite fortify_test_suite = {
	.name = "fortify",
	.test_cases = fortify_test_cases,
};

kunit_test_suite(fortify_test_suite);

MODULE_LICENSE("GPL");

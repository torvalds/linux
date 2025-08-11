// SPDX-License-Identifier: GPL-2.0
/*
 * Test the longest symbol length. Execute with:
 *  ./tools/testing/kunit/kunit.py run longest-symbol
 *  --arch=x86_64 --kconfig_add CONFIG_KPROBES=y --kconfig_add CONFIG_MODULES=y
 *  --kconfig_add CONFIG_CPU_MITIGATIONS=n --kconfig_add CONFIG_GCOV_KERNEL=n
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/stringify.h>
#include <linux/kprobes.h>
#include <linux/kallsyms.h>

#define DI(name) s##name##name
#define DDI(name) DI(n##name##name)
#define DDDI(name) DDI(n##name##name)
#define DDDDI(name) DDDI(n##name##name)
#define DDDDDI(name) DDDDI(n##name##name)

/*Generate a symbol whose name length is 511 */
#define LONGEST_SYM_NAME  DDDDDI(g1h2i3j4k5l6m7n)

#define RETURN_LONGEST_SYM 0xAAAAA

noinline int LONGEST_SYM_NAME(void);
noinline int LONGEST_SYM_NAME(void)
{
	return RETURN_LONGEST_SYM;
}

_Static_assert(sizeof(__stringify(LONGEST_SYM_NAME)) == KSYM_NAME_LEN,
"Incorrect symbol length found. Expected KSYM_NAME_LEN: "
__stringify(KSYM_NAME_LEN) ", but found: "
__stringify(sizeof(LONGEST_SYM_NAME)));

static void test_longest_symbol(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RETURN_LONGEST_SYM, LONGEST_SYM_NAME());
};

static void test_longest_symbol_kallsyms(struct kunit *test)
{
	unsigned long (*kallsyms_lookup_name)(const char *name);
	static int (*longest_sym)(void);

	struct kprobe kp = {
		.symbol_name = "kallsyms_lookup_name",
	};

	if (register_kprobe(&kp) < 0) {
		pr_info("%s: kprobe not registered", __func__);
		KUNIT_FAIL(test, "test_longest_symbol kallsyms: kprobe not registered\n");
		return;
	}

	kunit_warn(test, "test_longest_symbol kallsyms: kprobe registered\n");
	kallsyms_lookup_name = (unsigned long (*)(const char *name))kp.addr;
	unregister_kprobe(&kp);

	longest_sym =
		(void *) kallsyms_lookup_name(__stringify(LONGEST_SYM_NAME));
	KUNIT_EXPECT_EQ(test, RETURN_LONGEST_SYM, longest_sym());
};

static struct kunit_case longest_symbol_test_cases[] = {
	KUNIT_CASE(test_longest_symbol),
	KUNIT_CASE(test_longest_symbol_kallsyms),
	{}
};

static struct kunit_suite longest_symbol_test_suite = {
	.name = "longest-symbol",
	.test_cases = longest_symbol_test_cases,
};
kunit_test_suite(longest_symbol_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test the longest symbol length");
MODULE_AUTHOR("Sergio González Collado");

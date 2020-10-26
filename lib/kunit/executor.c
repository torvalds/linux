// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

/*
 * These symbols point to the .kunit_test_suites section and are defined in
 * include/asm-generic/vmlinux.lds.h, and consequently must be extern.
 */
extern struct kunit_suite * const * const __kunit_suites_start[];
extern struct kunit_suite * const * const __kunit_suites_end[];

#if IS_BUILTIN(CONFIG_KUNIT)

static void kunit_print_tap_header(void)
{
	struct kunit_suite * const * const *suites, * const *subsuite;
	int num_of_suites = 0;

	for (suites = __kunit_suites_start;
	     suites < __kunit_suites_end;
	     suites++)
		for (subsuite = *suites; *subsuite != NULL; subsuite++)
			num_of_suites++;

	pr_info("TAP version 14\n");
	pr_info("1..%d\n", num_of_suites);
}

int kunit_run_all_tests(void)
{
	struct kunit_suite * const * const *suites;

	kunit_print_tap_header();

	for (suites = __kunit_suites_start;
	     suites < __kunit_suites_end;
	     suites++)
			__kunit_test_suites_init(*suites);

	return 0;
}

#endif /* IS_BUILTIN(CONFIG_KUNIT) */

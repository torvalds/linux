// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

/*
 * These symbols point to the .kunit_test_suites section and are defined in
 * include/asm-generic/vmlinux.lds.h, and consequently must be extern.
 */
extern struct kunit_suite * const * const __kunit_suites_start[];
extern struct kunit_suite * const * const __kunit_suites_end[];

#if IS_BUILTIN(CONFIG_KUNIT)

int kunit_run_all_tests(void)
{
	struct kunit_suite * const * const *suites;

	for (suites = __kunit_suites_start;
	     suites < __kunit_suites_end;
	     suites++)
			__kunit_test_suites_init(*suites);

	return 0;
}

#endif /* IS_BUILTIN(CONFIG_KUNIT) */

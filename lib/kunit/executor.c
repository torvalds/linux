// SPDX-License-Identifier: GPL-2.0

#include <linux/reboot.h>
#include <kunit/test.h>
#include <linux/glob.h>
#include <linux/moduleparam.h>

/*
 * These symbols point to the .kunit_test_suites section and are defined in
 * include/asm-generic/vmlinux.lds.h, and consequently must be extern.
 */
extern struct kunit_suite * const * const __kunit_suites_start[];
extern struct kunit_suite * const * const __kunit_suites_end[];

#if IS_BUILTIN(CONFIG_KUNIT)

static char *filter_glob_param;
module_param_named(filter_glob, filter_glob_param, charp, 0);
MODULE_PARM_DESC(filter_glob,
		"Filter which KUnit test suites run at boot-time, e.g. list*");

static char *kunit_shutdown;
core_param(kunit_shutdown, kunit_shutdown, charp, 0644);

static struct kunit_suite * const *
kunit_filter_subsuite(struct kunit_suite * const * const subsuite,
			const char *filter_glob)
{
	int i, n = 0;
	struct kunit_suite **filtered;

	n = 0;
	for (i = 0; subsuite[i] != NULL; ++i) {
		if (glob_match(filter_glob, subsuite[i]->name))
			++n;
	}

	if (n == 0)
		return NULL;

	filtered = kmalloc_array(n + 1, sizeof(*filtered), GFP_KERNEL);
	if (!filtered)
		return NULL;

	n = 0;
	for (i = 0; subsuite[i] != NULL; ++i) {
		if (glob_match(filter_glob, subsuite[i]->name))
			filtered[n++] = subsuite[i];
	}
	filtered[n] = NULL;

	return filtered;
}

struct suite_set {
	struct kunit_suite * const * const *start;
	struct kunit_suite * const * const *end;
};

static struct suite_set kunit_filter_suites(const struct suite_set *suite_set,
					    const char *filter_glob)
{
	int i;
	struct kunit_suite * const **copy, * const *filtered_subsuite;
	struct suite_set filtered;

	const size_t max = suite_set->end - suite_set->start;

	copy = kmalloc_array(max, sizeof(*filtered.start), GFP_KERNEL);
	filtered.start = copy;
	if (!copy) { /* won't be able to run anything, return an empty set */
		filtered.end = copy;
		return filtered;
	}

	for (i = 0; i < max; ++i) {
		filtered_subsuite = kunit_filter_subsuite(suite_set->start[i], filter_glob);
		if (filtered_subsuite)
			*copy++ = filtered_subsuite;
	}
	filtered.end = copy;
	return filtered;
}

static void kunit_handle_shutdown(void)
{
	if (!kunit_shutdown)
		return;

	if (!strcmp(kunit_shutdown, "poweroff"))
		kernel_power_off();
	else if (!strcmp(kunit_shutdown, "halt"))
		kernel_halt();
	else if (!strcmp(kunit_shutdown, "reboot"))
		kernel_restart(NULL);

}

static void kunit_print_tap_header(struct suite_set *suite_set)
{
	struct kunit_suite * const * const *suites, * const *subsuite;
	int num_of_suites = 0;

	for (suites = suite_set->start; suites < suite_set->end; suites++)
		for (subsuite = *suites; *subsuite != NULL; subsuite++)
			num_of_suites++;

	pr_info("TAP version 14\n");
	pr_info("1..%d\n", num_of_suites);
}

int kunit_run_all_tests(void)
{
	struct kunit_suite * const * const *suites;
	struct suite_set suite_set = {
		.start = __kunit_suites_start,
		.end = __kunit_suites_end,
	};

	if (filter_glob_param)
		suite_set = kunit_filter_suites(&suite_set, filter_glob_param);

	kunit_print_tap_header(&suite_set);

	for (suites = suite_set.start; suites < suite_set.end; suites++)
		__kunit_test_suites_init(*suites);

	if (filter_glob_param) { /* a copy was made of each array */
		for (suites = suite_set.start; suites < suite_set.end; suites++)
			kfree(*suites);
		kfree(suite_set.start);
	}

	kunit_handle_shutdown();

	return 0;
}

#if IS_BUILTIN(CONFIG_KUNIT_TEST)
#include "executor_test.c"
#endif

#endif /* IS_BUILTIN(CONFIG_KUNIT) */

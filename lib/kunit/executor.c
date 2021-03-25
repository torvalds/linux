// SPDX-License-Identifier: GPL-2.0

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

static char *filter_glob;
module_param(filter_glob, charp, 0);
MODULE_PARM_DESC(filter_glob,
		"Filter which KUnit test suites run at boot-time, e.g. list*");

static struct kunit_suite * const *
kunit_filter_subsuite(struct kunit_suite * const * const subsuite)
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

static struct suite_set kunit_filter_suites(void)
{
	int i;
	struct kunit_suite * const **copy, * const *filtered_subsuite;
	struct suite_set filtered;

	const size_t max = __kunit_suites_end - __kunit_suites_start;

	if (!filter_glob) {
		filtered.start = __kunit_suites_start;
		filtered.end = __kunit_suites_end;
		return filtered;
	}

	copy = kmalloc_array(max, sizeof(*filtered.start), GFP_KERNEL);
	filtered.start = copy;
	if (!copy) { /* won't be able to run anything, return an empty set */
		filtered.end = copy;
		return filtered;
	}

	for (i = 0; i < max; ++i) {
		filtered_subsuite = kunit_filter_subsuite(__kunit_suites_start[i]);
		if (filtered_subsuite)
			*copy++ = filtered_subsuite;
	}
	filtered.end = copy;
	return filtered;
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

	struct suite_set suite_set = kunit_filter_suites();

	kunit_print_tap_header(&suite_set);

	for (suites = suite_set.start; suites < suite_set.end; suites++)
		__kunit_test_suites_init(*suites);

	if (filter_glob) { /* a copy was made of each array */
		for (suites = suite_set.start; suites < suite_set.end; suites++)
			kfree(*suites);
		kfree(suite_set.start);
	}

	return 0;
}

#endif /* IS_BUILTIN(CONFIG_KUNIT) */

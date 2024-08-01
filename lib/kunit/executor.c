// SPDX-License-Identifier: GPL-2.0

#include <linux/reboot.h>
#include <kunit/test.h>
#include <kunit/attributes.h>
#include <linux/glob.h>
#include <linux/moduleparam.h>

/*
 * These symbols point to the .kunit_test_suites section and are defined in
 * include/asm-generic/vmlinux.lds.h, and consequently must be extern.
 */
extern struct kunit_suite * const __kunit_suites_start[];
extern struct kunit_suite * const __kunit_suites_end[];
extern struct kunit_suite * const __kunit_init_suites_start[];
extern struct kunit_suite * const __kunit_init_suites_end[];

static char *action_param;

module_param_named(action, action_param, charp, 0400);
MODULE_PARM_DESC(action,
		 "Changes KUnit executor behavior, valid values are:\n"
		 "<none>: run the tests like normal\n"
		 "'list' to list test names instead of running them.\n"
		 "'list_attr' to list test names and attributes instead of running them.\n");

const char *kunit_action(void)
{
	return action_param;
}

static char *filter_glob_param;
static char *filter_param;
static char *filter_action_param;

module_param_named(filter_glob, filter_glob_param, charp, 0600);
MODULE_PARM_DESC(filter_glob,
		"Filter which KUnit test suites/tests run at boot-time, e.g. list* or list*.*del_test");
module_param_named(filter, filter_param, charp, 0600);
MODULE_PARM_DESC(filter,
		"Filter which KUnit test suites/tests run at boot-time using attributes, e.g. speed>slow");
module_param_named(filter_action, filter_action_param, charp, 0600);
MODULE_PARM_DESC(filter_action,
		"Changes behavior of filtered tests using attributes, valid values are:\n"
		"<none>: do not run filtered tests as normal\n"
		"'skip': skip all filtered tests instead so tests will appear in output\n");

const char *kunit_filter_glob(void)
{
	return filter_glob_param;
}

char *kunit_filter(void)
{
	return filter_param;
}

char *kunit_filter_action(void)
{
	return filter_action_param;
}

/* glob_match() needs NULL terminated strings, so we need a copy of filter_glob_param. */
struct kunit_glob_filter {
	char *suite_glob;
	char *test_glob;
};

/* Split "suite_glob.test_glob" into two. Assumes filter_glob is not empty. */
static int kunit_parse_glob_filter(struct kunit_glob_filter *parsed,
				    const char *filter_glob)
{
	const char *period = strchr(filter_glob, '.');

	if (!period) {
		parsed->suite_glob = kstrdup(filter_glob, GFP_KERNEL);
		if (!parsed->suite_glob)
			return -ENOMEM;
		parsed->test_glob = NULL;
		return 0;
	}

	parsed->suite_glob = kstrndup(filter_glob, period - filter_glob, GFP_KERNEL);
	if (!parsed->suite_glob)
		return -ENOMEM;

	parsed->test_glob = kstrdup(period + 1, GFP_KERNEL);
	if (!parsed->test_glob) {
		kfree(parsed->suite_glob);
		return -ENOMEM;
	}

	return 0;
}

/* Create a copy of suite with only tests that match test_glob. */
static struct kunit_suite *
kunit_filter_glob_tests(const struct kunit_suite *const suite, const char *test_glob)
{
	int n = 0;
	struct kunit_case *filtered, *test_case;
	struct kunit_suite *copy;

	kunit_suite_for_each_test_case(suite, test_case) {
		if (!test_glob || glob_match(test_glob, test_case->name))
			++n;
	}

	if (n == 0)
		return NULL;

	copy = kmemdup(suite, sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return ERR_PTR(-ENOMEM);

	filtered = kcalloc(n + 1, sizeof(*filtered), GFP_KERNEL);
	if (!filtered) {
		kfree(copy);
		return ERR_PTR(-ENOMEM);
	}

	n = 0;
	kunit_suite_for_each_test_case(suite, test_case) {
		if (!test_glob || glob_match(test_glob, test_case->name))
			filtered[n++] = *test_case;
	}

	copy->test_cases = filtered;
	return copy;
}

void kunit_free_suite_set(struct kunit_suite_set suite_set)
{
	struct kunit_suite * const *suites;

	for (suites = suite_set.start; suites < suite_set.end; suites++) {
		kfree((*suites)->test_cases);
		kfree(*suites);
	}
	kfree(suite_set.start);
}

/*
 * Filter and reallocate test suites. Must return the filtered test suites set
 * allocated at a valid virtual address or NULL in case of error.
 */
struct kunit_suite_set
kunit_filter_suites(const struct kunit_suite_set *suite_set,
		    const char *filter_glob,
		    char *filters,
		    char *filter_action,
		    int *err)
{
	int i, j, k;
	int filter_count = 0;
	struct kunit_suite **copy, **copy_start, *filtered_suite, *new_filtered_suite;
	struct kunit_suite_set filtered = {NULL, NULL};
	struct kunit_glob_filter parsed_glob;
	struct kunit_attr_filter *parsed_filters = NULL;
	struct kunit_suite * const *suites;

	const size_t max = suite_set->end - suite_set->start;

	copy = kcalloc(max, sizeof(*filtered.start), GFP_KERNEL);
	if (!copy) { /* won't be able to run anything, return an empty set */
		return filtered;
	}
	copy_start = copy;

	if (filter_glob) {
		*err = kunit_parse_glob_filter(&parsed_glob, filter_glob);
		if (*err)
			goto free_copy;
	}

	/* Parse attribute filters */
	if (filters) {
		filter_count = kunit_get_filter_count(filters);
		parsed_filters = kcalloc(filter_count, sizeof(*parsed_filters), GFP_KERNEL);
		if (!parsed_filters) {
			*err = -ENOMEM;
			goto free_parsed_glob;
		}
		for (j = 0; j < filter_count; j++)
			parsed_filters[j] = kunit_next_attr_filter(&filters, err);
		if (*err)
			goto free_parsed_filters;
	}

	for (i = 0; &suite_set->start[i] != suite_set->end; i++) {
		filtered_suite = suite_set->start[i];
		if (filter_glob) {
			if (!glob_match(parsed_glob.suite_glob, filtered_suite->name))
				continue;
			filtered_suite = kunit_filter_glob_tests(filtered_suite,
					parsed_glob.test_glob);
			if (IS_ERR(filtered_suite)) {
				*err = PTR_ERR(filtered_suite);
				goto free_filtered_suite;
			}
		}
		if (filter_count > 0 && parsed_filters != NULL) {
			for (k = 0; k < filter_count; k++) {
				new_filtered_suite = kunit_filter_attr_tests(filtered_suite,
						parsed_filters[k], filter_action, err);

				/* Free previous copy of suite */
				if (k > 0 || filter_glob) {
					kfree(filtered_suite->test_cases);
					kfree(filtered_suite);
				}

				filtered_suite = new_filtered_suite;

				if (*err)
					goto free_filtered_suite;

				if (IS_ERR(filtered_suite)) {
					*err = PTR_ERR(filtered_suite);
					goto free_filtered_suite;
				}
				if (!filtered_suite)
					break;
			}
		}

		if (!filtered_suite)
			continue;

		*copy++ = filtered_suite;
	}
	filtered.start = copy_start;
	filtered.end = copy;

free_filtered_suite:
	if (*err) {
		for (suites = copy_start; suites < copy; suites++) {
			kfree((*suites)->test_cases);
			kfree(*suites);
		}
	}

free_parsed_filters:
	if (filter_count)
		kfree(parsed_filters);

free_parsed_glob:
	if (filter_glob) {
		kfree(parsed_glob.suite_glob);
		kfree(parsed_glob.test_glob);
	}

free_copy:
	if (*err)
		kfree(copy_start);

	return filtered;
}

void kunit_exec_run_tests(struct kunit_suite_set *suite_set, bool builtin)
{
	size_t num_suites = suite_set->end - suite_set->start;

	if (builtin || num_suites) {
		pr_info("KTAP version 1\n");
		pr_info("1..%zu\n", num_suites);
	}

	__kunit_test_suites_init(suite_set->start, num_suites);
}

void kunit_exec_list_tests(struct kunit_suite_set *suite_set, bool include_attr)
{
	struct kunit_suite * const *suites;
	struct kunit_case *test_case;

	/* Hack: print a ktap header so kunit.py can find the start of KUnit output. */
	pr_info("KTAP version 1\n");

	for (suites = suite_set->start; suites < suite_set->end; suites++) {
		/* Print suite name and suite attributes */
		pr_info("%s\n", (*suites)->name);
		if (include_attr)
			kunit_print_attr((void *)(*suites), false, 0);

		/* Print test case name and attributes in suite */
		kunit_suite_for_each_test_case((*suites), test_case) {
			pr_info("%s.%s\n", (*suites)->name, test_case->name);
			if (include_attr)
				kunit_print_attr((void *)test_case, true, 0);
		}
	}
}

struct kunit_suite_set kunit_merge_suite_sets(struct kunit_suite_set init_suite_set,
		struct kunit_suite_set suite_set)
{
	struct kunit_suite_set total_suite_set = {NULL, NULL};
	struct kunit_suite **total_suite_start = NULL;
	size_t init_num_suites, num_suites, suite_size;
	int i = 0;

	init_num_suites = init_suite_set.end - init_suite_set.start;
	num_suites = suite_set.end - suite_set.start;
	suite_size = sizeof(suite_set.start);

	/* Allocate memory for array of all kunit suites */
	total_suite_start = kmalloc_array(init_num_suites + num_suites, suite_size, GFP_KERNEL);
	if (!total_suite_start)
		return total_suite_set;

	/* Append and mark init suites and then append all other kunit suites */
	memcpy(total_suite_start, init_suite_set.start, init_num_suites * suite_size);
	for (i = 0; i < init_num_suites; i++)
		total_suite_start[i]->is_init = true;

	memcpy(total_suite_start + init_num_suites, suite_set.start, num_suites * suite_size);

	/* Set kunit suite set start and end */
	total_suite_set.start = total_suite_start;
	total_suite_set.end = total_suite_start + (init_num_suites + num_suites);

	return total_suite_set;
}

#if IS_BUILTIN(CONFIG_KUNIT)

static char *kunit_shutdown;
core_param(kunit_shutdown, kunit_shutdown, charp, 0644);

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

int kunit_run_all_tests(void)
{
	struct kunit_suite_set suite_set = {NULL, NULL};
	struct kunit_suite_set filtered_suite_set = {NULL, NULL};
	struct kunit_suite_set init_suite_set = {
		__kunit_init_suites_start, __kunit_init_suites_end,
	};
	struct kunit_suite_set normal_suite_set = {
		__kunit_suites_start, __kunit_suites_end,
	};
	size_t init_num_suites = init_suite_set.end - init_suite_set.start;
	int err = 0;

	if (init_num_suites > 0) {
		suite_set = kunit_merge_suite_sets(init_suite_set, normal_suite_set);
		if (!suite_set.start)
			goto out;
	} else
		suite_set = normal_suite_set;

	if (!kunit_enabled()) {
		pr_info("kunit: disabled\n");
		goto free_out;
	}

	if (filter_glob_param || filter_param) {
		filtered_suite_set = kunit_filter_suites(&suite_set, filter_glob_param,
				filter_param, filter_action_param, &err);

		/* Free original suite set before using filtered suite set */
		if (init_num_suites > 0)
			kfree(suite_set.start);
		suite_set = filtered_suite_set;

		if (err) {
			pr_err("kunit executor: error filtering suites: %d\n", err);
			goto free_out;
		}
	}

	if (!action_param)
		kunit_exec_run_tests(&suite_set, true);
	else if (strcmp(action_param, "list") == 0)
		kunit_exec_list_tests(&suite_set, false);
	else if (strcmp(action_param, "list_attr") == 0)
		kunit_exec_list_tests(&suite_set, true);
	else
		pr_err("kunit executor: unknown action '%s'\n", action_param);

free_out:
	if (filter_glob_param || filter_param)
		kunit_free_suite_set(suite_set);
	else if (init_num_suites > 0)
		/* Don't use kunit_free_suite_set because suites aren't individually allocated */
		kfree(suite_set.start);

out:
	kunit_handle_shutdown();
	return err;
}

#if IS_BUILTIN(CONFIG_KUNIT_TEST)
#include "executor_test.c"
#endif

#endif /* IS_BUILTIN(CONFIG_KUNIT) */

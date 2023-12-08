// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, Oracle and/or its affiliates.
 *    Author: Alan Maguire <alan.maguire@oracle.com>
 */

#include <linux/debugfs.h>
#include <linux/module.h>

#include <kunit/test.h>

#include "string-stream.h"

#define KUNIT_DEBUGFS_ROOT             "kunit"
#define KUNIT_DEBUGFS_RESULTS          "results"

/*
 * Create a debugfs representation of test suites:
 *
 * Path						Semantics
 * /sys/kernel/debug/kunit/<testsuite>/results	Show results of last run for
 *						testsuite
 *
 */

static struct dentry *debugfs_rootdir;

void kunit_debugfs_cleanup(void)
{
	debugfs_remove_recursive(debugfs_rootdir);
}

void kunit_debugfs_init(void)
{
	if (!debugfs_rootdir)
		debugfs_rootdir = debugfs_create_dir(KUNIT_DEBUGFS_ROOT, NULL);
}

static void debugfs_print_result(struct seq_file *seq,
				 struct kunit_suite *suite,
				 struct kunit_case *test_case)
{
	if (!test_case || !test_case->log)
		return;

	seq_printf(seq, "%s", test_case->log);
}

/*
 * /sys/kernel/debug/kunit/<testsuite>/results shows all results for testsuite.
 */
static int debugfs_print_results(struct seq_file *seq, void *v)
{
	struct kunit_suite *suite = (struct kunit_suite *)seq->private;
	enum kunit_status success = kunit_suite_has_succeeded(suite);
	struct kunit_case *test_case;

	if (!suite)
		return 0;

	/* Print KTAP header so the debugfs log can be parsed as valid KTAP. */
	seq_puts(seq, "KTAP version 1\n");
	seq_puts(seq, "1..1\n");

	/* Print suite header because it is not stored in the test logs. */
	seq_puts(seq, KUNIT_SUBTEST_INDENT "KTAP version 1\n");
	seq_printf(seq, KUNIT_SUBTEST_INDENT "# Subtest: %s\n", suite->name);
	seq_printf(seq, KUNIT_SUBTEST_INDENT "1..%zd\n", kunit_suite_num_test_cases(suite));

	kunit_suite_for_each_test_case(suite, test_case)
		debugfs_print_result(seq, suite, test_case);

	if (suite->log)
		seq_printf(seq, "%s", suite->log);

	seq_printf(seq, "%s %d %s\n",
		   kunit_status_to_ok_not_ok(success), 1, suite->name);
	return 0;
}

static int debugfs_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static int debugfs_results_open(struct inode *inode, struct file *file)
{
	struct kunit_suite *suite;

	suite = (struct kunit_suite *)inode->i_private;

	return single_open(file, debugfs_print_results, suite);
}

static const struct file_operations debugfs_results_fops = {
	.open = debugfs_results_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = debugfs_release,
};

void kunit_debugfs_create_suite(struct kunit_suite *suite)
{
	struct kunit_case *test_case;

	/* Allocate logs before creating debugfs representation. */
	suite->log = kzalloc(KUNIT_LOG_SIZE, GFP_KERNEL);
	kunit_suite_for_each_test_case(suite, test_case)
		test_case->log = kzalloc(KUNIT_LOG_SIZE, GFP_KERNEL);

	suite->debugfs = debugfs_create_dir(suite->name, debugfs_rootdir);

	debugfs_create_file(KUNIT_DEBUGFS_RESULTS, S_IFREG | 0444,
			    suite->debugfs,
			    suite, &debugfs_results_fops);
}

void kunit_debugfs_destroy_suite(struct kunit_suite *suite)
{
	struct kunit_case *test_case;

	debugfs_remove_recursive(suite->debugfs);
	kfree(suite->log);
	kunit_suite_for_each_test_case(suite, test_case)
		kfree(test_case->log);
}

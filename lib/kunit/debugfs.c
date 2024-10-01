// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, Oracle and/or its affiliates.
 *    Author: Alan Maguire <alan.maguire@oracle.com>
 */

#include <linux/debugfs.h>
#include <linux/module.h>

#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "string-stream.h"
#include "debugfs.h"

#define KUNIT_DEBUGFS_ROOT             "kunit"
#define KUNIT_DEBUGFS_RESULTS          "results"
#define KUNIT_DEBUGFS_RUN              "run"

/*
 * Create a debugfs representation of test suites:
 *
 * Path						Semantics
 * /sys/kernel/debug/kunit/<testsuite>/results	Show results of last run for
 *						testsuite
 * /sys/kernel/debug/kunit/<testsuite>/run	Write to this file to trigger
 *						testsuite to run
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

static void debugfs_print_result(struct seq_file *seq, struct string_stream *log)
{
	struct string_stream_fragment *frag_container;

	if (!log)
		return;

	/*
	 * Walk the fragments so we don't need to allocate a temporary
	 * buffer to hold the entire string.
	 */
	spin_lock(&log->lock);
	list_for_each_entry(frag_container, &log->fragments, node)
		seq_printf(seq, "%s", frag_container->fragment);
	spin_unlock(&log->lock);
}

/*
 * /sys/kernel/debug/kunit/<testsuite>/results shows all results for testsuite.
 */
static int debugfs_print_results(struct seq_file *seq, void *v)
{
	struct kunit_suite *suite = (struct kunit_suite *)seq->private;
	enum kunit_status success;
	struct kunit_case *test_case;

	if (!suite)
		return 0;

	success = kunit_suite_has_succeeded(suite);

	/* Print KTAP header so the debugfs log can be parsed as valid KTAP. */
	seq_puts(seq, "KTAP version 1\n");
	seq_puts(seq, "1..1\n");

	/* Print suite header because it is not stored in the test logs. */
	seq_puts(seq, KUNIT_SUBTEST_INDENT "KTAP version 1\n");
	seq_printf(seq, KUNIT_SUBTEST_INDENT "# Subtest: %s\n", suite->name);
	seq_printf(seq, KUNIT_SUBTEST_INDENT "1..%zd\n", kunit_suite_num_test_cases(suite));

	kunit_suite_for_each_test_case(suite, test_case)
		debugfs_print_result(seq, test_case->log);

	debugfs_print_result(seq, suite->log);

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

/*
 * Print a usage message to the debugfs "run" file
 * (/sys/kernel/debug/kunit/<testsuite>/run) if opened.
 */
static int debugfs_print_run(struct seq_file *seq, void *v)
{
	struct kunit_suite *suite = (struct kunit_suite *)seq->private;

	seq_puts(seq, "Write to this file to trigger the test suite to run.\n");
	seq_printf(seq, "usage: echo \"any string\" > /sys/kernel/debugfs/kunit/%s/run\n",
			suite->name);
	return 0;
}

/*
 * The debugfs "run" file (/sys/kernel/debug/kunit/<testsuite>/run)
 * contains no information. Write to the file to trigger the test suite
 * to run.
 */
static int debugfs_run_open(struct inode *inode, struct file *file)
{
	struct kunit_suite *suite;

	suite = (struct kunit_suite *)inode->i_private;

	return single_open(file, debugfs_print_run, suite);
}

/*
 * Trigger a test suite to run by writing to the suite's "run" debugfs
 * file found at: /sys/kernel/debug/kunit/<testsuite>/run
 *
 * Note: what is written to this file will not be saved.
 */
static ssize_t debugfs_run(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct inode *f_inode = file->f_inode;
	struct kunit_suite *suite = (struct kunit_suite *) f_inode->i_private;

	__kunit_test_suites_init(&suite, 1);

	return count;
}

static const struct file_operations debugfs_results_fops = {
	.open = debugfs_results_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = debugfs_release,
};

static const struct file_operations debugfs_run_fops = {
	.open = debugfs_run_open,
	.read = seq_read,
	.write = debugfs_run,
	.llseek = seq_lseek,
	.release = debugfs_release,
};

void kunit_debugfs_create_suite(struct kunit_suite *suite)
{
	struct kunit_case *test_case;
	struct string_stream *stream;

	/* If suite log already allocated, do not create new debugfs files. */
	if (suite->log)
		return;

	/*
	 * Allocate logs before creating debugfs representation.
	 * The suite->log and test_case->log pointer are expected to be NULL
	 * if there isn't a log, so only set it if the log stream was created
	 * successfully.
	 */
	stream = alloc_string_stream(GFP_KERNEL);
	if (IS_ERR_OR_NULL(stream))
		return;

	string_stream_set_append_newlines(stream, true);
	suite->log = stream;

	kunit_suite_for_each_test_case(suite, test_case) {
		stream = alloc_string_stream(GFP_KERNEL);
		if (IS_ERR_OR_NULL(stream))
			goto err;

		string_stream_set_append_newlines(stream, true);
		test_case->log = stream;
	}

	suite->debugfs = debugfs_create_dir(suite->name, debugfs_rootdir);

	debugfs_create_file(KUNIT_DEBUGFS_RESULTS, S_IFREG | 0444,
			    suite->debugfs,
			    suite, &debugfs_results_fops);

	/* Do not create file to re-run test if test runs on init */
	if (!suite->is_init) {
		debugfs_create_file(KUNIT_DEBUGFS_RUN, S_IFREG | 0644,
				    suite->debugfs,
				    suite, &debugfs_run_fops);
	}
	return;

err:
	string_stream_destroy(suite->log);
	kunit_suite_for_each_test_case(suite, test_case)
		string_stream_destroy(test_case->log);
}

void kunit_debugfs_destroy_suite(struct kunit_suite *suite)
{
	struct kunit_case *test_case;

	debugfs_remove_recursive(suite->debugfs);
	string_stream_destroy(suite->log);
	kunit_suite_for_each_test_case(suite, test_case)
		string_stream_destroy(test_case->log);
}

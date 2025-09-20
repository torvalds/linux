/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 * Copyright (c) 2024 Tejun Heo <tj@kernel.org>
 */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include <bpf/bpf.h>
#include "scx_test.h"

const char help_fmt[] =
"The runner for sched_ext tests.\n"
"\n"
"The runner is statically linked against all testcases, and runs them all serially.\n"
"It's required for the testcases to be serial, as only a single host-wide sched_ext\n"
"scheduler may be loaded at any given time."
"\n"
"Usage: %s [-t TEST] [-h]\n"
"\n"
"  -t TEST       Only run tests whose name includes this string\n"
"  -s            Include print output for skipped tests\n"
"  -l            List all available tests\n"
"  -q            Don't print the test descriptions during run\n"
"  -h            Display this help and exit\n";

static volatile int exit_req;
static bool quiet, print_skipped, list;

#define MAX_SCX_TESTS 2048

static struct scx_test __scx_tests[MAX_SCX_TESTS];
static unsigned __scx_num_tests = 0;

static void sigint_handler(int simple)
{
	exit_req = 1;
}

static void print_test_preamble(const struct scx_test *test, bool quiet)
{
	printf("===== START =====\n");
	printf("TEST: %s\n", test->name);
	if (!quiet)
		printf("DESCRIPTION: %s\n", test->description);
	printf("OUTPUT:\n");
}

static const char *status_to_result(enum scx_test_status status)
{
	switch (status) {
	case SCX_TEST_PASS:
	case SCX_TEST_SKIP:
		return "ok";
	case SCX_TEST_FAIL:
		return "not ok";
	default:
		return "<UNKNOWN>";
	}
}

static void print_test_result(const struct scx_test *test,
			      enum scx_test_status status,
			      unsigned int testnum)
{
	const char *result = status_to_result(status);
	const char *directive = status == SCX_TEST_SKIP ? "SKIP " : "";

	printf("%s %u %s # %s\n", result, testnum, test->name, directive);
	printf("=====  END  =====\n");
}

static bool should_skip_test(const struct scx_test *test, const char * filter)
{
	return !strstr(test->name, filter);
}

static enum scx_test_status run_test(const struct scx_test *test)
{
	enum scx_test_status status;
	void *context = NULL;

	if (test->setup) {
		status = test->setup(&context);
		if (status != SCX_TEST_PASS)
			return status;
	}

	status = test->run(context);

	if (test->cleanup)
		test->cleanup(context);

	return status;
}

static bool test_valid(const struct scx_test *test)
{
	if (!test) {
		fprintf(stderr, "NULL test detected\n");
		return false;
	}

	if (!test->name) {
		fprintf(stderr,
			"Test with no name found. Must specify test name.\n");
		return false;
	}

	if (!test->description) {
		fprintf(stderr, "Test %s requires description.\n", test->name);
		return false;
	}

	if (!test->run) {
		fprintf(stderr, "Test %s has no run() callback\n", test->name);
		return false;
	}

	return true;
}

int main(int argc, char **argv)
{
	const char *filter = NULL;
	unsigned testnum = 0, i;
	unsigned passed = 0, skipped = 0, failed = 0;
	int opt;

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	while ((opt = getopt(argc, argv, "qslt:h")) != -1) {
		switch (opt) {
		case 'q':
			quiet = true;
			break;
		case 's':
			print_skipped = true;
			break;
		case 'l':
			list = true;
			break;
		case 't':
			filter = optarg;
			break;
		default:
			fprintf(stderr, help_fmt, basename(argv[0]));
			return opt != 'h';
		}
	}

	for (i = 0; i < __scx_num_tests; i++) {
		enum scx_test_status status;
		struct scx_test *test = &__scx_tests[i];

		if (list) {
			printf("%s\n", test->name);
			if (i == (__scx_num_tests - 1))
				return 0;
			continue;
		}

		if (filter && should_skip_test(test, filter)) {
			/*
			 * Printing the skipped tests and their preambles can
			 * add a lot of noise to the runner output. Printing
			 * this is only really useful for CI, so let's skip it
			 * by default.
			 */
			if (print_skipped) {
				print_test_preamble(test, quiet);
				print_test_result(test, SCX_TEST_SKIP, ++testnum);
			}
			continue;
		}

		print_test_preamble(test, quiet);
		status = run_test(test);
		print_test_result(test, status, ++testnum);
		switch (status) {
		case SCX_TEST_PASS:
			passed++;
			break;
		case SCX_TEST_SKIP:
			skipped++;
			break;
		case SCX_TEST_FAIL:
			failed++;
			break;
		}
	}
	printf("\n\n=============================\n\n");
	printf("RESULTS:\n\n");
	printf("PASSED:  %u\n", passed);
	printf("SKIPPED: %u\n", skipped);
	printf("FAILED:  %u\n", failed);

	return 0;
}

void scx_test_register(struct scx_test *test)
{
	SCX_BUG_ON(!test_valid(test), "Invalid test found");
	SCX_BUG_ON(__scx_num_tests >= MAX_SCX_TESTS, "Maximum tests exceeded");

	__scx_tests[__scx_num_tests++] = *test;
}

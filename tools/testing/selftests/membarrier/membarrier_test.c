#define _GNU_SOURCE
#include <linux/membarrier.h>
#include <syscall.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "../kselftest.h"

enum test_membarrier_status {
	TEST_MEMBARRIER_PASS = 0,
	TEST_MEMBARRIER_FAIL,
	TEST_MEMBARRIER_SKIP,
};

static int sys_membarrier(int cmd, int flags)
{
	return syscall(__NR_membarrier, cmd, flags);
}

static enum test_membarrier_status test_membarrier_cmd_fail(void)
{
	int cmd = -1, flags = 0;
	const char *test_name = "membarrier command cmd=-1. Wrong command should fail";

	if (sys_membarrier(cmd, flags) != -1) {
		ksft_test_result_fail(test_name);
		return TEST_MEMBARRIER_FAIL;
	}

	ksft_test_result_pass(test_name);
	return TEST_MEMBARRIER_PASS;
}

static enum test_membarrier_status test_membarrier_flags_fail(void)
{
	int cmd = MEMBARRIER_CMD_QUERY, flags = 1;
	const char *test_name = "MEMBARRIER_CMD_QUERY, flags=1, Wrong flags should fail";

	if (sys_membarrier(cmd, flags) != -1) {
		ksft_test_result_fail(test_name);
		return TEST_MEMBARRIER_FAIL;
	}

	ksft_test_result_pass(test_name);
	return TEST_MEMBARRIER_PASS;
}

static enum test_membarrier_status test_membarrier_success(void)
{
	int cmd = MEMBARRIER_CMD_SHARED, flags = 0;
	const char *test_name = "execute MEMBARRIER_CMD_SHARED";

	if (sys_membarrier(cmd, flags) != 0) {
		ksft_test_result_fail(test_name);
		return TEST_MEMBARRIER_FAIL;
	}

	ksft_test_result_pass(test_name);
	return TEST_MEMBARRIER_PASS;
}

static enum test_membarrier_status test_membarrier(void)
{
	enum test_membarrier_status status;

	status = test_membarrier_cmd_fail();
	if (status)
		return status;
	status = test_membarrier_flags_fail();
	if (status)
		return status;
	status = test_membarrier_success();
	if (status)
		return status;
	return TEST_MEMBARRIER_PASS;
}

static enum test_membarrier_status test_membarrier_query(void)
{
	int flags = 0, ret;

	ret = sys_membarrier(MEMBARRIER_CMD_QUERY, flags);
	if (ret < 0) {
		if (errno == ENOSYS) {
			/*
			 * It is valid to build a kernel with
			 * CONFIG_MEMBARRIER=n. However, this skips the tests.
			 */
			ksft_exit_skip("CONFIG_MEMBARRIER is not enabled\n");
		}
		ksft_test_result_fail("sys_membarrier() failed\n");
		return TEST_MEMBARRIER_FAIL;
	}
	if (!(ret & MEMBARRIER_CMD_SHARED)) {
		ksft_test_result_fail("command MEMBARRIER_CMD_SHARED is not supported.\n");
		return TEST_MEMBARRIER_FAIL;
	}
	ksft_test_result_pass("sys_membarrier available");
	return TEST_MEMBARRIER_PASS;
}

int main(int argc, char **argv)
{
	ksft_print_header();
	switch (test_membarrier_query()) {
	case TEST_MEMBARRIER_FAIL:
		return ksft_exit_fail();
	case TEST_MEMBARRIER_SKIP:
		return ksft_exit_skip(NULL);
	}
	switch (test_membarrier()) {
	case TEST_MEMBARRIER_FAIL:
		return ksft_exit_fail();
	case TEST_MEMBARRIER_SKIP:
		return ksft_exit_skip(NULL);
	}

	return ksft_exit_pass();
}

#define _GNU_SOURCE
#include <linux/membarrier.h>
#include <syscall.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "../kselftest.h"

static int sys_membarrier(int cmd, int flags)
{
	return syscall(__NR_membarrier, cmd, flags);
}

static int test_membarrier_cmd_fail(void)
{
	int cmd = -1, flags = 0;

	if (sys_membarrier(cmd, flags) != -1) {
		ksft_exit_fail_msg(
			"sys membarrier invalid command test: command = %d, flags = %d. Should fail, but passed\n",
			cmd, flags);
	}

	ksft_test_result_pass(
		"sys membarrier invalid command test: command = %d, flags = %d. Failed as expected\n",
		cmd, flags);
	return 0;
}

static int test_membarrier_flags_fail(void)
{
	int cmd = MEMBARRIER_CMD_QUERY, flags = 1;

	if (sys_membarrier(cmd, flags) != -1) {
		ksft_exit_fail_msg(
			"sys membarrier MEMBARRIER_CMD_QUERY invalid flags test: flags = %d. Should fail, but passed\n",
			flags);
	}

	ksft_test_result_pass(
		"sys membarrier MEMBARRIER_CMD_QUERY invalid flags test: flags = %d. Failed as expected\n",
		flags);
	return 0;
}

static int test_membarrier_success(void)
{
	int cmd = MEMBARRIER_CMD_SHARED, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_SHARED\n";

	if (sys_membarrier(cmd, flags) != 0) {
		ksft_exit_fail_msg(
			"sys membarrier MEMBARRIER_CMD_SHARED test: flags = %d\n",
			flags);
	}

	ksft_test_result_pass(
		"sys membarrier MEMBARRIER_CMD_SHARED test: flags = %d\n",
		flags);
	return 0;
}

static int test_membarrier(void)
{
	int status;

	status = test_membarrier_cmd_fail();
	if (status)
		return status;
	status = test_membarrier_flags_fail();
	if (status)
		return status;
	status = test_membarrier_success();
	if (status)
		return status;
	return 0;
}

static int test_membarrier_query(void)
{
	int flags = 0, ret;

	ret = sys_membarrier(MEMBARRIER_CMD_QUERY, flags);
	if (ret < 0) {
		if (errno == ENOSYS) {
			/*
			 * It is valid to build a kernel with
			 * CONFIG_MEMBARRIER=n. However, this skips the tests.
			 */
			ksft_exit_skip(
				"sys membarrier (CONFIG_MEMBARRIER) is disabled.\n");
		}
		ksft_exit_fail_msg("sys_membarrier() failed\n");
	}
	if (!(ret & MEMBARRIER_CMD_SHARED))
		ksft_exit_fail_msg("sys_membarrier is not supported.\n");

	ksft_test_result_pass("sys_membarrier available\n");
	return 0;
}

int main(int argc, char **argv)
{
	ksft_print_header();

	test_membarrier_query();
	test_membarrier();

	ksft_exit_pass();
}

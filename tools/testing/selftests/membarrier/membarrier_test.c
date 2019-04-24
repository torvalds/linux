// SPDX-License-Identifier: GPL-2.0
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
	const char *test_name = "sys membarrier invalid command";

	if (sys_membarrier(cmd, flags) != -1) {
		ksft_exit_fail_msg(
			"%s test: command = %d, flags = %d. Should fail, but passed\n",
			test_name, cmd, flags);
	}
	if (errno != EINVAL) {
		ksft_exit_fail_msg(
			"%s test: flags = %d. Should return (%d: \"%s\"), but returned (%d: \"%s\").\n",
			test_name, flags, EINVAL, strerror(EINVAL),
			errno, strerror(errno));
	}

	ksft_test_result_pass(
		"%s test: command = %d, flags = %d, errno = %d. Failed as expected\n",
		test_name, cmd, flags, errno);
	return 0;
}

static int test_membarrier_flags_fail(void)
{
	int cmd = MEMBARRIER_CMD_QUERY, flags = 1;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_QUERY invalid flags";

	if (sys_membarrier(cmd, flags) != -1) {
		ksft_exit_fail_msg(
			"%s test: flags = %d. Should fail, but passed\n",
			test_name, flags);
	}
	if (errno != EINVAL) {
		ksft_exit_fail_msg(
			"%s test: flags = %d. Should return (%d: \"%s\"), but returned (%d: \"%s\").\n",
			test_name, flags, EINVAL, strerror(EINVAL),
			errno, strerror(errno));
	}

	ksft_test_result_pass(
		"%s test: flags = %d, errno = %d. Failed as expected\n",
		test_name, flags, errno);
	return 0;
}

static int test_membarrier_global_success(void)
{
	int cmd = MEMBARRIER_CMD_GLOBAL, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_GLOBAL";

	if (sys_membarrier(cmd, flags) != 0) {
		ksft_exit_fail_msg(
			"%s test: flags = %d, errno = %d\n",
			test_name, flags, errno);
	}

	ksft_test_result_pass(
		"%s test: flags = %d\n", test_name, flags);
	return 0;
}

static int test_membarrier_private_expedited_fail(void)
{
	int cmd = MEMBARRIER_CMD_PRIVATE_EXPEDITED, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_PRIVATE_EXPEDITED not registered failure";

	if (sys_membarrier(cmd, flags) != -1) {
		ksft_exit_fail_msg(
			"%s test: flags = %d. Should fail, but passed\n",
			test_name, flags);
	}
	if (errno != EPERM) {
		ksft_exit_fail_msg(
			"%s test: flags = %d. Should return (%d: \"%s\"), but returned (%d: \"%s\").\n",
			test_name, flags, EPERM, strerror(EPERM),
			errno, strerror(errno));
	}

	ksft_test_result_pass(
		"%s test: flags = %d, errno = %d\n",
		test_name, flags, errno);
	return 0;
}

static int test_membarrier_register_private_expedited_success(void)
{
	int cmd = MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED";

	if (sys_membarrier(cmd, flags) != 0) {
		ksft_exit_fail_msg(
			"%s test: flags = %d, errno = %d\n",
			test_name, flags, errno);
	}

	ksft_test_result_pass(
		"%s test: flags = %d\n",
		test_name, flags);
	return 0;
}

static int test_membarrier_private_expedited_success(void)
{
	int cmd = MEMBARRIER_CMD_PRIVATE_EXPEDITED, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_PRIVATE_EXPEDITED";

	if (sys_membarrier(cmd, flags) != 0) {
		ksft_exit_fail_msg(
			"%s test: flags = %d, errno = %d\n",
			test_name, flags, errno);
	}

	ksft_test_result_pass(
		"%s test: flags = %d\n",
		test_name, flags);
	return 0;
}

static int test_membarrier_private_expedited_sync_core_fail(void)
{
	int cmd = MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE not registered failure";

	if (sys_membarrier(cmd, flags) != -1) {
		ksft_exit_fail_msg(
			"%s test: flags = %d. Should fail, but passed\n",
			test_name, flags);
	}
	if (errno != EPERM) {
		ksft_exit_fail_msg(
			"%s test: flags = %d. Should return (%d: \"%s\"), but returned (%d: \"%s\").\n",
			test_name, flags, EPERM, strerror(EPERM),
			errno, strerror(errno));
	}

	ksft_test_result_pass(
		"%s test: flags = %d, errno = %d\n",
		test_name, flags, errno);
	return 0;
}

static int test_membarrier_register_private_expedited_sync_core_success(void)
{
	int cmd = MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE";

	if (sys_membarrier(cmd, flags) != 0) {
		ksft_exit_fail_msg(
			"%s test: flags = %d, errno = %d\n",
			test_name, flags, errno);
	}

	ksft_test_result_pass(
		"%s test: flags = %d\n",
		test_name, flags);
	return 0;
}

static int test_membarrier_private_expedited_sync_core_success(void)
{
	int cmd = MEMBARRIER_CMD_PRIVATE_EXPEDITED, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE";

	if (sys_membarrier(cmd, flags) != 0) {
		ksft_exit_fail_msg(
			"%s test: flags = %d, errno = %d\n",
			test_name, flags, errno);
	}

	ksft_test_result_pass(
		"%s test: flags = %d\n",
		test_name, flags);
	return 0;
}

static int test_membarrier_register_global_expedited_success(void)
{
	int cmd = MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED";

	if (sys_membarrier(cmd, flags) != 0) {
		ksft_exit_fail_msg(
			"%s test: flags = %d, errno = %d\n",
			test_name, flags, errno);
	}

	ksft_test_result_pass(
		"%s test: flags = %d\n",
		test_name, flags);
	return 0;
}

static int test_membarrier_global_expedited_success(void)
{
	int cmd = MEMBARRIER_CMD_GLOBAL_EXPEDITED, flags = 0;
	const char *test_name = "sys membarrier MEMBARRIER_CMD_GLOBAL_EXPEDITED";

	if (sys_membarrier(cmd, flags) != 0) {
		ksft_exit_fail_msg(
			"%s test: flags = %d, errno = %d\n",
			test_name, flags, errno);
	}

	ksft_test_result_pass(
		"%s test: flags = %d\n",
		test_name, flags);
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
	status = test_membarrier_global_success();
	if (status)
		return status;
	status = test_membarrier_private_expedited_fail();
	if (status)
		return status;
	status = test_membarrier_register_private_expedited_success();
	if (status)
		return status;
	status = test_membarrier_private_expedited_success();
	if (status)
		return status;
	status = sys_membarrier(MEMBARRIER_CMD_QUERY, 0);
	if (status < 0) {
		ksft_test_result_fail("sys_membarrier() failed\n");
		return status;
	}
	if (status & MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE) {
		status = test_membarrier_private_expedited_sync_core_fail();
		if (status)
			return status;
		status = test_membarrier_register_private_expedited_sync_core_success();
		if (status)
			return status;
		status = test_membarrier_private_expedited_sync_core_success();
		if (status)
			return status;
	}
	/*
	 * It is valid to send a global membarrier from a non-registered
	 * process.
	 */
	status = test_membarrier_global_expedited_success();
	if (status)
		return status;
	status = test_membarrier_register_global_expedited_success();
	if (status)
		return status;
	status = test_membarrier_global_expedited_success();
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
	if (!(ret & MEMBARRIER_CMD_GLOBAL))
		ksft_exit_skip(
			"sys_membarrier unsupported: CMD_GLOBAL not found.\n");

	ksft_test_result_pass("sys_membarrier available\n");
	return 0;
}

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(13);

	test_membarrier_query();
	test_membarrier();

	return ksft_exit_pass();
}

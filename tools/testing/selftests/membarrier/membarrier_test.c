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

	if (sys_membarrier(cmd, flags) != -1) {
		printf("membarrier: Wrong command should fail but passed.\n");
		return TEST_MEMBARRIER_FAIL;
	}
	return TEST_MEMBARRIER_PASS;
}

static enum test_membarrier_status test_membarrier_flags_fail(void)
{
	int cmd = MEMBARRIER_CMD_QUERY, flags = 1;

	if (sys_membarrier(cmd, flags) != -1) {
		printf("membarrier: Wrong flags should fail but passed.\n");
		return TEST_MEMBARRIER_FAIL;
	}
	return TEST_MEMBARRIER_PASS;
}

static enum test_membarrier_status test_membarrier_success(void)
{
	int cmd = MEMBARRIER_CMD_SHARED, flags = 0;

	if (sys_membarrier(cmd, flags) != 0) {
		printf("membarrier: Executing MEMBARRIER_CMD_SHARED failed. %s.\n",
				strerror(errno));
		return TEST_MEMBARRIER_FAIL;
	}

	printf("membarrier: MEMBARRIER_CMD_SHARED success.\n");
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

	printf("membarrier MEMBARRIER_CMD_QUERY ");
	ret = sys_membarrier(MEMBARRIER_CMD_QUERY, flags);
	if (ret < 0) {
		printf("failed. %s.\n", strerror(errno));
		switch (errno) {
		case ENOSYS:
			/*
			 * It is valid to build a kernel with
			 * CONFIG_MEMBARRIER=n. However, this skips the tests.
			 */
			return TEST_MEMBARRIER_SKIP;
		case EINVAL:
		default:
			return TEST_MEMBARRIER_FAIL;
		}
	}
	if (!(ret & MEMBARRIER_CMD_SHARED)) {
		printf("command MEMBARRIER_CMD_SHARED is not supported.\n");
		return TEST_MEMBARRIER_FAIL;
	}
	printf("syscall available.\n");
	return TEST_MEMBARRIER_PASS;
}

int main(int argc, char **argv)
{
	switch (test_membarrier_query()) {
	case TEST_MEMBARRIER_FAIL:
		return ksft_exit_fail();
	case TEST_MEMBARRIER_SKIP:
		return ksft_exit_skip();
	}
	switch (test_membarrier()) {
	case TEST_MEMBARRIER_FAIL:
		return ksft_exit_fail();
	case TEST_MEMBARRIER_SKIP:
		return ksft_exit_skip();
	}

	printf("membarrier: tests done!\n");
	return ksft_exit_pass();
}

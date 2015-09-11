#define _GNU_SOURCE
#define __EXPORTED_HEADERS__

#include <linux/membarrier.h>
#include <asm-generic/unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "../kselftest.h"

static int sys_membarrier(int cmd, int flags)
{
	return syscall(__NR_membarrier, cmd, flags);
}

static void test_membarrier_fail(void)
{
	int cmd = -1, flags = 0;

	if (sys_membarrier(cmd, flags) != -1) {
		printf("membarrier: Should fail but passed\n");
		ksft_exit_fail();
	}
}

static void test_membarrier_success(void)
{
	int flags = 0;

	if (sys_membarrier(MEMBARRIER_CMD_SHARED, flags) != 0) {
		printf("membarrier: Executing MEMBARRIER failed, %s\n",
				strerror(errno));
		ksft_exit_fail();
	}

	printf("membarrier: MEMBARRIER_CMD_SHARED success\n");
}

static void test_membarrier(void)
{
	test_membarrier_fail();
	test_membarrier_success();
}

static int test_membarrier_exists(void)
{
	int flags = 0;

	if (sys_membarrier(MEMBARRIER_CMD_QUERY, flags))
		return 0;

	return 1;
}

int main(int argc, char **argv)
{
	printf("membarrier: MEMBARRIER_CMD_QUERY ");
	if (test_membarrier_exists()) {
		printf("syscall implemented\n");
		test_membarrier();
	} else {
		printf("syscall not implemented!\n");
		return ksft_exit_fail();
	}

	printf("membarrier: tests done!\n");

	return ksft_exit_pass();
}

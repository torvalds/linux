// SPDX-License-Identifier: GPL-2.0+
/* This testcase operates with the test_fpu kernel driver.
 * It modifies the FPU control register in user mode and calls the kernel
 * module to perform floating point operations in the kernel. The control
 * register value should be independent between kernel and user mode.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fenv.h>
#include <unistd.h>
#include <fcntl.h>

const char *test_fpu_path = "/sys/kernel/debug/selftest_helpers/test_fpu";

int main(void)
{
	char dummy[1];
	int fd = open(test_fpu_path, O_RDONLY);

	if (fd < 0) {
		printf("[SKIP]\tcan't access %s: %s\n",
		       test_fpu_path, strerror(errno));
		return 0;
	}

	if (read(fd, dummy, 1) < 0) {
		printf("[FAIL]\taccess with default rounding mode failed\n");
		return 1;
	}

	fesetround(FE_DOWNWARD);
	if (read(fd, dummy, 1) < 0) {
		printf("[FAIL]\taccess with downward rounding mode failed\n");
		return 2;
	}
	if (fegetround() != FE_DOWNWARD) {
		printf("[FAIL]\tusermode rounding mode clobbered\n");
		return 3;
	}

	/* Note: the tests up to this point are quite safe and will only return
	 * an error. But the exception mask setting can cause misbehaving kernel
	 * to crash.
	 */
	feclearexcept(FE_ALL_EXCEPT);
	feenableexcept(FE_ALL_EXCEPT);
	if (read(fd, dummy, 1) < 0) {
		printf("[FAIL]\taccess with fpu exceptions unmasked failed\n");
		return 4;
	}
	if (fegetexcept() != FE_ALL_EXCEPT) {
		printf("[FAIL]\tusermode fpu exception mask clobbered\n");
		return 5;
	}

	printf("[OK]\ttest_fpu\n");
	return 0;
}

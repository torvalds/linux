// SPDX-License-Identifier: GPL-2.0

/* if dlemopen is not implemented, skip test */
#if defined(__x86_64__) && defined(__linux__)
#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <lkl.h>
#include <lkl_host.h>
#include <lkl_config.h>

#include "test.h"

#define CMD_LINE "mem=16M loglevel=8"

/* glibc (may) only supports dlmopen(3) */
#ifndef LM_ID_NEWLM
#define NO_DLMOPEN_LINUX 1
#else
#define NO_DLMOPEN_LINUX 0
#endif

static int lkl_test_dlmopen(void)
{
	void *handle;
	long ret;
	char *filename = "liblkl.so";
	long params[6] = {0};
	int (*fn_init)(struct lkl_host_operations *ops);
	int (*fn_start)(char *fmt, ...);
	long (*fn_syscall)(long no, long *params);
	long (*fn_sys_halt)(void);
	void (*fn_tls_mode)(void);
	struct lkl_host_operations *lkl_host_ops;

	handle = dlmopen(LM_ID_NEWLM, filename, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		lkl_test_logf("%s: dlmopen failed, %s\n", __func__, dlerror());
		return TEST_FAILURE;
	}

	fn_init = dlsym(handle, "lkl_init");
	if (!fn_init) {
		lkl_test_logf("%s: dlsym failed, %s\n", __func__, dlerror());
		return TEST_FAILURE;
	}

	lkl_host_ops = dlsym(handle, "lkl_host_ops");
	if (!lkl_host_ops) {
		lkl_test_logf("%s: dlsym failed, %s\n", __func__, dlerror());
		return TEST_FAILURE;
	}

	fn_start = dlsym(handle, "lkl_start_kernel");
	if (!fn_start) {
		lkl_test_logf("%s: dlsym failed, %s\n", __func__, dlerror());
		return TEST_FAILURE;
	}

	fn_syscall = dlsym(handle, "lkl_syscall");
	if (!fn_syscall) {
		lkl_test_logf("%s: dlsym failed, %s\n", __func__, dlerror());
		return TEST_FAILURE;
	}

	fn_sys_halt = dlsym(handle, "lkl_sys_halt");
	if (!fn_sys_halt) {
		lkl_test_logf("%s: dlsym failed, %s\n", __func__, dlerror());
		return TEST_FAILURE;
	}

	fn_tls_mode = dlsym(handle, "lkl_change_tls_mode");
	if (!fn_tls_mode) {
		lkl_test_logf("%s: dlsym failed, %s\n", __func__, dlerror());
		return TEST_FAILURE;
	}

	/* start calling resolved symbols */
	fn_tls_mode();
	fn_init(lkl_host_ops);
	ret = fn_start(CMD_LINE);
	if (ret != 0) {
		lkl_test_logf("lkl_start_kernel() = %ld %s\n",
			      ret, ret < 0 ? lkl_strerror(ret) : "");
		return TEST_FAILURE;
	}

	ret = fn_syscall(__lkl__NR_getpid, params);
	lkl_test_logf("getpid() = %ld\n", ret);
	if (ret != 1) {
		lkl_test_logf("getpid() = %ld %s\n", ret, ret < 0 ? lkl_strerror(ret) : "");
		return TEST_FAILURE;
	}

	ret = fn_sys_halt();
	if (ret != 0) {
		lkl_test_logf("halt() = %ld %s\n", ret, ret < 0 ? lkl_strerror(ret) : "");
		return TEST_FAILURE;
	}

	return ret == 0 ? TEST_SUCCESS : TEST_FAILURE;
}

struct lkl_test tests[] = {
	LKL_TEST(dlmopen),
};

int main(int argc, const char **argv)
{
	int ret;

	if (NO_DLMOPEN_LINUX) {
		lkl_test_logf("no dlmopen support\n");
		return TEST_SKIP;
	}

	ret = lkl_test_run(tests, sizeof(tests)/sizeof(struct lkl_test),
			    "dlmopen");

	lkl_cleanup();

	return ret;
}

#else
#include "test.h"

int main(int argc, const char **argv)
{
	lkl_test_logf("no x86_64 arch supported\n");
	return TEST_SKIP;
}
#endif /* defined (__x86_64__) && defined (__linux__) */

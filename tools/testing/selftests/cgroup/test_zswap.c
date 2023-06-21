// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <linux/limits.h>
#include <unistd.h>
#include <stdio.h>

#include "../kselftest.h"
#include "cgroup_util.h"

#define T(x) { x, #x }
struct zswap_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
};
#undef T

static bool zswap_configured(void)
{
	return access("/sys/module/zswap", F_OK) == 0;
}

int main(int argc, char **argv)
{
	char root[PATH_MAX];
	int i, ret = EXIT_SUCCESS;

	if (cg_find_unified_root(root, sizeof(root)))
		ksft_exit_skip("cgroup v2 isn't mounted\n");

	if (!zswap_configured())
		ksft_exit_skip("zswap isn't configured\n");

	/*
	 * Check that memory controller is available:
	 * memory is listed in cgroup.controllers
	 */
	if (cg_read_strstr(root, "cgroup.controllers", "memory"))
		ksft_exit_skip("memory controller isn't available\n");

	if (cg_read_strstr(root, "cgroup.subtree_control", "memory"))
		if (cg_write(root, "cgroup.subtree_control", "+memory"))
			ksft_exit_skip("Failed to set memory controller\n");

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		switch (tests[i].fn(root)) {
		case KSFT_PASS:
			ksft_test_result_pass("%s\n", tests[i].name);
			break;
		case KSFT_SKIP:
			ksft_test_result_skip("%s\n", tests[i].name);
			break;
		default:
			ret = EXIT_FAILURE;
			ksft_test_result_fail("%s\n", tests[i].name);
			break;
		}
	}

	return ret;
}

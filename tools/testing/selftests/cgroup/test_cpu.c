// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <linux/limits.h>
#include <stdio.h>

#include "../kselftest.h"
#include "cgroup_util.h"

/*
 * This test creates two nested cgroups with and without enabling
 * the cpu controller.
 */
static int test_cpucg_subtree_control(const char *root)
{
	char *parent = NULL, *child = NULL, *parent2 = NULL, *child2 = NULL;
	int ret = KSFT_FAIL;

	// Create two nested cgroups with the cpu controller enabled.
	parent = cg_name(root, "cpucg_test_0");
	if (!parent)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+cpu"))
		goto cleanup;

	child = cg_name(parent, "cpucg_test_child");
	if (!child)
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_read_strstr(child, "cgroup.controllers", "cpu"))
		goto cleanup;

	// Create two nested cgroups without enabling the cpu controller.
	parent2 = cg_name(root, "cpucg_test_1");
	if (!parent2)
		goto cleanup;

	if (cg_create(parent2))
		goto cleanup;

	child2 = cg_name(parent2, "cpucg_test_child");
	if (!child2)
		goto cleanup;

	if (cg_create(child2))
		goto cleanup;

	if (!cg_read_strstr(child2, "cgroup.controllers", "cpu"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_destroy(child);
	free(child);
	cg_destroy(child2);
	free(child2);
	cg_destroy(parent);
	free(parent);
	cg_destroy(parent2);
	free(parent2);

	return ret;
}

#define T(x) { x, #x }
struct cpucg_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cpucg_subtree_control),
};
#undef T

int main(int argc, char *argv[])
{
	char root[PATH_MAX];
	int i, ret = EXIT_SUCCESS;

	if (cg_find_unified_root(root, sizeof(root)))
		ksft_exit_skip("cgroup v2 isn't mounted\n");

	if (cg_read_strstr(root, "cgroup.subtree_control", "cpu"))
		if (cg_write(root, "cgroup.subtree_control", "+cpu"))
			ksft_exit_skip("Failed to set cpu controller\n");

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

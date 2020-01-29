/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>

#include "../kselftest.h"
#include "cgroup_util.h"

/*
 * A(0) - B(0) - C(1)
 *        \ D(0)
 *
 * A, B and C's "populated" fields would be 1 while D's 0.
 * test that after the one process in C is moved to root,
 * A,B and C's "populated" fields would flip to "0" and file
 * modified events will be generated on the
 * "cgroup.events" files of both cgroups.
 */
static int test_cgcore_populated(const char *root)
{
	int ret = KSFT_FAIL;
	char *cg_test_a = NULL, *cg_test_b = NULL;
	char *cg_test_c = NULL, *cg_test_d = NULL;

	cg_test_a = cg_name(root, "cg_test_a");
	cg_test_b = cg_name(root, "cg_test_a/cg_test_b");
	cg_test_c = cg_name(root, "cg_test_a/cg_test_b/cg_test_c");
	cg_test_d = cg_name(root, "cg_test_a/cg_test_b/cg_test_d");

	if (!cg_test_a || !cg_test_b || !cg_test_c || !cg_test_d)
		goto cleanup;

	if (cg_create(cg_test_a))
		goto cleanup;

	if (cg_create(cg_test_b))
		goto cleanup;

	if (cg_create(cg_test_c))
		goto cleanup;

	if (cg_create(cg_test_d))
		goto cleanup;

	if (cg_enter_current(cg_test_c))
		goto cleanup;

	if (cg_read_strcmp(cg_test_a, "cgroup.events", "populated 1\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_b, "cgroup.events", "populated 1\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_c, "cgroup.events", "populated 1\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_d, "cgroup.events", "populated 0\n"))
		goto cleanup;

	if (cg_enter_current(root))
		goto cleanup;

	if (cg_read_strcmp(cg_test_a, "cgroup.events", "populated 0\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_b, "cgroup.events", "populated 0\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_c, "cgroup.events", "populated 0\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_d, "cgroup.events", "populated 0\n"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (cg_test_d)
		cg_destroy(cg_test_d);
	if (cg_test_c)
		cg_destroy(cg_test_c);
	if (cg_test_b)
		cg_destroy(cg_test_b);
	if (cg_test_a)
		cg_destroy(cg_test_a);
	free(cg_test_d);
	free(cg_test_c);
	free(cg_test_b);
	free(cg_test_a);
	return ret;
}

/*
 * A (domain threaded) - B (threaded) - C (domain)
 *
 * test that C can't be used until it is turned into a
 * threaded cgroup.  "cgroup.type" file will report "domain (invalid)" in
 * these cases. Operations which fail due to invalid topology use
 * EOPNOTSUPP as the errno.
 */
static int test_cgcore_invalid_domain(const char *root)
{
	int ret = KSFT_FAIL;
	char *grandparent = NULL, *parent = NULL, *child = NULL;

	grandparent = cg_name(root, "cg_test_grandparent");
	parent = cg_name(root, "cg_test_grandparent/cg_test_parent");
	child = cg_name(root, "cg_test_grandparent/cg_test_parent/cg_test_child");
	if (!parent || !child || !grandparent)
		goto cleanup;

	if (cg_create(grandparent))
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(parent, "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_read_strcmp(child, "cgroup.type", "domain invalid\n"))
		goto cleanup;

	if (!cg_enter_current(child))
		goto cleanup;

	if (errno != EOPNOTSUPP)
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	if (grandparent)
		cg_destroy(grandparent);
	free(child);
	free(parent);
	free(grandparent);
	return ret;
}

/*
 * Test that when a child becomes threaded
 * the parent type becomes domain threaded.
 */
static int test_cgcore_parent_becomes_threaded(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(child, "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_read_strcmp(parent, "cgroup.type", "domain threaded\n"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;

}

/*
 * Test that there's no internal process constrain on threaded cgroups.
 * You can add threads/processes on a parent with a controller enabled.
 */
static int test_cgcore_no_internal_process_constraint_on_threads(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	if (cg_read_strstr(root, "cgroup.controllers", "cpu") ||
	    cg_write(root, "cgroup.subtree_control", "+cpu")) {
		ret = KSFT_SKIP;
		goto cleanup;
	}

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(parent, "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_write(child, "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+cpu"))
		goto cleanup;

	if (cg_enter_current(parent))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	cg_enter_current(root);
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;
}

/*
 * Test that you can't enable a controller on a child if it's not enabled
 * on the parent.
 */
static int test_cgcore_top_down_constraint_enable(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (!cg_write(child, "cgroup.subtree_control", "+memory"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;
}

/*
 * Test that you can't disable a controller on a parent
 * if it's enabled in a child.
 */
static int test_cgcore_top_down_constraint_disable(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+memory"))
		goto cleanup;

	if (cg_write(child, "cgroup.subtree_control", "+memory"))
		goto cleanup;

	if (!cg_write(parent, "cgroup.subtree_control", "-memory"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;
}

/*
 * Test internal process constraint.
 * You can't add a pid to a domain parent if a controller is enabled.
 */
static int test_cgcore_internal_process_constraint(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+memory"))
		goto cleanup;

	if (!cg_enter_current(parent))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;
}

static void *dummy_thread_fn(void *arg)
{
	return (void *)(size_t)pause();
}

/*
 * Test threadgroup migration.
 * All threads of a process are migrated together.
 */
static int test_cgcore_proc_migration(const char *root)
{
	int ret = KSFT_FAIL;
	int t, c_threads = 0, n_threads = 13;
	char *src = NULL, *dst = NULL;
	pthread_t threads[n_threads];

	src = cg_name(root, "cg_src");
	dst = cg_name(root, "cg_dst");
	if (!src || !dst)
		goto cleanup;

	if (cg_create(src))
		goto cleanup;
	if (cg_create(dst))
		goto cleanup;

	if (cg_enter_current(src))
		goto cleanup;

	for (c_threads = 0; c_threads < n_threads; ++c_threads) {
		if (pthread_create(&threads[c_threads], NULL, dummy_thread_fn, NULL))
			goto cleanup;
	}

	cg_enter_current(dst);
	if (cg_read_lc(dst, "cgroup.threads") != n_threads + 1)
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	for (t = 0; t < c_threads; ++t) {
		pthread_cancel(threads[t]);
	}

	for (t = 0; t < c_threads; ++t) {
		pthread_join(threads[t], NULL);
	}

	cg_enter_current(root);

	if (dst)
		cg_destroy(dst);
	if (src)
		cg_destroy(src);
	free(dst);
	free(src);
	return ret;
}

static void *migrating_thread_fn(void *arg)
{
	int g, i, n_iterations = 1000;
	char **grps = arg;
	char lines[3][PATH_MAX];

	for (g = 1; g < 3; ++g)
		snprintf(lines[g], sizeof(lines[g]), "0::%s", grps[g] + strlen(grps[0]));

	for (i = 0; i < n_iterations; ++i) {
		cg_enter_current_thread(grps[(i % 2) + 1]);

		if (proc_read_strstr(0, 1, "cgroup", lines[(i % 2) + 1]))
			return (void *)-1;
	}
	return NULL;
}

/*
 * Test single thread migration.
 * Threaded cgroups allow successful migration of a thread.
 */
static int test_cgcore_thread_migration(const char *root)
{
	int ret = KSFT_FAIL;
	char *dom = NULL;
	char line[PATH_MAX];
	char *grps[3] = { (char *)root, NULL, NULL };
	pthread_t thr;
	void *retval;

	dom = cg_name(root, "cg_dom");
	grps[1] = cg_name(root, "cg_dom/cg_src");
	grps[2] = cg_name(root, "cg_dom/cg_dst");
	if (!grps[1] || !grps[2] || !dom)
		goto cleanup;

	if (cg_create(dom))
		goto cleanup;
	if (cg_create(grps[1]))
		goto cleanup;
	if (cg_create(grps[2]))
		goto cleanup;

	if (cg_write(grps[1], "cgroup.type", "threaded"))
		goto cleanup;
	if (cg_write(grps[2], "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_enter_current(grps[1]))
		goto cleanup;

	if (pthread_create(&thr, NULL, migrating_thread_fn, grps))
		goto cleanup;

	if (pthread_join(thr, &retval))
		goto cleanup;

	if (retval)
		goto cleanup;

	snprintf(line, sizeof(line), "0::%s", grps[1] + strlen(grps[0]));
	if (proc_read_strstr(0, 1, "cgroup", line))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	if (grps[2])
		cg_destroy(grps[2]);
	if (grps[1])
		cg_destroy(grps[1]);
	if (dom)
		cg_destroy(dom);
	free(grps[2]);
	free(grps[1]);
	free(dom);
	return ret;
}

#define T(x) { x, #x }
struct corecg_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cgcore_internal_process_constraint),
	T(test_cgcore_top_down_constraint_enable),
	T(test_cgcore_top_down_constraint_disable),
	T(test_cgcore_no_internal_process_constraint_on_threads),
	T(test_cgcore_parent_becomes_threaded),
	T(test_cgcore_invalid_domain),
	T(test_cgcore_populated),
	T(test_cgcore_proc_migration),
	T(test_cgcore_thread_migration),
};
#undef T

int main(int argc, char *argv[])
{
	char root[PATH_MAX];
	int i, ret = EXIT_SUCCESS;

	if (cg_find_unified_root(root, sizeof(root)))
		ksft_exit_skip("cgroup v2 isn't mounted\n");

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

// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <linux/limits.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/sysinfo.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include "../kselftest.h"
#include "cgroup_util.h"

static int read_int(const char *path, size_t *value)
{
	FILE *file;
	int ret = 0;

	file = fopen(path, "r");
	if (!file)
		return -1;
	if (fscanf(file, "%ld", value) != 1)
		ret = -1;
	fclose(file);
	return ret;
}

static int set_min_free_kb(size_t value)
{
	FILE *file;
	int ret;

	file = fopen("/proc/sys/vm/min_free_kbytes", "w");
	if (!file)
		return -1;
	ret = fprintf(file, "%ld\n", value);
	fclose(file);
	return ret;
}

static int read_min_free_kb(size_t *value)
{
	return read_int("/proc/sys/vm/min_free_kbytes", value);
}

static int get_zswap_stored_pages(size_t *value)
{
	return read_int("/sys/kernel/debug/zswap/stored_pages", value);
}

struct no_kmem_bypass_child_args {
	size_t target_alloc_bytes;
	size_t child_allocated;
};

static int no_kmem_bypass_child(const char *cgroup, void *arg)
{
	struct no_kmem_bypass_child_args *values = arg;
	void *allocation;

	allocation = malloc(values->target_alloc_bytes);
	if (!allocation) {
		values->child_allocated = true;
		return -1;
	}
	for (long i = 0; i < values->target_alloc_bytes; i += 4095)
		((char *)allocation)[i] = 'a';
	values->child_allocated = true;
	pause();
	free(allocation);
	return 0;
}

/*
 * When pages owned by a memcg are pushed to zswap by kswapd, they should be
 * charged to that cgroup. This wasn't the case before commit
 * cd08d80ecdac("mm: correctly charge compressed memory to its memcg").
 *
 * The test first allocates memory in a memcg, then raises min_free_kbytes to
 * a very high value so that the allocation falls below low wm, then makes
 * another allocation to trigger kswapd that should push the memcg-owned pages
 * to zswap and verifies that the zswap pages are correctly charged.
 *
 * To be run on a VM with at most 4G of memory.
 */
static int test_no_kmem_bypass(const char *root)
{
	size_t min_free_kb_high, min_free_kb_low, min_free_kb_original;
	struct no_kmem_bypass_child_args *values;
	size_t trigger_allocation_size;
	int wait_child_iteration = 0;
	long stored_pages_threshold;
	struct sysinfo sys_info;
	int ret = KSFT_FAIL;
	int child_status;
	char *test_group;
	pid_t child_pid;

	/* Read sys info and compute test values accordingly */
	if (sysinfo(&sys_info) != 0)
		return KSFT_FAIL;
	if (sys_info.totalram > 5000000000)
		return KSFT_SKIP;
	values = mmap(0, sizeof(struct no_kmem_bypass_child_args), PROT_READ |
			PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (values == MAP_FAILED)
		return KSFT_FAIL;
	if (read_min_free_kb(&min_free_kb_original))
		return KSFT_FAIL;
	min_free_kb_high = sys_info.totalram / 2000;
	min_free_kb_low = sys_info.totalram / 500000;
	values->target_alloc_bytes = (sys_info.totalram - min_free_kb_high * 1000) +
		sys_info.totalram * 5 / 100;
	stored_pages_threshold = sys_info.totalram / 5 / 4096;
	trigger_allocation_size = sys_info.totalram / 20;

	/* Set up test memcg */
	if (cg_write(root, "cgroup.subtree_control", "+memory"))
		goto out;
	test_group = cg_name(root, "kmem_bypass_test");
	if (!test_group)
		goto out;

	/* Spawn memcg child and wait for it to allocate */
	set_min_free_kb(min_free_kb_low);
	if (cg_create(test_group))
		goto out;
	values->child_allocated = false;
	child_pid = cg_run_nowait(test_group, no_kmem_bypass_child, values);
	if (child_pid < 0)
		goto out;
	while (!values->child_allocated && wait_child_iteration++ < 10000)
		usleep(1000);

	/* Try to wakeup kswapd and let it push child memory to zswap */
	set_min_free_kb(min_free_kb_high);
	for (int i = 0; i < 20; i++) {
		size_t stored_pages;
		char *trigger_allocation = malloc(trigger_allocation_size);

		if (!trigger_allocation)
			break;
		for (int i = 0; i < trigger_allocation_size; i += 4095)
			trigger_allocation[i] = 'b';
		usleep(100000);
		free(trigger_allocation);
		if (get_zswap_stored_pages(&stored_pages))
			break;
		if (stored_pages < 0)
			break;
		/* If memory was pushed to zswap, verify it belongs to memcg */
		if (stored_pages > stored_pages_threshold) {
			int zswapped = cg_read_key_long(test_group, "memory.stat", "zswapped ");
			int delta = stored_pages * 4096 - zswapped;
			int result_ok = delta < stored_pages * 4096 / 4;

			ret = result_ok ? KSFT_PASS : KSFT_FAIL;
			break;
		}
	}

	kill(child_pid, SIGTERM);
	waitpid(child_pid, &child_status, 0);
out:
	set_min_free_kb(min_free_kb_original);
	cg_destroy(test_group);
	free(test_group);
	return ret;
}

#define T(x) { x, #x }
struct zswap_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_no_kmem_bypass),
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

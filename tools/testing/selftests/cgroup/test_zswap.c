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

static long get_cg_wb_count(const char *cg)
{
	return cg_read_key_long(cg, "memory.stat", "zswpwb");
}

static long get_zswpout(const char *cgroup)
{
	return cg_read_key_long(cgroup, "memory.stat", "zswpout ");
}

static int allocate_and_read_bytes(const char *cgroup, void *arg)
{
	size_t size = (size_t)arg;
	char *mem = (char *)malloc(size);
	int ret = 0;

	if (!mem)
		return -1;
	for (int i = 0; i < size; i += 4095)
		mem[i] = 'a';

	/* Go through the allocated memory to (z)swap in and out pages */
	for (int i = 0; i < size; i += 4095) {
		if (mem[i] != 'a')
			ret = -1;
	}

	free(mem);
	return ret;
}

static int allocate_bytes(const char *cgroup, void *arg)
{
	size_t size = (size_t)arg;
	char *mem = (char *)malloc(size);

	if (!mem)
		return -1;
	for (int i = 0; i < size; i += 4095)
		mem[i] = 'a';
	free(mem);
	return 0;
}

static char *setup_test_group_1M(const char *root, const char *name)
{
	char *group_name = cg_name(root, name);

	if (!group_name)
		return NULL;
	if (cg_create(group_name))
		goto fail;
	if (cg_write(group_name, "memory.max", "1M")) {
		cg_destroy(group_name);
		goto fail;
	}
	return group_name;
fail:
	free(group_name);
	return NULL;
}

/*
 * Sanity test to check that pages are written into zswap.
 */
static int test_zswap_usage(const char *root)
{
	long zswpout_before, zswpout_after;
	int ret = KSFT_FAIL;
	char *test_group;

	test_group = cg_name(root, "no_shrink_test");
	if (!test_group)
		goto out;
	if (cg_create(test_group))
		goto out;
	if (cg_write(test_group, "memory.max", "1M"))
		goto out;

	zswpout_before = get_zswpout(test_group);
	if (zswpout_before < 0) {
		ksft_print_msg("Failed to get zswpout\n");
		goto out;
	}

	/* Allocate more than memory.max to push memory into zswap */
	if (cg_run(test_group, allocate_bytes, (void *)MB(4)))
		goto out;

	/* Verify that pages come into zswap */
	zswpout_after = get_zswpout(test_group);
	if (zswpout_after <= zswpout_before) {
		ksft_print_msg("zswpout does not increase after test program\n");
		goto out;
	}
	ret = KSFT_PASS;

out:
	cg_destroy(test_group);
	free(test_group);
	return ret;
}

/*
 * Check that when memory.zswap.max = 0, no pages can go to the zswap pool for
 * the cgroup.
 */
static int test_swapin_nozswap(const char *root)
{
	int ret = KSFT_FAIL;
	char *test_group;
	long swap_peak, zswpout;

	test_group = cg_name(root, "no_zswap_test");
	if (!test_group)
		goto out;
	if (cg_create(test_group))
		goto out;
	if (cg_write(test_group, "memory.max", "8M"))
		goto out;
	if (cg_write(test_group, "memory.zswap.max", "0"))
		goto out;

	/* Allocate and read more than memory.max to trigger swapin */
	if (cg_run(test_group, allocate_and_read_bytes, (void *)MB(32)))
		goto out;

	/* Verify that pages are swapped out, but no zswap happened */
	swap_peak = cg_read_long(test_group, "memory.swap.peak");
	if (swap_peak < 0) {
		ksft_print_msg("failed to get cgroup's swap_peak\n");
		goto out;
	}

	if (swap_peak < MB(24)) {
		ksft_print_msg("at least 24MB of memory should be swapped out\n");
		goto out;
	}

	zswpout = get_zswpout(test_group);
	if (zswpout < 0) {
		ksft_print_msg("failed to get zswpout\n");
		goto out;
	}

	if (zswpout > 0) {
		ksft_print_msg("zswapout > 0 when memory.zswap.max = 0\n");
		goto out;
	}

	ret = KSFT_PASS;

out:
	cg_destroy(test_group);
	free(test_group);
	return ret;
}

/* Simple test to verify the (z)swapin code paths */
static int test_zswapin(const char *root)
{
	int ret = KSFT_FAIL;
	char *test_group;
	long zswpin;

	test_group = cg_name(root, "zswapin_test");
	if (!test_group)
		goto out;
	if (cg_create(test_group))
		goto out;
	if (cg_write(test_group, "memory.max", "8M"))
		goto out;
	if (cg_write(test_group, "memory.zswap.max", "max"))
		goto out;

	/* Allocate and read more than memory.max to trigger (z)swap in */
	if (cg_run(test_group, allocate_and_read_bytes, (void *)MB(32)))
		goto out;

	zswpin = cg_read_key_long(test_group, "memory.stat", "zswpin ");
	if (zswpin < 0) {
		ksft_print_msg("failed to get zswpin\n");
		goto out;
	}

	if (zswpin < MB(24) / PAGE_SIZE) {
		ksft_print_msg("at least 24MB should be brought back from zswap\n");
		goto out;
	}

	ret = KSFT_PASS;

out:
	cg_destroy(test_group);
	free(test_group);
	return ret;
}

/*
 * Attempt writeback with the following steps:
 * 1. Allocate memory.
 * 2. Reclaim memory equal to the amount that was allocated in step 1.
      This will move it into zswap.
 * 3. Save current zswap usage.
 * 4. Move the memory allocated in step 1 back in from zswap.
 * 5. Set zswap.max to half the amount that was recorded in step 3.
 * 6. Attempt to reclaim memory equal to the amount that was allocated,
      this will either trigger writeback if it's enabled, or reclamation
      will fail if writeback is disabled as there isn't enough zswap space.
 */
static int attempt_writeback(const char *cgroup, void *arg)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	char *test_group = arg;
	size_t memsize = MB(4);
	char buf[pagesize];
	long zswap_usage;
	bool wb_enabled;
	int ret = -1;
	char *mem;

	wb_enabled = cg_read_long(test_group, "memory.zswap.writeback");
	mem = (char *)malloc(memsize);
	if (!mem)
		return ret;

	/*
	 * Fill half of each page with increasing data, and keep other
	 * half empty, this will result in data that is still compressible
	 * and ends up in zswap, with material zswap usage.
	 */
	for (int i = 0; i < pagesize; i++)
		buf[i] = i < pagesize/2 ? (char) i : 0;

	for (int i = 0; i < memsize; i += pagesize)
		memcpy(&mem[i], buf, pagesize);

	/* Try and reclaim allocated memory */
	if (cg_write_numeric(test_group, "memory.reclaim", memsize)) {
		ksft_print_msg("Failed to reclaim all of the requested memory\n");
		goto out;
	}

	zswap_usage = cg_read_long(test_group, "memory.zswap.current");

	/* zswpin */
	for (int i = 0; i < memsize; i += pagesize) {
		if (memcmp(&mem[i], buf, pagesize)) {
			ksft_print_msg("invalid memory\n");
			goto out;
		}
	}

	if (cg_write_numeric(test_group, "memory.zswap.max", zswap_usage/2))
		goto out;

	/*
	 * If writeback is enabled, trying to reclaim memory now will trigger a
	 * writeback as zswap.max is half of what was needed when reclaim ran the first time.
	 * If writeback is disabled, memory reclaim will fail as zswap is limited and
	 * it can't writeback to swap.
	 */
	ret = cg_write_numeric(test_group, "memory.reclaim", memsize);
	if (!wb_enabled)
		ret = (ret == -EAGAIN) ? 0 : -1;

out:
	free(mem);
	return ret;
}

/* Test to verify the zswap writeback path */
static int test_zswap_writeback(const char *root, bool wb)
{
	long zswpwb_before, zswpwb_after;
	int ret = KSFT_FAIL;
	char *test_group;

	test_group = cg_name(root, "zswap_writeback_test");
	if (!test_group)
		goto out;
	if (cg_create(test_group))
		goto out;
	if (cg_write(test_group, "memory.zswap.writeback", wb ? "1" : "0"))
		goto out;

	zswpwb_before = get_cg_wb_count(test_group);
	if (zswpwb_before != 0) {
		ksft_print_msg("zswpwb_before = %ld instead of 0\n", zswpwb_before);
		goto out;
	}

	if (cg_run(test_group, attempt_writeback, (void *) test_group))
		goto out;

	/* Verify that zswap writeback occurred only if writeback was enabled */
	zswpwb_after = get_cg_wb_count(test_group);
	if (zswpwb_after < 0)
		goto out;

	if (wb != !!zswpwb_after) {
		ksft_print_msg("zswpwb_after is %ld while wb is %s",
				zswpwb_after, wb ? "enabled" : "disabled");
		goto out;
	}

	ret = KSFT_PASS;

out:
	cg_destroy(test_group);
	free(test_group);
	return ret;
}

static int test_zswap_writeback_enabled(const char *root)
{
	return test_zswap_writeback(root, true);
}

static int test_zswap_writeback_disabled(const char *root)
{
	return test_zswap_writeback(root, false);
}

/*
 * When trying to store a memcg page in zswap, if the memcg hits its memory
 * limit in zswap, writeback should affect only the zswapped pages of that
 * memcg.
 */
static int test_no_invasive_cgroup_shrink(const char *root)
{
	int ret = KSFT_FAIL;
	size_t control_allocation_size = MB(10);
	char *control_allocation = NULL, *wb_group = NULL, *control_group = NULL;

	wb_group = setup_test_group_1M(root, "per_memcg_wb_test1");
	if (!wb_group)
		return KSFT_FAIL;
	if (cg_write(wb_group, "memory.zswap.max", "10K"))
		goto out;
	control_group = setup_test_group_1M(root, "per_memcg_wb_test2");
	if (!control_group)
		goto out;

	/* Push some test_group2 memory into zswap */
	if (cg_enter_current(control_group))
		goto out;
	control_allocation = malloc(control_allocation_size);
	for (int i = 0; i < control_allocation_size; i += 4095)
		control_allocation[i] = 'a';
	if (cg_read_key_long(control_group, "memory.stat", "zswapped") < 1)
		goto out;

	/* Allocate 10x memory.max to push wb_group memory into zswap and trigger wb */
	if (cg_run(wb_group, allocate_bytes, (void *)MB(10)))
		goto out;

	/* Verify that only zswapped memory from gwb_group has been written back */
	if (get_cg_wb_count(wb_group) > 0 && get_cg_wb_count(control_group) == 0)
		ret = KSFT_PASS;
out:
	cg_enter_current(root);
	if (control_group) {
		cg_destroy(control_group);
		free(control_group);
	}
	cg_destroy(wb_group);
	free(wb_group);
	if (control_allocation)
		free(control_allocation);
	return ret;
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
	char *test_group = NULL;
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
	T(test_zswap_usage),
	T(test_swapin_nozswap),
	T(test_zswapin),
	T(test_zswap_writeback_enabled),
	T(test_zswap_writeback_disabled),
	T(test_no_kmem_bypass),
	T(test_no_invasive_cgroup_shrink),
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

	if (cg_find_unified_root(root, sizeof(root), NULL))
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

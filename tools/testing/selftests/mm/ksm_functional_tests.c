// SPDX-License-Identifier: GPL-2.0-only
/*
 * KSM functional tests
 *
 * Copyright 2022, Red Hat, Inc.
 *
 * Author(s): David Hildenbrand <david@redhat.com>
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <asm-generic/unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/userfaultfd.h>

#include "../kselftest.h"
#include "vm_util.h"

#define KiB 1024u
#define MiB (1024 * KiB)
#define FORK_EXEC_CHILD_PRG_NAME "ksm_fork_exec_child"

#define MAP_MERGE_FAIL ((void *)-1)
#define MAP_MERGE_SKIP ((void *)-2)

enum ksm_merge_mode {
	KSM_MERGE_PRCTL,
	KSM_MERGE_MADVISE,
	KSM_MERGE_NONE, /* PRCTL already set */
};

static int mem_fd;
static int ksm_fd;
static int ksm_full_scans_fd;
static int proc_self_ksm_stat_fd;
static int proc_self_ksm_merging_pages_fd;
static int ksm_use_zero_pages_fd;
static int pagemap_fd;
static size_t pagesize;

static bool range_maps_duplicates(char *addr, unsigned long size)
{
	unsigned long offs_a, offs_b, pfn_a, pfn_b;

	/*
	 * There is no easy way to check if there are KSM pages mapped into
	 * this range. We only check that the range does not map the same PFN
	 * twice by comparing each pair of mapped pages.
	 */
	for (offs_a = 0; offs_a < size; offs_a += pagesize) {
		pfn_a = pagemap_get_pfn(pagemap_fd, addr + offs_a);
		/* Page not present or PFN not exposed by the kernel. */
		if (pfn_a == -1ul || !pfn_a)
			continue;

		for (offs_b = offs_a + pagesize; offs_b < size;
		     offs_b += pagesize) {
			pfn_b = pagemap_get_pfn(pagemap_fd, addr + offs_b);
			if (pfn_b == -1ul || !pfn_b)
				continue;
			if (pfn_a == pfn_b)
				return true;
		}
	}
	return false;
}

static long get_my_ksm_zero_pages(void)
{
	char buf[200];
	char *substr_ksm_zero;
	size_t value_pos;
	ssize_t read_size;
	unsigned long my_ksm_zero_pages;

	if (!proc_self_ksm_stat_fd)
		return 0;

	read_size = pread(proc_self_ksm_stat_fd, buf, sizeof(buf) - 1, 0);
	if (read_size < 0)
		return -errno;

	buf[read_size] = 0;

	substr_ksm_zero = strstr(buf, "ksm_zero_pages");
	if (!substr_ksm_zero)
		return 0;

	value_pos = strcspn(substr_ksm_zero, "0123456789");
	my_ksm_zero_pages = strtol(substr_ksm_zero + value_pos, NULL, 10);

	return my_ksm_zero_pages;
}

static long get_my_merging_pages(void)
{
	char buf[10];
	ssize_t ret;

	if (proc_self_ksm_merging_pages_fd < 0)
		return proc_self_ksm_merging_pages_fd;

	ret = pread(proc_self_ksm_merging_pages_fd, buf, sizeof(buf) - 1, 0);
	if (ret <= 0)
		return -errno;
	buf[ret] = 0;

	return strtol(buf, NULL, 10);
}

static long ksm_get_full_scans(void)
{
	char buf[10];
	ssize_t ret;

	ret = pread(ksm_full_scans_fd, buf, sizeof(buf) - 1, 0);
	if (ret <= 0)
		return -errno;
	buf[ret] = 0;

	return strtol(buf, NULL, 10);
}

static int ksm_merge(void)
{
	long start_scans, end_scans;

	/* Wait for two full scans such that any possible merging happened. */
	start_scans = ksm_get_full_scans();
	if (start_scans < 0)
		return start_scans;
	if (write(ksm_fd, "1", 1) != 1)
		return -errno;
	do {
		end_scans = ksm_get_full_scans();
		if (end_scans < 0)
			return end_scans;
	} while (end_scans < start_scans + 2);

	return 0;
}

static int ksm_unmerge(void)
{
	if (write(ksm_fd, "2", 1) != 1)
		return -errno;
	return 0;
}

static char *__mmap_and_merge_range(char val, unsigned long size, int prot,
				  enum ksm_merge_mode mode)
{
	char *map;
	char *err_map = MAP_MERGE_FAIL;
	int ret;

	/* Stabilize accounting by disabling KSM completely. */
	if (ksm_unmerge()) {
		ksft_print_msg("Disabling (unmerging) KSM failed\n");
		return err_map;
	}

	if (get_my_merging_pages() > 0) {
		ksft_print_msg("Still pages merged\n");
		return err_map;
	}

	map = mmap(NULL, size, PROT_READ|PROT_WRITE,
		   MAP_PRIVATE|MAP_ANON, -1, 0);
	if (map == MAP_FAILED) {
		ksft_print_msg("mmap() failed\n");
		return err_map;
	}

	/* Don't use THP. Ignore if THP are not around on a kernel. */
	if (madvise(map, size, MADV_NOHUGEPAGE) && errno != EINVAL) {
		ksft_print_msg("MADV_NOHUGEPAGE failed\n");
		goto unmap;
	}

	/* Make sure each page contains the same values to merge them. */
	memset(map, val, size);

	if (mprotect(map, size, prot)) {
		ksft_print_msg("mprotect() failed\n");
		err_map = MAP_MERGE_SKIP;
		goto unmap;
	}

	switch (mode) {
	case KSM_MERGE_PRCTL:
		ret = prctl(PR_SET_MEMORY_MERGE, 1, 0, 0, 0);
		if (ret < 0 && errno == EINVAL) {
			ksft_print_msg("PR_SET_MEMORY_MERGE not supported\n");
			err_map = MAP_MERGE_SKIP;
			goto unmap;
		} else if (ret) {
			ksft_print_msg("PR_SET_MEMORY_MERGE=1 failed\n");
			goto unmap;
		}
		break;
	case KSM_MERGE_MADVISE:
		if (madvise(map, size, MADV_MERGEABLE)) {
			ksft_print_msg("MADV_MERGEABLE failed\n");
			goto unmap;
		}
		break;
	case KSM_MERGE_NONE:
		break;
	}

	/* Run KSM to trigger merging and wait. */
	if (ksm_merge()) {
		ksft_print_msg("Running KSM failed\n");
		goto unmap;
	}

	/*
	 * Check if anything was merged at all. Ignore the zero page that is
	 * accounted differently (depending on kernel support).
	 */
	if (val && !get_my_merging_pages()) {
		ksft_print_msg("No pages got merged\n");
		goto unmap;
	}

	return map;
unmap:
	munmap(map, size);
	return err_map;
}

static char *mmap_and_merge_range(char val, unsigned long size, int prot,
				  enum ksm_merge_mode mode)
{
	char *map;
	char *ret = MAP_FAILED;

	map = __mmap_and_merge_range(val, size, prot, mode);
	if (map == MAP_MERGE_FAIL)
		ksft_test_result_fail("Merging memory failed");
	else if (map == MAP_MERGE_SKIP)
		ksft_test_result_skip("Merging memory skipped");
	else
		ret = map;

	return ret;
}

static void test_unmerge(void)
{
	const unsigned int size = 2 * MiB;
	char *map;

	ksft_print_msg("[RUN] %s\n", __func__);

	map = mmap_and_merge_range(0xcf, size, PROT_READ | PROT_WRITE, KSM_MERGE_MADVISE);
	if (map == MAP_FAILED)
		return;

	if (madvise(map, size, MADV_UNMERGEABLE)) {
		ksft_test_result_fail("MADV_UNMERGEABLE failed\n");
		goto unmap;
	}

	ksft_test_result(!range_maps_duplicates(map, size),
			 "Pages were unmerged\n");
unmap:
	munmap(map, size);
}

static void test_unmerge_zero_pages(void)
{
	const unsigned int size = 2 * MiB;
	char *map;
	unsigned int offs;
	unsigned long pages_expected;

	ksft_print_msg("[RUN] %s\n", __func__);

	if (proc_self_ksm_stat_fd < 0) {
		ksft_test_result_skip("open(\"/proc/self/ksm_stat\") failed\n");
		return;
	}
	if (ksm_use_zero_pages_fd < 0) {
		ksft_test_result_skip("open \"/sys/kernel/mm/ksm/use_zero_pages\" failed\n");
		return;
	}
	if (write(ksm_use_zero_pages_fd, "1", 1) != 1) {
		ksft_test_result_skip("write \"/sys/kernel/mm/ksm/use_zero_pages\" failed\n");
		return;
	}

	/* Let KSM deduplicate zero pages. */
	map = mmap_and_merge_range(0x00, size, PROT_READ | PROT_WRITE, KSM_MERGE_MADVISE);
	if (map == MAP_FAILED)
		return;

	/* Check if ksm_zero_pages is updated correctly after KSM merging */
	pages_expected = size / pagesize;
	if (pages_expected != get_my_ksm_zero_pages()) {
		ksft_test_result_fail("'ksm_zero_pages' updated after merging\n");
		goto unmap;
	}

	/* Try to unmerge half of the region */
	if (madvise(map, size / 2, MADV_UNMERGEABLE)) {
		ksft_test_result_fail("MADV_UNMERGEABLE failed\n");
		goto unmap;
	}

	/* Check if ksm_zero_pages is updated correctly after unmerging */
	pages_expected /= 2;
	if (pages_expected != get_my_ksm_zero_pages()) {
		ksft_test_result_fail("'ksm_zero_pages' updated after unmerging\n");
		goto unmap;
	}

	/* Trigger unmerging of the other half by writing to the pages. */
	for (offs = size / 2; offs < size; offs += pagesize)
		*((unsigned int *)&map[offs]) = offs;

	/* Now we should have no zeropages remaining. */
	if (get_my_ksm_zero_pages()) {
		ksft_test_result_fail("'ksm_zero_pages' updated after write fault\n");
		goto unmap;
	}

	/* Check if ksm zero pages are really unmerged */
	ksft_test_result(!range_maps_duplicates(map, size),
			"KSM zero pages were unmerged\n");
unmap:
	munmap(map, size);
}

static void test_unmerge_discarded(void)
{
	const unsigned int size = 2 * MiB;
	char *map;

	ksft_print_msg("[RUN] %s\n", __func__);

	map = mmap_and_merge_range(0xcf, size, PROT_READ | PROT_WRITE, KSM_MERGE_MADVISE);
	if (map == MAP_FAILED)
		return;

	/* Discard half of all mapped pages so we have pte_none() entries. */
	if (madvise(map, size / 2, MADV_DONTNEED)) {
		ksft_test_result_fail("MADV_DONTNEED failed\n");
		goto unmap;
	}

	if (madvise(map, size, MADV_UNMERGEABLE)) {
		ksft_test_result_fail("MADV_UNMERGEABLE failed\n");
		goto unmap;
	}

	ksft_test_result(!range_maps_duplicates(map, size),
			 "Pages were unmerged\n");
unmap:
	munmap(map, size);
}

static void test_unmerge_uffd_wp(void)
{
	struct uffdio_writeprotect uffd_writeprotect;
	const unsigned int size = 2 * MiB;
	struct uffdio_api uffdio_api;
	char *map;
	int uffd;

	ksft_print_msg("[RUN] %s\n", __func__);

	map = mmap_and_merge_range(0xcf, size, PROT_READ | PROT_WRITE, KSM_MERGE_MADVISE);
	if (map == MAP_FAILED)
		return;

	/* See if UFFD is around. */
	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd < 0) {
		ksft_test_result_skip("__NR_userfaultfd failed\n");
		goto unmap;
	}

	/* See if UFFD-WP is around. */
	uffdio_api.api = UFFD_API;
	uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) < 0) {
		ksft_test_result_fail("UFFDIO_API failed\n");
		goto close_uffd;
	}
	if (!(uffdio_api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP)) {
		ksft_test_result_skip("UFFD_FEATURE_PAGEFAULT_FLAG_WP not available\n");
		goto close_uffd;
	}

	/* Register UFFD-WP, no need for an actual handler. */
	if (uffd_register(uffd, map, size, false, true, false)) {
		ksft_test_result_fail("UFFDIO_REGISTER_MODE_WP failed\n");
		goto close_uffd;
	}

	/* Write-protect the range using UFFD-WP. */
	uffd_writeprotect.range.start = (unsigned long) map;
	uffd_writeprotect.range.len = size;
	uffd_writeprotect.mode = UFFDIO_WRITEPROTECT_MODE_WP;
	if (ioctl(uffd, UFFDIO_WRITEPROTECT, &uffd_writeprotect)) {
		ksft_test_result_fail("UFFDIO_WRITEPROTECT failed\n");
		goto close_uffd;
	}

	if (madvise(map, size, MADV_UNMERGEABLE)) {
		ksft_test_result_fail("MADV_UNMERGEABLE failed\n");
		goto close_uffd;
	}

	ksft_test_result(!range_maps_duplicates(map, size),
			 "Pages were unmerged\n");
close_uffd:
	close(uffd);
unmap:
	munmap(map, size);
}

/* Verify that KSM can be enabled / queried with prctl. */
static void test_prctl(void)
{
	int ret;

	ksft_print_msg("[RUN] %s\n", __func__);

	ret = prctl(PR_SET_MEMORY_MERGE, 1, 0, 0, 0);
	if (ret < 0 && errno == EINVAL) {
		ksft_test_result_skip("PR_SET_MEMORY_MERGE not supported\n");
		return;
	} else if (ret) {
		ksft_test_result_fail("PR_SET_MEMORY_MERGE=1 failed\n");
		return;
	}

	ret = prctl(PR_GET_MEMORY_MERGE, 0, 0, 0, 0);
	if (ret < 0) {
		ksft_test_result_fail("PR_GET_MEMORY_MERGE failed\n");
		return;
	} else if (ret != 1) {
		ksft_test_result_fail("PR_SET_MEMORY_MERGE=1 not effective\n");
		return;
	}

	ret = prctl(PR_SET_MEMORY_MERGE, 0, 0, 0, 0);
	if (ret) {
		ksft_test_result_fail("PR_SET_MEMORY_MERGE=0 failed\n");
		return;
	}

	ret = prctl(PR_GET_MEMORY_MERGE, 0, 0, 0, 0);
	if (ret < 0) {
		ksft_test_result_fail("PR_GET_MEMORY_MERGE failed\n");
		return;
	} else if (ret != 0) {
		ksft_test_result_fail("PR_SET_MEMORY_MERGE=0 not effective\n");
		return;
	}

	ksft_test_result_pass("Setting/clearing PR_SET_MEMORY_MERGE works\n");
}

static int test_child_ksm(void)
{
	const unsigned int size = 2 * MiB;
	char *map;

	/* Test if KSM is enabled for the process. */
	if (prctl(PR_GET_MEMORY_MERGE, 0, 0, 0, 0) != 1)
		return -1;

	/* Test if merge could really happen. */
	map = __mmap_and_merge_range(0xcf, size, PROT_READ | PROT_WRITE, KSM_MERGE_NONE);
	if (map == MAP_MERGE_FAIL)
		return -2;
	else if (map == MAP_MERGE_SKIP)
		return -3;

	munmap(map, size);
	return 0;
}

static void test_child_ksm_err(int status)
{
	if (status == -1)
		ksft_test_result_fail("unexpected PR_GET_MEMORY_MERGE result in child\n");
	else if (status == -2)
		ksft_test_result_fail("Merge in child failed\n");
	else if (status == -3)
		ksft_test_result_skip("Merge in child skipped\n");
}

/* Verify that prctl ksm flag is inherited. */
static void test_prctl_fork(void)
{
	int ret, status;
	pid_t child_pid;

	ksft_print_msg("[RUN] %s\n", __func__);

	ret = prctl(PR_SET_MEMORY_MERGE, 1, 0, 0, 0);
	if (ret < 0 && errno == EINVAL) {
		ksft_test_result_skip("PR_SET_MEMORY_MERGE not supported\n");
		return;
	} else if (ret) {
		ksft_test_result_fail("PR_SET_MEMORY_MERGE=1 failed\n");
		return;
	}

	child_pid = fork();
	if (!child_pid) {
		exit(test_child_ksm());
	} else if (child_pid < 0) {
		ksft_test_result_fail("fork() failed\n");
		return;
	}

	if (waitpid(child_pid, &status, 0) < 0) {
		ksft_test_result_fail("waitpid() failed\n");
		return;
	}

	status = WEXITSTATUS(status);
	if (status) {
		test_child_ksm_err(status);
		return;
	}

	if (prctl(PR_SET_MEMORY_MERGE, 0, 0, 0, 0)) {
		ksft_test_result_fail("PR_SET_MEMORY_MERGE=0 failed\n");
		return;
	}

	ksft_test_result_pass("PR_SET_MEMORY_MERGE value is inherited\n");
}

static void test_prctl_fork_exec(void)
{
	int ret, status;
	pid_t child_pid;

	ksft_print_msg("[RUN] %s\n", __func__);

	ret = prctl(PR_SET_MEMORY_MERGE, 1, 0, 0, 0);
	if (ret < 0 && errno == EINVAL) {
		ksft_test_result_skip("PR_SET_MEMORY_MERGE not supported\n");
		return;
	} else if (ret) {
		ksft_test_result_fail("PR_SET_MEMORY_MERGE=1 failed\n");
		return;
	}

	child_pid = fork();
	if (child_pid == -1) {
		ksft_test_result_skip("fork() failed\n");
		return;
	} else if (child_pid == 0) {
		char *prg_name = "./ksm_functional_tests";
		char *argv_for_program[] = { prg_name, FORK_EXEC_CHILD_PRG_NAME };

		execv(prg_name, argv_for_program);
		return;
	}

	if (waitpid(child_pid, &status, 0) > 0) {
		if (WIFEXITED(status)) {
			status = WEXITSTATUS(status);
			if (status) {
				test_child_ksm_err(status);
				return;
			}
		} else {
			ksft_test_result_fail("program didn't terminate normally\n");
			return;
		}
	} else {
		ksft_test_result_fail("waitpid() failed\n");
		return;
	}

	if (prctl(PR_SET_MEMORY_MERGE, 0, 0, 0, 0)) {
		ksft_test_result_fail("PR_SET_MEMORY_MERGE=0 failed\n");
		return;
	}

	ksft_test_result_pass("PR_SET_MEMORY_MERGE value is inherited\n");
}

static void test_prctl_unmerge(void)
{
	const unsigned int size = 2 * MiB;
	char *map;

	ksft_print_msg("[RUN] %s\n", __func__);

	map = mmap_and_merge_range(0xcf, size, PROT_READ | PROT_WRITE, KSM_MERGE_PRCTL);
	if (map == MAP_FAILED)
		return;

	if (prctl(PR_SET_MEMORY_MERGE, 0, 0, 0, 0)) {
		ksft_test_result_fail("PR_SET_MEMORY_MERGE=0 failed\n");
		goto unmap;
	}

	ksft_test_result(!range_maps_duplicates(map, size),
			 "Pages were unmerged\n");
unmap:
	munmap(map, size);
}

static void test_prot_none(void)
{
	const unsigned int size = 2 * MiB;
	char *map;
	int i;

	ksft_print_msg("[RUN] %s\n", __func__);

	map = mmap_and_merge_range(0x11, size, PROT_NONE, KSM_MERGE_MADVISE);
	if (map == MAP_FAILED)
		goto unmap;

	/* Store a unique value in each page on one half using ptrace */
	for (i = 0; i < size / 2; i += pagesize) {
		lseek(mem_fd, (uintptr_t) map + i, SEEK_SET);
		if (write(mem_fd, &i, sizeof(i)) != sizeof(i)) {
			ksft_test_result_fail("ptrace write failed\n");
			goto unmap;
		}
	}

	/* Trigger unsharing on the other half. */
	if (madvise(map + size / 2, size / 2, MADV_UNMERGEABLE)) {
		ksft_test_result_fail("MADV_UNMERGEABLE failed\n");
		goto unmap;
	}

	ksft_test_result(!range_maps_duplicates(map, size),
			 "Pages were unmerged\n");
unmap:
	munmap(map, size);
}

static void init_global_file_handles(void)
{
	mem_fd = open("/proc/self/mem", O_RDWR);
	if (mem_fd < 0)
		ksft_exit_fail_msg("opening /proc/self/mem failed\n");
	ksm_fd = open("/sys/kernel/mm/ksm/run", O_RDWR);
	if (ksm_fd < 0)
		ksft_exit_skip("open(\"/sys/kernel/mm/ksm/run\") failed\n");
	ksm_full_scans_fd = open("/sys/kernel/mm/ksm/full_scans", O_RDONLY);
	if (ksm_full_scans_fd < 0)
		ksft_exit_skip("open(\"/sys/kernel/mm/ksm/full_scans\") failed\n");
	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		ksft_exit_skip("open(\"/proc/self/pagemap\") failed\n");
	proc_self_ksm_stat_fd = open("/proc/self/ksm_stat", O_RDONLY);
	proc_self_ksm_merging_pages_fd = open("/proc/self/ksm_merging_pages",
						O_RDONLY);
	ksm_use_zero_pages_fd = open("/sys/kernel/mm/ksm/use_zero_pages", O_RDWR);
}

int main(int argc, char **argv)
{
	unsigned int tests = 8;
	int err;

	if (argc > 1 && !strcmp(argv[1], FORK_EXEC_CHILD_PRG_NAME)) {
		init_global_file_handles();
		exit(test_child_ksm());
	}

	tests++;

	ksft_print_header();
	ksft_set_plan(tests);

	pagesize = getpagesize();

	init_global_file_handles();

	test_unmerge();
	test_unmerge_zero_pages();
	test_unmerge_discarded();
	test_unmerge_uffd_wp();

	test_prot_none();

	test_prctl();
	test_prctl_fork();
	test_prctl_fork_exec();
	test_prctl_unmerge();

	err = ksft_get_fail_cnt();
	if (err)
		ksft_exit_fail_msg("%d out of %d tests failed\n",
				   err, ksft_test_num());
	ksft_exit_pass();
}

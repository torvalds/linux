// SPDX-License-Identifier: GPL-2.0-only
/*
 * COW (Copy On Write) tests for anonymous memory.
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
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "../kselftest.h"
#include "vm_util.h"

static size_t pagesize;
static int pagemap_fd;
static size_t thpsize;

static void detect_thpsize(void)
{
	int fd = open("/sys/kernel/mm/transparent_hugepage/hpage_pmd_size",
		      O_RDONLY);
	size_t size = 0;
	char buf[15];
	int ret;

	if (fd < 0)
		return;

	ret = pread(fd, buf, sizeof(buf), 0);
	if (ret > 0 && ret < sizeof(buf)) {
		buf[ret] = 0;

		size = strtoul(buf, NULL, 10);
		if (size < pagesize)
			size = 0;
		if (size > 0) {
			thpsize = size;
			ksft_print_msg("[INFO] detected THP size: %zu KiB\n",
				       thpsize / 1024);
		}
	}

	close(fd);
}

static bool range_is_swapped(void *addr, size_t size)
{
	for (; size; addr += pagesize, size -= pagesize)
		if (!pagemap_is_swapped(pagemap_fd, addr))
			return false;
	return true;
}

struct comm_pipes {
	int child_ready[2];
	int parent_ready[2];
};

static int setup_comm_pipes(struct comm_pipes *comm_pipes)
{
	if (pipe(comm_pipes->child_ready) < 0)
		return -errno;
	if (pipe(comm_pipes->parent_ready) < 0) {
		close(comm_pipes->child_ready[0]);
		close(comm_pipes->child_ready[1]);
		return -errno;
	}

	return 0;
}

static void close_comm_pipes(struct comm_pipes *comm_pipes)
{
	close(comm_pipes->child_ready[0]);
	close(comm_pipes->child_ready[1]);
	close(comm_pipes->parent_ready[0]);
	close(comm_pipes->parent_ready[1]);
}

static int child_memcmp_fn(char *mem, size_t size,
			   struct comm_pipes *comm_pipes)
{
	char *old = malloc(size);
	char buf;

	/* Backup the original content. */
	memcpy(old, mem, size);

	/* Wait until the parent modified the page. */
	write(comm_pipes->child_ready[1], "0", 1);
	while (read(comm_pipes->parent_ready[0], &buf, 1) != 1)
		;

	/* See if we still read the old values. */
	return memcmp(old, mem, size);
}

static int child_vmsplice_memcmp_fn(char *mem, size_t size,
				    struct comm_pipes *comm_pipes)
{
	struct iovec iov = {
		.iov_base = mem,
		.iov_len = size,
	};
	ssize_t cur, total, transferred;
	char *old, *new;
	int fds[2];
	char buf;

	old = malloc(size);
	new = malloc(size);

	/* Backup the original content. */
	memcpy(old, mem, size);

	if (pipe(fds) < 0)
		return -errno;

	/* Trigger a read-only pin. */
	transferred = vmsplice(fds[1], &iov, 1, 0);
	if (transferred < 0)
		return -errno;
	if (transferred == 0)
		return -EINVAL;

	/* Unmap it from our page tables. */
	if (munmap(mem, size) < 0)
		return -errno;

	/* Wait until the parent modified it. */
	write(comm_pipes->child_ready[1], "0", 1);
	while (read(comm_pipes->parent_ready[0], &buf, 1) != 1)
		;

	/* See if we still read the old values via the pipe. */
	for (total = 0; total < transferred; total += cur) {
		cur = read(fds[0], new + total, transferred - total);
		if (cur < 0)
			return -errno;
	}

	return memcmp(old, new, transferred);
}

typedef int (*child_fn)(char *mem, size_t size, struct comm_pipes *comm_pipes);

static void do_test_cow_in_parent(char *mem, size_t size, child_fn fn)
{
	struct comm_pipes comm_pipes;
	char buf;
	int ret;

	ret = setup_comm_pipes(&comm_pipes);
	if (ret) {
		ksft_test_result_fail("pipe() failed\n");
		return;
	}

	ret = fork();
	if (ret < 0) {
		ksft_test_result_fail("fork() failed\n");
		goto close_comm_pipes;
	} else if (!ret) {
		exit(fn(mem, size, &comm_pipes));
	}

	while (read(comm_pipes.child_ready[0], &buf, 1) != 1)
		;
	/* Modify the page. */
	memset(mem, 0xff, size);
	write(comm_pipes.parent_ready[1], "0", 1);

	wait(&ret);
	if (WIFEXITED(ret))
		ret = WEXITSTATUS(ret);
	else
		ret = -EINVAL;

	ksft_test_result(!ret, "No leak from parent into child\n");
close_comm_pipes:
	close_comm_pipes(&comm_pipes);
}

static void test_cow_in_parent(char *mem, size_t size)
{
	do_test_cow_in_parent(mem, size, child_memcmp_fn);
}

static void test_vmsplice_in_child(char *mem, size_t size)
{
	do_test_cow_in_parent(mem, size, child_vmsplice_memcmp_fn);
}

static void do_test_vmsplice_in_parent(char *mem, size_t size,
				       bool before_fork)
{
	struct iovec iov = {
		.iov_base = mem,
		.iov_len = size,
	};
	ssize_t cur, total, transferred;
	struct comm_pipes comm_pipes;
	char *old, *new;
	int ret, fds[2];
	char buf;

	old = malloc(size);
	new = malloc(size);

	memcpy(old, mem, size);

	ret = setup_comm_pipes(&comm_pipes);
	if (ret) {
		ksft_test_result_fail("pipe() failed\n");
		goto free;
	}

	if (pipe(fds) < 0) {
		ksft_test_result_fail("pipe() failed\n");
		goto close_comm_pipes;
	}

	if (before_fork) {
		transferred = vmsplice(fds[1], &iov, 1, 0);
		if (transferred <= 0) {
			ksft_test_result_fail("vmsplice() failed\n");
			goto close_pipe;
		}
	}

	ret = fork();
	if (ret < 0) {
		ksft_test_result_fail("fork() failed\n");
		goto close_pipe;
	} else if (!ret) {
		write(comm_pipes.child_ready[1], "0", 1);
		while (read(comm_pipes.parent_ready[0], &buf, 1) != 1)
			;
		/* Modify page content in the child. */
		memset(mem, 0xff, size);
		exit(0);
	}

	if (!before_fork) {
		transferred = vmsplice(fds[1], &iov, 1, 0);
		if (transferred <= 0) {
			ksft_test_result_fail("vmsplice() failed\n");
			wait(&ret);
			goto close_pipe;
		}
	}

	while (read(comm_pipes.child_ready[0], &buf, 1) != 1)
		;
	if (munmap(mem, size) < 0) {
		ksft_test_result_fail("munmap() failed\n");
		goto close_pipe;
	}
	write(comm_pipes.parent_ready[1], "0", 1);

	/* Wait until the child is done writing. */
	wait(&ret);
	if (!WIFEXITED(ret)) {
		ksft_test_result_fail("wait() failed\n");
		goto close_pipe;
	}

	/* See if we still read the old values. */
	for (total = 0; total < transferred; total += cur) {
		cur = read(fds[0], new + total, transferred - total);
		if (cur < 0) {
			ksft_test_result_fail("read() failed\n");
			goto close_pipe;
		}
	}

	ksft_test_result(!memcmp(old, new, transferred),
			 "No leak from child into parent\n");
close_pipe:
	close(fds[0]);
	close(fds[1]);
close_comm_pipes:
	close_comm_pipes(&comm_pipes);
free:
	free(old);
	free(new);
}

static void test_vmsplice_before_fork(char *mem, size_t size)
{
	do_test_vmsplice_in_parent(mem, size, true);
}

static void test_vmsplice_after_fork(char *mem, size_t size)
{
	do_test_vmsplice_in_parent(mem, size, false);
}

typedef void (*test_fn)(char *mem, size_t size);

static void do_run_with_base_page(test_fn fn, bool swapout)
{
	char *mem;
	int ret;

	mem = mmap(NULL, pagesize, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return;
	}

	ret = madvise(mem, pagesize, MADV_NOHUGEPAGE);
	/* Ignore if not around on a kernel. */
	if (ret && errno != EINVAL) {
		ksft_test_result_fail("MADV_NOHUGEPAGE failed\n");
		goto munmap;
	}

	/* Populate a base page. */
	memset(mem, 0, pagesize);

	if (swapout) {
		madvise(mem, pagesize, MADV_PAGEOUT);
		if (!pagemap_is_swapped(pagemap_fd, mem)) {
			ksft_test_result_skip("MADV_PAGEOUT did not work, is swap enabled?\n");
			goto munmap;
		}
	}

	fn(mem, pagesize);
munmap:
	munmap(mem, pagesize);
}

static void run_with_base_page(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with base page\n", desc);
	do_run_with_base_page(fn, false);
}

static void run_with_base_page_swap(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with swapped out base page\n", desc);
	do_run_with_base_page(fn, true);
}

enum thp_run {
	THP_RUN_PMD,
	THP_RUN_PMD_SWAPOUT,
	THP_RUN_PTE,
	THP_RUN_PTE_SWAPOUT,
	THP_RUN_SINGLE_PTE,
	THP_RUN_SINGLE_PTE_SWAPOUT,
	THP_RUN_PARTIAL_MREMAP,
	THP_RUN_PARTIAL_SHARED,
};

static void do_run_with_thp(test_fn fn, enum thp_run thp_run)
{
	char *mem, *mmap_mem, *tmp, *mremap_mem = MAP_FAILED;
	size_t size, mmap_size, mremap_size;
	int ret;

	/* For alignment purposes, we need twice the thp size. */
	mmap_size = 2 * thpsize;
	mmap_mem = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mmap_mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return;
	}

	/* We need a THP-aligned memory area. */
	mem = (char *)(((uintptr_t)mmap_mem + thpsize) & ~(thpsize - 1));

	ret = madvise(mem, thpsize, MADV_HUGEPAGE);
	if (ret) {
		ksft_test_result_fail("MADV_HUGEPAGE failed\n");
		goto munmap;
	}

	/*
	 * Try to populate a THP. Touch the first sub-page and test if we get
	 * another sub-page populated automatically.
	 */
	mem[0] = 0;
	if (!pagemap_is_populated(pagemap_fd, mem + pagesize)) {
		ksft_test_result_skip("Did not get a THP populated\n");
		goto munmap;
	}
	memset(mem, 0, thpsize);

	size = thpsize;
	switch (thp_run) {
	case THP_RUN_PMD:
	case THP_RUN_PMD_SWAPOUT:
		break;
	case THP_RUN_PTE:
	case THP_RUN_PTE_SWAPOUT:
		/*
		 * Trigger PTE-mapping the THP by temporarily mapping a single
		 * subpage R/O.
		 */
		ret = mprotect(mem + pagesize, pagesize, PROT_READ);
		if (ret) {
			ksft_test_result_fail("mprotect() failed\n");
			goto munmap;
		}
		ret = mprotect(mem + pagesize, pagesize, PROT_READ | PROT_WRITE);
		if (ret) {
			ksft_test_result_fail("mprotect() failed\n");
			goto munmap;
		}
		break;
	case THP_RUN_SINGLE_PTE:
	case THP_RUN_SINGLE_PTE_SWAPOUT:
		/*
		 * Discard all but a single subpage of that PTE-mapped THP. What
		 * remains is a single PTE mapping a single subpage.
		 */
		ret = madvise(mem + pagesize, thpsize - pagesize, MADV_DONTNEED);
		if (ret) {
			ksft_test_result_fail("MADV_DONTNEED failed\n");
			goto munmap;
		}
		size = pagesize;
		break;
	case THP_RUN_PARTIAL_MREMAP:
		/*
		 * Remap half of the THP. We need some new memory location
		 * for that.
		 */
		mremap_size = thpsize / 2;
		mremap_mem = mmap(NULL, mremap_size, PROT_NONE,
				  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mem == MAP_FAILED) {
			ksft_test_result_fail("mmap() failed\n");
			goto munmap;
		}
		tmp = mremap(mem + mremap_size, mremap_size, mremap_size,
			     MREMAP_MAYMOVE | MREMAP_FIXED, mremap_mem);
		if (tmp != mremap_mem) {
			ksft_test_result_fail("mremap() failed\n");
			goto munmap;
		}
		size = mremap_size;
		break;
	case THP_RUN_PARTIAL_SHARED:
		/*
		 * Share the first page of the THP with a child and quit the
		 * child. This will result in some parts of the THP never
		 * have been shared.
		 */
		ret = madvise(mem + pagesize, thpsize - pagesize, MADV_DONTFORK);
		if (ret) {
			ksft_test_result_fail("MADV_DONTFORK failed\n");
			goto munmap;
		}
		ret = fork();
		if (ret < 0) {
			ksft_test_result_fail("fork() failed\n");
			goto munmap;
		} else if (!ret) {
			exit(0);
		}
		wait(&ret);
		/* Allow for sharing all pages again. */
		ret = madvise(mem + pagesize, thpsize - pagesize, MADV_DOFORK);
		if (ret) {
			ksft_test_result_fail("MADV_DOFORK failed\n");
			goto munmap;
		}
		break;
	default:
		assert(false);
	}

	switch (thp_run) {
	case THP_RUN_PMD_SWAPOUT:
	case THP_RUN_PTE_SWAPOUT:
	case THP_RUN_SINGLE_PTE_SWAPOUT:
		madvise(mem, size, MADV_PAGEOUT);
		if (!range_is_swapped(mem, size)) {
			ksft_test_result_skip("MADV_PAGEOUT did not work, is swap enabled?\n");
			goto munmap;
		}
		break;
	default:
		break;
	}

	fn(mem, size);
munmap:
	munmap(mmap_mem, mmap_size);
	if (mremap_mem != MAP_FAILED)
		munmap(mremap_mem, mremap_size);
}

static void run_with_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PMD);
}

static void run_with_thp_swap(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with swapped-out THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PMD_SWAPOUT);
}

static void run_with_pte_mapped_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with PTE-mapped THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PTE);
}

static void run_with_pte_mapped_thp_swap(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with swapped-out, PTE-mapped THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PTE_SWAPOUT);
}

static void run_with_single_pte_of_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with single PTE of THP\n", desc);
	do_run_with_thp(fn, THP_RUN_SINGLE_PTE);
}

static void run_with_single_pte_of_thp_swap(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with single PTE of swapped-out THP\n", desc);
	do_run_with_thp(fn, THP_RUN_SINGLE_PTE_SWAPOUT);
}

static void run_with_partial_mremap_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with partially mremap()'ed THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PARTIAL_MREMAP);
}

static void run_with_partial_shared_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with partially shared THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PARTIAL_SHARED);
}

struct test_case {
	const char *desc;
	test_fn fn;
};

static const struct test_case test_cases[] = {
	/*
	 * Basic COW tests for fork() without any GUP. If we miss to break COW,
	 * either the child can observe modifications by the parent or the
	 * other way around.
	 */
	{
		"Basic COW after fork()",
		test_cow_in_parent,
	},
	/*
	 * vmsplice() [R/O GUP] + unmap in the child; modify in the parent. If
	 * we miss to break COW, the child observes modifications by the parent.
	 * This is CVE-2020-29374 reported by Jann Horn.
	 */
	{
		"vmsplice() + unmap in child",
		test_vmsplice_in_child
	},
	/*
	 * vmsplice() [R/O GUP] in parent before fork(), unmap in parent after
	 * fork(); modify in the child. If we miss to break COW, the parent
	 * observes modifications by the child.
	 */
	{
		"vmsplice() before fork(), unmap in parent after fork()",
		test_vmsplice_before_fork,
	},
	/*
	 * vmsplice() [R/O GUP] + unmap in parent after fork(); modify in the
	 * child. If we miss to break COW, the parent observes modifications by
	 * the child.
	 */
	{
		"vmsplice() + unmap in parent after fork()",
		test_vmsplice_after_fork,
	},
};

static void run_test_case(struct test_case const *test_case)
{
	run_with_base_page(test_case->fn, test_case->desc);
	run_with_base_page_swap(test_case->fn, test_case->desc);
	if (thpsize) {
		run_with_thp(test_case->fn, test_case->desc);
		run_with_thp_swap(test_case->fn, test_case->desc);
		run_with_pte_mapped_thp(test_case->fn, test_case->desc);
		run_with_pte_mapped_thp_swap(test_case->fn, test_case->desc);
		run_with_single_pte_of_thp(test_case->fn, test_case->desc);
		run_with_single_pte_of_thp_swap(test_case->fn, test_case->desc);
		run_with_partial_mremap_thp(test_case->fn, test_case->desc);
		run_with_partial_shared_thp(test_case->fn, test_case->desc);
	}
}

static void run_test_cases(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++)
		run_test_case(&test_cases[i]);
}

static int tests_per_test_case(void)
{
	int tests = 2;

	if (thpsize)
		tests += 8;
	return tests;
}

int main(int argc, char **argv)
{
	int nr_test_cases = ARRAY_SIZE(test_cases);
	int err;

	pagesize = getpagesize();
	detect_thpsize();

	ksft_print_header();
	ksft_set_plan(nr_test_cases * tests_per_test_case());

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		ksft_exit_fail_msg("opening pagemap failed\n");

	run_test_cases();

	err = ksft_get_fail_cnt();
	if (err)
		ksft_exit_fail_msg("%d out of %d tests failed\n",
				   err, ksft_test_num());
	return ksft_exit_pass();
}

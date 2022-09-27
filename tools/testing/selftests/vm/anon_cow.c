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
}

static void run_test_cases(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++)
		run_test_case(&test_cases[i]);
}

int main(int argc, char **argv)
{
	int nr_test_cases = ARRAY_SIZE(test_cases);
	int err;

	pagesize = getpagesize();

	ksft_print_header();
	ksft_set_plan(nr_test_cases * 2);

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

// SPDX-License-Identifier: GPL-2.0-or-later

#define _GNU_SOURCE
#include "../kselftest_harness.h"
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include "vm_util.h"

#include "../pidfd/pidfd.h"

FIXTURE(process_madvise)
{
	unsigned long page_size;
	pid_t child_pid;
	int remote_pidfd;
	int pidfd;
};

FIXTURE_SETUP(process_madvise)
{
	self->page_size = (unsigned long)sysconf(_SC_PAGESIZE);
	self->pidfd = PIDFD_SELF;
	self->remote_pidfd = -1;
	self->child_pid = -1;
};

FIXTURE_TEARDOWN_PARENT(process_madvise)
{
	/* This teardown is guaranteed to run, even if tests SKIP or ASSERT */
	if (self->child_pid > 0) {
		kill(self->child_pid, SIGKILL);
		waitpid(self->child_pid, NULL, 0);
	}

	if (self->remote_pidfd >= 0)
		close(self->remote_pidfd);
}

static ssize_t sys_process_madvise(int pidfd, const struct iovec *iovec,
				   size_t vlen, int advice, unsigned int flags)
{
	return syscall(__NR_process_madvise, pidfd, iovec, vlen, advice, flags);
}

/*
 * This test uses PIDFD_SELF to target the current process. The main
 * goal is to verify the basic behavior of process_madvise() with
 * a vector of non-contiguous memory ranges, not its cross-process
 * capabilities.
 */
TEST_F(process_madvise, basic)
{
	const unsigned long pagesize = self->page_size;
	const int madvise_pages = 4;
	struct iovec vec[madvise_pages];
	int pidfd = self->pidfd;
	ssize_t ret;
	char *map;

	/*
	 * Create a single large mapping. We will pick pages from this
	 * mapping to advise on. This ensures we test non-contiguous iovecs.
	 */
	map = mmap(NULL, pagesize * 10, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED)
		SKIP(return, "mmap failed, not enough memory.\n");

	/* Fill the entire region with a known pattern. */
	memset(map, 'A', pagesize * 10);

	/*
	 * Setup the iovec to point to 4 non-contiguous pages
	 * within the mapping.
	 */
	vec[0].iov_base = &map[0 * pagesize];
	vec[0].iov_len = pagesize;
	vec[1].iov_base = &map[3 * pagesize];
	vec[1].iov_len = pagesize;
	vec[2].iov_base = &map[5 * pagesize];
	vec[2].iov_len = pagesize;
	vec[3].iov_base = &map[8 * pagesize];
	vec[3].iov_len = pagesize;

	ret = sys_process_madvise(pidfd, vec, madvise_pages, MADV_DONTNEED, 0);
	if (ret == -1 && errno == EPERM)
		SKIP(return,
			   "process_madvise() unsupported or permission denied, try running as root.\n");
	else if (errno == EINVAL)
		SKIP(return,
			   "process_madvise() unsupported or parameter invalid, please check arguments.\n");

	/* The call should succeed and report the total bytes processed. */
	ASSERT_EQ(ret, madvise_pages * pagesize);

	/* Check that advised pages are now zero. */
	for (int i = 0; i < madvise_pages; i++) {
		char *advised_page = (char *)vec[i].iov_base;

		/* Content must be 0, not 'A'. */
		ASSERT_EQ(*advised_page, '\0');
	}

	/* Check that an un-advised page in between is still 'A'. */
	char *unadvised_page = &map[1 * pagesize];

	for (int i = 0; i < pagesize; i++)
		ASSERT_EQ(unadvised_page[i], 'A');

	/* Cleanup. */
	ASSERT_EQ(munmap(map, pagesize * 10), 0);
}

/*
 * This test deterministically validates process_madvise() with MADV_COLLAPSE
 * on a remote process, other advices are difficult to verify reliably.
 *
 * The test verifies that a memory region in a child process,
 * focus on process_madv remote result, only check addresses and lengths.
 * The correctness of the MADV_COLLAPSE can be found in the relevant test examples in khugepaged.
 */
TEST_F(process_madvise, remote_collapse)
{
	const unsigned long pagesize = self->page_size;
	long huge_page_size;
	int pipe_info[2];
	ssize_t ret;
	struct iovec vec;

	struct child_info {
		pid_t pid;
		void *map_addr;
	} info;

	huge_page_size = read_pmd_pagesize();
	if (huge_page_size <= 0)
		SKIP(return, "Could not determine a valid huge page size.\n");

	ASSERT_EQ(pipe(pipe_info), 0);

	self->child_pid = fork();
	ASSERT_NE(self->child_pid, -1);

	if (self->child_pid == 0) {
		char *map;
		size_t map_size = 2 * huge_page_size;

		close(pipe_info[0]);

		map = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		ASSERT_NE(map, MAP_FAILED);

		/* Fault in as small pages */
		for (size_t i = 0; i < map_size; i += pagesize)
			map[i] = 'A';

		/* Send info and pause */
		info.pid = getpid();
		info.map_addr = map;
		ret = write(pipe_info[1], &info, sizeof(info));
		ASSERT_EQ(ret, sizeof(info));
		close(pipe_info[1]);

		pause();
		exit(0);
	}

	close(pipe_info[1]);

	/* Receive child info */
	ret = read(pipe_info[0], &info, sizeof(info));
	if (ret <= 0) {
		waitpid(self->child_pid, NULL, 0);
		SKIP(return, "Failed to read child info from pipe.\n");
	}
	ASSERT_EQ(ret, sizeof(info));
	close(pipe_info[0]);
	self->child_pid = info.pid;

	self->remote_pidfd = syscall(__NR_pidfd_open, self->child_pid, 0);
	ASSERT_GE(self->remote_pidfd, 0);

	vec.iov_base = info.map_addr;
	vec.iov_len = huge_page_size;

	ret = sys_process_madvise(self->remote_pidfd, &vec, 1, MADV_COLLAPSE,
				  0);
	if (ret == -1) {
		if (errno == EINVAL)
			SKIP(return, "PROCESS_MADV_ADVISE is not supported.\n");
		else if (errno == EPERM)
			SKIP(return,
				   "No process_madvise() permissions, try running as root.\n");
		return;
	}

	ASSERT_EQ(ret, huge_page_size);
}

/*
 * Test process_madvise() with a pidfd for a process that has already
 * exited to ensure correct error handling.
 */
TEST_F(process_madvise, exited_process_pidfd)
{
	const unsigned long pagesize = self->page_size;
	struct iovec vec;
	char *map;
	ssize_t ret;

	map = mmap(NULL, pagesize, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		   0);
	if (map == MAP_FAILED)
		SKIP(return, "mmap failed, not enough memory.\n");

	vec.iov_base = map;
	vec.iov_len = pagesize;

	/*
	 * Using a pidfd for a process that has already exited should fail
	 * with ESRCH.
	 */
	self->child_pid = fork();
	ASSERT_NE(self->child_pid, -1);

	if (self->child_pid == 0)
		exit(0);

	self->remote_pidfd = syscall(__NR_pidfd_open, self->child_pid, 0);
	ASSERT_GE(self->remote_pidfd, 0);

	/* Wait for the child to ensure it has terminated. */
	waitpid(self->child_pid, NULL, 0);

	ret = sys_process_madvise(self->remote_pidfd, &vec, 1, MADV_DONTNEED,
				  0);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ESRCH);
}

/*
 * Test process_madvise() with bad pidfds to ensure correct error
 * handling.
 */
TEST_F(process_madvise, bad_pidfd)
{
	const unsigned long pagesize = self->page_size;
	struct iovec vec;
	char *map;
	ssize_t ret;

	map = mmap(NULL, pagesize, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		   0);
	if (map == MAP_FAILED)
		SKIP(return, "mmap failed, not enough memory.\n");

	vec.iov_base = map;
	vec.iov_len = pagesize;

	/* Using an invalid fd number (-1) should fail with EBADF. */
	ret = sys_process_madvise(-1, &vec, 1, MADV_DONTNEED, 0);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EBADF);

	/*
	 * Using a valid fd that is not a pidfd (e.g. stdin) should fail
	 * with EBADF.
	 */
	ret = sys_process_madvise(STDIN_FILENO, &vec, 1, MADV_DONTNEED, 0);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EBADF);
}

/*
 * Test that process_madvise() rejects vlen > UIO_MAXIOV.
 * The kernel should return -EINVAL when the number of iovecs exceeds 1024.
 */
TEST_F(process_madvise, invalid_vlen)
{
	const unsigned long pagesize = self->page_size;
	int pidfd = self->pidfd;
	struct iovec vec;
	char *map;
	ssize_t ret;

	map = mmap(NULL, pagesize, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		   0);
	if (map == MAP_FAILED)
		SKIP(return, "mmap failed, not enough memory.\n");

	vec.iov_base = map;
	vec.iov_len = pagesize;

	ret = sys_process_madvise(pidfd, &vec, 1025, MADV_DONTNEED, 0);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	/* Cleanup. */
	ASSERT_EQ(munmap(map, pagesize), 0);
}

/*
 * Test process_madvise() with an invalid flag value. Currently, only a flag
 * value of 0 is supported. This test is reserved for the future, e.g., if
 * synchronous flags are added.
 */
TEST_F(process_madvise, flag)
{
	const unsigned long pagesize = self->page_size;
	unsigned int invalid_flag;
	int pidfd = self->pidfd;
	struct iovec vec;
	char *map;
	ssize_t ret;

	map = mmap(NULL, pagesize, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		   0);
	if (map == MAP_FAILED)
		SKIP(return, "mmap failed, not enough memory.\n");

	vec.iov_base = map;
	vec.iov_len = pagesize;

	invalid_flag = 0x80000000;

	ret = sys_process_madvise(pidfd, &vec, 1, MADV_DONTNEED, invalid_flag);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	/* Cleanup. */
	ASSERT_EQ(munmap(map, pagesize), 0);
}

TEST_HARNESS_MAIN

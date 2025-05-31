// SPDX-License-Identifier: GPL-2.0-only
/*
 * Basic VM_PFNMAP tests relying on mmap() of '/dev/mem'
 *
 * Copyright 2025, Red Hat, Inc.
 *
 * Author(s): David Hildenbrand <david@redhat.com>
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <linux/mman.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "../kselftest_harness.h"
#include "vm_util.h"

static sigjmp_buf sigjmp_buf_env;

static void signal_handler(int sig)
{
	siglongjmp(sigjmp_buf_env, -EFAULT);
}

static int test_read_access(char *addr, size_t size, size_t pagesize)
{
	size_t offs;
	int ret;

	if (signal(SIGSEGV, signal_handler) == SIG_ERR)
		return -EINVAL;

	ret = sigsetjmp(sigjmp_buf_env, 1);
	if (!ret) {
		for (offs = 0; offs < size; offs += pagesize)
			/* Force a read that the compiler cannot optimize out. */
			*((volatile char *)(addr + offs));
	}
	if (signal(SIGSEGV, signal_handler) == SIG_ERR)
		return -EINVAL;

	return ret;
}

FIXTURE(pfnmap)
{
	size_t pagesize;
	int dev_mem_fd;
	char *addr1;
	size_t size1;
	char *addr2;
	size_t size2;
};

FIXTURE_SETUP(pfnmap)
{
	self->pagesize = getpagesize();

	self->dev_mem_fd = open("/dev/mem", O_RDONLY);
	if (self->dev_mem_fd < 0)
		SKIP(return, "Cannot open '/dev/mem'\n");

	/* We'll require the first two pages throughout our tests ... */
	self->size1 = self->pagesize * 2;
	self->addr1 = mmap(NULL, self->size1, PROT_READ, MAP_SHARED,
			   self->dev_mem_fd, 0);
	if (self->addr1 == MAP_FAILED)
		SKIP(return, "Cannot mmap '/dev/mem'\n");

	/* ... and want to be able to read from them. */
	if (test_read_access(self->addr1, self->size1, self->pagesize))
		SKIP(return, "Cannot read-access mmap'ed '/dev/mem'\n");

	self->size2 = 0;
	self->addr2 = MAP_FAILED;
}

FIXTURE_TEARDOWN(pfnmap)
{
	if (self->addr2 != MAP_FAILED)
		munmap(self->addr2, self->size2);
	if (self->addr1 != MAP_FAILED)
		munmap(self->addr1, self->size1);
	if (self->dev_mem_fd >= 0)
		close(self->dev_mem_fd);
}

TEST_F(pfnmap, madvise_disallowed)
{
	int advices[] = {
		MADV_DONTNEED,
		MADV_DONTNEED_LOCKED,
		MADV_FREE,
		MADV_WIPEONFORK,
		MADV_COLD,
		MADV_PAGEOUT,
		MADV_POPULATE_READ,
		MADV_POPULATE_WRITE,
	};
	int i;

	/* All these advices must be rejected. */
	for (i = 0; i < ARRAY_SIZE(advices); i++) {
		EXPECT_LT(madvise(self->addr1, self->pagesize, advices[i]), 0);
		EXPECT_EQ(errno, EINVAL);
	}
}

TEST_F(pfnmap, munmap_split)
{
	/*
	 * Unmap the first page. This munmap() call is not really expected to
	 * fail, but we might be able to trigger other internal issues.
	 */
	ASSERT_EQ(munmap(self->addr1, self->pagesize), 0);

	/*
	 * Remap the first page while the second page is still mapped. This
	 * makes sure that any PAT tracking on x86 will allow for mmap()'ing
	 * a page again while some parts of the first mmap() are still
	 * around.
	 */
	self->size2 = self->pagesize;
	self->addr2 = mmap(NULL, self->pagesize, PROT_READ, MAP_SHARED,
			   self->dev_mem_fd, 0);
	ASSERT_NE(self->addr2, MAP_FAILED);
}

TEST_F(pfnmap, mremap_fixed)
{
	char *ret;

	/* Reserve a destination area. */
	self->size2 = self->size1;
	self->addr2 = mmap(NULL, self->size2, PROT_READ, MAP_ANON | MAP_PRIVATE,
			   -1, 0);
	ASSERT_NE(self->addr2, MAP_FAILED);

	/* mremap() over our destination. */
	ret = mremap(self->addr1, self->size1, self->size2,
		     MREMAP_FIXED | MREMAP_MAYMOVE, self->addr2);
	ASSERT_NE(ret, MAP_FAILED);
}

TEST_F(pfnmap, mremap_shrink)
{
	char *ret;

	/* Shrinking is expected to work. */
	ret = mremap(self->addr1, self->size1, self->size1 - self->pagesize, 0);
	ASSERT_NE(ret, MAP_FAILED);
}

TEST_F(pfnmap, mremap_expand)
{
	/*
	 * Growing is not expected to work, and getting it right would
	 * be challenging. So this test primarily serves as an early warning
	 * that something that probably should never work suddenly works.
	 */
	self->size2 = self->size1 + self->pagesize;
	self->addr2 = mremap(self->addr1, self->size1, self->size2, MREMAP_MAYMOVE);
	ASSERT_EQ(self->addr2, MAP_FAILED);
}

TEST_F(pfnmap, fork)
{
	pid_t pid;
	int ret;

	/* fork() a child and test if the child can access the pages. */
	pid = fork();
	ASSERT_GE(pid, 0);

	if (!pid) {
		EXPECT_EQ(test_read_access(self->addr1, self->size1,
					   self->pagesize), 0);
		exit(0);
	}

	wait(&ret);
	if (WIFEXITED(ret))
		ret = WEXITSTATUS(ret);
	else
		ret = -EINVAL;
	ASSERT_EQ(ret, 0);
}

TEST_HARNESS_MAIN

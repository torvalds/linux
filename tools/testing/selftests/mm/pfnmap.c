// SPDX-License-Identifier: GPL-2.0-only
/*
 * Basic VM_PFNMAP tests relying on mmap() of input file provided.
 * Use '/dev/mem' as default.
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
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <linux/mman.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_harness.h"
#include "vm_util.h"

#define DEV_MEM_NPAGES	2

static sigjmp_buf sigjmp_buf_env;
static char *file = "/dev/mem";
static off_t file_offset;
static int fd;

static void signal_handler(int sig)
{
	siglongjmp(sigjmp_buf_env, -EFAULT);
}

static int test_read_access(char *addr, size_t size, size_t pagesize)
{
	int ret;

	if (signal(SIGSEGV, signal_handler) == SIG_ERR)
		return -EINVAL;

	ret = sigsetjmp(sigjmp_buf_env, 1);
	if (!ret)
		force_read_pages(addr, size/pagesize, pagesize);

	if (signal(SIGSEGV, SIG_DFL) == SIG_ERR)
		return -EINVAL;

	return ret;
}

static int find_ram_target(off_t *offset,
		unsigned long long pagesize)
{
	unsigned long long start, end;
	char line[80], *end_ptr;
	FILE *file;

	/* Search /proc/iomem for the first suitable "System RAM" range. */
	file = fopen("/proc/iomem", "r");
	if (!file)
		return -errno;

	while (fgets(line, sizeof(line), file)) {
		/* Ignore any child nodes. */
		if (!isalnum(line[0]))
			continue;

		if (!strstr(line, "System RAM\n"))
			continue;

		start = strtoull(line, &end_ptr, 16);
		/* Skip over the "-" */
		end_ptr++;
		/* Make end "exclusive". */
		end = strtoull(end_ptr, NULL, 16) + 1;

		/* Actual addresses are not exported */
		if (!start && !end)
			break;

		/* We need full pages. */
		start = (start + pagesize - 1) & ~(pagesize - 1);
		end &= ~(pagesize - 1);

		if (start != (off_t)start)
			break;

		/* We need two pages. */
		if (end > start + DEV_MEM_NPAGES * pagesize) {
			fclose(file);
			*offset = start;
			return 0;
		}
	}
	return -ENOENT;
}

static void pfnmap_init(void)
{
	size_t pagesize = getpagesize();
	size_t size = DEV_MEM_NPAGES * pagesize;
	void *addr;

	if (strncmp(file, "/dev/mem", strlen("/dev/mem")) == 0) {
		int err = find_ram_target(&file_offset, pagesize);

		if (err)
			ksft_exit_skip("Cannot find ram target in '/proc/iomem': %s\n",
				       strerror(-err));
	} else {
		file_offset = 0;
	}

	fd = open(file, O_RDONLY);
	if (fd < 0)
		ksft_exit_skip("Cannot open '%s': %s\n", file, strerror(errno));

	/*
	 * Make sure we can map the file, and perform some basic checks; skip
	 * the whole suite if anything goes wrong.
	 * A fresh mapping is then created for every test case by
	 * FIXTURE_SETUP(pfnmap).
	 */
	addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, file_offset);
	if (addr == MAP_FAILED)
		ksft_exit_skip("Cannot mmap '%s': %s\n", file, strerror(errno));

	if (!check_vmflag_pfnmap(addr))
		ksft_exit_skip("Invalid file: '%s'. Not pfnmap'ed\n", file);

	if (test_read_access(addr, size, pagesize))
		ksft_exit_skip("Cannot read-access mmap'ed '%s'\n", file);

	munmap(addr, size);
}

FIXTURE(pfnmap)
{
	size_t pagesize;
	char *addr1;
	size_t size1;
	char *addr2;
	size_t size2;
};

FIXTURE_SETUP(pfnmap)
{
	self->pagesize = getpagesize();

	self->size1 = DEV_MEM_NPAGES * self->pagesize;
	self->addr1 = mmap(NULL, self->size1, PROT_READ, MAP_SHARED,
			   fd, file_offset);
	ASSERT_NE(self->addr1, MAP_FAILED);

	self->size2 = 0;
	self->addr2 = MAP_FAILED;
}

FIXTURE_TEARDOWN(pfnmap)
{
	if (self->addr2 != MAP_FAILED)
		munmap(self->addr2, self->size2);
	if (self->addr1 != MAP_FAILED)
		munmap(self->addr1, self->size1);
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
			   fd, file_offset);
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

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0) {
			if (i + 1 < argc && strlen(argv[i + 1]) > 0)
				file = argv[i + 1];
			argc = i;
			break;
		}
	}

	pfnmap_init();

	return test_harness_run(argc, argv);
}

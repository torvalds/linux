// SPDX-License-Identifier: GPL-2.0-or-later

#define _GNU_SOURCE
#include "../kselftest_harness.h"
#include <asm-generic/mman.h> /* Force the import of the tools version. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <linux/userfaultfd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#include "vm_util.h"

#include "../pidfd/pidfd.h"

/*
 * Ignore the checkpatch warning, as per the C99 standard, section 7.14.1.1:
 *
 * "If the signal occurs other than as the result of calling the abort or raise
 *  function, the behavior is undefined if the signal handler refers to any
 *  object with static storage duration other than by assigning a value to an
 *  object declared as volatile sig_atomic_t"
 */
static volatile sig_atomic_t signal_jump_set;
static sigjmp_buf signal_jmp_buf;

/*
 * Ignore the checkpatch warning, we must read from x but don't want to do
 * anything with it in order to trigger a read page fault. We therefore must use
 * volatile to stop the compiler from optimising this away.
 */
#define FORCE_READ(x) (*(volatile typeof(x) *)x)

/*
 * How is the test backing the mapping being tested?
 */
enum backing_type {
	ANON_BACKED,
	SHMEM_BACKED,
	LOCAL_FILE_BACKED,
};

FIXTURE(guard_regions)
{
	unsigned long page_size;
	char path[PATH_MAX];
	int fd;
};

FIXTURE_VARIANT(guard_regions)
{
	enum backing_type backing;
};

FIXTURE_VARIANT_ADD(guard_regions, anon)
{
	.backing = ANON_BACKED,
};

FIXTURE_VARIANT_ADD(guard_regions, shmem)
{
	.backing = SHMEM_BACKED,
};

FIXTURE_VARIANT_ADD(guard_regions, file)
{
	.backing = LOCAL_FILE_BACKED,
};

static bool is_anon_backed(const FIXTURE_VARIANT(guard_regions) * variant)
{
	switch (variant->backing) {
	case  ANON_BACKED:
	case  SHMEM_BACKED:
		return true;
	default:
		return false;
	}
}

static void *mmap_(FIXTURE_DATA(guard_regions) * self,
		   const FIXTURE_VARIANT(guard_regions) * variant,
		   void *addr, size_t length, int prot, int extra_flags,
		   off_t offset)
{
	int fd;
	int flags = extra_flags;

	switch (variant->backing) {
	case ANON_BACKED:
		flags |= MAP_PRIVATE | MAP_ANON;
		fd = -1;
		break;
	case SHMEM_BACKED:
	case LOCAL_FILE_BACKED:
		flags |= MAP_SHARED;
		fd = self->fd;
		break;
	default:
		ksft_exit_fail();
		break;
	}

	return mmap(addr, length, prot, flags, fd, offset);
}

static int userfaultfd(int flags)
{
	return syscall(SYS_userfaultfd, flags);
}

static void handle_fatal(int c)
{
	if (!signal_jump_set)
		return;

	siglongjmp(signal_jmp_buf, c);
}

static ssize_t sys_process_madvise(int pidfd, const struct iovec *iovec,
				   size_t n, int advice, unsigned int flags)
{
	return syscall(__NR_process_madvise, pidfd, iovec, n, advice, flags);
}

/*
 * Enable our signal catcher and try to read/write the specified buffer. The
 * return value indicates whether the read/write succeeds without a fatal
 * signal.
 */
static bool try_access_buf(char *ptr, bool write)
{
	bool failed;

	/* Tell signal handler to jump back here on fatal signal. */
	signal_jump_set = true;
	/* If a fatal signal arose, we will jump back here and failed is set. */
	failed = sigsetjmp(signal_jmp_buf, 0) != 0;

	if (!failed) {
		if (write)
			*ptr = 'x';
		else
			FORCE_READ(ptr);
	}

	signal_jump_set = false;
	return !failed;
}

/* Try and read from a buffer, return true if no fatal signal. */
static bool try_read_buf(char *ptr)
{
	return try_access_buf(ptr, false);
}

/* Try and write to a buffer, return true if no fatal signal. */
static bool try_write_buf(char *ptr)
{
	return try_access_buf(ptr, true);
}

/*
 * Try and BOTH read from AND write to a buffer, return true if BOTH operations
 * succeed.
 */
static bool try_read_write_buf(char *ptr)
{
	return try_read_buf(ptr) && try_write_buf(ptr);
}

static void setup_sighandler(void)
{
	struct sigaction act = {
		.sa_handler = &handle_fatal,
		.sa_flags = SA_NODEFER,
	};

	sigemptyset(&act.sa_mask);
	if (sigaction(SIGSEGV, &act, NULL))
		ksft_exit_fail_perror("sigaction");
}

static void teardown_sighandler(void)
{
	struct sigaction act = {
		.sa_handler = SIG_DFL,
		.sa_flags = SA_NODEFER,
	};

	sigemptyset(&act.sa_mask);
	sigaction(SIGSEGV, &act, NULL);
}

static int open_file(const char *prefix, char *path)
{
	int fd;

	snprintf(path, PATH_MAX, "%sguard_regions_test_file_XXXXXX", prefix);
	fd = mkstemp(path);
	if (fd < 0)
		ksft_exit_fail_perror("mkstemp");

	return fd;
}

/* Establish a varying pattern in a buffer. */
static void set_pattern(char *ptr, size_t num_pages, size_t page_size)
{
	size_t i;

	for (i = 0; i < num_pages; i++) {
		char *ptr2 = &ptr[i * page_size];

		memset(ptr2, 'a' + (i % 26), page_size);
	}
}

/*
 * Check that a buffer contains the pattern set by set_pattern(), starting at a
 * page offset of pgoff within the buffer.
 */
static bool check_pattern_offset(char *ptr, size_t num_pages, size_t page_size,
				 size_t pgoff)
{
	size_t i;

	for (i = 0; i < num_pages * page_size; i++) {
		size_t offset = pgoff * page_size + i;
		char actual = ptr[offset];
		char expected = 'a' + ((offset / page_size) % 26);

		if (actual != expected)
			return false;
	}

	return true;
}

/* Check that a buffer contains the pattern set by set_pattern(). */
static bool check_pattern(char *ptr, size_t num_pages, size_t page_size)
{
	return check_pattern_offset(ptr, num_pages, page_size, 0);
}

/* Determine if a buffer contains only repetitions of a specified char. */
static bool is_buf_eq(char *buf, size_t size, char chr)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (buf[i] != chr)
			return false;
	}

	return true;
}

FIXTURE_SETUP(guard_regions)
{
	self->page_size = (unsigned long)sysconf(_SC_PAGESIZE);
	setup_sighandler();

	if (variant->backing == ANON_BACKED)
		return;

	self->fd = open_file(
		variant->backing == SHMEM_BACKED ? "/tmp/" : "",
		self->path);

	/* We truncate file to at least 100 pages, tests can modify as needed. */
	ASSERT_EQ(ftruncate(self->fd, 100 * self->page_size), 0);
};

FIXTURE_TEARDOWN_PARENT(guard_regions)
{
	teardown_sighandler();

	if (variant->backing == ANON_BACKED)
		return;

	if (self->fd >= 0)
		close(self->fd);

	if (self->path[0] != '\0')
		unlink(self->path);
}

TEST_F(guard_regions, basic)
{
	const unsigned long NUM_PAGES = 10;
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	ptr = mmap_(self, variant, NULL, NUM_PAGES * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Trivially assert we can touch the first page. */
	ASSERT_TRUE(try_read_write_buf(ptr));

	ASSERT_EQ(madvise(ptr, page_size, MADV_GUARD_INSTALL), 0);

	/* Establish that 1st page SIGSEGV's. */
	ASSERT_FALSE(try_read_write_buf(ptr));

	/* Ensure we can touch everything else.*/
	for (i = 1; i < NUM_PAGES; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	/* Establish a guard page at the end of the mapping. */
	ASSERT_EQ(madvise(&ptr[(NUM_PAGES - 1) * page_size], page_size,
			  MADV_GUARD_INSTALL), 0);

	/* Check that both guard pages result in SIGSEGV. */
	ASSERT_FALSE(try_read_write_buf(ptr));
	ASSERT_FALSE(try_read_write_buf(&ptr[(NUM_PAGES - 1) * page_size]));

	/* Remove the first guard page. */
	ASSERT_FALSE(madvise(ptr, page_size, MADV_GUARD_REMOVE));

	/* Make sure we can touch it. */
	ASSERT_TRUE(try_read_write_buf(ptr));

	/* Remove the last guard page. */
	ASSERT_FALSE(madvise(&ptr[(NUM_PAGES - 1) * page_size], page_size,
			     MADV_GUARD_REMOVE));

	/* Make sure we can touch it. */
	ASSERT_TRUE(try_read_write_buf(&ptr[(NUM_PAGES - 1) * page_size]));

	/*
	 *  Test setting a _range_ of pages, namely the first 3. The first of
	 *  these be faulted in, so this also tests that we can install guard
	 *  pages over backed pages.
	 */
	ASSERT_EQ(madvise(ptr, 3 * page_size, MADV_GUARD_INSTALL), 0);

	/* Make sure they are all guard pages. */
	for (i = 0; i < 3; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Make sure the rest are not. */
	for (i = 3; i < NUM_PAGES; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	/* Remove guard pages. */
	ASSERT_EQ(madvise(ptr, NUM_PAGES * page_size, MADV_GUARD_REMOVE), 0);

	/* Now make sure we can touch everything. */
	for (i = 0; i < NUM_PAGES; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	/*
	 * Now remove all guard pages, make sure we don't remove existing
	 * entries.
	 */
	ASSERT_EQ(madvise(ptr, NUM_PAGES * page_size, MADV_GUARD_REMOVE), 0);

	for (i = 0; i < NUM_PAGES * page_size; i += page_size) {
		char chr = ptr[i];

		ASSERT_EQ(chr, 'x');
	}

	ASSERT_EQ(munmap(ptr, NUM_PAGES * page_size), 0);
}

/* Assert that operations applied across multiple VMAs work as expected. */
TEST_F(guard_regions, multi_vma)
{
	const unsigned long page_size = self->page_size;
	char *ptr_region, *ptr, *ptr1, *ptr2, *ptr3;
	int i;

	/* Reserve a 100 page region over which we can install VMAs. */
	ptr_region = mmap_(self, variant, NULL, 100 * page_size,
			   PROT_NONE, 0, 0);
	ASSERT_NE(ptr_region, MAP_FAILED);

	/* Place a VMA of 10 pages size at the start of the region. */
	ptr1 = mmap_(self, variant, ptr_region, 10 * page_size,
		     PROT_READ | PROT_WRITE, MAP_FIXED, 0);
	ASSERT_NE(ptr1, MAP_FAILED);

	/* Place a VMA of 5 pages size 50 pages into the region. */
	ptr2 = mmap_(self, variant, &ptr_region[50 * page_size], 5 * page_size,
		     PROT_READ | PROT_WRITE, MAP_FIXED, 0);
	ASSERT_NE(ptr2, MAP_FAILED);

	/* Place a VMA of 20 pages size at the end of the region. */
	ptr3 = mmap_(self, variant, &ptr_region[80 * page_size], 20 * page_size,
		     PROT_READ | PROT_WRITE, MAP_FIXED, 0);
	ASSERT_NE(ptr3, MAP_FAILED);

	/* Unmap gaps. */
	ASSERT_EQ(munmap(&ptr_region[10 * page_size], 40 * page_size), 0);
	ASSERT_EQ(munmap(&ptr_region[55 * page_size], 25 * page_size), 0);

	/*
	 * We end up with VMAs like this:
	 *
	 * 0    10 .. 50   55 .. 80   100
	 * [---]      [---]      [---]
	 */

	/*
	 * Now mark the whole range as guard pages and make sure all VMAs are as
	 * such.
	 */

	/*
	 * madvise() is certifiable and lets you perform operations over gaps,
	 * everything works, but it indicates an error and errno is set to
	 * -ENOMEM. Also if anything runs out of memory it is set to
	 * -ENOMEM. You are meant to guess which is which.
	 */
	ASSERT_EQ(madvise(ptr_region, 100 * page_size, MADV_GUARD_INSTALL), -1);
	ASSERT_EQ(errno, ENOMEM);

	for (i = 0; i < 10; i++) {
		char *curr = &ptr1[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	for (i = 0; i < 5; i++) {
		char *curr = &ptr2[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	for (i = 0; i < 20; i++) {
		char *curr = &ptr3[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Now remove guar pages over range and assert the opposite. */

	ASSERT_EQ(madvise(ptr_region, 100 * page_size, MADV_GUARD_REMOVE), -1);
	ASSERT_EQ(errno, ENOMEM);

	for (i = 0; i < 10; i++) {
		char *curr = &ptr1[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	for (i = 0; i < 5; i++) {
		char *curr = &ptr2[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	for (i = 0; i < 20; i++) {
		char *curr = &ptr3[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	/* Now map incompatible VMAs in the gaps. */
	ptr = mmap_(self, variant, &ptr_region[10 * page_size], 40 * page_size,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ptr = mmap_(self, variant, &ptr_region[55 * page_size], 25 * page_size,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/*
	 * We end up with VMAs like this:
	 *
	 * 0    10 .. 50   55 .. 80   100
	 * [---][xxxx][---][xxxx][---]
	 *
	 * Where 'x' signifies VMAs that cannot be merged with those adjacent to
	 * them.
	 */

	/* Multiple VMAs adjacent to one another should result in no error. */
	ASSERT_EQ(madvise(ptr_region, 100 * page_size, MADV_GUARD_INSTALL), 0);
	for (i = 0; i < 100; i++) {
		char *curr = &ptr_region[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}
	ASSERT_EQ(madvise(ptr_region, 100 * page_size, MADV_GUARD_REMOVE), 0);
	for (i = 0; i < 100; i++) {
		char *curr = &ptr_region[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr_region, 100 * page_size), 0);
}

/*
 * Assert that batched operations performed using process_madvise() work as
 * expected.
 */
TEST_F(guard_regions, process_madvise)
{
	const unsigned long page_size = self->page_size;
	char *ptr_region, *ptr1, *ptr2, *ptr3;
	ssize_t count;
	struct iovec vec[6];

	/* Reserve region to map over. */
	ptr_region = mmap_(self, variant, NULL, 100 * page_size,
			   PROT_NONE, 0, 0);
	ASSERT_NE(ptr_region, MAP_FAILED);

	/*
	 * 10 pages offset 1 page into reserve region. We MAP_POPULATE so we
	 * overwrite existing entries and test this code path against
	 * overwriting existing entries.
	 */
	ptr1 = mmap_(self, variant, &ptr_region[page_size], 10 * page_size,
		     PROT_READ | PROT_WRITE, MAP_FIXED | MAP_POPULATE, 0);
	ASSERT_NE(ptr1, MAP_FAILED);
	/* We want guard markers at start/end of each VMA. */
	vec[0].iov_base = ptr1;
	vec[0].iov_len = page_size;
	vec[1].iov_base = &ptr1[9 * page_size];
	vec[1].iov_len = page_size;

	/* 5 pages offset 50 pages into reserve region. */
	ptr2 = mmap_(self, variant, &ptr_region[50 * page_size], 5 * page_size,
		     PROT_READ | PROT_WRITE, MAP_FIXED, 0);
	ASSERT_NE(ptr2, MAP_FAILED);
	vec[2].iov_base = ptr2;
	vec[2].iov_len = page_size;
	vec[3].iov_base = &ptr2[4 * page_size];
	vec[3].iov_len = page_size;

	/* 20 pages offset 79 pages into reserve region. */
	ptr3 = mmap_(self, variant, &ptr_region[79 * page_size], 20 * page_size,
		    PROT_READ | PROT_WRITE, MAP_FIXED, 0);
	ASSERT_NE(ptr3, MAP_FAILED);
	vec[4].iov_base = ptr3;
	vec[4].iov_len = page_size;
	vec[5].iov_base = &ptr3[19 * page_size];
	vec[5].iov_len = page_size;

	/* Free surrounding VMAs. */
	ASSERT_EQ(munmap(ptr_region, page_size), 0);
	ASSERT_EQ(munmap(&ptr_region[11 * page_size], 39 * page_size), 0);
	ASSERT_EQ(munmap(&ptr_region[55 * page_size], 24 * page_size), 0);
	ASSERT_EQ(munmap(&ptr_region[99 * page_size], page_size), 0);

	/* Now guard in one step. */
	count = sys_process_madvise(PIDFD_SELF, vec, 6, MADV_GUARD_INSTALL, 0);

	/* OK we don't have permission to do this, skip. */
	if (count == -1 && errno == EPERM)
		ksft_exit_skip("No process_madvise() permissions, try running as root.\n");

	/* Returns the number of bytes advised. */
	ASSERT_EQ(count, 6 * page_size);

	/* Now make sure the guarding was applied. */

	ASSERT_FALSE(try_read_write_buf(ptr1));
	ASSERT_FALSE(try_read_write_buf(&ptr1[9 * page_size]));

	ASSERT_FALSE(try_read_write_buf(ptr2));
	ASSERT_FALSE(try_read_write_buf(&ptr2[4 * page_size]));

	ASSERT_FALSE(try_read_write_buf(ptr3));
	ASSERT_FALSE(try_read_write_buf(&ptr3[19 * page_size]));

	/* Now do the same with unguard... */
	count = sys_process_madvise(PIDFD_SELF, vec, 6, MADV_GUARD_REMOVE, 0);

	/* ...and everything should now succeed. */

	ASSERT_TRUE(try_read_write_buf(ptr1));
	ASSERT_TRUE(try_read_write_buf(&ptr1[9 * page_size]));

	ASSERT_TRUE(try_read_write_buf(ptr2));
	ASSERT_TRUE(try_read_write_buf(&ptr2[4 * page_size]));

	ASSERT_TRUE(try_read_write_buf(ptr3));
	ASSERT_TRUE(try_read_write_buf(&ptr3[19 * page_size]));

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr1, 10 * page_size), 0);
	ASSERT_EQ(munmap(ptr2, 5 * page_size), 0);
	ASSERT_EQ(munmap(ptr3, 20 * page_size), 0);
}

/* Assert that unmapping ranges does not leave guard markers behind. */
TEST_F(guard_regions, munmap)
{
	const unsigned long page_size = self->page_size;
	char *ptr, *ptr_new1, *ptr_new2;

	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Guard first and last pages. */
	ASSERT_EQ(madvise(ptr, page_size, MADV_GUARD_INSTALL), 0);
	ASSERT_EQ(madvise(&ptr[9 * page_size], page_size, MADV_GUARD_INSTALL), 0);

	/* Assert that they are guarded. */
	ASSERT_FALSE(try_read_write_buf(ptr));
	ASSERT_FALSE(try_read_write_buf(&ptr[9 * page_size]));

	/* Unmap them. */
	ASSERT_EQ(munmap(ptr, page_size), 0);
	ASSERT_EQ(munmap(&ptr[9 * page_size], page_size), 0);

	/* Map over them.*/
	ptr_new1 = mmap_(self, variant, ptr, page_size, PROT_READ | PROT_WRITE,
			 MAP_FIXED, 0);
	ASSERT_NE(ptr_new1, MAP_FAILED);
	ptr_new2 = mmap_(self, variant, &ptr[9 * page_size], page_size,
			 PROT_READ | PROT_WRITE, MAP_FIXED, 0);
	ASSERT_NE(ptr_new2, MAP_FAILED);

	/* Assert that they are now not guarded. */
	ASSERT_TRUE(try_read_write_buf(ptr_new1));
	ASSERT_TRUE(try_read_write_buf(ptr_new2));

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/* Assert that mprotect() operations have no bearing on guard markers. */
TEST_F(guard_regions, mprotect)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Guard the middle of the range. */
	ASSERT_EQ(madvise(&ptr[5 * page_size], 2 * page_size,
			  MADV_GUARD_INSTALL), 0);

	/* Assert that it is indeed guarded. */
	ASSERT_FALSE(try_read_write_buf(&ptr[5 * page_size]));
	ASSERT_FALSE(try_read_write_buf(&ptr[6 * page_size]));

	/* Now make these pages read-only. */
	ASSERT_EQ(mprotect(&ptr[5 * page_size], 2 * page_size, PROT_READ), 0);

	/* Make sure the range is still guarded. */
	ASSERT_FALSE(try_read_buf(&ptr[5 * page_size]));
	ASSERT_FALSE(try_read_buf(&ptr[6 * page_size]));

	/* Make sure we can guard again without issue.*/
	ASSERT_EQ(madvise(&ptr[5 * page_size], 2 * page_size,
			  MADV_GUARD_INSTALL), 0);

	/* Make sure the range is, yet again, still guarded. */
	ASSERT_FALSE(try_read_buf(&ptr[5 * page_size]));
	ASSERT_FALSE(try_read_buf(&ptr[6 * page_size]));

	/* Now unguard the whole range. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

	/* Make sure the whole range is readable. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_TRUE(try_read_buf(curr));
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/* Split and merge VMAs and make sure guard pages still behave. */
TEST_F(guard_regions, split_merge)
{
	const unsigned long page_size = self->page_size;
	char *ptr, *ptr_new;
	int i;

	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Guard the whole range. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_INSTALL), 0);

	/* Make sure the whole range is guarded. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Now unmap some pages in the range so we split. */
	ASSERT_EQ(munmap(&ptr[2 * page_size], page_size), 0);
	ASSERT_EQ(munmap(&ptr[5 * page_size], page_size), 0);
	ASSERT_EQ(munmap(&ptr[8 * page_size], page_size), 0);

	/* Make sure the remaining ranges are guarded post-split. */
	for (i = 0; i < 2; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}
	for (i = 2; i < 5; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}
	for (i = 6; i < 8; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}
	for (i = 9; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Now map them again - the unmap will have cleared the guards. */
	ptr_new = mmap_(self, variant, &ptr[2 * page_size], page_size,
			PROT_READ | PROT_WRITE, MAP_FIXED, 0);
	ASSERT_NE(ptr_new, MAP_FAILED);
	ptr_new = mmap_(self, variant, &ptr[5 * page_size], page_size,
			PROT_READ | PROT_WRITE, MAP_FIXED, 0);
	ASSERT_NE(ptr_new, MAP_FAILED);
	ptr_new = mmap_(self, variant, &ptr[8 * page_size], page_size,
			PROT_READ | PROT_WRITE, MAP_FIXED, 0);
	ASSERT_NE(ptr_new, MAP_FAILED);

	/* Now make sure guard pages are established. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];
		bool result = try_read_write_buf(curr);
		bool expect_true = i == 2 || i == 5 || i == 8;

		ASSERT_TRUE(expect_true ? result : !result);
	}

	/* Now guard everything again. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_INSTALL), 0);

	/* Make sure the whole range is guarded. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Now split the range into three. */
	ASSERT_EQ(mprotect(ptr, 3 * page_size, PROT_READ), 0);
	ASSERT_EQ(mprotect(&ptr[7 * page_size], 3 * page_size, PROT_READ), 0);

	/* Make sure the whole range is guarded for read. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_buf(curr));
	}

	/* Now reset protection bits so we merge the whole thing. */
	ASSERT_EQ(mprotect(ptr, 3 * page_size, PROT_READ | PROT_WRITE), 0);
	ASSERT_EQ(mprotect(&ptr[7 * page_size], 3 * page_size,
			   PROT_READ | PROT_WRITE), 0);

	/* Make sure the whole range is still guarded. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Split range into 3 again... */
	ASSERT_EQ(mprotect(ptr, 3 * page_size, PROT_READ), 0);
	ASSERT_EQ(mprotect(&ptr[7 * page_size], 3 * page_size, PROT_READ), 0);

	/* ...and unguard the whole range. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

	/* Make sure the whole range is remedied for read. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_TRUE(try_read_buf(curr));
	}

	/* Merge them again. */
	ASSERT_EQ(mprotect(ptr, 3 * page_size, PROT_READ | PROT_WRITE), 0);
	ASSERT_EQ(mprotect(&ptr[7 * page_size], 3 * page_size,
			   PROT_READ | PROT_WRITE), 0);

	/* Now ensure the merged range is remedied for read/write. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/* Assert that MADV_DONTNEED does not remove guard markers. */
TEST_F(guard_regions, dontneed)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Back the whole range. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		*curr = 'y';
	}

	/* Guard every other page. */
	for (i = 0; i < 10; i += 2) {
		char *curr = &ptr[i * page_size];
		int res = madvise(curr, page_size, MADV_GUARD_INSTALL);

		ASSERT_EQ(res, 0);
	}

	/* Indicate that we don't need any of the range. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_DONTNEED), 0);

	/* Check to ensure guard markers are still in place. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];
		bool result = try_read_buf(curr);

		if (i % 2 == 0) {
			ASSERT_FALSE(result);
		} else {
			ASSERT_TRUE(result);
			switch (variant->backing) {
			case ANON_BACKED:
				/* If anon, then we get a zero page. */
				ASSERT_EQ(*curr, '\0');
				break;
			default:
				/* Otherwise, we get the file data. */
				ASSERT_EQ(*curr, 'y');
				break;
			}
		}

		/* Now write... */
		result = try_write_buf(&ptr[i * page_size]);

		/* ...and make sure same result. */
		ASSERT_TRUE(i % 2 != 0 ? result : !result);
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/* Assert that mlock()'ed pages work correctly with guard markers. */
TEST_F(guard_regions, mlock)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Populate. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		*curr = 'y';
	}

	/* Lock. */
	ASSERT_EQ(mlock(ptr, 10 * page_size), 0);

	/* Now try to guard, should fail with EINVAL. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_INSTALL), -1);
	ASSERT_EQ(errno, EINVAL);

	/* OK unlock. */
	ASSERT_EQ(munlock(ptr, 10 * page_size), 0);

	/* Guard first half of range, should now succeed. */
	ASSERT_EQ(madvise(ptr, 5 * page_size, MADV_GUARD_INSTALL), 0);

	/* Make sure guard works. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];
		bool result = try_read_write_buf(curr);

		if (i < 5) {
			ASSERT_FALSE(result);
		} else {
			ASSERT_TRUE(result);
			ASSERT_EQ(*curr, 'x');
		}
	}

	/*
	 * Now lock the latter part of the range. We can't lock the guard pages,
	 * as this would result in the pages being populated and the guarding
	 * would cause this to error out.
	 */
	ASSERT_EQ(mlock(&ptr[5 * page_size], 5 * page_size), 0);

	/*
	 * Now remove guard pages, we permit mlock()'d ranges to have guard
	 * pages removed as it is a non-destructive operation.
	 */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

	/* Now check that no guard pages remain. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/*
 * Assert that moving, extending and shrinking memory via mremap() retains
 * guard markers where possible.
 *
 * - Moving a mapping alone should retain markers as they are.
 */
TEST_F(guard_regions, mremap_move)
{
	const unsigned long page_size = self->page_size;
	char *ptr, *ptr_new;

	/* Map 5 pages. */
	ptr = mmap_(self, variant, NULL, 5 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Place guard markers at both ends of the 5 page span. */
	ASSERT_EQ(madvise(ptr, page_size, MADV_GUARD_INSTALL), 0);
	ASSERT_EQ(madvise(&ptr[4 * page_size], page_size, MADV_GUARD_INSTALL), 0);

	/* Make sure the guard pages are in effect. */
	ASSERT_FALSE(try_read_write_buf(ptr));
	ASSERT_FALSE(try_read_write_buf(&ptr[4 * page_size]));

	/* Map a new region we will move this range into. Doing this ensures
	 * that we have reserved a range to map into.
	 */
	ptr_new = mmap_(self, variant, NULL, 5 * page_size, PROT_NONE, 0, 0);
	ASSERT_NE(ptr_new, MAP_FAILED);

	ASSERT_EQ(mremap(ptr, 5 * page_size, 5 * page_size,
			 MREMAP_MAYMOVE | MREMAP_FIXED, ptr_new), ptr_new);

	/* Make sure the guard markers are retained. */
	ASSERT_FALSE(try_read_write_buf(ptr_new));
	ASSERT_FALSE(try_read_write_buf(&ptr_new[4 * page_size]));

	/*
	 * Clean up - we only need reference the new pointer as we overwrote the
	 * PROT_NONE range and moved the existing one.
	 */
	munmap(ptr_new, 5 * page_size);
}

/*
 * Assert that moving, extending and shrinking memory via mremap() retains
 * guard markers where possible.
 *
 * Expanding should retain guard pages, only now in different position. The user
 * will have to remove guard pages manually to fix up (they'd have to do the
 * same if it were a PROT_NONE mapping).
 */
TEST_F(guard_regions, mremap_expand)
{
	const unsigned long page_size = self->page_size;
	char *ptr, *ptr_new;

	/* Map 10 pages... */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	/* ...But unmap the last 5 so we can ensure we can expand into them. */
	ASSERT_EQ(munmap(&ptr[5 * page_size], 5 * page_size), 0);

	/* Place guard markers at both ends of the 5 page span. */
	ASSERT_EQ(madvise(ptr, page_size, MADV_GUARD_INSTALL), 0);
	ASSERT_EQ(madvise(&ptr[4 * page_size], page_size, MADV_GUARD_INSTALL), 0);

	/* Make sure the guarding is in effect. */
	ASSERT_FALSE(try_read_write_buf(ptr));
	ASSERT_FALSE(try_read_write_buf(&ptr[4 * page_size]));

	/* Now expand to 10 pages. */
	ptr = mremap(ptr, 5 * page_size, 10 * page_size, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/*
	 * Make sure the guard markers are retained in their original positions.
	 */
	ASSERT_FALSE(try_read_write_buf(ptr));
	ASSERT_FALSE(try_read_write_buf(&ptr[4 * page_size]));

	/* Reserve a region which we can move to and expand into. */
	ptr_new = mmap_(self, variant, NULL, 20 * page_size, PROT_NONE, 0, 0);
	ASSERT_NE(ptr_new, MAP_FAILED);

	/* Now move and expand into it. */
	ptr = mremap(ptr, 10 * page_size, 20 * page_size,
		     MREMAP_MAYMOVE | MREMAP_FIXED, ptr_new);
	ASSERT_EQ(ptr, ptr_new);

	/*
	 * Again, make sure the guard markers are retained in their original positions.
	 */
	ASSERT_FALSE(try_read_write_buf(ptr));
	ASSERT_FALSE(try_read_write_buf(&ptr[4 * page_size]));

	/*
	 * A real user would have to remove guard markers, but would reasonably
	 * expect all characteristics of the mapping to be retained, including
	 * guard markers.
	 */

	/* Cleanup. */
	munmap(ptr, 20 * page_size);
}
/*
 * Assert that moving, extending and shrinking memory via mremap() retains
 * guard markers where possible.
 *
 * Shrinking will result in markers that are shrunk over being removed. Again,
 * if the user were using a PROT_NONE mapping they'd have to manually fix this
 * up also so this is OK.
 */
TEST_F(guard_regions, mremap_shrink)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	/* Map 5 pages. */
	ptr = mmap_(self, variant, NULL, 5 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Place guard markers at both ends of the 5 page span. */
	ASSERT_EQ(madvise(ptr, page_size, MADV_GUARD_INSTALL), 0);
	ASSERT_EQ(madvise(&ptr[4 * page_size], page_size, MADV_GUARD_INSTALL), 0);

	/* Make sure the guarding is in effect. */
	ASSERT_FALSE(try_read_write_buf(ptr));
	ASSERT_FALSE(try_read_write_buf(&ptr[4 * page_size]));

	/* Now shrink to 3 pages. */
	ptr = mremap(ptr, 5 * page_size, 3 * page_size, MREMAP_MAYMOVE);
	ASSERT_NE(ptr, MAP_FAILED);

	/* We expect the guard marker at the start to be retained... */
	ASSERT_FALSE(try_read_write_buf(ptr));

	/* ...But remaining pages will not have guard markers. */
	for (i = 1; i < 3; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	/*
	 * As with expansion, a real user would have to remove guard pages and
	 * fixup. But you'd have to do similar manual things with PROT_NONE
	 * mappings too.
	 */

	/*
	 * If we expand back to the original size, the end marker will, of
	 * course, no longer be present.
	 */
	ptr = mremap(ptr, 3 * page_size, 5 * page_size, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Again, we expect the guard marker at the start to be retained... */
	ASSERT_FALSE(try_read_write_buf(ptr));

	/* ...But remaining pages will not have guard markers. */
	for (i = 1; i < 5; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_TRUE(try_read_write_buf(curr));
	}

	/* Cleanup. */
	munmap(ptr, 5 * page_size);
}

/*
 * Assert that forking a process with VMAs that do not have VM_WIPEONFORK set
 * retain guard pages.
 */
TEST_F(guard_regions, fork)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	pid_t pid;
	int i;

	/* Map 10 pages. */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Establish guard pages in the first 5 pages. */
	ASSERT_EQ(madvise(ptr, 5 * page_size, MADV_GUARD_INSTALL), 0);

	pid = fork();
	ASSERT_NE(pid, -1);
	if (!pid) {
		/* This is the child process now. */

		/* Assert that the guarding is in effect. */
		for (i = 0; i < 10; i++) {
			char *curr = &ptr[i * page_size];
			bool result = try_read_write_buf(curr);

			ASSERT_TRUE(i >= 5 ? result : !result);
		}

		/* Now unguard the range.*/
		ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

		exit(0);
	}

	/* Parent process. */

	/* Parent simply waits on child. */
	waitpid(pid, NULL, 0);

	/* Child unguard does not impact parent page table state. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];
		bool result = try_read_write_buf(curr);

		ASSERT_TRUE(i >= 5 ? result : !result);
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/*
 * Assert expected behaviour after we fork populated ranges of anonymous memory
 * and then guard and unguard the range.
 */
TEST_F(guard_regions, fork_cow)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	pid_t pid;
	int i;

	if (variant->backing != ANON_BACKED)
		SKIP(return, "CoW only supported on anon mappings");

	/* Map 10 pages. */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Populate range. */
	for (i = 0; i < 10 * page_size; i++) {
		char chr = 'a' + (i % 26);

		ptr[i] = chr;
	}

	pid = fork();
	ASSERT_NE(pid, -1);
	if (!pid) {
		/* This is the child process now. */

		/* Ensure the range is as expected. */
		for (i = 0; i < 10 * page_size; i++) {
			char expected = 'a' + (i % 26);
			char actual = ptr[i];

			ASSERT_EQ(actual, expected);
		}

		/* Establish guard pages across the whole range. */
		ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_INSTALL), 0);
		/* Remove it. */
		ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

		/*
		 * By removing the guard pages, the page tables will be
		 * cleared. Assert that we are looking at the zero page now.
		 */
		for (i = 0; i < 10 * page_size; i++) {
			char actual = ptr[i];

			ASSERT_EQ(actual, '\0');
		}

		exit(0);
	}

	/* Parent process. */

	/* Parent simply waits on child. */
	waitpid(pid, NULL, 0);

	/* Ensure the range is unchanged in parent anon range. */
	for (i = 0; i < 10 * page_size; i++) {
		char expected = 'a' + (i % 26);
		char actual = ptr[i];

		ASSERT_EQ(actual, expected);
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/*
 * Assert that forking a process with VMAs that do have VM_WIPEONFORK set
 * behave as expected.
 */
TEST_F(guard_regions, fork_wipeonfork)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	pid_t pid;
	int i;

	if (variant->backing != ANON_BACKED)
		SKIP(return, "Wipe on fork only supported on anon mappings");

	/* Map 10 pages. */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Mark wipe on fork. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_WIPEONFORK), 0);

	/* Guard the first 5 pages. */
	ASSERT_EQ(madvise(ptr, 5 * page_size, MADV_GUARD_INSTALL), 0);

	pid = fork();
	ASSERT_NE(pid, -1);
	if (!pid) {
		/* This is the child process now. */

		/* Guard will have been wiped. */
		for (i = 0; i < 10; i++) {
			char *curr = &ptr[i * page_size];

			ASSERT_TRUE(try_read_write_buf(curr));
		}

		exit(0);
	}

	/* Parent process. */

	waitpid(pid, NULL, 0);

	/* Guard markers should be in effect.*/
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];
		bool result = try_read_write_buf(curr);

		ASSERT_TRUE(i >= 5 ? result : !result);
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/* Ensure that MADV_FREE retains guard entries as expected. */
TEST_F(guard_regions, lazyfree)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	if (variant->backing != ANON_BACKED)
		SKIP(return, "MADV_FREE only supported on anon mappings");

	/* Map 10 pages. */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Guard range. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_INSTALL), 0);

	/* Ensure guarded. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Lazyfree range. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_FREE), 0);

	/* This should leave the guard markers in place. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/* Ensure that MADV_POPULATE_READ, MADV_POPULATE_WRITE behave as expected. */
TEST_F(guard_regions, populate)
{
	const unsigned long page_size = self->page_size;
	char *ptr;

	/* Map 10 pages. */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Guard range. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_INSTALL), 0);

	/* Populate read should error out... */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_POPULATE_READ), -1);
	ASSERT_EQ(errno, EFAULT);

	/* ...as should populate write. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_POPULATE_WRITE), -1);
	ASSERT_EQ(errno, EFAULT);

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/* Ensure that MADV_COLD, MADV_PAGEOUT do not remove guard markers. */
TEST_F(guard_regions, cold_pageout)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	/* Map 10 pages. */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Guard range. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_INSTALL), 0);

	/* Ensured guarded. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Now mark cold. This should have no impact on guard markers. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_COLD), 0);

	/* Should remain guarded. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* OK, now page out. This should equally, have no effect on markers. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_PAGEOUT), 0);

	/* Should remain guarded. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/* Ensure that guard pages do not break userfaultd. */
TEST_F(guard_regions, uffd)
{
	const unsigned long page_size = self->page_size;
	int uffd;
	char *ptr;
	int i;
	struct uffdio_api api = {
		.api = UFFD_API,
		.features = 0,
	};
	struct uffdio_register reg;
	struct uffdio_range range;

	if (!is_anon_backed(variant))
		SKIP(return, "uffd only works on anon backing");

	/* Set up uffd. */
	uffd = userfaultfd(0);
	if (uffd == -1 && errno == EPERM)
		ksft_exit_skip("No userfaultfd permissions, try running as root.\n");
	ASSERT_NE(uffd, -1);

	ASSERT_EQ(ioctl(uffd, UFFDIO_API, &api), 0);

	/* Map 10 pages. */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Register the range with uffd. */
	range.start = (unsigned long)ptr;
	range.len = 10 * page_size;
	reg.range = range;
	reg.mode = UFFDIO_REGISTER_MODE_MISSING;
	ASSERT_EQ(ioctl(uffd, UFFDIO_REGISTER, &reg), 0);

	/* Guard the range. This should not trigger the uffd. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_INSTALL), 0);

	/* The guarding should behave as usual with no uffd intervention. */
	for (i = 0; i < 10; i++) {
		char *curr = &ptr[i * page_size];

		ASSERT_FALSE(try_read_write_buf(curr));
	}

	/* Cleanup. */
	ASSERT_EQ(ioctl(uffd, UFFDIO_UNREGISTER, &range), 0);
	close(uffd);
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/*
 * Mark a region within a file-backed mapping using MADV_SEQUENTIAL so we
 * aggressively read-ahead, then install guard regions and assert that it
 * behaves correctly.
 *
 * We page out using MADV_PAGEOUT before checking guard regions so we drop page
 * cache folios, meaning we maximise the possibility of some broken readahead.
 */
TEST_F(guard_regions, madvise_sequential)
{
	char *ptr;
	int i;
	const unsigned long page_size = self->page_size;

	if (variant->backing == ANON_BACKED)
		SKIP(return, "MADV_SEQUENTIAL meaningful only for file-backed");

	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Establish a pattern of data in the file. */
	set_pattern(ptr, 10, page_size);
	ASSERT_TRUE(check_pattern(ptr, 10, page_size));

	/* Mark it as being accessed sequentially. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_SEQUENTIAL), 0);

	/* Mark every other page a guard page. */
	for (i = 0; i < 10; i += 2) {
		char *ptr2 = &ptr[i * page_size];

		ASSERT_EQ(madvise(ptr2, page_size, MADV_GUARD_INSTALL), 0);
	}

	/* Now page it out. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_PAGEOUT), 0);

	/* Now make sure pages are as expected. */
	for (i = 0; i < 10; i++) {
		char *chrp = &ptr[i * page_size];

		if (i % 2 == 0) {
			bool result = try_read_write_buf(chrp);

			ASSERT_FALSE(result);
		} else {
			ASSERT_EQ(*chrp, 'a' + i);
		}
	}

	/* Now remove guard pages. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

	/* Now make sure all data is as expected. */
	if (!check_pattern(ptr, 10, page_size))
		ASSERT_TRUE(false);

	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/*
 * Check that file-backed mappings implement guard regions with MAP_PRIVATE
 * correctly.
 */
TEST_F(guard_regions, map_private)
{
	const unsigned long page_size = self->page_size;
	char *ptr_shared, *ptr_private;
	int i;

	if (variant->backing == ANON_BACKED)
		SKIP(return, "MAP_PRIVATE test specific to file-backed");

	ptr_shared = mmap_(self, variant, NULL, 10 * page_size, PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr_shared, MAP_FAILED);

	/* Manually mmap(), do not use mmap_() wrapper so we can force MAP_PRIVATE. */
	ptr_private = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, self->fd, 0);
	ASSERT_NE(ptr_private, MAP_FAILED);

	/* Set pattern in shared mapping. */
	set_pattern(ptr_shared, 10, page_size);

	/* Install guard regions in every other page in the shared mapping. */
	for (i = 0; i < 10; i += 2) {
		char *ptr = &ptr_shared[i * page_size];

		ASSERT_EQ(madvise(ptr, page_size, MADV_GUARD_INSTALL), 0);
	}

	for (i = 0; i < 10; i++) {
		/* Every even shared page should be guarded. */
		ASSERT_EQ(try_read_buf(&ptr_shared[i * page_size]), i % 2 != 0);
		/* Private mappings should always be readable. */
		ASSERT_TRUE(try_read_buf(&ptr_private[i * page_size]));
	}

	/* Install guard regions in every other page in the private mapping. */
	for (i = 0; i < 10; i += 2) {
		char *ptr = &ptr_private[i * page_size];

		ASSERT_EQ(madvise(ptr, page_size, MADV_GUARD_INSTALL), 0);
	}

	for (i = 0; i < 10; i++) {
		/* Every even shared page should be guarded. */
		ASSERT_EQ(try_read_buf(&ptr_shared[i * page_size]), i % 2 != 0);
		/* Every odd private page should be guarded. */
		ASSERT_EQ(try_read_buf(&ptr_private[i * page_size]), i % 2 != 0);
	}

	/* Remove guard regions from shared mapping. */
	ASSERT_EQ(madvise(ptr_shared, 10 * page_size, MADV_GUARD_REMOVE), 0);

	for (i = 0; i < 10; i++) {
		/* Shared mappings should always be readable. */
		ASSERT_TRUE(try_read_buf(&ptr_shared[i * page_size]));
		/* Every even private page should be guarded. */
		ASSERT_EQ(try_read_buf(&ptr_private[i * page_size]), i % 2 != 0);
	}

	/* Remove guard regions from private mapping. */
	ASSERT_EQ(madvise(ptr_private, 10 * page_size, MADV_GUARD_REMOVE), 0);

	for (i = 0; i < 10; i++) {
		/* Shared mappings should always be readable. */
		ASSERT_TRUE(try_read_buf(&ptr_shared[i * page_size]));
		/* Private mappings should always be readable. */
		ASSERT_TRUE(try_read_buf(&ptr_private[i * page_size]));
	}

	/* Ensure patterns are intact. */
	ASSERT_TRUE(check_pattern(ptr_shared, 10, page_size));
	ASSERT_TRUE(check_pattern(ptr_private, 10, page_size));

	/* Now write out every other page to MAP_PRIVATE. */
	for (i = 0; i < 10; i += 2) {
		char *ptr = &ptr_private[i * page_size];

		memset(ptr, 'a' + i, page_size);
	}

	/*
	 * At this point the mapping is:
	 *
	 * 0123456789
	 * SPSPSPSPSP
	 *
	 * Where S = shared, P = private mappings.
	 */

	/* Now mark the beginning of the mapping guarded. */
	ASSERT_EQ(madvise(ptr_private, 5 * page_size, MADV_GUARD_INSTALL), 0);

	/*
	 * This renders the mapping:
	 *
	 * 0123456789
	 * xxxxxPSPSP
	 */

	for (i = 0; i < 10; i++) {
		char *ptr = &ptr_private[i * page_size];

		/* Ensure guard regions as expected. */
		ASSERT_EQ(try_read_buf(ptr), i >= 5);
		/* The shared mapping should always succeed. */
		ASSERT_TRUE(try_read_buf(&ptr_shared[i * page_size]));
	}

	/* Remove the guard regions altogether. */
	ASSERT_EQ(madvise(ptr_private, 10 * page_size, MADV_GUARD_REMOVE), 0);

	/*
	 *
	 * We now expect the mapping to be:
	 *
	 * 0123456789
	 * SSSSSPSPSP
	 *
	 * As we removed guard regions, the private pages from the first 5 will
	 * have been zapped, so on fault will reestablish the shared mapping.
	 */

	for (i = 0; i < 10; i++) {
		char *ptr = &ptr_private[i * page_size];

		/*
		 * Assert that shared mappings in the MAP_PRIVATE mapping match
		 * the shared mapping.
		 */
		if (i < 5 || i % 2 == 0) {
			char *ptr_s = &ptr_shared[i * page_size];

			ASSERT_EQ(memcmp(ptr, ptr_s, page_size), 0);
			continue;
		}

		/* Everything else is a private mapping. */
		ASSERT_TRUE(is_buf_eq(ptr, page_size, 'a' + i));
	}

	ASSERT_EQ(munmap(ptr_shared, 10 * page_size), 0);
	ASSERT_EQ(munmap(ptr_private, 10 * page_size), 0);
}

/* Test that guard regions established over a read-only mapping function correctly. */
TEST_F(guard_regions, readonly_file)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	if (variant->backing == ANON_BACKED)
		SKIP(return, "Read-only test specific to file-backed");

	/* Map shared so we can populate with pattern, populate it, unmap. */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	set_pattern(ptr, 10, page_size);
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
	/* Close the fd so we can re-open read-only. */
	ASSERT_EQ(close(self->fd), 0);

	/* Re-open read-only. */
	self->fd = open(self->path, O_RDONLY);
	ASSERT_NE(self->fd, -1);
	/* Re-map read-only. */
	ptr = mmap_(self, variant, NULL, 10 * page_size, PROT_READ, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Mark every other page guarded. */
	for (i = 0; i < 10; i += 2) {
		char *ptr_pg = &ptr[i * page_size];

		ASSERT_EQ(madvise(ptr_pg, page_size, MADV_GUARD_INSTALL), 0);
	}

	/* Assert that the guard regions are in place.*/
	for (i = 0; i < 10; i++) {
		char *ptr_pg = &ptr[i * page_size];

		ASSERT_EQ(try_read_buf(ptr_pg), i % 2 != 0);
	}

	/* Remove guard regions. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

	/* Ensure the data is as expected. */
	ASSERT_TRUE(check_pattern(ptr, 10, page_size));

	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

TEST_F(guard_regions, fault_around)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	if (variant->backing == ANON_BACKED)
		SKIP(return, "Fault-around test specific to file-backed");

	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Establish a pattern in the backing file. */
	set_pattern(ptr, 10, page_size);

	/*
	 * Now drop it from the page cache so we get major faults when next we
	 * map it.
	 */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_PAGEOUT), 0);

	/* Unmap and remap 'to be sure'. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Now make every even page guarded. */
	for (i = 0; i < 10; i += 2) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(madvise(ptr_p, page_size, MADV_GUARD_INSTALL), 0);
	}

	/* Now fault in every odd page. This should trigger fault-around. */
	for (i = 1; i < 10; i += 2) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_TRUE(try_read_buf(ptr_p));
	}

	/* Finally, ensure that guard regions are intact as expected. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(try_read_buf(ptr_p), i % 2 != 0);
	}

	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

TEST_F(guard_regions, truncation)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	if (variant->backing == ANON_BACKED)
		SKIP(return, "Truncation test specific to file-backed");

	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/*
	 * Establish a pattern in the backing file, just so there is data
	 * there.
	 */
	set_pattern(ptr, 10, page_size);

	/* Now make every even page guarded. */
	for (i = 0; i < 10; i += 2) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(madvise(ptr_p, page_size, MADV_GUARD_INSTALL), 0);
	}

	/* Now assert things are as expected. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(try_read_write_buf(ptr_p), i % 2 != 0);
	}

	/* Now truncate to actually used size (initialised to 100). */
	ASSERT_EQ(ftruncate(self->fd, 10 * page_size), 0);

	/* Here the guard regions will remain intact. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(try_read_write_buf(ptr_p), i % 2 != 0);
	}

	/* Now truncate to half the size, then truncate again to the full size. */
	ASSERT_EQ(ftruncate(self->fd, 5 * page_size), 0);
	ASSERT_EQ(ftruncate(self->fd, 10 * page_size), 0);

	/* Again, guard pages will remain intact. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(try_read_write_buf(ptr_p), i % 2 != 0);
	}

	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

TEST_F(guard_regions, hole_punch)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	if (variant->backing == ANON_BACKED)
		SKIP(return, "Truncation test specific to file-backed");

	/* Establish pattern in mapping. */
	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	set_pattern(ptr, 10, page_size);

	/* Install a guard region in the middle of the mapping. */
	ASSERT_EQ(madvise(&ptr[3 * page_size], 4 * page_size,
			  MADV_GUARD_INSTALL), 0);

	/*
	 * The buffer will now be:
	 *
	 * 0123456789
	 * ***xxxx***
	 *
	 * Where * is data and x is the guard region.
	 */

	/* Ensure established. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(try_read_buf(ptr_p), i < 3 || i >= 7);
	}

	/* Now hole punch the guarded region. */
	ASSERT_EQ(madvise(&ptr[3 * page_size], 4 * page_size,
			  MADV_REMOVE), 0);

	/* Ensure guard regions remain. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(try_read_buf(ptr_p), i < 3 || i >= 7);
	}

	/* Now remove guard region throughout. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

	/* Check that the pattern exists in non-hole punched region. */
	ASSERT_TRUE(check_pattern(ptr, 3, page_size));
	/* Check that hole punched region is zeroed. */
	ASSERT_TRUE(is_buf_eq(&ptr[3 * page_size], 4 * page_size, '\0'));
	/* Check that the pattern exists in the remainder of the file. */
	ASSERT_TRUE(check_pattern_offset(ptr, 3, page_size, 7));

	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/*
 * Ensure that a memfd works correctly with guard regions, that we can write
 * seal it then open the mapping read-only and still establish guard regions
 * within, remove those guard regions and have everything work correctly.
 */
TEST_F(guard_regions, memfd_write_seal)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	if (variant->backing != SHMEM_BACKED)
		SKIP(return, "memfd write seal test specific to shmem");

	/* OK, we need a memfd, so close existing one. */
	ASSERT_EQ(close(self->fd), 0);

	/* Create and truncate memfd. */
	self->fd = memfd_create("guard_regions_memfd_seals_test",
				MFD_ALLOW_SEALING);
	ASSERT_NE(self->fd, -1);
	ASSERT_EQ(ftruncate(self->fd, 10 * page_size), 0);

	/* Map, set pattern, unmap. */
	ptr = mmap_(self, variant, NULL, 10 * page_size, PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	set_pattern(ptr, 10, page_size);
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);

	/* Write-seal the memfd. */
	ASSERT_EQ(fcntl(self->fd, F_ADD_SEALS, F_SEAL_WRITE), 0);

	/* Now map the memfd readonly. */
	ptr = mmap_(self, variant, NULL, 10 * page_size, PROT_READ, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Ensure pattern is as expected. */
	ASSERT_TRUE(check_pattern(ptr, 10, page_size));

	/* Now make every even page guarded. */
	for (i = 0; i < 10; i += 2) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(madvise(ptr_p, page_size, MADV_GUARD_INSTALL), 0);
	}

	/* Now assert things are as expected. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(try_read_buf(ptr_p), i % 2 != 0);
	}

	/* Now remove guard regions. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

	/* Ensure pattern is as expected. */
	ASSERT_TRUE(check_pattern(ptr, 10, page_size));

	/* Ensure write seal intact. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_FALSE(try_write_buf(ptr_p));
	}

	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}


/*
 * Since we are now permitted to establish guard regions in read-only anonymous
 * mappings, for the sake of thoroughness, though it probably has no practical
 * use, test that guard regions function with a mapping to the anonymous zero
 * page.
 */
TEST_F(guard_regions, anon_zeropage)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	if (!is_anon_backed(variant))
		SKIP(return, "anon zero page test specific to anon/shmem");

	/* Obtain a read-only i.e. anon zero page mapping. */
	ptr = mmap_(self, variant, NULL, 10 * page_size, PROT_READ, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Now make every even page guarded. */
	for (i = 0; i < 10; i += 2) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(madvise(ptr_p, page_size, MADV_GUARD_INSTALL), 0);
	}

	/* Now assert things are as expected. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(try_read_buf(ptr_p), i % 2 != 0);
	}

	/* Now remove all guard regions. */
	ASSERT_EQ(madvise(ptr, 10 * page_size, MADV_GUARD_REMOVE), 0);

	/* Now assert things are as expected. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_TRUE(try_read_buf(ptr_p));
	}

	/* Ensure zero page...*/
	ASSERT_TRUE(is_buf_eq(ptr, 10 * page_size, '\0'));

	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/*
 * Assert that /proc/$pid/pagemap correctly identifies guard region ranges.
 */
TEST_F(guard_regions, pagemap)
{
	const unsigned long page_size = self->page_size;
	int proc_fd;
	char *ptr;
	int i;

	proc_fd = open("/proc/self/pagemap", O_RDONLY);
	ASSERT_NE(proc_fd, -1);

	ptr = mmap_(self, variant, NULL, 10 * page_size,
		    PROT_READ | PROT_WRITE, 0, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Read from pagemap, and assert no guard regions are detected. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];
		unsigned long entry = pagemap_get_entry(proc_fd, ptr_p);
		unsigned long masked = entry & PM_GUARD_REGION;

		ASSERT_EQ(masked, 0);
	}

	/* Install a guard region in every other page. */
	for (i = 0; i < 10; i += 2) {
		char *ptr_p = &ptr[i * page_size];

		ASSERT_EQ(madvise(ptr_p, page_size, MADV_GUARD_INSTALL), 0);
	}

	/* Re-read from pagemap, and assert guard regions are detected. */
	for (i = 0; i < 10; i++) {
		char *ptr_p = &ptr[i * page_size];
		unsigned long entry = pagemap_get_entry(proc_fd, ptr_p);
		unsigned long masked = entry & PM_GUARD_REGION;

		ASSERT_EQ(masked, i % 2 == 0 ? PM_GUARD_REGION : 0);
	}

	ASSERT_EQ(close(proc_fd), 0);
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

TEST_HARNESS_MAIN

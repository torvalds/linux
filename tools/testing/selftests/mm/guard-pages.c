// SPDX-License-Identifier: GPL-2.0-or-later

#define _GNU_SOURCE
#include "../kselftest_harness.h"
#include <asm-generic/mman.h> /* Force the import of the tools version. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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

static int pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
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

FIXTURE(guard_pages)
{
	unsigned long page_size;
};

FIXTURE_SETUP(guard_pages)
{
	struct sigaction act = {
		.sa_handler = &handle_fatal,
		.sa_flags = SA_NODEFER,
	};

	sigemptyset(&act.sa_mask);
	if (sigaction(SIGSEGV, &act, NULL))
		ksft_exit_fail_perror("sigaction");

	self->page_size = (unsigned long)sysconf(_SC_PAGESIZE);
};

FIXTURE_TEARDOWN(guard_pages)
{
	struct sigaction act = {
		.sa_handler = SIG_DFL,
		.sa_flags = SA_NODEFER,
	};

	sigemptyset(&act.sa_mask);
	sigaction(SIGSEGV, &act, NULL);
}

TEST_F(guard_pages, basic)
{
	const unsigned long NUM_PAGES = 10;
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	ptr = mmap(NULL, NUM_PAGES * page_size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANON, -1, 0);
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
TEST_F(guard_pages, multi_vma)
{
	const unsigned long page_size = self->page_size;
	char *ptr_region, *ptr, *ptr1, *ptr2, *ptr3;
	int i;

	/* Reserve a 100 page region over which we can install VMAs. */
	ptr_region = mmap(NULL, 100 * page_size, PROT_NONE,
			  MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr_region, MAP_FAILED);

	/* Place a VMA of 10 pages size at the start of the region. */
	ptr1 = mmap(ptr_region, 10 * page_size, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr1, MAP_FAILED);

	/* Place a VMA of 5 pages size 50 pages into the region. */
	ptr2 = mmap(&ptr_region[50 * page_size], 5 * page_size,
		    PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);

	/* Place a VMA of 20 pages size at the end of the region. */
	ptr3 = mmap(&ptr_region[80 * page_size], 20 * page_size,
		    PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
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
	ptr = mmap(&ptr_region[10 * page_size], 40 * page_size,
		   PROT_READ | PROT_WRITE | PROT_EXEC,
		   MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ptr = mmap(&ptr_region[55 * page_size], 25 * page_size,
		   PROT_READ | PROT_WRITE | PROT_EXEC,
		   MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, process_madvise)
{
	const unsigned long page_size = self->page_size;
	pid_t pid = getpid();
	int pidfd = pidfd_open(pid, 0);
	char *ptr_region, *ptr1, *ptr2, *ptr3;
	ssize_t count;
	struct iovec vec[6];

	ASSERT_NE(pidfd, -1);

	/* Reserve region to map over. */
	ptr_region = mmap(NULL, 100 * page_size, PROT_NONE,
			  MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr_region, MAP_FAILED);

	/*
	 * 10 pages offset 1 page into reserve region. We MAP_POPULATE so we
	 * overwrite existing entries and test this code path against
	 * overwriting existing entries.
	 */
	ptr1 = mmap(&ptr_region[page_size], 10 * page_size,
		    PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_ANON | MAP_PRIVATE | MAP_POPULATE, -1, 0);
	ASSERT_NE(ptr1, MAP_FAILED);
	/* We want guard markers at start/end of each VMA. */
	vec[0].iov_base = ptr1;
	vec[0].iov_len = page_size;
	vec[1].iov_base = &ptr1[9 * page_size];
	vec[1].iov_len = page_size;

	/* 5 pages offset 50 pages into reserve region. */
	ptr2 = mmap(&ptr_region[50 * page_size], 5 * page_size,
		    PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);
	vec[2].iov_base = ptr2;
	vec[2].iov_len = page_size;
	vec[3].iov_base = &ptr2[4 * page_size];
	vec[3].iov_len = page_size;

	/* 20 pages offset 79 pages into reserve region. */
	ptr3 = mmap(&ptr_region[79 * page_size], 20 * page_size,
		    PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
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
	count = process_madvise(pidfd, vec, 6, MADV_GUARD_INSTALL, 0);

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
	count = process_madvise(pidfd, vec, 6, MADV_GUARD_REMOVE, 0);

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
	close(pidfd);
}

/* Assert that unmapping ranges does not leave guard markers behind. */
TEST_F(guard_pages, munmap)
{
	const unsigned long page_size = self->page_size;
	char *ptr, *ptr_new1, *ptr_new2;

	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
	ptr_new1 = mmap(ptr, page_size, PROT_READ | PROT_WRITE,
			MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr_new1, MAP_FAILED);
	ptr_new2 = mmap(&ptr[9 * page_size], page_size, PROT_READ | PROT_WRITE,
			MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr_new2, MAP_FAILED);

	/* Assert that they are now not guarded. */
	ASSERT_TRUE(try_read_write_buf(ptr_new1));
	ASSERT_TRUE(try_read_write_buf(ptr_new2));

	/* Cleanup. */
	ASSERT_EQ(munmap(ptr, 10 * page_size), 0);
}

/* Assert that mprotect() operations have no bearing on guard markers. */
TEST_F(guard_pages, mprotect)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, split_merge)
{
	const unsigned long page_size = self->page_size;
	char *ptr, *ptr_new;
	int i;

	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
	ptr_new = mmap(&ptr[2 * page_size], page_size, PROT_READ | PROT_WRITE,
		       MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr_new, MAP_FAILED);
	ptr_new = mmap(&ptr[5 * page_size], page_size, PROT_READ | PROT_WRITE,
		       MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr_new, MAP_FAILED);
	ptr_new = mmap(&ptr[8 * page_size], page_size, PROT_READ | PROT_WRITE,
		       MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, dontneed)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
			/* Make sure we really did get reset to zero page. */
			ASSERT_EQ(*curr, '\0');
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
TEST_F(guard_pages, mlock)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, mremap_move)
{
	const unsigned long page_size = self->page_size;
	char *ptr, *ptr_new;

	/* Map 5 pages. */
	ptr = mmap(NULL, 5 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
	ptr_new = mmap(NULL, 5 * page_size, PROT_NONE, MAP_ANON | MAP_PRIVATE,
		       -1, 0);
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
TEST_F(guard_pages, mremap_expand)
{
	const unsigned long page_size = self->page_size;
	char *ptr, *ptr_new;

	/* Map 10 pages... */
	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
	ptr_new = mmap(NULL, 20 * page_size, PROT_NONE,
		       MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, mremap_shrink)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	/* Map 5 pages. */
	ptr = mmap(NULL, 5 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, fork)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	pid_t pid;
	int i;

	/* Map 10 pages. */
	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Establish guard apges in the first 5 pages. */
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
 * Assert that forking a process with VMAs that do have VM_WIPEONFORK set
 * behave as expected.
 */
TEST_F(guard_pages, fork_wipeonfork)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	pid_t pid;
	int i;

	/* Map 10 pages. */
	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, lazyfree)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	/* Map 10 pages. */
	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, populate)
{
	const unsigned long page_size = self->page_size;
	char *ptr;

	/* Map 10 pages. */
	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, cold_pageout)
{
	const unsigned long page_size = self->page_size;
	char *ptr;
	int i;

	/* Map 10 pages. */
	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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
TEST_F(guard_pages, uffd)
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

	/* Set up uffd. */
	uffd = userfaultfd(0);
	if (uffd == -1 && errno == EPERM)
		ksft_exit_skip("No userfaultfd permissions, try running as root.\n");
	ASSERT_NE(uffd, -1);

	ASSERT_EQ(ioctl(uffd, UFFDIO_API, &api), 0);

	/* Map 10 pages. */
	ptr = mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE, -1, 0);
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

TEST_HARNESS_MAIN

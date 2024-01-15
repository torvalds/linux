// SPDX-License-Identifier: GPL-2.0

/*
 * Tests for mremap w/ MREMAP_DONTUNMAP.
 *
 * Copyright 2020, Brian Geffon <bgeffon@google.com>
 */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <linux/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../kselftest.h"

unsigned long page_size;
char *page_buffer;

static void dump_maps(void)
{
	char cmd[32];

	snprintf(cmd, sizeof(cmd), "cat /proc/%d/maps", getpid());
	system(cmd);
}

#define BUG_ON(condition, description)					      \
	do {								      \
		if (condition) {					      \
			fprintf(stderr, "[FAIL]\t%s():%d\t%s:%s\n", __func__, \
				__LINE__, (description), strerror(errno));    \
			dump_maps();					  \
			exit(1);					      \
		} 							      \
	} while (0)

// Try a simple operation for to "test" for kernel support this prevents
// reporting tests as failed when it's run on an older kernel.
static int kernel_support_for_mremap_dontunmap()
{
	int ret = 0;
	unsigned long num_pages = 1;
	void *source_mapping = mmap(NULL, num_pages * page_size, PROT_NONE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	BUG_ON(source_mapping == MAP_FAILED, "mmap");

	// This simple remap should only fail if MREMAP_DONTUNMAP isn't
	// supported.
	void *dest_mapping =
	    mremap(source_mapping, num_pages * page_size, num_pages * page_size,
		   MREMAP_DONTUNMAP | MREMAP_MAYMOVE, 0);
	if (dest_mapping == MAP_FAILED) {
		ret = errno;
	} else {
		BUG_ON(munmap(dest_mapping, num_pages * page_size) == -1,
		       "unable to unmap destination mapping");
	}

	BUG_ON(munmap(source_mapping, num_pages * page_size) == -1,
	       "unable to unmap source mapping");
	return ret;
}

// This helper will just validate that an entire mapping contains the expected
// byte.
static int check_region_contains_byte(void *addr, unsigned long size, char byte)
{
	BUG_ON(size & (page_size - 1),
	       "check_region_contains_byte expects page multiples");
	BUG_ON((unsigned long)addr & (page_size - 1),
	       "check_region_contains_byte expects page alignment");

	memset(page_buffer, byte, page_size);

	unsigned long num_pages = size / page_size;
	unsigned long i;

	// Compare each page checking that it contains our expected byte.
	for (i = 0; i < num_pages; ++i) {
		int ret =
		    memcmp(addr + (i * page_size), page_buffer, page_size);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

// this test validates that MREMAP_DONTUNMAP moves the pagetables while leaving
// the source mapping mapped.
static void mremap_dontunmap_simple()
{
	unsigned long num_pages = 5;

	void *source_mapping =
	    mmap(NULL, num_pages * page_size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	BUG_ON(source_mapping == MAP_FAILED, "mmap");

	memset(source_mapping, 'a', num_pages * page_size);

	// Try to just move the whole mapping anywhere (not fixed).
	void *dest_mapping =
	    mremap(source_mapping, num_pages * page_size, num_pages * page_size,
		   MREMAP_DONTUNMAP | MREMAP_MAYMOVE, NULL);
	BUG_ON(dest_mapping == MAP_FAILED, "mremap");

	// Validate that the pages have been moved, we know they were moved if
	// the dest_mapping contains a's.
	BUG_ON(check_region_contains_byte
	       (dest_mapping, num_pages * page_size, 'a') != 0,
	       "pages did not migrate");
	BUG_ON(check_region_contains_byte
	       (source_mapping, num_pages * page_size, 0) != 0,
	       "source should have no ptes");

	BUG_ON(munmap(dest_mapping, num_pages * page_size) == -1,
	       "unable to unmap destination mapping");
	BUG_ON(munmap(source_mapping, num_pages * page_size) == -1,
	       "unable to unmap source mapping");
}

// This test validates that MREMAP_DONTUNMAP on a shared mapping works as expected.
static void mremap_dontunmap_simple_shmem()
{
	unsigned long num_pages = 5;

	int mem_fd = memfd_create("memfd", MFD_CLOEXEC);
	BUG_ON(mem_fd < 0, "memfd_create");

	BUG_ON(ftruncate(mem_fd, num_pages * page_size) < 0,
			"ftruncate");

	void *source_mapping =
	    mmap(NULL, num_pages * page_size, PROT_READ | PROT_WRITE,
		 MAP_FILE | MAP_SHARED, mem_fd, 0);
	BUG_ON(source_mapping == MAP_FAILED, "mmap");

	BUG_ON(close(mem_fd) < 0, "close");

	memset(source_mapping, 'a', num_pages * page_size);

	// Try to just move the whole mapping anywhere (not fixed).
	void *dest_mapping =
	    mremap(source_mapping, num_pages * page_size, num_pages * page_size,
		   MREMAP_DONTUNMAP | MREMAP_MAYMOVE, NULL);
	if (dest_mapping == MAP_FAILED && errno == EINVAL) {
		// Old kernel which doesn't support MREMAP_DONTUNMAP on shmem.
		BUG_ON(munmap(source_mapping, num_pages * page_size) == -1,
			"unable to unmap source mapping");
		return;
	}

	BUG_ON(dest_mapping == MAP_FAILED, "mremap");

	// Validate that the pages have been moved, we know they were moved if
	// the dest_mapping contains a's.
	BUG_ON(check_region_contains_byte
	       (dest_mapping, num_pages * page_size, 'a') != 0,
	       "pages did not migrate");

	// Because the region is backed by shmem, we will actually see the same
	// memory at the source location still.
	BUG_ON(check_region_contains_byte
	       (source_mapping, num_pages * page_size, 'a') != 0,
	       "source should have no ptes");

	BUG_ON(munmap(dest_mapping, num_pages * page_size) == -1,
	       "unable to unmap destination mapping");
	BUG_ON(munmap(source_mapping, num_pages * page_size) == -1,
	       "unable to unmap source mapping");
}

// This test validates MREMAP_DONTUNMAP will move page tables to a specific
// destination using MREMAP_FIXED, also while validating that the source
// remains intact.
static void mremap_dontunmap_simple_fixed()
{
	unsigned long num_pages = 5;

	// Since we want to guarantee that we can remap to a point, we will
	// create a mapping up front.
	void *dest_mapping =
	    mmap(NULL, num_pages * page_size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	BUG_ON(dest_mapping == MAP_FAILED, "mmap");
	memset(dest_mapping, 'X', num_pages * page_size);

	void *source_mapping =
	    mmap(NULL, num_pages * page_size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	BUG_ON(source_mapping == MAP_FAILED, "mmap");
	memset(source_mapping, 'a', num_pages * page_size);

	void *remapped_mapping =
	    mremap(source_mapping, num_pages * page_size, num_pages * page_size,
		   MREMAP_FIXED | MREMAP_DONTUNMAP | MREMAP_MAYMOVE,
		   dest_mapping);
	BUG_ON(remapped_mapping == MAP_FAILED, "mremap");
	BUG_ON(remapped_mapping != dest_mapping,
	       "mremap should have placed the remapped mapping at dest_mapping");

	// The dest mapping will have been unmap by mremap so we expect the Xs
	// to be gone and replaced with a's.
	BUG_ON(check_region_contains_byte
	       (dest_mapping, num_pages * page_size, 'a') != 0,
	       "pages did not migrate");

	// And the source mapping will have had its ptes dropped.
	BUG_ON(check_region_contains_byte
	       (source_mapping, num_pages * page_size, 0) != 0,
	       "source should have no ptes");

	BUG_ON(munmap(dest_mapping, num_pages * page_size) == -1,
	       "unable to unmap destination mapping");
	BUG_ON(munmap(source_mapping, num_pages * page_size) == -1,
	       "unable to unmap source mapping");
}

// This test validates that we can MREMAP_DONTUNMAP for a portion of an
// existing mapping.
static void mremap_dontunmap_partial_mapping()
{
	/*
	 *  source mapping:
	 *  --------------
	 *  | aaaaaaaaaa |
	 *  --------------
	 *  to become:
	 *  --------------
	 *  | aaaaa00000 |
	 *  --------------
	 *  With the destination mapping containing 5 pages of As.
	 *  ---------
	 *  | aaaaa |
	 *  ---------
	 */
	unsigned long num_pages = 10;
	void *source_mapping =
	    mmap(NULL, num_pages * page_size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	BUG_ON(source_mapping == MAP_FAILED, "mmap");
	memset(source_mapping, 'a', num_pages * page_size);

	// We will grab the last 5 pages of the source and move them.
	void *dest_mapping =
	    mremap(source_mapping + (5 * page_size), 5 * page_size,
		   5 * page_size,
		   MREMAP_DONTUNMAP | MREMAP_MAYMOVE, NULL);
	BUG_ON(dest_mapping == MAP_FAILED, "mremap");

	// We expect the first 5 pages of the source to contain a's and the
	// final 5 pages to contain zeros.
	BUG_ON(check_region_contains_byte(source_mapping, 5 * page_size, 'a') !=
	       0, "first 5 pages of source should have original pages");
	BUG_ON(check_region_contains_byte
	       (source_mapping + (5 * page_size), 5 * page_size, 0) != 0,
	       "final 5 pages of source should have no ptes");

	// Finally we expect the destination to have 5 pages worth of a's.
	BUG_ON(check_region_contains_byte(dest_mapping, 5 * page_size, 'a') !=
	       0, "dest mapping should contain ptes from the source");

	BUG_ON(munmap(dest_mapping, 5 * page_size) == -1,
	       "unable to unmap destination mapping");
	BUG_ON(munmap(source_mapping, num_pages * page_size) == -1,
	       "unable to unmap source mapping");
}

// This test validates that we can remap over only a portion of a mapping.
static void mremap_dontunmap_partial_mapping_overwrite(void)
{
	/*
	 *  source mapping:
	 *  ---------
	 *  |aaaaa|
	 *  ---------
	 *  dest mapping initially:
	 *  -----------
	 *  |XXXXXXXXXX|
	 *  ------------
	 *  Source to become:
	 *  ---------
	 *  |00000|
	 *  ---------
	 *  With the destination mapping containing 5 pages of As.
	 *  ------------
	 *  |aaaaaXXXXX|
	 *  ------------
	 */
	void *source_mapping =
	    mmap(NULL, 5 * page_size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	BUG_ON(source_mapping == MAP_FAILED, "mmap");
	memset(source_mapping, 'a', 5 * page_size);

	void *dest_mapping =
	    mmap(NULL, 10 * page_size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	BUG_ON(dest_mapping == MAP_FAILED, "mmap");
	memset(dest_mapping, 'X', 10 * page_size);

	// We will grab the last 5 pages of the source and move them.
	void *remapped_mapping =
	    mremap(source_mapping, 5 * page_size,
		   5 * page_size,
		   MREMAP_DONTUNMAP | MREMAP_MAYMOVE | MREMAP_FIXED, dest_mapping);
	BUG_ON(dest_mapping == MAP_FAILED, "mremap");
	BUG_ON(dest_mapping != remapped_mapping, "expected to remap to dest_mapping");

	BUG_ON(check_region_contains_byte(source_mapping, 5 * page_size, 0) !=
	       0, "first 5 pages of source should have no ptes");

	// Finally we expect the destination to have 5 pages worth of a's.
	BUG_ON(check_region_contains_byte(dest_mapping, 5 * page_size, 'a') != 0,
			"dest mapping should contain ptes from the source");

	// Finally the last 5 pages shouldn't have been touched.
	BUG_ON(check_region_contains_byte(dest_mapping + (5 * page_size),
				5 * page_size, 'X') != 0,
			"dest mapping should have retained the last 5 pages");

	BUG_ON(munmap(dest_mapping, 10 * page_size) == -1,
	       "unable to unmap destination mapping");
	BUG_ON(munmap(source_mapping, 5 * page_size) == -1,
	       "unable to unmap source mapping");
}

int main(void)
{
	page_size = sysconf(_SC_PAGE_SIZE);

	// test for kernel support for MREMAP_DONTUNMAP skipping the test if
	// not.
	if (kernel_support_for_mremap_dontunmap() != 0) {
		printf("No kernel support for MREMAP_DONTUNMAP\n");
		return KSFT_SKIP;
	}

	// Keep a page sized buffer around for when we need it.
	page_buffer =
	    mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	BUG_ON(page_buffer == MAP_FAILED, "unable to mmap a page.");

	mremap_dontunmap_simple();
	mremap_dontunmap_simple_shmem();
	mremap_dontunmap_simple_fixed();
	mremap_dontunmap_partial_mapping();
	mremap_dontunmap_partial_mapping_overwrite();

	BUG_ON(munmap(page_buffer, page_size) == -1,
	       "unable to unmap page buffer");

	printf("OK\n");
	return 0;
}

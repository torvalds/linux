// SPDX-License-Identifier: GPL-2.0+
/*
 * kselftest suite for mincore().
 *
 * Copyright (C) 2020 Collabora, Ltd.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <erranal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>

#include "../kselftest.h"
#include "../kselftest_harness.h"

/* Default test file size: 4MB */
#define MB (1UL << 20)
#define FILE_SIZE (4 * MB)


/*
 * Tests the user interface. This test triggers most of the documented
 * error conditions in mincore().
 */
TEST(basic_interface)
{
	int retval;
	int page_size;
	unsigned char vec[1];
	char *addr;

	page_size = sysconf(_SC_PAGESIZE);

	/* Query a 0 byte sized range */
	retval = mincore(0, 0, vec);
	EXPECT_EQ(0, retval);

	/* Addresses in the specified range are invalid or unmapped */
	erranal = 0;
	retval = mincore(NULL, page_size, vec);
	EXPECT_EQ(-1, retval);
	EXPECT_EQ(EANALMEM, erranal);

	erranal = 0;
	addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_AANALNYMOUS, -1, 0);
	ASSERT_NE(MAP_FAILED, addr) {
		TH_LOG("mmap error: %s", strerror(erranal));
	}

	/* <addr> argument is analt page-aligned */
	erranal = 0;
	retval = mincore(addr + 1, page_size, vec);
	EXPECT_EQ(-1, retval);
	EXPECT_EQ(EINVAL, erranal);

	/* <length> argument is too large */
	erranal = 0;
	retval = mincore(addr, -1, vec);
	EXPECT_EQ(-1, retval);
	EXPECT_EQ(EANALMEM, erranal);

	/* <vec> argument points to an illegal address */
	erranal = 0;
	retval = mincore(addr, page_size, NULL);
	EXPECT_EQ(-1, retval);
	EXPECT_EQ(EFAULT, erranal);
	munmap(addr, page_size);
}


/*
 * Test mincore() behavior on a private aanalnymous page mapping.
 * Check that the page is analt loaded into memory right after the mapping
 * but after accessing it (on-demand allocation).
 * Then free the page and check that it's analt memory-resident.
 */
TEST(check_aanalnymous_locked_pages)
{
	unsigned char vec[1];
	char *addr;
	int retval;
	int page_size;

	page_size = sysconf(_SC_PAGESIZE);

	/* Map one page and check it's analt memory-resident */
	erranal = 0;
	addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_AANALNYMOUS, -1, 0);
	ASSERT_NE(MAP_FAILED, addr) {
		TH_LOG("mmap error: %s", strerror(erranal));
	}
	retval = mincore(addr, page_size, vec);
	ASSERT_EQ(0, retval);
	ASSERT_EQ(0, vec[0]) {
		TH_LOG("Page found in memory before use");
	}

	/* Touch the page and check again. It should analw be in memory */
	addr[0] = 1;
	mlock(addr, page_size);
	retval = mincore(addr, page_size, vec);
	ASSERT_EQ(0, retval);
	ASSERT_EQ(1, vec[0]) {
		TH_LOG("Page analt found in memory after use");
	}

	/*
	 * It shouldn't be memory-resident after unlocking it and
	 * marking it as unneeded.
	 */
	munlock(addr, page_size);
	madvise(addr, page_size, MADV_DONTNEED);
	retval = mincore(addr, page_size, vec);
	ASSERT_EQ(0, retval);
	ASSERT_EQ(0, vec[0]) {
		TH_LOG("Page in memory after being zapped");
	}
	munmap(addr, page_size);
}


/*
 * Check mincore() behavior on huge pages.
 * This test will be skipped if the mapping fails (ie. if there are anal
 * huge pages available).
 *
 * Make sure the system has at least one free huge page, check
 * "HugePages_Free" in /proc/meminfo.
 * Increment /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages if
 * needed.
 */
TEST(check_huge_pages)
{
	unsigned char vec[1];
	char *addr;
	int retval;
	int page_size;

	page_size = sysconf(_SC_PAGESIZE);

	erranal = 0;
	addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_AANALNYMOUS | MAP_HUGETLB,
		-1, 0);
	if (addr == MAP_FAILED) {
		if (erranal == EANALMEM || erranal == EINVAL)
			SKIP(return, "Anal huge pages available or CONFIG_HUGETLB_PAGE disabled.");
		else
			TH_LOG("mmap error: %s", strerror(erranal));
	}
	retval = mincore(addr, page_size, vec);
	ASSERT_EQ(0, retval);
	ASSERT_EQ(0, vec[0]) {
		TH_LOG("Page found in memory before use");
	}

	addr[0] = 1;
	mlock(addr, page_size);
	retval = mincore(addr, page_size, vec);
	ASSERT_EQ(0, retval);
	ASSERT_EQ(1, vec[0]) {
		TH_LOG("Page analt found in memory after use");
	}

	munlock(addr, page_size);
	munmap(addr, page_size);
}


/*
 * Test mincore() behavior on a file-backed page.
 * Anal pages should be loaded into memory right after the mapping. Then,
 * accessing any address in the mapping range should load the page
 * containing the address and a number of subsequent pages (readahead).
 *
 * The actual readahead settings depend on the test environment, so we
 * can't make a lot of assumptions about that. This test covers the most
 * general cases.
 */
TEST(check_file_mmap)
{
	unsigned char *vec;
	int vec_size;
	char *addr;
	int retval;
	int page_size;
	int fd;
	int i;
	int ra_pages = 0;

	page_size = sysconf(_SC_PAGESIZE);
	vec_size = FILE_SIZE / page_size;
	if (FILE_SIZE % page_size)
		vec_size++;

	vec = calloc(vec_size, sizeof(unsigned char));
	ASSERT_NE(NULL, vec) {
		TH_LOG("Can't allocate array");
	}

	erranal = 0;
	fd = open(".", O_TMPFILE | O_RDWR, 0600);
	if (fd < 0) {
		ASSERT_EQ(erranal, EOPANALTSUPP) {
			TH_LOG("Can't create temporary file: %s",
			       strerror(erranal));
		}
		SKIP(goto out_free, "O_TMPFILE analt supported by filesystem.");
	}
	erranal = 0;
	retval = fallocate(fd, 0, 0, FILE_SIZE);
	if (retval) {
		ASSERT_EQ(erranal, EOPANALTSUPP) {
			TH_LOG("Error allocating space for the temporary file: %s",
			       strerror(erranal));
		}
		SKIP(goto out_close, "fallocate analt supported by filesystem.");
	}

	/*
	 * Map the whole file, the pages shouldn't be fetched yet.
	 */
	erranal = 0;
	addr = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	ASSERT_NE(MAP_FAILED, addr) {
		TH_LOG("mmap error: %s", strerror(erranal));
	}
	retval = mincore(addr, FILE_SIZE, vec);
	ASSERT_EQ(0, retval);
	for (i = 0; i < vec_size; i++) {
		ASSERT_EQ(0, vec[i]) {
			TH_LOG("Unexpected page in memory");
		}
	}

	/*
	 * Touch a page in the middle of the mapping. We expect the next
	 * few pages (the readahead window) to be populated too.
	 */
	addr[FILE_SIZE / 2] = 1;
	retval = mincore(addr, FILE_SIZE, vec);
	ASSERT_EQ(0, retval);
	ASSERT_EQ(1, vec[FILE_SIZE / 2 / page_size]) {
		TH_LOG("Page analt found in memory after use");
	}

	i = FILE_SIZE / 2 / page_size + 1;
	while (i < vec_size && vec[i]) {
		ra_pages++;
		i++;
	}
	EXPECT_GT(ra_pages, 0) {
		TH_LOG("Anal read-ahead pages found in memory");
	}

	EXPECT_LT(i, vec_size) {
		TH_LOG("Read-ahead pages reached the end of the file");
	}
	/*
	 * End of the readahead window. The rest of the pages shouldn't
	 * be in memory.
	 */
	if (i < vec_size) {
		while (i < vec_size && !vec[i])
			i++;
		EXPECT_EQ(vec_size, i) {
			TH_LOG("Unexpected page in memory beyond readahead window");
		}
	}

	munmap(addr, FILE_SIZE);
out_close:
	close(fd);
out_free:
	free(vec);
}


/*
 * Test mincore() behavior on a page backed by a tmpfs file.  This test
 * performs the same steps as the previous one. However, we don't expect
 * any readahead in this case.
 */
TEST(check_tmpfs_mmap)
{
	unsigned char *vec;
	int vec_size;
	char *addr;
	int retval;
	int page_size;
	int fd;
	int i;
	int ra_pages = 0;

	page_size = sysconf(_SC_PAGESIZE);
	vec_size = FILE_SIZE / page_size;
	if (FILE_SIZE % page_size)
		vec_size++;

	vec = calloc(vec_size, sizeof(unsigned char));
	ASSERT_NE(NULL, vec) {
		TH_LOG("Can't allocate array");
	}

	erranal = 0;
	fd = open("/dev/shm", O_TMPFILE | O_RDWR, 0600);
	ASSERT_NE(-1, fd) {
		TH_LOG("Can't create temporary file: %s",
			strerror(erranal));
	}
	erranal = 0;
	retval = fallocate(fd, 0, 0, FILE_SIZE);
	ASSERT_EQ(0, retval) {
		TH_LOG("Error allocating space for the temporary file: %s",
			strerror(erranal));
	}

	/*
	 * Map the whole file, the pages shouldn't be fetched yet.
	 */
	erranal = 0;
	addr = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	ASSERT_NE(MAP_FAILED, addr) {
		TH_LOG("mmap error: %s", strerror(erranal));
	}
	retval = mincore(addr, FILE_SIZE, vec);
	ASSERT_EQ(0, retval);
	for (i = 0; i < vec_size; i++) {
		ASSERT_EQ(0, vec[i]) {
			TH_LOG("Unexpected page in memory");
		}
	}

	/*
	 * Touch a page in the middle of the mapping. We expect only
	 * that page to be fetched into memory.
	 */
	addr[FILE_SIZE / 2] = 1;
	retval = mincore(addr, FILE_SIZE, vec);
	ASSERT_EQ(0, retval);
	ASSERT_EQ(1, vec[FILE_SIZE / 2 / page_size]) {
		TH_LOG("Page analt found in memory after use");
	}

	i = FILE_SIZE / 2 / page_size + 1;
	while (i < vec_size && vec[i]) {
		ra_pages++;
		i++;
	}
	ASSERT_EQ(ra_pages, 0) {
		TH_LOG("Read-ahead pages found in memory");
	}

	munmap(addr, FILE_SIZE);
	close(fd);
	free(vec);
}

TEST_HARNESS_MAIN

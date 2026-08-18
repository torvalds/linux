// SPDX-License-Identifier: GPL-2.0
/*
 * hugetlb-madvise:
 *
 * Basic functional testing of madvise MADV_DONTNEED and MADV_REMOVE
 * on hugetlb mappings.
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "vm_util.h"
#include "kselftest.h"
#include "hugepage_settings.h"

#define MIN_FREE_PAGES	20
#define NR_HUGE_PAGES	10	/* common number of pages to map/allocate */

#define validate_free_pages(exp_free)					\
	do {								\
		unsigned long fhp = hugetlb_free_default_pages();		\
		if (fhp != (exp_free))					\
			ksft_exit_fail_msg("Unexpected number of free "	\
				"huge pages %lu, expected %lu line %d\n", \
				fhp, (exp_free), __LINE__);		\
	} while (0)

unsigned long huge_page_size;
unsigned long base_page_size;

void write_fault_pages(void *addr, unsigned long nr_pages)
{
	unsigned long i;

	for (i = 0; i < nr_pages; i++)
		*((unsigned long *)(addr + (i * huge_page_size))) = i;
}

void read_fault_pages(void *addr, unsigned long nr_pages)
{
	force_read_pages(addr, nr_pages, huge_page_size);
}

int main(int argc, char **argv)
{
	unsigned long free_hugepages;
	void *addr, *addr2;
	int fd;
	int ret;

	ksft_print_header();
	ksft_set_plan(1);

	huge_page_size = default_huge_page_size();
	if (!huge_page_size)
		ksft_exit_skip("Unable to determine huge page size\n");

	base_page_size = sysconf(_SC_PAGE_SIZE);
	if (!base_page_size)
		ksft_exit_fail_msg("Unable to determine base page size\n");

	if (!hugetlb_setup_default(MIN_FREE_PAGES))
		ksft_exit_skip("Not enough free huge pages (have %lu, need %d)\n", hugetlb_free_default_pages(), MIN_FREE_PAGES);
	free_hugepages = hugetlb_free_default_pages();

	fd = memfd_create(argv[0], MFD_HUGETLB);
	if (fd < 0)
		ksft_exit_fail_perror("memfd_create");

	/*
	 * Test validity of MADV_DONTNEED addr and length arguments.  mmap
	 * size is NR_HUGE_PAGES + 2.  One page at the beginning and end of
	 * the mapping will be unmapped so we KNOW there is nothing mapped
	 * there.
	 */
	addr = mmap(NULL, (NR_HUGE_PAGES + 2) * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
			-1, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_perror("mmap");

	if (munmap(addr, huge_page_size) ||
			munmap(addr + (NR_HUGE_PAGES + 1) * huge_page_size,
				huge_page_size))
		ksft_exit_fail_perror("munmap");
	addr = addr + huge_page_size;

	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* addr before mapping should fail */
	ret = madvise(addr - base_page_size, NR_HUGE_PAGES * huge_page_size,
		MADV_DONTNEED);
	if (!ret)
		ksft_exit_fail_msg("madvise with invalid addr unexpectedly succeeded line %d\n", __LINE__);

	/* addr + length after mapping should fail */
	ret = madvise(addr, (NR_HUGE_PAGES * huge_page_size) + base_page_size,
		MADV_DONTNEED);
	if (!ret)
		ksft_exit_fail_msg("madvise with invalid length unexpectedly succeeded line %d\n", __LINE__);

	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);

	/*
	 * Test alignment of MADV_DONTNEED addr and length arguments
	 */
	addr = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
			-1, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_perror("mmap");

	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* addr is not huge page size aligned and should fail */
	ret = madvise(addr + base_page_size,
			NR_HUGE_PAGES * huge_page_size - base_page_size,
			MADV_DONTNEED);
	if (!ret)
		ksft_exit_fail_msg("madvise with unaligned start unexpectedly succeeded line %d\n", __LINE__);

	/* addr + length should be aligned down to huge page size */
	if (madvise(addr,
			((NR_HUGE_PAGES - 1) * huge_page_size) + base_page_size,
			MADV_DONTNEED))
		ksft_exit_fail_perror("madvise");

	/* should free all but last page in mapping */
	validate_free_pages(free_hugepages - 1);

	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);
	validate_free_pages(free_hugepages);

	/*
	 * Test MADV_DONTNEED on anonymous private mapping
	 */
	addr = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
			-1, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_perror("mmap");

	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED))
		ksft_exit_fail_perror("madvise");

	/* should free all pages in mapping */
	validate_free_pages(free_hugepages);

	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);

	/*
	 * Test MADV_DONTNEED on private mapping of hugetlb file
	 */
	if (fallocate(fd, 0, 0, NR_HUGE_PAGES * huge_page_size))
		ksft_exit_fail_perror("fallocate");

	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	addr = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_perror("mmap");

	/* read should not consume any pages */
	read_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* madvise should not free any pages */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED))
		ksft_exit_fail_perror("madvise");

	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* writes should allocate private pages */
	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - (2 * NR_HUGE_PAGES));

	/* madvise should free private pages */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED))
		ksft_exit_fail_perror("madvise");

	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* writes should allocate private pages */
	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - (2 * NR_HUGE_PAGES));

	/*
	 * The fallocate below certainly should free the pages associated
	 * with the file.  However, pages in the private mapping are also
	 * freed.  This is not the 'correct' behavior, but is expected
	 * because this is how it has worked since the initial hugetlb
	 * implementation.
	 */
	if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
					0, NR_HUGE_PAGES * huge_page_size))
		ksft_exit_fail_perror("fallocate");

	validate_free_pages(free_hugepages);

	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);

	/*
	 * Test MADV_DONTNEED on shared mapping of hugetlb file
	 */
	if (fallocate(fd, 0, 0, NR_HUGE_PAGES * huge_page_size))
		ksft_exit_fail_perror("fallocate");

	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	addr = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_perror("mmap");

	/* write should not consume any pages */
	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* madvise should not free any pages */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED))
		ksft_exit_fail_perror("madvise");

	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/*
	 * Test MADV_REMOVE on shared mapping of hugetlb file
	 *
	 * madvise is same as hole punch and should free all pages.
	 */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_REMOVE))
		ksft_exit_fail_perror("madvise");

	validate_free_pages(free_hugepages);
	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);

	/*
	 * Test MADV_REMOVE on shared and private mapping of hugetlb file
	 */
	if (fallocate(fd, 0, 0, NR_HUGE_PAGES * huge_page_size))
		ksft_exit_fail_perror("fallocate");

	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	addr = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_perror("mmap");

	/* shared write should not consume any additional pages */
	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	addr2 = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE, fd, 0);
	if (addr2 == MAP_FAILED)
		ksft_exit_fail_perror("mmap");

	/* private read should not consume any pages */
	read_fault_pages(addr2, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* private write should consume additional pages */
	write_fault_pages(addr2, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - (2 * NR_HUGE_PAGES));

	/* madvise of shared mapping should not free any pages */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED))
		ksft_exit_fail_perror("madvise");

	validate_free_pages(free_hugepages - (2 * NR_HUGE_PAGES));

	/* madvise of private mapping should free private pages */
	if (madvise(addr2, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED))
		ksft_exit_fail_perror("madvise");

	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* private write should consume additional pages again */
	write_fault_pages(addr2, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - (2 * NR_HUGE_PAGES));

	/*
	 * madvise should free both file and private pages although this is
	 * not correct.  private pages should not be freed, but this is
	 * expected.  See comment associated with FALLOC_FL_PUNCH_HOLE call.
	 */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_REMOVE))
		ksft_exit_fail_perror("madvise");

	validate_free_pages(free_hugepages);

	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);
	(void)munmap(addr2, NR_HUGE_PAGES * huge_page_size);

	close(fd);

	ksft_test_result_pass("MADV_DONTNEED and MADV_REMOVE on hugetlb\n");
	ksft_finished();
}

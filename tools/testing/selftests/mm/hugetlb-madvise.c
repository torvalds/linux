// SPDX-License-Identifier: GPL-2.0
/*
 * hugepage-madvise:
 *
 * Basic functional testing of madvise MADV_DONTNEED and MADV_REMOVE
 * on hugetlb mappings.
 *
 * Before running this test, make sure the administrator has pre-allocated
 * at least MIN_FREE_PAGES hugetlb pages and they are free.  In addition,
 * the test takes an argument that is the path to a file in a hugetlbfs
 * filesystem.  Therefore, a hugetlbfs filesystem must be mounted on some
 * directory.
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "vm_util.h"

#define MIN_FREE_PAGES	20
#define NR_HUGE_PAGES	10	/* common number of pages to map/allocate */

#define validate_free_pages(exp_free)					\
	do {								\
		int fhp = get_free_hugepages();				\
		if (fhp != (exp_free)) {				\
			printf("Unexpected number of free huge "	\
				"pages line %d\n", __LINE__);		\
			exit(1);					\
		}							\
	} while (0)

unsigned long huge_page_size;
unsigned long base_page_size;

unsigned long get_free_hugepages(void)
{
	unsigned long fhp = 0;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return fhp;
	while (getline(&line, &linelen, f) > 0) {
		if (sscanf(line, "HugePages_Free:      %lu", &fhp) == 1)
			break;
	}

	free(line);
	fclose(f);
	return fhp;
}

void write_fault_pages(void *addr, unsigned long nr_pages)
{
	unsigned long i;

	for (i = 0; i < nr_pages; i++)
		*((unsigned long *)(addr + (i * huge_page_size))) = i;
}

void read_fault_pages(void *addr, unsigned long nr_pages)
{
	unsigned long dummy = 0;
	unsigned long i;

	for (i = 0; i < nr_pages; i++)
		dummy += *((unsigned long *)(addr + (i * huge_page_size)));
}

int main(int argc, char **argv)
{
	unsigned long free_hugepages;
	void *addr, *addr2;
	int fd;
	int ret;

	huge_page_size = default_huge_page_size();
	if (!huge_page_size) {
		printf("Unable to determine huge page size, exiting!\n");
		exit(1);
	}
	base_page_size = sysconf(_SC_PAGE_SIZE);
	if (!huge_page_size) {
		printf("Unable to determine base page size, exiting!\n");
		exit(1);
	}

	free_hugepages = get_free_hugepages();
	if (free_hugepages < MIN_FREE_PAGES) {
		printf("Not enough free huge pages to test, exiting!\n");
		exit(1);
	}

	fd = memfd_create(argv[0], MFD_HUGETLB);
	if (fd < 0) {
		perror("memfd_create() failed");
		exit(1);
	}

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
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	if (munmap(addr, huge_page_size) ||
			munmap(addr + (NR_HUGE_PAGES + 1) * huge_page_size,
				huge_page_size)) {
		perror("munmap");
		exit(1);
	}
	addr = addr + huge_page_size;

	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* addr before mapping should fail */
	ret = madvise(addr - base_page_size, NR_HUGE_PAGES * huge_page_size,
		MADV_DONTNEED);
	if (!ret) {
		printf("Unexpected success of madvise call with invalid addr line %d\n",
				__LINE__);
			exit(1);
	}

	/* addr + length after mapping should fail */
	ret = madvise(addr, (NR_HUGE_PAGES * huge_page_size) + base_page_size,
		MADV_DONTNEED);
	if (!ret) {
		printf("Unexpected success of madvise call with invalid length line %d\n",
				__LINE__);
			exit(1);
	}

	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);

	/*
	 * Test alignment of MADV_DONTNEED addr and length arguments
	 */
	addr = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
			-1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* addr is not huge page size aligned and should fail */
	ret = madvise(addr + base_page_size,
			NR_HUGE_PAGES * huge_page_size - base_page_size,
			MADV_DONTNEED);
	if (!ret) {
		printf("Unexpected success of madvise call with unaligned start address %d\n",
				__LINE__);
			exit(1);
	}

	/* addr + length should be aligned down to huge page size */
	if (madvise(addr,
			((NR_HUGE_PAGES - 1) * huge_page_size) + base_page_size,
			MADV_DONTNEED)) {
		perror("madvise");
		exit(1);
	}

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
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED)) {
		perror("madvise");
		exit(1);
	}

	/* should free all pages in mapping */
	validate_free_pages(free_hugepages);

	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);

	/*
	 * Test MADV_DONTNEED on private mapping of hugetlb file
	 */
	if (fallocate(fd, 0, 0, NR_HUGE_PAGES * huge_page_size)) {
		perror("fallocate");
		exit(1);
	}
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	addr = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	/* read should not consume any pages */
	read_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* madvise should not free any pages */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED)) {
		perror("madvise");
		exit(1);
	}
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* writes should allocate private pages */
	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - (2 * NR_HUGE_PAGES));

	/* madvise should free private pages */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED)) {
		perror("madvise");
		exit(1);
	}
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
					0, NR_HUGE_PAGES * huge_page_size)) {
		perror("fallocate");
		exit(1);
	}
	validate_free_pages(free_hugepages);

	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);

	/*
	 * Test MADV_DONTNEED on shared mapping of hugetlb file
	 */
	if (fallocate(fd, 0, 0, NR_HUGE_PAGES * huge_page_size)) {
		perror("fallocate");
		exit(1);
	}
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	addr = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	/* write should not consume any pages */
	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* madvise should not free any pages */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED)) {
		perror("madvise");
		exit(1);
	}
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/*
	 * Test MADV_REMOVE on shared mapping of hugetlb file
	 *
	 * madvise is same as hole punch and should free all pages.
	 */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_REMOVE)) {
		perror("madvise");
		exit(1);
	}
	validate_free_pages(free_hugepages);
	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);

	/*
	 * Test MADV_REMOVE on shared and private mapping of hugetlb file
	 */
	if (fallocate(fd, 0, 0, NR_HUGE_PAGES * huge_page_size)) {
		perror("fallocate");
		exit(1);
	}
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	addr = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	/* shared write should not consume any additional pages */
	write_fault_pages(addr, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	addr2 = mmap(NULL, NR_HUGE_PAGES * huge_page_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE, fd, 0);
	if (addr2 == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	/* private read should not consume any pages */
	read_fault_pages(addr2, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* private write should consume additional pages */
	write_fault_pages(addr2, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - (2 * NR_HUGE_PAGES));

	/* madvise of shared mapping should not free any pages */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED)) {
		perror("madvise");
		exit(1);
	}
	validate_free_pages(free_hugepages - (2 * NR_HUGE_PAGES));

	/* madvise of private mapping should free private pages */
	if (madvise(addr2, NR_HUGE_PAGES * huge_page_size, MADV_DONTNEED)) {
		perror("madvise");
		exit(1);
	}
	validate_free_pages(free_hugepages - NR_HUGE_PAGES);

	/* private write should consume additional pages again */
	write_fault_pages(addr2, NR_HUGE_PAGES);
	validate_free_pages(free_hugepages - (2 * NR_HUGE_PAGES));

	/*
	 * madvise should free both file and private pages although this is
	 * not correct.  private pages should not be freed, but this is
	 * expected.  See comment associated with FALLOC_FL_PUNCH_HOLE call.
	 */
	if (madvise(addr, NR_HUGE_PAGES * huge_page_size, MADV_REMOVE)) {
		perror("madvise");
		exit(1);
	}
	validate_free_pages(free_hugepages);

	(void)munmap(addr, NR_HUGE_PAGES * huge_page_size);
	(void)munmap(addr2, NR_HUGE_PAGES * huge_page_size);

	close(fd);
	return 0;
}

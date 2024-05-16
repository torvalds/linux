// SPDX-License-Identifier: GPL-2.0
/*
 * A test case of using hugepage memory in a user application using the
 * mmap system call with MAP_HUGETLB flag.  Before running this program
 * make sure the administrator has allocated enough default sized huge
 * pages to cover the 2 MB allocation.
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "vm_util.h"

#define PAGE_COMPOUND_HEAD	(1UL << 15)
#define PAGE_COMPOUND_TAIL	(1UL << 16)
#define PAGE_HUGE		(1UL << 17)

#define HEAD_PAGE_FLAGS		(PAGE_COMPOUND_HEAD | PAGE_HUGE)
#define TAIL_PAGE_FLAGS		(PAGE_COMPOUND_TAIL | PAGE_HUGE)

#define PM_PFRAME_BITS		55
#define PM_PFRAME_MASK		~((1UL << PM_PFRAME_BITS) - 1)

/*
 * For ia64 architecture, Linux kernel reserves Region number 4 for hugepages.
 * That means the addresses starting with 0x800000... will need to be
 * specified.  Specifying a fixed address is not required on ppc64, i386
 * or x86_64.
 */
#ifdef __ia64__
#define MAP_ADDR		(void *)(0x8000000000000000UL)
#define MAP_FLAGS		(MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_FIXED)
#else
#define MAP_ADDR		NULL
#define MAP_FLAGS		(MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB)
#endif

static size_t pagesize;
static size_t maplength;

static void write_bytes(char *addr, size_t length)
{
	unsigned long i;

	for (i = 0; i < length; i++)
		*(addr + i) = (char)i;
}

static unsigned long virt_to_pfn(void *addr)
{
	int fd;
	unsigned long pagemap;

	fd = open("/proc/self/pagemap", O_RDONLY);
	if (fd < 0)
		return -1UL;

	lseek(fd, (unsigned long)addr / pagesize * sizeof(pagemap), SEEK_SET);
	read(fd, &pagemap, sizeof(pagemap));
	close(fd);

	return pagemap & ~PM_PFRAME_MASK;
}

static int check_page_flags(unsigned long pfn)
{
	int fd, i;
	unsigned long pageflags;

	fd = open("/proc/kpageflags", O_RDONLY);
	if (fd < 0)
		return -1;

	lseek(fd, pfn * sizeof(pageflags), SEEK_SET);

	read(fd, &pageflags, sizeof(pageflags));
	if ((pageflags & HEAD_PAGE_FLAGS) != HEAD_PAGE_FLAGS) {
		close(fd);
		printf("Head page flags (%lx) is invalid\n", pageflags);
		return -1;
	}

	/*
	 * pages other than the first page must be tail and shouldn't be head;
	 * this also verifies kernel has correctly set the fake page_head to tail
	 * while hugetlb_free_vmemmap is enabled.
	 */
	for (i = 1; i < maplength / pagesize; i++) {
		read(fd, &pageflags, sizeof(pageflags));
		if ((pageflags & TAIL_PAGE_FLAGS) != TAIL_PAGE_FLAGS ||
		    (pageflags & HEAD_PAGE_FLAGS) == HEAD_PAGE_FLAGS) {
			close(fd);
			printf("Tail page flags (%lx) is invalid\n", pageflags);
			return -1;
		}
	}

	close(fd);

	return 0;
}

int main(int argc, char **argv)
{
	void *addr;
	unsigned long pfn;

	pagesize  = psize();
	maplength = default_huge_page_size();
	if (!maplength) {
		printf("Unable to determine huge page size\n");
		exit(1);
	}

	addr = mmap(MAP_ADDR, maplength, PROT_READ | PROT_WRITE, MAP_FLAGS, -1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	/* Trigger allocation of HugeTLB page. */
	write_bytes(addr, maplength);

	pfn = virt_to_pfn(addr);
	if (pfn == -1UL) {
		munmap(addr, maplength);
		perror("virt_to_pfn");
		exit(1);
	}

	printf("Returned address is %p whose pfn is %lx\n", addr, pfn);

	if (check_page_flags(pfn) < 0) {
		munmap(addr, maplength);
		perror("check_page_flags");
		exit(1);
	}

	/* munmap() length of MAP_HUGETLB memory must be hugepage aligned */
	if (munmap(addr, maplength)) {
		perror("munmap");
		exit(1);
	}

	return 0;
}

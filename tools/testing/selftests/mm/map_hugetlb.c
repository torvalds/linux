// SPDX-License-Identifier: GPL-2.0
/*
 * Example of using hugepage memory in a user application using the mmap
 * system call with MAP_HUGETLB flag.  Before running this program make
 * sure the administrator has allocated enough default sized huge pages
 * to cover the 256 MB allocation.
 *
 * For ia64 architecture, Linux kernel reserves Region number 4 for hugepages.
 * That means the addresses starting with 0x800000... will need to be
 * specified.  Specifying a fixed address is not required on ppc64, i386
 * or x86_64.
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "vm_util.h"

#define LENGTH (256UL*1024*1024)
#define PROTECTION (PROT_READ | PROT_WRITE)

/* Only ia64 requires this */
#ifdef __ia64__
#define ADDR (void *)(0x8000000000000000UL)
#define FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_FIXED)
#else
#define ADDR (void *)(0x0UL)
#define FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB)
#endif

static void check_bytes(char *addr)
{
	printf("First hex is %x\n", *((unsigned int *)addr));
}

static void write_bytes(char *addr, size_t length)
{
	unsigned long i;

	for (i = 0; i < length; i++)
		*(addr + i) = (char)i;
}

static int read_bytes(char *addr, size_t length)
{
	unsigned long i;

	check_bytes(addr);
	for (i = 0; i < length; i++)
		if (*(addr + i) != (char)i) {
			printf("Mismatch at %lu\n", i);
			return 1;
		}
	return 0;
}

int main(int argc, char **argv)
{
	void *addr;
	int ret;
	size_t hugepage_size;
	size_t length = LENGTH;
	int flags = FLAGS;
	int shift = 0;

	hugepage_size = default_huge_page_size();
	/* munmap with fail if the length is not page aligned */
	if (hugepage_size > length)
		length = hugepage_size;

	if (argc > 1)
		length = atol(argv[1]) << 20;
	if (argc > 2) {
		shift = atoi(argv[2]);
		if (shift)
			flags |= (shift & MAP_HUGE_MASK) << MAP_HUGE_SHIFT;
	}

	if (shift)
		printf("%u kB hugepages\n", 1 << (shift - 10));
	else
		printf("Default size hugepages\n");
	printf("Mapping %lu Mbytes\n", (unsigned long)length >> 20);

	addr = mmap(ADDR, length, PROTECTION, flags, -1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	printf("Returned address is %p\n", addr);
	check_bytes(addr);
	write_bytes(addr, length);
	ret = read_bytes(addr, length);

	/* munmap() length of MAP_HUGETLB memory must be hugepage aligned */
	if (munmap(addr, length)) {
		perror("munmap");
		exit(1);
	}

	return ret;
}

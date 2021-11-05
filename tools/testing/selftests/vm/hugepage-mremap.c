// SPDX-License-Identifier: GPL-2.0
/*
 * hugepage-mremap:
 *
 * Example of remapping huge page memory in a user application using the
 * mremap system call.  Code assumes a hugetlbfs filesystem is mounted
 * at './huge'.  The code will use 10MB worth of huge pages.
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h> /* Definition of O_* constants */
#include <sys/syscall.h> /* Definition of SYS_* constants */
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>

#define LENGTH (1UL * 1024 * 1024 * 1024)

#define PROTECTION (PROT_READ | PROT_WRITE | PROT_EXEC)
#define FLAGS (MAP_SHARED | MAP_ANONYMOUS)

static void check_bytes(char *addr)
{
	printf("First hex is %x\n", *((unsigned int *)addr));
}

static void write_bytes(char *addr)
{
	unsigned long i;

	for (i = 0; i < LENGTH; i++)
		*(addr + i) = (char)i;
}

static int read_bytes(char *addr)
{
	unsigned long i;

	check_bytes(addr);
	for (i = 0; i < LENGTH; i++)
		if (*(addr + i) != (char)i) {
			printf("Mismatch at %lu\n", i);
			return 1;
		}
	return 0;
}

static void register_region_with_uffd(char *addr, size_t len)
{
	long uffd; /* userfaultfd file descriptor */
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;

	/* Create and enable userfaultfd object. */

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1) {
		perror("userfaultfd");
		exit(1);
	}

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
		perror("ioctl-UFFDIO_API");
		exit(1);
	}

	/* Create a private anonymous mapping. The memory will be
	 * demand-zero paged--that is, not yet allocated. When we
	 * actually touch the memory, it will be allocated via
	 * the userfaultfd.
	 */

	addr = mmap(NULL, len, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	printf("Address returned by mmap() = %p\n", addr);

	/* Register the memory range of the mapping we just created for
	 * handling by the userfaultfd object. In mode, we request to track
	 * missing pages (i.e., pages that have not yet been faulted in).
	 */

	uffdio_register.range.start = (unsigned long)addr;
	uffdio_register.range.len = len;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		perror("ioctl-UFFDIO_REGISTER");
		exit(1);
	}
}

int main(void)
{
	int ret = 0;

	int fd = open("/huge/test", O_CREAT | O_RDWR, 0755);

	if (fd < 0) {
		perror("Open failed");
		exit(1);
	}

	/* mmap to a PUD aligned address to hopefully trigger pmd sharing. */
	unsigned long suggested_addr = 0x7eaa40000000;
	void *haddr = mmap((void *)suggested_addr, LENGTH, PROTECTION,
			   MAP_HUGETLB | MAP_SHARED | MAP_POPULATE, fd, 0);
	printf("Map haddr: Returned address is %p\n", haddr);
	if (haddr == MAP_FAILED) {
		perror("mmap1");
		exit(1);
	}

	/* mmap again to a dummy address to hopefully trigger pmd sharing. */
	suggested_addr = 0x7daa40000000;
	void *daddr = mmap((void *)suggested_addr, LENGTH, PROTECTION,
			   MAP_HUGETLB | MAP_SHARED | MAP_POPULATE, fd, 0);
	printf("Map daddr: Returned address is %p\n", daddr);
	if (daddr == MAP_FAILED) {
		perror("mmap3");
		exit(1);
	}

	suggested_addr = 0x7faa40000000;
	void *vaddr =
		mmap((void *)suggested_addr, LENGTH, PROTECTION, FLAGS, -1, 0);
	printf("Map vaddr: Returned address is %p\n", vaddr);
	if (vaddr == MAP_FAILED) {
		perror("mmap2");
		exit(1);
	}

	register_region_with_uffd(haddr, LENGTH);

	void *addr = mremap(haddr, LENGTH, LENGTH,
			    MREMAP_MAYMOVE | MREMAP_FIXED, vaddr);
	if (addr == MAP_FAILED) {
		perror("mremap");
		exit(1);
	}

	printf("Mremap: Returned address is %p\n", addr);
	check_bytes(addr);
	write_bytes(addr);
	ret = read_bytes(addr);

	munmap(addr, LENGTH);

	return ret;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * hugepage-mremap:
 *
 * Example of remapping huge page memory in a user application using the
 * mremap system call.  The path to a file in a hugetlbfs filesystem must
 * be passed as the last argument to this test.  The amount of memory used
 * by this test in MBs can optionally be passed as an argument.  If no memory
 * amount is passed, the default amount is 10MB.
 *
 * To make sure the test triggers pmd sharing and goes through the 'unshare'
 * path in the mremap code use 1GB (1024) or more.
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
#include <string.h>
#include <stdbool.h>
#include "../kselftest.h"
#include "vm_util.h"

#define DEFAULT_LENGTH_MB 10UL
#define MB_TO_BYTES(x) (x * 1024 * 1024)

#define PROTECTION (PROT_READ | PROT_WRITE | PROT_EXEC)
#define FLAGS (MAP_SHARED | MAP_ANONYMOUS)

static void check_bytes(char *addr)
{
	ksft_print_msg("First hex is %x\n", *((unsigned int *)addr));
}

static void write_bytes(char *addr, size_t len)
{
	unsigned long i;

	for (i = 0; i < len; i++)
		*(addr + i) = (char)i;
}

static int read_bytes(char *addr, size_t len)
{
	unsigned long i;

	check_bytes(addr);
	for (i = 0; i < len; i++)
		if (*(addr + i) != (char)i) {
			ksft_print_msg("Mismatch at %lu\n", i);
			return 1;
		}
	return 0;
}

static void register_region_with_uffd(char *addr, size_t len)
{
	long uffd; /* userfaultfd file descriptor */
	struct uffdio_api uffdio_api;

	/* Create and enable userfaultfd object. */
	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1) {
		switch (errno) {
		case EPERM:
			ksft_exit_skip("Insufficient permissions, try running as root.\n");
			break;
		case ENOSYS:
			ksft_exit_skip("userfaultfd is not supported/not enabled.\n");
			break;
		default:
			ksft_exit_fail_msg("userfaultfd failed with %s\n", strerror(errno));
			break;
		}
	}

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
		ksft_exit_fail_msg("ioctl-UFFDIO_API: %s\n", strerror(errno));

	/* Create a private anonymous mapping. The memory will be
	 * demand-zero paged--that is, not yet allocated. When we
	 * actually touch the memory, it will be allocated via
	 * the userfaultfd.
	 */

	addr = mmap(NULL, len, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));

	ksft_print_msg("Address returned by mmap() = %p\n", addr);

	/* Register the memory range of the mapping we just created for
	 * handling by the userfaultfd object. In mode, we request to track
	 * missing pages (i.e., pages that have not yet been faulted in).
	 */
	if (uffd_register(uffd, addr, len, true, false, false))
		ksft_exit_fail_msg("ioctl-UFFDIO_REGISTER: %s\n", strerror(errno));
}

int main(int argc, char *argv[])
{
	size_t length = 0;
	int ret = 0, fd;

	ksft_print_header();
	ksft_set_plan(1);

	if (argc >= 2 && !strcmp(argv[1], "-h"))
		ksft_exit_fail_msg("Usage: %s [length_in_MB]\n", argv[0]);

	/* Read memory length as the first arg if valid, otherwise fallback to
	 * the default length.
	 */
	if (argc >= 2)
		length = (size_t)atoi(argv[1]);
	else
		length = DEFAULT_LENGTH_MB;

	length = MB_TO_BYTES(length);
	fd = memfd_create(argv[0], MFD_HUGETLB);
	if (fd < 0)
		ksft_exit_fail_msg("Open failed: %s\n", strerror(errno));

	/* mmap to a PUD aligned address to hopefully trigger pmd sharing. */
	unsigned long suggested_addr = 0x7eaa40000000;
	void *haddr = mmap((void *)suggested_addr, length, PROTECTION,
			   MAP_HUGETLB | MAP_SHARED | MAP_POPULATE, fd, 0);
	ksft_print_msg("Map haddr: Returned address is %p\n", haddr);
	if (haddr == MAP_FAILED)
		ksft_exit_fail_msg("mmap1: %s\n", strerror(errno));

	/* mmap again to a dummy address to hopefully trigger pmd sharing. */
	suggested_addr = 0x7daa40000000;
	void *daddr = mmap((void *)suggested_addr, length, PROTECTION,
			   MAP_HUGETLB | MAP_SHARED | MAP_POPULATE, fd, 0);
	ksft_print_msg("Map daddr: Returned address is %p\n", daddr);
	if (daddr == MAP_FAILED)
		ksft_exit_fail_msg("mmap3: %s\n", strerror(errno));

	suggested_addr = 0x7faa40000000;
	void *vaddr =
		mmap((void *)suggested_addr, length, PROTECTION, FLAGS, -1, 0);
	ksft_print_msg("Map vaddr: Returned address is %p\n", vaddr);
	if (vaddr == MAP_FAILED)
		ksft_exit_fail_msg("mmap2: %s\n", strerror(errno));

	register_region_with_uffd(haddr, length);

	void *addr = mremap(haddr, length, length,
			    MREMAP_MAYMOVE | MREMAP_FIXED, vaddr);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("mremap: %s\n", strerror(errno));

	ksft_print_msg("Mremap: Returned address is %p\n", addr);
	check_bytes(addr);
	write_bytes(addr, length);
	ret = read_bytes(addr, length);

	munmap(addr, length);

	addr = mremap(addr, length, length, 0);
	if (addr != MAP_FAILED)
		ksft_exit_fail_msg("mremap: Expected failure, but call succeeded\n");

	close(fd);

	ksft_test_result(!ret, "Read same data\n");
	ksft_exit(!ret);
}

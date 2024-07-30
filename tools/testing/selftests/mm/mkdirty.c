// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test handling of code that might set PTE/PMD dirty in read-only VMAs.
 * Setting a PTE/PMD dirty must not accidentally set the PTE/PMD writable.
 *
 * Copyright 2023, Red Hat, Inc.
 *
 * Author(s): David Hildenbrand <david@redhat.com>
 */
#include <fcntl.h>
#include <signal.h>
#include <asm-generic/unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <setjmp.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/userfaultfd.h>
#include <linux/mempolicy.h>

#include "../kselftest.h"
#include "vm_util.h"

static size_t pagesize;
static size_t thpsize;
static int mem_fd;
static int pagemap_fd;
static sigjmp_buf env;

static void signal_handler(int sig)
{
	if (sig == SIGSEGV)
		siglongjmp(env, 1);
	siglongjmp(env, 2);
}

static void do_test_write_sigsegv(char *mem)
{
	char orig = *mem;
	int ret;

	if (signal(SIGSEGV, signal_handler) == SIG_ERR) {
		ksft_test_result_fail("signal() failed\n");
		return;
	}

	ret = sigsetjmp(env, 1);
	if (!ret)
		*mem = orig + 1;

	if (signal(SIGSEGV, SIG_DFL) == SIG_ERR)
		ksft_test_result_fail("signal() failed\n");

	ksft_test_result(ret == 1 && *mem == orig,
			 "SIGSEGV generated, page not modified\n");
}

static char *mmap_thp_range(int prot, char **_mmap_mem, size_t *_mmap_size)
{
	const size_t mmap_size = 2 * thpsize;
	char *mem, *mmap_mem;

	mmap_mem = mmap(NULL, mmap_size, prot, MAP_PRIVATE|MAP_ANON,
			-1, 0);
	if (mmap_mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return MAP_FAILED;
	}
	mem = (char *)(((uintptr_t)mmap_mem + thpsize) & ~(thpsize - 1));

	if (madvise(mem, thpsize, MADV_HUGEPAGE)) {
		ksft_test_result_skip("MADV_HUGEPAGE failed\n");
		munmap(mmap_mem, mmap_size);
		return MAP_FAILED;
	}

	*_mmap_mem = mmap_mem;
	*_mmap_size = mmap_size;
	return mem;
}

static void test_ptrace_write(void)
{
	char data = 1;
	char *mem;
	int ret;

	ksft_print_msg("[INFO] PTRACE write access\n");

	mem = mmap(NULL, pagesize, PROT_READ, MAP_PRIVATE|MAP_ANON, -1, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return;
	}

	/* Fault in the shared zeropage. */
	if (*mem != 0) {
		ksft_test_result_fail("Memory not zero\n");
		goto munmap;
	}

	/*
	 * Unshare the page (populating a fresh anon page that might be set
	 * dirty in the PTE) in the read-only VMA using ptrace (FOLL_FORCE).
	 */
	lseek(mem_fd, (uintptr_t) mem, SEEK_SET);
	ret = write(mem_fd, &data, 1);
	if (ret != 1 || *mem != data) {
		ksft_test_result_fail("write() failed\n");
		goto munmap;
	}

	do_test_write_sigsegv(mem);
munmap:
	munmap(mem, pagesize);
}

static void test_ptrace_write_thp(void)
{
	char *mem, *mmap_mem;
	size_t mmap_size;
	char data = 1;
	int ret;

	ksft_print_msg("[INFO] PTRACE write access to THP\n");

	mem = mmap_thp_range(PROT_READ, &mmap_mem, &mmap_size);
	if (mem == MAP_FAILED)
		return;

	/*
	 * Write to the first subpage in the read-only VMA using
	 * ptrace(FOLL_FORCE), eventually placing a fresh THP that is marked
	 * dirty in the PMD.
	 */
	lseek(mem_fd, (uintptr_t) mem, SEEK_SET);
	ret = write(mem_fd, &data, 1);
	if (ret != 1 || *mem != data) {
		ksft_test_result_fail("write() failed\n");
		goto munmap;
	}

	/* MM populated a THP if we got the last subpage populated as well. */
	if (!pagemap_is_populated(pagemap_fd, mem + thpsize - pagesize)) {
		ksft_test_result_skip("Did not get a THP populated\n");
		goto munmap;
	}

	do_test_write_sigsegv(mem);
munmap:
	munmap(mmap_mem, mmap_size);
}

static void test_page_migration(void)
{
	char *mem;

	ksft_print_msg("[INFO] Page migration\n");

	mem = mmap(NULL, pagesize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON,
		   -1, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return;
	}

	/* Populate a fresh page and dirty it. */
	memset(mem, 1, pagesize);
	if (mprotect(mem, pagesize, PROT_READ)) {
		ksft_test_result_fail("mprotect() failed\n");
		goto munmap;
	}

	/* Trigger page migration. Might not be available or fail. */
	if (syscall(__NR_mbind, mem, pagesize, MPOL_LOCAL, NULL, 0x7fful,
		    MPOL_MF_MOVE)) {
		ksft_test_result_skip("mbind() failed\n");
		goto munmap;
	}

	do_test_write_sigsegv(mem);
munmap:
	munmap(mem, pagesize);
}

static void test_page_migration_thp(void)
{
	char *mem, *mmap_mem;
	size_t mmap_size;

	ksft_print_msg("[INFO] Page migration of THP\n");

	mem = mmap_thp_range(PROT_READ|PROT_WRITE, &mmap_mem, &mmap_size);
	if (mem == MAP_FAILED)
		return;

	/*
	 * Write to the first page, which might populate a fresh anon THP
	 * and dirty it.
	 */
	memset(mem, 1, pagesize);
	if (mprotect(mem, thpsize, PROT_READ)) {
		ksft_test_result_fail("mprotect() failed\n");
		goto munmap;
	}

	/* MM populated a THP if we got the last subpage populated as well. */
	if (!pagemap_is_populated(pagemap_fd, mem + thpsize - pagesize)) {
		ksft_test_result_skip("Did not get a THP populated\n");
		goto munmap;
	}

	/* Trigger page migration. Might not be available or fail. */
	if (syscall(__NR_mbind, mem, thpsize, MPOL_LOCAL, NULL, 0x7fful,
		    MPOL_MF_MOVE)) {
		ksft_test_result_skip("mbind() failed\n");
		goto munmap;
	}

	do_test_write_sigsegv(mem);
munmap:
	munmap(mmap_mem, mmap_size);
}

static void test_pte_mapped_thp(void)
{
	char *mem, *mmap_mem;
	size_t mmap_size;

	ksft_print_msg("[INFO] PTE-mapping a THP\n");

	mem = mmap_thp_range(PROT_READ|PROT_WRITE, &mmap_mem, &mmap_size);
	if (mem == MAP_FAILED)
		return;

	/*
	 * Write to the first page, which might populate a fresh anon THP
	 * and dirty it.
	 */
	memset(mem, 1, pagesize);
	if (mprotect(mem, thpsize, PROT_READ)) {
		ksft_test_result_fail("mprotect() failed\n");
		goto munmap;
	}

	/* MM populated a THP if we got the last subpage populated as well. */
	if (!pagemap_is_populated(pagemap_fd, mem + thpsize - pagesize)) {
		ksft_test_result_skip("Did not get a THP populated\n");
		goto munmap;
	}

	/* Trigger PTE-mapping the THP by mprotect'ing the last subpage. */
	if (mprotect(mem + thpsize - pagesize, pagesize,
		     PROT_READ|PROT_WRITE)) {
		ksft_test_result_fail("mprotect() failed\n");
		goto munmap;
	}

	do_test_write_sigsegv(mem);
munmap:
	munmap(mmap_mem, mmap_size);
}

static void test_uffdio_copy(void)
{
	struct uffdio_register uffdio_register;
	struct uffdio_copy uffdio_copy;
	struct uffdio_api uffdio_api;
	char *dst, *src;
	int uffd;

	ksft_print_msg("[INFO] UFFDIO_COPY\n");

	src = malloc(pagesize);
	memset(src, 1, pagesize);
	dst = mmap(NULL, pagesize, PROT_READ, MAP_PRIVATE|MAP_ANON, -1, 0);
	if (dst == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return;
	}

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd < 0) {
		ksft_test_result_skip("__NR_userfaultfd failed\n");
		goto munmap;
	}

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) < 0) {
		ksft_test_result_fail("UFFDIO_API failed\n");
		goto close_uffd;
	}

	uffdio_register.range.start = (unsigned long) dst;
	uffdio_register.range.len = pagesize;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register)) {
		ksft_test_result_fail("UFFDIO_REGISTER failed\n");
		goto close_uffd;
	}

	/* Place a page in a read-only VMA, which might set the PTE dirty. */
	uffdio_copy.dst = (unsigned long) dst;
	uffdio_copy.src = (unsigned long) src;
	uffdio_copy.len = pagesize;
	uffdio_copy.mode = 0;
	if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy)) {
		ksft_test_result_fail("UFFDIO_COPY failed\n");
		goto close_uffd;
	}

	do_test_write_sigsegv(dst);
close_uffd:
	close(uffd);
munmap:
	munmap(dst, pagesize);
	free(src);
}

int main(void)
{
	int err, tests = 2;

	pagesize = getpagesize();
	thpsize = read_pmd_pagesize();
	if (thpsize) {
		ksft_print_msg("[INFO] detected THP size: %zu KiB\n",
			       thpsize / 1024);
		tests += 3;
	}
	tests += 1;

	ksft_print_header();
	ksft_set_plan(tests);

	mem_fd = open("/proc/self/mem", O_RDWR);
	if (mem_fd < 0)
		ksft_exit_fail_msg("opening /proc/self/mem failed\n");
	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		ksft_exit_fail_msg("opening /proc/self/pagemap failed\n");

	/*
	 * On some ptrace(FOLL_FORCE) write access via /proc/self/mem in
	 * read-only VMAs, the kernel may set the PTE/PMD dirty.
	 */
	test_ptrace_write();
	if (thpsize)
		test_ptrace_write_thp();
	/*
	 * On page migration, the kernel may set the PTE/PMD dirty when
	 * remapping the page.
	 */
	test_page_migration();
	if (thpsize)
		test_page_migration_thp();
	/* PTE-mapping a THP might propagate the dirty PMD bit to the PTEs. */
	if (thpsize)
		test_pte_mapped_thp();
	/* Placing a fresh page via userfaultfd may set the PTE dirty. */
	test_uffdio_copy();

	err = ksft_get_fail_cnt();
	if (err)
		ksft_exit_fail_msg("%d out of %d tests failed\n",
				   err, ksft_test_num());
	ksft_exit_pass();
}

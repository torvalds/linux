// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2017, Anshuman Khandual, IBM Corp.
 *
 * Works on architectures which support 128TB virtual
 * address range and beyond.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <fcntl.h>

#include "vm_util.h"
#include "../kselftest.h"

/*
 * Maximum address range mapped with a single mmap()
 * call is little bit more than 1GB. Hence 1GB is
 * chosen as the single chunk size for address space
 * mapping.
 */

#define SZ_1GB	(1024 * 1024 * 1024UL)
#define SZ_1TB	(1024 * 1024 * 1024 * 1024UL)

#define MAP_CHUNK_SIZE	SZ_1GB

/*
 * Address space till 128TB is mapped without any hint
 * and is enabled by default. Address space beyond 128TB
 * till 512TB is obtained by passing hint address as the
 * first argument into mmap() system call.
 *
 * The process heap address space is divided into two
 * different areas one below 128TB and one above 128TB
 * till it reaches 512TB. One with size 128TB and the
 * other being 384TB.
 *
 * On Arm64 the address space is 256TB and support for
 * high mappings up to 4PB virtual address space has
 * been added.
 */

#define NR_CHUNKS_128TB   ((128 * SZ_1TB) / MAP_CHUNK_SIZE) /* Number of chunks for 128TB */
#define NR_CHUNKS_256TB   (NR_CHUNKS_128TB * 2UL)
#define NR_CHUNKS_384TB   (NR_CHUNKS_128TB * 3UL)
#define NR_CHUNKS_3840TB  (NR_CHUNKS_128TB * 30UL)

#define ADDR_MARK_128TB  (1UL << 47) /* First address beyond 128TB */
#define ADDR_MARK_256TB  (1UL << 48) /* First address beyond 256TB */

#ifdef __aarch64__
#define HIGH_ADDR_MARK  ADDR_MARK_256TB
#define HIGH_ADDR_SHIFT 49
#define NR_CHUNKS_LOW   NR_CHUNKS_256TB
#define NR_CHUNKS_HIGH  NR_CHUNKS_3840TB
#else
#define HIGH_ADDR_MARK  ADDR_MARK_128TB
#define HIGH_ADDR_SHIFT 48
#define NR_CHUNKS_LOW   NR_CHUNKS_128TB
#define NR_CHUNKS_HIGH  NR_CHUNKS_384TB
#endif

static char *hint_addr(void)
{
	int bits = HIGH_ADDR_SHIFT + rand() % (63 - HIGH_ADDR_SHIFT);

	return (char *) (1UL << bits);
}

static void validate_addr(char *ptr, int high_addr)
{
	unsigned long addr = (unsigned long) ptr;

	if (high_addr && addr < HIGH_ADDR_MARK)
		ksft_exit_fail_msg("Bad address %lx\n", addr);

	if (addr > HIGH_ADDR_MARK)
		ksft_exit_fail_msg("Bad address %lx\n", addr);
}

static void mark_range(char *ptr, size_t size)
{
	if (prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, ptr, size, "virtual_address_range") == -1) {
		if (errno == EINVAL) {
			/* Depends on CONFIG_ANON_VMA_NAME */
			ksft_test_result_skip("prctl(PR_SET_VMA_ANON_NAME) not supported\n");
			ksft_finished();
		} else {
			ksft_exit_fail_perror("prctl(PR_SET_VMA_ANON_NAME) failed\n");
		}
	}
}

static int is_marked_vma(const char *vma_name)
{
	return vma_name && !strcmp(vma_name, "[anon:virtual_address_range]\n");
}

static int validate_lower_address_hint(void)
{
	char *ptr;

	ptr = mmap((void *) (1UL << 45), MAP_CHUNK_SIZE, PROT_READ |
		   PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (ptr == MAP_FAILED)
		return 0;

	return 1;
}

static int validate_complete_va_space(void)
{
	unsigned long start_addr, end_addr, prev_end_addr;
	char line[400];
	char prot[6];
	FILE *file;
	int fd;

	fd = open("va_dump", O_CREAT | O_WRONLY, 0600);
	unlink("va_dump");
	if (fd < 0) {
		ksft_test_result_skip("cannot create or open dump file\n");
		ksft_finished();
	}

	file = fopen("/proc/self/maps", "r");
	if (file == NULL)
		ksft_exit_fail_msg("cannot open /proc/self/maps\n");

	prev_end_addr = 0;
	while (fgets(line, sizeof(line), file)) {
		const char *vma_name = NULL;
		int vma_name_start = 0;
		unsigned long hop;

		if (sscanf(line, "%lx-%lx %4s %*s %*s %*s %n",
			   &start_addr, &end_addr, prot, &vma_name_start) != 3)
			ksft_exit_fail_msg("cannot parse /proc/self/maps\n");

		if (vma_name_start)
			vma_name = line + vma_name_start;

		/* end of userspace mappings; ignore vsyscall mapping */
		if (start_addr & (1UL << 63))
			return 0;

		/* /proc/self/maps must have gaps less than MAP_CHUNK_SIZE */
		if (start_addr - prev_end_addr >= MAP_CHUNK_SIZE)
			return 1;

		prev_end_addr = end_addr;

		if (prot[0] != 'r')
			continue;

		if (check_vmflag_io((void *)start_addr))
			continue;

		/*
		 * Confirm whether MAP_CHUNK_SIZE chunk can be found or not.
		 * If write succeeds, no need to check MAP_CHUNK_SIZE - 1
		 * addresses after that. If the address was not held by this
		 * process, write would fail with errno set to EFAULT.
		 * Anyways, if write returns anything apart from 1, exit the
		 * program since that would mean a bug in /proc/self/maps.
		 */
		hop = 0;
		while (start_addr + hop < end_addr) {
			if (write(fd, (void *)(start_addr + hop), 1) != 1)
				return 1;
			lseek(fd, 0, SEEK_SET);

			if (is_marked_vma(vma_name))
				munmap((char *)(start_addr + hop), MAP_CHUNK_SIZE);

			hop += MAP_CHUNK_SIZE;
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char *ptr[NR_CHUNKS_LOW];
	char **hptr;
	char *hint;
	unsigned long i, lchunks, hchunks;

	ksft_print_header();
	ksft_set_plan(1);

	for (i = 0; i < NR_CHUNKS_LOW; i++) {
		ptr[i] = mmap(NULL, MAP_CHUNK_SIZE, PROT_READ,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (ptr[i] == MAP_FAILED) {
			if (validate_lower_address_hint())
				ksft_exit_fail_msg("mmap unexpectedly succeeded with hint\n");
			break;
		}

		mark_range(ptr[i], MAP_CHUNK_SIZE);
		validate_addr(ptr[i], 0);
	}
	lchunks = i;
	hptr = (char **) calloc(NR_CHUNKS_HIGH, sizeof(char *));
	if (hptr == NULL) {
		ksft_test_result_skip("Memory constraint not fulfilled\n");
		ksft_finished();
	}

	for (i = 0; i < NR_CHUNKS_HIGH; i++) {
		hint = hint_addr();
		hptr[i] = mmap(hint, MAP_CHUNK_SIZE, PROT_READ,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (hptr[i] == MAP_FAILED)
			break;

		mark_range(ptr[i], MAP_CHUNK_SIZE);
		validate_addr(hptr[i], 1);
	}
	hchunks = i;
	if (validate_complete_va_space()) {
		ksft_test_result_fail("BUG in mmap() or /proc/self/maps\n");
		ksft_finished();
	}

	for (i = 0; i < lchunks; i++)
		munmap(ptr[i], MAP_CHUNK_SIZE);

	for (i = 0; i < hchunks; i++)
		munmap(hptr[i], MAP_CHUNK_SIZE);

	free(hptr);

	ksft_test_result_pass("Test\n");
	ksft_finished();
}

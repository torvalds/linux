// SPDX-License-Identifier: GPL-2.0
/* Test selecting other page sizes for mmap/shmget. */

#define _GNU_SOURCE
#include <sys/mman.h>
#include <linux/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include "vm_util.h"
#include "kselftest.h"
#include "hugepage_settings.h"

#if !defined(MAP_HUGETLB)
#define MAP_HUGETLB	0x40000
#endif

#define SHM_HUGETLB     04000   /* segment will use huge TLB pages */
#ifndef SHM_HUGE_SHIFT
#define SHM_HUGE_SHIFT  26
#endif

#define NUM_PAGESIZES   5
#define NUM_PAGES 4

unsigned long page_sizes[NUM_PAGESIZES];
int num_page_sizes;

int ilog2(unsigned long v)
{
	int l = 0;
	while ((1UL << l) < v)
		l++;
	return l;
}

void show(unsigned long ps)
{
	if (ps == getpagesize())
		return;

	ksft_print_msg("%luMB: %lu\n", ps >> 20, hugetlb_free_pages(ps));
}

void test_mmap(unsigned long size, unsigned flags)
{
	char *map;
	unsigned long before, after;

	before = hugetlb_free_pages(size);
	map = mmap(NULL, size*NUM_PAGES, PROT_READ|PROT_WRITE,
			MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB|flags, -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));

	memset(map, 0xff, size*NUM_PAGES);
	after = hugetlb_free_pages(size);

	show(size);
	ksft_test_result(size == getpagesize() || (before - after) == NUM_PAGES,
			 "%s mmap %lu %x\n", __func__, size, flags);

	if (munmap(map, size * NUM_PAGES))
		ksft_exit_fail_msg("%s: unmap %s\n", __func__, strerror(errno));
}

void test_shmget(unsigned long size, unsigned flags)
{
	int id;
	unsigned long before, after;
	struct shm_info i;
	char *map;

	before = hugetlb_free_pages(size);
	id = shmget(IPC_PRIVATE, size * NUM_PAGES, IPC_CREAT|0600|flags);
	if (id < 0) {
		if (errno == EPERM) {
			ksft_test_result_skip("shmget requires root privileges: %s\n",
					      strerror(errno));
			return;
		}
		ksft_exit_fail_msg("shmget: %s\n", strerror(errno));
	}

	if (shmctl(id, SHM_INFO, (void *)&i) < 0)
		ksft_exit_fail_msg("shmctl: %s\n", strerror(errno));

	map = shmat(id, NULL, 0600);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("shmat: %s\n", strerror(errno));

	shmctl(id, IPC_RMID, NULL);

	memset(map, 0xff, size*NUM_PAGES);
	after = hugetlb_free_pages(size);

	show(size);
	ksft_test_result(size == getpagesize() || (before - after) == NUM_PAGES,
			 "%s: mmap %lu %x\n", __func__, size, flags);
	if (shmdt(map))
		ksft_exit_fail_msg("%s: shmdt: %s\n", __func__, strerror(errno));
}

void find_pagesizes(void)
{
	unsigned long largest = getpagesize();
	int i;

	num_page_sizes = hugetlb_setup(NUM_PAGES, page_sizes, ARRAY_SIZE(page_sizes));

	for (i = 0; i < num_page_sizes; i++)
		if (page_sizes[i] > largest)
			largest = page_sizes[i];

	shm_limits_prepare(NUM_PAGES * largest);
}

int main(void)
{
	unsigned default_hps = default_huge_page_size();
	int i;

	ksft_print_header();

	find_pagesizes();

	if (!num_page_sizes)
		ksft_finished();

	ksft_set_plan(2 * num_page_sizes + 3);

	for (i = 0; i < num_page_sizes; i++) {
		unsigned long ps = page_sizes[i];
		int arg = ilog2(ps) << MAP_HUGE_SHIFT;

		ksft_print_msg("Testing %luMB mmap with shift %x\n", ps >> 20, arg);
		test_mmap(ps, MAP_HUGETLB | arg);
	}

	ksft_print_msg("Testing default huge mmap\n");
	test_mmap(default_hps, MAP_HUGETLB);

	ksft_print_msg("Testing non-huge shmget\n");
	test_shmget(getpagesize(), 0);

	for (i = 0; i < num_page_sizes; i++) {
		unsigned long ps = page_sizes[i];
		int arg = ilog2(ps) << SHM_HUGE_SHIFT;
		ksft_print_msg("Testing %luMB shmget with shift %x\n", ps >> 20, arg);
		test_shmget(ps, SHM_HUGETLB | arg);
	}

	ksft_print_msg("default huge shmget\n");
	test_shmget(default_hps, SHM_HUGETLB);

	ksft_finished();
}

SHM_LIMITS_RESTORE()

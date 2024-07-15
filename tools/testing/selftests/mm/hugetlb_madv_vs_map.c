// SPDX-License-Identifier: GPL-2.0
/*
 * A test case that must run on a system with one and only one huge page available.
 *	# echo 1 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
 *
 * During setup, the test allocates the only available page, and starts three threads:
 *  - thread1:
 *	* madvise(MADV_DONTNEED) on the allocated huge page
 *  - thread 2:
 *	* Write to the allocated huge page
 *  - thread 3:
 *	* Try to allocated an extra huge page (which must not available)
 *
 *  The test fails if thread3 is able to allocate a page.
 *
 *  Touching the first page after thread3's allocation will raise a SIGBUS
 *
 *  Author: Breno Leitao <leitao@debian.org>
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "vm_util.h"
#include "../kselftest.h"

#define MMAP_SIZE (1 << 21)
#define INLOOP_ITER 100

char *huge_ptr;

/* Touch the memory while it is being madvised() */
void *touch(void *unused)
{
	for (int i = 0; i < INLOOP_ITER; i++)
		huge_ptr[0] = '.';

	return NULL;
}

void *madv(void *unused)
{
	for (int i = 0; i < INLOOP_ITER; i++)
		madvise(huge_ptr, MMAP_SIZE, MADV_DONTNEED);

	return NULL;
}

/*
 * We got here, and there must be no huge page available for mapping
 * The other hugepage should be flipping from used <-> reserved, because
 * of madvise(DONTNEED).
 */
void *map_extra(void *unused)
{
	void *ptr;

	for (int i = 0; i < INLOOP_ITER; i++) {
		ptr = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
			   -1, 0);

		if ((long)ptr != -1) {
			/* Touching the other page now will cause a SIGBUG
			 * huge_ptr[0] = '1';
			 */
			return ptr;
		}
	}

	return NULL;
}

int main(void)
{
	pthread_t thread1, thread2, thread3;
	unsigned long free_hugepages;
	void *ret;

	/*
	 * On kernel 6.7, we are able to reproduce the problem with ~10
	 * interactions
	 */
	int max = 10;

	free_hugepages = get_free_hugepages();

	if (free_hugepages != 1) {
		ksft_exit_skip("This test needs one and only one page to execute. Got %lu\n",
			       free_hugepages);
	}

	while (max--) {
		huge_ptr = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
				-1, 0);

		if ((unsigned long)huge_ptr == -1) {
			ksft_exit_skip("Failed to allocated huge page\n");
			return KSFT_SKIP;
		}

		pthread_create(&thread1, NULL, madv, NULL);
		pthread_create(&thread2, NULL, touch, NULL);
		pthread_create(&thread3, NULL, map_extra, NULL);

		pthread_join(thread1, NULL);
		pthread_join(thread2, NULL);
		pthread_join(thread3, &ret);

		if (ret) {
			ksft_test_result_fail("Unexpected huge page allocation\n");
			return KSFT_FAIL;
		}

		/* Unmap and restart */
		munmap(huge_ptr, MMAP_SIZE);
	}

	return KSFT_PASS;
}

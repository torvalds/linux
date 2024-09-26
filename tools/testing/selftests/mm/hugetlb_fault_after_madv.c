// SPDX-License-Identifier: GPL-2.0
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "vm_util.h"
#include "../kselftest.h"

#define INLOOP_ITER 100

static char *huge_ptr;
static size_t huge_page_size;

/* Touch the memory while it is being madvised() */
void *touch(void *unused)
{
	char *ptr = (char *)huge_ptr;

	for (int i = 0; i < INLOOP_ITER; i++)
		ptr[0] = '.';

	return NULL;
}

void *madv(void *unused)
{
	usleep(rand() % 10);

	for (int i = 0; i < INLOOP_ITER; i++)
		madvise(huge_ptr, huge_page_size, MADV_DONTNEED);

	return NULL;
}

int main(void)
{
	unsigned long free_hugepages;
	pthread_t thread1, thread2;
	/*
	 * On kernel 6.4, we are able to reproduce the problem with ~1000
	 * interactions
	 */
	int max = 10000;

	srand(getpid());

	huge_page_size = default_huge_page_size();
	if (!huge_page_size)
		ksft_exit_skip("Could not detect default hugetlb page size.");

	free_hugepages = get_free_hugepages();
	if (free_hugepages != 1) {
		ksft_exit_skip("This test needs one and only one page to execute. Got %lu\n",
			       free_hugepages);
	}

	while (max--) {
		huge_ptr = mmap(NULL, huge_page_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
				-1, 0);

		if ((unsigned long)huge_ptr == -1)
			ksft_exit_skip("Failed to allocated huge page\n");

		pthread_create(&thread1, NULL, madv, NULL);
		pthread_create(&thread2, NULL, touch, NULL);

		pthread_join(thread1, NULL);
		pthread_join(thread2, NULL);
		munmap(huge_ptr, huge_page_size);
	}

	return KSFT_PASS;
}

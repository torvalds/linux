// SPDX-License-Identifier: GPL-2.0
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>

#include "vm_util.h"
#include "../kselftest.h"

#define INLOOP_ITER 100

static char *huge_ptr;
static size_t huge_page_size;

static sigjmp_buf sigbuf;
static bool sigbus_triggered;

static void signal_handler(int signal)
{
	if (signal == SIGBUS) {
		sigbus_triggered = true;
		siglongjmp(sigbuf, 1);
	}
}

/* Touch the memory while it is being madvised() */
void *touch(void *unused)
{
	char *ptr = (char *)huge_ptr;

	if (sigsetjmp(sigbuf, 1))
		return NULL;

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
	int err;

	ksft_print_header();
	ksft_set_plan(1);

	srand(getpid());

	if (signal(SIGBUS, signal_handler) == SIG_ERR)
		ksft_exit_skip("Could not register signal handler.");

	huge_page_size = default_huge_page_size();
	if (!huge_page_size)
		ksft_exit_skip("Could not detect default hugetlb page size.");

	ksft_print_msg("[INFO] detected default hugetlb page size: %zu KiB\n",
		       huge_page_size / 1024);

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

	ksft_test_result(!sigbus_triggered, "SIGBUS behavior\n");

	err = ksft_get_fail_cnt();
	if (err)
		ksft_exit_fail_msg("%d out of %d tests failed\n",
				   err, ksft_test_num());
	ksft_exit_pass();
}

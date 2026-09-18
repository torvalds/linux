// SPDX-License-Identifier: GPL-2.0
/*
 * A test case that must run on a system with one and only one huge page available.
 *	# echo 1 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
 *
 *  Author: Breno Leitao <leitao@debian.org>
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vm_util.h"

#define INLOOP_ITER 100

size_t mmap_size;
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
		madvise(huge_ptr, mmap_size, MADV_DONTNEED);

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
		ptr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
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

/* During setup in main, the only available page was allocated. This test then
 * starts three threads:
 *
 * - thread1:
 *	* madvise(MADV_DONTNEED) on the allocated huge page
 *  - thread 2:
 *	* Write to the allocated huge page
 *  - thread 3:
 *	* Try to allocated an extra huge page (which must not available)
 *
 *  The test fails if thread3 is able to allocate a page.
 *
 *  Touching the first page after thread3's allocation will raise a SIGBUS
 */
void test_madv_vs_map(void)
{
	pthread_t thread1, thread2, thread3;
	void *ret;

	/*
	 * On kernel 6.7, we are able to reproduce the problem with ~10
	 * interactions
	 */
	int max = 10;

	mmap_size = default_huge_page_size();

	while (max--) {
		huge_ptr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
				-1, 0);

		if ((unsigned long)huge_ptr == -1)
			ksft_exit_fail_perror("Failed to allocate huge page");

		pthread_create(&thread1, NULL, madv, NULL);
		pthread_create(&thread2, NULL, touch, NULL);
		pthread_create(&thread3, NULL, map_extra, NULL);

		pthread_join(thread1, NULL);
		pthread_join(thread2, NULL);
		pthread_join(thread3, &ret);

		if (ret) {
			ksft_test_result_fail("Unexpected huge page allocation\n");
			ksft_finished();
		}

		/* Unmap and restart */
		munmap(huge_ptr, mmap_size);
	}

	ksft_test_result_pass("No unexpected huge page allocations\n");
}

/*  We create a child process, then unmap the page in the parent while the child
 *  waits and verify that there is no underflow of the reserved count. We also
 *  verify that after the child exits, the reserved count is properly restored.
 */
void test_underflow(void)
{
	pid_t pid;
	int pipe_fds[2];
	unsigned long nr_reserved = 0;

	huge_ptr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);

	if ((unsigned long)huge_ptr == -1)
		ksft_exit_fail_perror("Failed to allocate huge page");

	nr_reserved = hugetlb_nr_resv_pages(default_huge_page_size());
	if (nr_reserved != 1)
		ksft_exit_fail_msg("Unexpected number of reserved pages: %lu, expected 1\n",
				   nr_reserved);

	/* Force the fault to ensure the reservation is consumed */
	*huge_ptr = 0;
	nr_reserved = hugetlb_nr_resv_pages(default_huge_page_size());
	if (nr_reserved != 0)
		ksft_exit_fail_msg("Unexpected number of reserved pages: %lu, expected 0\n",
				   nr_reserved);

	if (pipe(pipe_fds) != 0)
		ksft_exit_fail_perror("pipe failed");

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_perror("fork failed");

	if (pid == 0) {
		/* Child: Simply wait for the parent */
		char b;

		close(pipe_fds[1]);
		if (read(pipe_fds[0], &b, 1) < 0)
			ksft_perror("child read failed");
		/* Let the parent do the cleanup */
		_exit(0);
	}

	/* Parent */
	close(pipe_fds[0]);

	/* First unmap, this will close the vma */
	if (munmap(huge_ptr, mmap_size) != 0) {
		ksft_perror("munmap failed");
		goto err_cleanup;
	}

	nr_reserved = hugetlb_nr_resv_pages(default_huge_page_size());
	if (nr_reserved == 0) {
		ksft_test_result_pass("Underflow not present!\n");
	} else {
		ksft_test_result_fail("Unexpected HugePages_Rsvd=%ld after munmap, should be 0\n",
				      nr_reserved);
		goto err_cleanup;
	}
	/* Make the child exit, this should restore HugePages_Rsvd to 0 */
	if (write(pipe_fds[1], &nr_reserved, 1) < 0) {
		/* If write failed, the child is likely already gone */
		ksft_exit_fail_perror("write failed");
	}
	close(pipe_fds[1]);
	if (waitpid(pid, NULL, 0) <= 0)
		ksft_exit_fail_msg("waitpid failed\n");

	nr_reserved = hugetlb_nr_resv_pages(default_huge_page_size());
	if (nr_reserved == 0)
		ksft_test_result_pass("After the child dies, HugePages_Rsvd is properly set to 0\n");
	else
		ksft_exit_fail_msg("Unexpected HugePages_Rsvd=%ld after the child termination munmap, should be 0\n", nr_reserved);

	return;

err_cleanup:
	if (write(pipe_fds[1], &nr_reserved, 1) < 0)
		ksft_exit_fail_perror("write failed");
	if (waitpid(pid, NULL, 0) <= 0)
		ksft_exit_fail_perror("waitpid failed");

	ksft_exit_fail();
}

int main(void)
{
	ksft_print_header();
	ksft_set_plan(3);

	if (!hugetlb_setup_default_exact(1))
		ksft_exit_skip("This test needs one and only one page to execute. Got %lu\n",
			       hugetlb_free_default_pages());

	test_madv_vs_map();
	test_underflow();

	ksft_finished();
}

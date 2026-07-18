// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <linux/mman.h>

#include "kselftest.h"

int main(int argc, char *argv[])
{
	const size_t alloc_size = 2 * 1024 * 1024;
	int retry_count = 10;
	bool dropped;
	void *alloc;

	ksft_print_header();
	ksft_set_plan(1);

	alloc = mmap(0, alloc_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_DROPPABLE, -1, 0);
	if (alloc == MAP_FAILED) {
		if ((errno == EOPNOTSUPP) || (errno == EINVAL)) {
			ksft_test_result_skip("MAP_DROPPABLE not supported\n");
			exit(KSFT_SKIP);
		}
		ksft_test_result_fail("mmap error: %s\n", strerror(errno));
		exit(KSFT_FAIL);
	}
	memset(alloc, 'A', alloc_size);

	while (retry_count--) {
		if (madvise(alloc, alloc_size, MADV_PAGEOUT)) {
			if (errno == EINVAL) {
				ksft_test_result_skip("madvise(MADV_PAGEOUT) not supported\n");
				exit(KSFT_SKIP);
			}
			ksft_test_result_fail("madvise(MADV_PAGEOUT) error: %s\n", strerror(errno));
			exit(KSFT_FAIL);
		}

		dropped = memchr(alloc, 'A', alloc_size) == NULL;

		/*
		 * Speculative reference can temporarily prevent some
		 * pages from getting dropped. So sleep and retry.
		 *
		 * If a page is not droppable for 10s, something
		 * is seriously messed up and we want to fail.
		 */
		if (dropped)
			break;
		sleep(1);
	}

	ksft_test_result(dropped, "madvise(MADV_PAGEOUT) behavior\n");

	ksft_finished();
}

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

#include "../kselftest.h"

int main(int argc, char *argv[])
{
	size_t alloc_size = 134217728;
	size_t page_size = getpagesize();
	void *alloc;
	pid_t child;

	ksft_print_header();
	ksft_set_plan(1);

	alloc = mmap(0, alloc_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_DROPPABLE, -1, 0);
	assert(alloc != MAP_FAILED);
	memset(alloc, 'A', alloc_size);
	for (size_t i = 0; i < alloc_size; i += page_size)
		assert(*(uint8_t *)(alloc + i));

	child = fork();
	assert(child >= 0);
	if (!child) {
		for (;;)
			*(char *)malloc(page_size) = 'B';
	}

	for (bool done = false; !done;) {
		for (size_t i = 0; i < alloc_size; i += page_size) {
			if (!*(uint8_t *)(alloc + i)) {
				done = true;
				break;
			}
		}
	}
	kill(child, SIGTERM);

	ksft_test_result_pass("MAP_DROPPABLE: PASS\n");
	exit(KSFT_PASS);
}

// SPDX-License-Identifier: GPL-2.0
/*
 * fill_buf benchmark
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *    Sai Praneeth Prakhya <sai.praneeth.prakhya@intel.com>,
 *    Fenghua Yu <fenghua.yu@intel.com>
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <string.h>

#include "resctrl.h"

#define CL_SIZE			(64)
#define PAGE_SIZE		(4 * 1024)
#define MB			(1024 * 1024)

static void sb(void)
{
#if defined(__i386) || defined(__x86_64)
	asm volatile("sfence\n\t"
		     : : : "memory");
#endif
}

static void cl_flush(void *p)
{
#if defined(__i386) || defined(__x86_64)
	asm volatile("clflush (%0)\n\t"
		     : : "r"(p) : "memory");
#endif
}

void mem_flush(unsigned char *buf, size_t buf_size)
{
	unsigned char *cp = buf;
	size_t i = 0;

	buf_size = buf_size / CL_SIZE; /* mem size in cache lines */

	for (i = 0; i < buf_size; i++)
		cl_flush(&cp[i * CL_SIZE]);

	sb();
}

/*
 * Buffer index step advance to workaround HW prefetching interfering with
 * the measurements.
 *
 * Must be a prime to step through all indexes of the buffer.
 *
 * Some primes work better than others on some architectures (from MBA/MBM
 * result stability point of view).
 */
#define FILL_IDX_MULT	23

static int fill_one_span_read(unsigned char *buf, size_t buf_size)
{
	unsigned int size = buf_size / (CL_SIZE / 2);
	unsigned int i, idx = 0;
	unsigned char sum = 0;

	/*
	 * Read the buffer in an order that is unexpected by HW prefetching
	 * optimizations to prevent them interfering with the caching pattern.
	 *
	 * The read order is (in terms of halves of cachelines):
	 *	i * FILL_IDX_MULT % size
	 * The formula is open-coded below to avoiding modulo inside the loop
	 * as it improves MBA/MBM result stability on some architectures.
	 */
	for (i = 0; i < size; i++) {
		sum += buf[idx * (CL_SIZE / 2)];

		idx += FILL_IDX_MULT;
		while (idx >= size)
			idx -= size;
	}

	return sum;
}

void fill_cache_read(unsigned char *buf, size_t buf_size, bool once)
{
	int ret = 0;

	while (1) {
		ret = fill_one_span_read(buf, buf_size);
		if (once)
			break;
	}

	/* Consume read result so that reading memory is not optimized out. */
	*value_sink = ret;
}

unsigned char *alloc_buffer(size_t buf_size, bool memflush)
{
	void *buf = NULL;
	uint64_t *p64;
	ssize_t s64;
	int ret;

	ret = posix_memalign(&buf, PAGE_SIZE, buf_size);
	if (ret < 0)
		return NULL;

	/* Initialize the buffer */
	p64 = buf;
	s64 = buf_size / sizeof(uint64_t);

	while (s64 > 0) {
		*p64 = (uint64_t)rand();
		p64 += (CL_SIZE / sizeof(uint64_t));
		s64 -= (CL_SIZE / sizeof(uint64_t));
	}

	/* Flush the memory before using to avoid "cache hot pages" effect */
	if (memflush)
		mem_flush(buf, buf_size);

	return buf;
}

ssize_t get_fill_buf_size(int cpu_no, const char *cache_type)
{
	unsigned long cache_total_size = 0;
	int ret;

	ret = get_cache_size(cpu_no, cache_type, &cache_total_size);
	if (ret)
		return ret;

	return cache_total_size * 2 > MINIMUM_SPAN ?
			cache_total_size * 2 : MINIMUM_SPAN;
}

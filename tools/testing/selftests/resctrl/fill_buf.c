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

static void mem_flush(unsigned char *buf, size_t buf_size)
{
	unsigned char *cp = buf;
	size_t i = 0;

	buf_size = buf_size / CL_SIZE; /* mem size in cache lines */

	for (i = 0; i < buf_size; i++)
		cl_flush(&cp[i * CL_SIZE]);

	sb();
}

static void *malloc_and_init_memory(size_t buf_size)
{
	void *p = NULL;
	uint64_t *p64;
	size_t s64;
	int ret;

	ret = posix_memalign(&p, PAGE_SIZE, buf_size);
	if (ret < 0)
		return NULL;

	p64 = (uint64_t *)p;
	s64 = buf_size / sizeof(uint64_t);

	while (s64 > 0) {
		*p64 = (uint64_t)rand();
		p64 += (CL_SIZE / sizeof(uint64_t));
		s64 -= (CL_SIZE / sizeof(uint64_t));
	}

	return p;
}

static int fill_one_span_read(unsigned char *buf, size_t buf_size)
{
	unsigned char *end_ptr = buf + buf_size;
	unsigned char sum, *p;

	sum = 0;
	p = buf;
	while (p < end_ptr) {
		sum += *p;
		p += (CL_SIZE / 2);
	}

	return sum;
}

static void fill_one_span_write(unsigned char *buf, size_t buf_size)
{
	unsigned char *end_ptr = buf + buf_size;
	unsigned char *p;

	p = buf;
	while (p < end_ptr) {
		*p = '1';
		p += (CL_SIZE / 2);
	}
}

static int fill_cache_read(unsigned char *buf, size_t buf_size, bool once)
{
	int ret = 0;
	FILE *fp;

	while (1) {
		ret = fill_one_span_read(buf, buf_size);
		if (once)
			break;
	}

	/* Consume read result so that reading memory is not optimized out. */
	fp = fopen("/dev/null", "w");
	if (!fp) {
		perror("Unable to write to /dev/null");
		return -1;
	}
	fprintf(fp, "Sum: %d ", ret);
	fclose(fp);

	return 0;
}

static int fill_cache_write(unsigned char *buf, size_t buf_size, bool once)
{
	while (1) {
		fill_one_span_write(buf, buf_size);
		if (once)
			break;
	}

	return 0;
}

static int fill_cache(size_t buf_size, int memflush, int op, bool once)
{
	unsigned char *buf;
	int ret;

	buf = malloc_and_init_memory(buf_size);
	if (!buf)
		return -1;

	/* Flush the memory before using to avoid "cache hot pages" effect */
	if (memflush)
		mem_flush(buf, buf_size);

	if (op == 0)
		ret = fill_cache_read(buf, buf_size, once);
	else
		ret = fill_cache_write(buf, buf_size, once);

	free(buf);

	if (ret) {
		printf("\n Error in fill cache read/write...\n");
		return -1;
	}


	return 0;
}

int run_fill_buf(size_t span, int memflush, int op, bool once)
{
	size_t cache_size = span;
	int ret;

	ret = fill_cache(cache_size, memflush, op, once);
	if (ret) {
		printf("\n Error in fill cache\n");
		return -1;
	}

	return 0;
}

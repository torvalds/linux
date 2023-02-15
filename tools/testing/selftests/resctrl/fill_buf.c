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
#include <malloc.h>
#include <string.h>

#include "resctrl.h"

#define CL_SIZE			(64)
#define PAGE_SIZE		(4 * 1024)
#define MB			(1024 * 1024)

static unsigned char *startptr;

static void sb(void)
{
#if defined(__i386) || defined(__x86_64)
	asm volatile("sfence\n\t"
		     : : : "memory");
#endif
}

static void ctrl_handler(int signo)
{
	free(startptr);
	printf("\nEnding\n");
	sb();
	exit(EXIT_SUCCESS);
}

static void cl_flush(void *p)
{
#if defined(__i386) || defined(__x86_64)
	asm volatile("clflush (%0)\n\t"
		     : : "r"(p) : "memory");
#endif
}

static void mem_flush(void *p, size_t s)
{
	char *cp = (char *)p;
	size_t i = 0;

	s = s / CL_SIZE; /* mem size in cache llines */

	for (i = 0; i < s; i++)
		cl_flush(&cp[i * CL_SIZE]);

	sb();
}

static void *malloc_and_init_memory(size_t s)
{
	uint64_t *p64;
	size_t s64;

	void *p = memalign(PAGE_SIZE, s);
	if (!p)
		return NULL;

	p64 = (uint64_t *)p;
	s64 = s / sizeof(uint64_t);

	while (s64 > 0) {
		*p64 = (uint64_t)rand();
		p64 += (CL_SIZE / sizeof(uint64_t));
		s64 -= (CL_SIZE / sizeof(uint64_t));
	}

	return p;
}

static int fill_one_span_read(unsigned char *start_ptr, unsigned char *end_ptr)
{
	unsigned char sum, *p;

	sum = 0;
	p = start_ptr;
	while (p < end_ptr) {
		sum += *p;
		p += (CL_SIZE / 2);
	}

	return sum;
}

static
void fill_one_span_write(unsigned char *start_ptr, unsigned char *end_ptr)
{
	unsigned char *p;

	p = start_ptr;
	while (p < end_ptr) {
		*p = '1';
		p += (CL_SIZE / 2);
	}
}

static int fill_cache_read(unsigned char *start_ptr, unsigned char *end_ptr,
			   char *resctrl_val)
{
	int ret = 0;
	FILE *fp;

	while (1) {
		ret = fill_one_span_read(start_ptr, end_ptr);
		if (!strncmp(resctrl_val, CAT_STR, sizeof(CAT_STR)))
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

static int fill_cache_write(unsigned char *start_ptr, unsigned char *end_ptr,
			    char *resctrl_val)
{
	while (1) {
		fill_one_span_write(start_ptr, end_ptr);
		if (!strncmp(resctrl_val, CAT_STR, sizeof(CAT_STR)))
			break;
	}

	return 0;
}

static int
fill_cache(unsigned long long buf_size, int malloc_and_init, int memflush,
	   int op, char *resctrl_val)
{
	unsigned char *start_ptr, *end_ptr;
	unsigned long long i;
	int ret;

	if (malloc_and_init)
		start_ptr = malloc_and_init_memory(buf_size);
	else
		start_ptr = malloc(buf_size);

	if (!start_ptr)
		return -1;

	startptr = start_ptr;
	end_ptr = start_ptr + buf_size;

	/*
	 * It's better to touch the memory once to avoid any compiler
	 * optimizations
	 */
	if (!malloc_and_init) {
		for (i = 0; i < buf_size; i++)
			*start_ptr++ = (unsigned char)rand();
	}

	start_ptr = startptr;

	/* Flush the memory before using to avoid "cache hot pages" effect */
	if (memflush)
		mem_flush(start_ptr, buf_size);

	if (op == 0)
		ret = fill_cache_read(start_ptr, end_ptr, resctrl_val);
	else
		ret = fill_cache_write(start_ptr, end_ptr, resctrl_val);

	if (ret) {
		printf("\n Error in fill cache read/write...\n");
		return -1;
	}

	free(startptr);

	return 0;
}

int run_fill_buf(unsigned long span, int malloc_and_init_memory,
		 int memflush, int op, char *resctrl_val)
{
	unsigned long long cache_size = span;
	int ret;

	/* set up ctrl-c handler */
	if (signal(SIGINT, ctrl_handler) == SIG_ERR)
		printf("Failed to catch SIGINT!\n");
	if (signal(SIGHUP, ctrl_handler) == SIG_ERR)
		printf("Failed to catch SIGHUP!\n");

	ret = fill_cache(cache_size, malloc_and_init_memory, memflush, op,
			 resctrl_val);
	if (ret) {
		printf("\n Error in fill cache\n");
		return -1;
	}

	return 0;
}

// SPDX-License-Identifier: GPL-2.0
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include "utils.h"

#define SIZE 256
#define ITERATIONS 10000

#define LARGE_SIZE (5 * 1024)
#define LARGE_ITERATIONS 1000
#define LARGE_MAX_OFFSET 32
#define LARGE_SIZE_START 4096

/* This is big enough to fit LARGE_SIZE and works on 4K & 64K kernels */
#define MAP_SIZE (64 * 1024)

#define MAX_OFFSET_DIFF_S1_S2 48

int vmx_count;
int enter_vmx_ops(void)
{
	vmx_count++;
	return 1;
}

void exit_vmx_ops(void)
{
	vmx_count--;
}
int test_memcmp(const void *s1, const void *s2, size_t n);

/* test all offsets and lengths */
static void test_one(char *s1, char *s2, unsigned long max_offset,
		unsigned long size_start, unsigned long max_size)
{
	unsigned long offset, size;

	for (offset = 0; offset < max_offset; offset++) {
		for (size = size_start; size < (max_size - offset); size++) {
			int x, y;
			unsigned long i;

			y = memcmp(s1+offset, s2+offset, size);
			x = test_memcmp(s1+offset, s2+offset, size);

			if (((x ^ y) < 0) &&	/* Trick to compare sign */
				((x | y) != 0)) { /* check for zero */
				printf("memcmp returned %d, should have returned %d (offset %ld size %ld)\n", x, y, offset, size);

				for (i = offset; i < offset+size; i++)
					printf("%02x ", s1[i]);
				printf("\n");

				for (i = offset; i < offset+size; i++)
					printf("%02x ", s2[i]);
				printf("\n");
				abort();
			}

			if (vmx_count != 0) {
				printf("vmx enter/exit not paired.(offset:%ld size:%ld s1:%p s2:%p vc:%d\n",
					offset, size, s1, s2, vmx_count);
				printf("\n");
				abort();
			}
		}
	}
}

static int testcase(bool islarge)
{
	unsigned long i, comp_size, alloc_size;
	char *p, *s1, *s2;
	int iterations;

	comp_size = (islarge ? LARGE_SIZE : SIZE);
	alloc_size = comp_size + MAX_OFFSET_DIFF_S1_S2;
	iterations = islarge ? LARGE_ITERATIONS : ITERATIONS;

	p = mmap(NULL, 4 * MAP_SIZE, PROT_READ | PROT_WRITE,
		 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	FAIL_IF(p == MAP_FAILED);

	/* Put s1/s2 at the end of a page */
	s1 = p + MAP_SIZE - alloc_size;
	s2 = p + 3 * MAP_SIZE - alloc_size;

	/* And unmap the subsequent page to force a fault if we overread */
	munmap(p + MAP_SIZE, MAP_SIZE);
	munmap(p + 3 * MAP_SIZE, MAP_SIZE);

	srandom(time(0));

	for (i = 0; i < iterations; i++) {
		unsigned long j;
		unsigned long change;
		char *rand_s1 = s1;
		char *rand_s2 = s2;

		for (j = 0; j < alloc_size; j++)
			s1[j] = random();

		rand_s1 += random() % MAX_OFFSET_DIFF_S1_S2;
		rand_s2 += random() % MAX_OFFSET_DIFF_S1_S2;
		memcpy(rand_s2, rand_s1, comp_size);

		/* change one byte */
		change = random() % comp_size;
		rand_s2[change] = random() & 0xff;

		if (islarge)
			test_one(rand_s1, rand_s2, LARGE_MAX_OFFSET,
					LARGE_SIZE_START, comp_size);
		else
			test_one(rand_s1, rand_s2, SIZE, 0, comp_size);
	}

	srandom(time(0));

	for (i = 0; i < iterations; i++) {
		unsigned long j;
		unsigned long change;
		char *rand_s1 = s1;
		char *rand_s2 = s2;

		for (j = 0; j < alloc_size; j++)
			s1[j] = random();

		rand_s1 += random() % MAX_OFFSET_DIFF_S1_S2;
		rand_s2 += random() % MAX_OFFSET_DIFF_S1_S2;
		memcpy(rand_s2, rand_s1, comp_size);

		/* change multiple bytes, 1/8 of total */
		for (j = 0; j < comp_size / 8; j++) {
			change = random() % comp_size;
			s2[change] = random() & 0xff;
		}

		if (islarge)
			test_one(rand_s1, rand_s2, LARGE_MAX_OFFSET,
					LARGE_SIZE_START, comp_size);
		else
			test_one(rand_s1, rand_s2, SIZE, 0, comp_size);
	}

	return 0;
}

static int testcases(void)
{
	testcase(0);
	testcase(1);
	return 0;
}

int main(void)
{
	test_harness_set_timeout(300);
	return test_harness(testcases, "memcmp");
}

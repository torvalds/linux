// SPDX-License-Identifier: GPL-2.0
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

#define SIZE 256
#define ITERATIONS 10000

int test_memcmp(const void *s1, const void *s2, size_t n);

/* test all offsets and lengths */
static void test_one(char *s1, char *s2)
{
	unsigned long offset, size;

	for (offset = 0; offset < SIZE; offset++) {
		for (size = 0; size < (SIZE-offset); size++) {
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
		}
	}
}

static int testcase(void)
{
	char *s1;
	char *s2;
	unsigned long i;

	s1 = memalign(128, SIZE);
	if (!s1) {
		perror("memalign");
		exit(1);
	}

	s2 = memalign(128, SIZE);
	if (!s2) {
		perror("memalign");
		exit(1);
	}

	srandom(1);

	for (i = 0; i < ITERATIONS; i++) {
		unsigned long j;
		unsigned long change;

		for (j = 0; j < SIZE; j++)
			s1[j] = random();

		memcpy(s2, s1, SIZE);

		/* change one byte */
		change = random() % SIZE;
		s2[change] = random() & 0xff;

		test_one(s1, s2);
	}

	srandom(1);

	for (i = 0; i < ITERATIONS; i++) {
		unsigned long j;
		unsigned long change;

		for (j = 0; j < SIZE; j++)
			s1[j] = random();

		memcpy(s2, s1, SIZE);

		/* change multiple bytes, 1/8 of total */
		for (j = 0; j < SIZE / 8; j++) {
			change = random() % SIZE;
			s2[change] = random() & 0xff;
		}

		test_one(s1, s2);
	}

	return 0;
}

int main(void)
{
	return test_harness(testcase, "memcmp");
}

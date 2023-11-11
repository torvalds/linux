// SPDX-License-Identifier: GPL-2.0
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "utils.h"

void *TEST_MEMMOVE(const void *s1, const void *s2, size_t n);

#define BUF_LEN 65536
#define MAX_OFFSET 512

size_t max(size_t a, size_t b)
{
	if (a >= b)
		return a;
	return b;
}

static int testcase_run(void)
{
	size_t i, src_off, dst_off, len;

	char *usermap = memalign(BUF_LEN, BUF_LEN);
	char *kernelmap = memalign(BUF_LEN, BUF_LEN);

	assert(usermap != NULL);
	assert(kernelmap != NULL);

	memset(usermap, 0, BUF_LEN);
	memset(kernelmap, 0, BUF_LEN);

	for (i = 0; i < BUF_LEN; i++) {
		usermap[i] = i & 0xff;
		kernelmap[i] = i & 0xff;
	}

	for (src_off = 0; src_off < MAX_OFFSET; src_off++) {
		for (dst_off = 0; dst_off < MAX_OFFSET; dst_off++) {
			for (len = 1; len < MAX_OFFSET - max(src_off, dst_off); len++) {

				memmove(usermap + dst_off, usermap + src_off, len);
				TEST_MEMMOVE(kernelmap + dst_off, kernelmap + src_off, len);
				if (memcmp(usermap, kernelmap, MAX_OFFSET) != 0) {
					printf("memmove failed at %ld %ld %ld\n",
							src_off, dst_off, len);
					abort();
				}
			}
		}
	}
	return 0;
}

int main(void)
{
	return test_harness(testcase_run, "memmove");
}

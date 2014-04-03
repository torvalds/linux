#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "../utils.h"

#define MAX_LEN 8192
#define MAX_OFFSET 16
#define MIN_REDZONE 128
#define BUFLEN (MAX_LEN+MAX_OFFSET+2*MIN_REDZONE)
#define POISON 0xa5

unsigned long COPY_LOOP(void *to, const void *from, unsigned long size);

static void do_one(char *src, char *dst, unsigned long src_off,
		   unsigned long dst_off, unsigned long len, void *redzone,
		   void *fill)
{
	char *srcp, *dstp;
	unsigned long ret;
	unsigned long i;

	srcp = src + MIN_REDZONE + src_off;
	dstp = dst + MIN_REDZONE + dst_off;

	memset(src, POISON, BUFLEN);
	memset(dst, POISON, BUFLEN);
	memcpy(srcp, fill, len);

	ret = COPY_LOOP(dstp, srcp, len);
	if (ret && ret != (unsigned long)dstp) {
		printf("(%p,%p,%ld) returned %ld\n", dstp, srcp, len, ret);
		abort();
	}

	if (memcmp(dstp, srcp, len)) {
		printf("(%p,%p,%ld) miscompare\n", dstp, srcp, len);
		printf("src: ");
		for (i = 0; i < len; i++)
			printf("%02x ", srcp[i]);
		printf("\ndst: ");
		for (i = 0; i < len; i++)
			printf("%02x ", dstp[i]);
		printf("\n");
		abort();
	}

	if (memcmp(dst, redzone, dstp - dst)) {
		printf("(%p,%p,%ld) redzone before corrupted\n",
		       dstp, srcp, len);
		abort();
	}

	if (memcmp(dstp+len, redzone, dst+BUFLEN-(dstp+len))) {
		printf("(%p,%p,%ld) redzone after corrupted\n",
		       dstp, srcp, len);
		abort();
	}
}

int test_copy_loop(void)
{
	char *src, *dst, *redzone, *fill;
	unsigned long len, src_off, dst_off;
	unsigned long i;

	src = memalign(BUFLEN, BUFLEN);
	dst = memalign(BUFLEN, BUFLEN);
	redzone = malloc(BUFLEN);
	fill = malloc(BUFLEN);

	if (!src || !dst || !redzone || !fill) {
		fprintf(stderr, "malloc failed\n");
		exit(1);
	}

	memset(redzone, POISON, BUFLEN);

	/* Fill with sequential bytes */
	for (i = 0; i < BUFLEN; i++)
		fill[i] = i & 0xff;

	for (len = 1; len < MAX_LEN; len++) {
		for (src_off = 0; src_off < MAX_OFFSET; src_off++) {
			for (dst_off = 0; dst_off < MAX_OFFSET; dst_off++) {
				do_one(src, dst, src_off, dst_off, len,
				       redzone, fill);
			}
		}
	}

	return 0;
}

int main(void)
{
	return test_harness(test_copy_loop, str(COPY_LOOP));
}

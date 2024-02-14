// SPDX-License-Identifier: GPL-2.0
// Carsten Haitzler <carsten.haitzler@arm.com>, 2021
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

struct args {
	pthread_t th;
	unsigned int in;
	void *ret;
};

static void *thrfn(void *arg)
{
	struct args *a = arg;
	unsigned int i, in = a->in;

	for (i = 0; i < 10000; i++) {
		asm volatile (
// force an unroll of thia add instruction so we can test long runs of code
#define SNIP1 "add %[in], %[in], #1\n"
// 10
#define SNIP2 SNIP1 SNIP1 SNIP1 SNIP1 SNIP1 SNIP1 SNIP1 SNIP1 SNIP1 SNIP1
// 100
#define SNIP3 SNIP2 SNIP2 SNIP2 SNIP2 SNIP2 SNIP2 SNIP2 SNIP2 SNIP2 SNIP2
// 1000
#define SNIP4 SNIP3 SNIP3 SNIP3 SNIP3 SNIP3 SNIP3 SNIP3 SNIP3 SNIP3 SNIP3
// 10000
#define SNIP5 SNIP4 SNIP4 SNIP4 SNIP4 SNIP4 SNIP4 SNIP4 SNIP4 SNIP4 SNIP4
// 100000
			SNIP5 SNIP5 SNIP5 SNIP5 SNIP5 SNIP5 SNIP5 SNIP5 SNIP5 SNIP5
			: /* out */
			: /* in */ [in] "r" (in)
			: /* clobber */
		);
	}
}

static pthread_t new_thr(void *(*fn) (void *arg), void *arg)
{
	pthread_t t;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_create(&t, &attr, fn, arg);
	return t;
}

int main(int argc, char **argv)
{
	unsigned int i, thr;
	struct args args[256];

	if (argc < 2) {
		printf("ERR: %s [numthreads]\n", argv[0]);
		exit(1);
	}

	thr = atoi(argv[1]);
	if ((thr > 256) || (thr < 1)) {
		printf("ERR: threads 1-256\n");
		exit(1);
	}
	for (i = 0; i < thr; i++) {
		args[i].in = rand();
		args[i].th = new_thr(thrfn, &(args[i]));
	}
	for (i = 0; i < thr; i++)
		pthread_join(args[i].th, &(args[i].ret));
	return 0;
}

// SPDX-License-Identifier: GPL-2.0
// Carsten Haitzler <carsten.haitzler@arm.com>, 2021

// define this for gettid()
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/syscall.h>
#ifndef SYS_gettid
// gettid is 178 on arm64
# define SYS_gettid 178
#endif
#define gettid() syscall(SYS_gettid)

struct args {
	unsigned int loops;
	pthread_t th;
	void *ret;
};

static void *thrfn(void *arg)
{
	struct args *a = arg;
	int i = 0, len = a->loops;

	if (getenv("SHOW_TID")) {
		unsigned long long tid = gettid();

		printf("%llu\n", tid);
	}
	asm volatile(
		"loop:\n"
		"add %w[i], %w[i], #1\n"
		"cmp %w[i], %w[len]\n"
		"blt loop\n"
		: /* out */
		: /* in */ [i] "r" (i), [len] "r" (len)
		: /* clobber */
	);
	return (void *)(long)i;
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
	unsigned int i, len, thr;
	struct args args[256];

	if (argc < 3) {
		printf("ERR: %s [numthreads] [numloops (millions)]\n", argv[0]);
		exit(1);
	}

	thr = atoi(argv[1]);
	if ((thr < 1) || (thr > 256)) {
		printf("ERR: threads 1-256\n");
		exit(1);
	}
	len = atoi(argv[2]);
	if ((len < 1) || (len > 4000)) {
		printf("ERR: max loops 4000 (millions)\n");
		exit(1);
	}
	len *= 1000000;
	for (i = 0; i < thr; i++) {
		args[i].loops = len;
		args[i].th = new_thr(thrfn, &(args[i]));
	}
	for (i = 0; i < thr; i++)
		pthread_join(args[i].th, &(args[i].ret));
	return 0;
}

// SPDX-License-Identifier: GPL-2.0
// Carsten Haitzler <carsten.haitzler@arm.com>, 2021
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

struct args {
	unsigned long loops;
	unsigned long size;
	pthread_t th;
	void *ret;
};

static void *thrfn(void *arg)
{
	struct args *a = arg;
	unsigned long i, len = a->loops;
	unsigned char *src, *dst;

	src = malloc(a->size * 1024);
	dst = malloc(a->size * 1024);
	if ((!src) || (!dst)) {
		printf("ERR: Can't allocate memory\n");
		exit(1);
	}
	for (i = 0; i < len; i++)
		memcpy(dst, src, a->size * 1024);
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
	unsigned long i, len, size, thr;
	struct args args[256];
	long long v;

	if (argc < 4) {
		printf("ERR: %s [copysize Kb] [numthreads] [numloops (hundreds)]\n", argv[0]);
		exit(1);
	}

	v = atoll(argv[1]);
	if ((v < 1) || (v > (1024 * 1024))) {
		printf("ERR: max memory 1GB (1048576 KB)\n");
		exit(1);
	}
	size = v;
	thr = atol(argv[2]);
	if ((thr < 1) || (thr > 256)) {
		printf("ERR: threads 1-256\n");
		exit(1);
	}
	v = atoll(argv[3]);
	if ((v < 1) || (v > 40000000000ll)) {
		printf("ERR: loops 1-40000000000 (hundreds)\n");
		exit(1);
	}
	len = v * 100;
	for (i = 0; i < thr; i++) {
		args[i].loops = len;
		args[i].size = size;
		args[i].th = new_thr(thrfn, &(args[i]));
	}
	for (i = 0; i < thr; i++)
		pthread_join(args[i].th, &(args[i].ret));
	return 0;
}

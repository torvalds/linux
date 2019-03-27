/*
 * This program tests if a new thread's initial tls data
 * is clean.
 *
 * David Xu <davidxu@freebsd.org>
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

int __thread n;

void
*f1(void *arg)
{
	if (n != 0) {
		printf("bug, n == %d \n", n);
		exit(1);
	}
	n = 1;
	return (0);
}

int
main(void)
{
	pthread_t td;
	int i;

	for (i = 0; i < 1000; ++i) {
		pthread_create(&td, NULL, f1, NULL);
		pthread_join(td, NULL);
	}
	sleep(2);
	for (i = 0; i < 1000; ++i) {
		pthread_create(&td, NULL, f1, NULL);
		pthread_join(td, NULL);
	}
	return (0);
}

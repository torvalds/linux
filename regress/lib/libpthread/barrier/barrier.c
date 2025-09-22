/*	$OpenBSD: barrier.c,v 1.2 2020/04/06 00:01:08 pirofti Exp $	*/
/* Paul Irofti <paul@irofti.net>, 2012. Public Domain. */

#include <stdio.h>

#include <pthread.h>
#include <errno.h>

#include "test.h"

void *
foo(void *arg)
{
	int rc = 0;
	pthread_barrier_t b = (pthread_barrier_t)arg;
	rc = pthread_barrier_wait(&b);
	if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD)
		PANIC("pthread_barrier_wait returned %d", rc);
	return NULL;
}

int main()
{
	int i;
	pthread_t thr[10];
	pthread_barrier_t b;

	pthread_barrierattr_t attr;
	CHECKr(pthread_barrierattr_init(&attr));
	CHECKr(pthread_barrierattr_getpshared(&attr, &i));
	_CHECK(i, == PTHREAD_PROCESS_PRIVATE, strerror(_x));
	CHECKr(pthread_barrierattr_setpshared(&attr, i));
	_CHECK(pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED), == ENOTSUP, strerror(_x));

	CHECKr(pthread_barrier_init(&b, &attr, 10));
	for (i = 0; i < 10; i++) {
		printf("Thread %d started\n", i);
		CHECKr(pthread_create(&thr[i], NULL, foo, (void *)b));
	}
	for (i = 0; i < 10; i++) {
		CHECKr(pthread_join(thr[i], NULL));
	}
	CHECKr(pthread_barrierattr_destroy(&attr));
	CHECKr(pthread_barrier_destroy(&b));

	SUCCEED;
}

/*	$OpenBSD: spinlock.c,v 1.2 2020/04/06 00:01:08 pirofti Exp $	*/
/* Paul Irofti <paul@irofti.net>, 2012. Public Domain. */

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include "test.h"

void *
foo(void *arg)
{
	int rc = 0;
	pthread_spinlock_t l = (pthread_spinlock_t)arg;
	rc = pthread_spin_trylock(&l);
	if (rc != 0 && rc != EBUSY) {
		PANIC("pthread_trylock returned %d", rc);
	}
	if (rc == 0) {
		CHECKr(pthread_spin_unlock(&l));
	}
	CHECKr(pthread_spin_lock(&l));
	CHECKr(pthread_spin_unlock(&l));
	return NULL;
}

int main()
{
	int i;
	pthread_t thr[10];
	pthread_spinlock_t l;

	_CHECK(pthread_spin_init(&l, PTHREAD_PROCESS_SHARED), == ENOTSUP,
	    strerror(_x));

	CHECKr(pthread_spin_init(&l, PTHREAD_PROCESS_PRIVATE));
	for (i = 0; i < 10; i++) {
		printf("Thread %d started\n", i);
		CHECKr(pthread_create(&thr[i], NULL, foo, (void *)l));
	}
	for (i = 0; i < 10; i++) {
		CHECKr(pthread_join(thr[i], NULL));
	}
	CHECKr(pthread_spin_destroy(&l));

	SUCCEED;
}

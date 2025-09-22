/*	$OpenBSD: sem_destroy.c,v 1.1.1.1 2012/01/04 17:36:40 mpi Exp $	*/
/*
 * Martin Pieuchot <mpi@openbsd.org>, 2011. Public Domain.
 */

#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include "test.h"

int val;
sem_t cons_sem;
sem_t prod_sem;

void *producer(void *arg);
void *consumer(void *arg);

int
main(int argc, char **argv)
{
	pthread_t prod_th, cons_th;
	long counter = 4;

	CHECKn(sem_destroy(&cons_sem));
	ASSERT(errno == EINVAL);

	val = 0;

	CHECKr(sem_init(&cons_sem, 0, 0));
	CHECKr(sem_init(&prod_sem, 0, 1));

	CHECKr(pthread_create(&prod_th, NULL, producer, &counter));
	CHECKr(pthread_create(&cons_th, NULL, consumer, &counter));

	CHECKr(pthread_join(prod_th, NULL));
	CHECKr(pthread_join(cons_th, NULL));

	pthread_exit(NULL);

	CHECKr(sem_destroy(&prod_sem));
	CHECKr(sem_destroy(&cons_sem));

	SUCCEED;
}

void *
producer(void *arg)
{
	long *counter = arg;
	int i;

	for (i = 0; i < *counter; i++) {
		sem_wait(&prod_sem);
		val++;
		sem_post(&cons_sem);
	}

	return (NULL);
}

void *
consumer(void *arg)
{
	long *counter = arg;
	int i;

	for (i = 0; i < *counter; i++) {
		sem_wait(&cons_sem);
		val--;
		sem_post(&prod_sem);
	}

	return (NULL);
}

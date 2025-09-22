/* $OpenBSD: pthread_rwlock.c,v 1.3 2014/05/20 01:25:24 guenther Exp $ */
/* PUBLIC DOMAIN Feb 2012 <guenther@openbsd.org> */

#include <sys/types.h>
#include <assert.h>
#include <err.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * Set up an rwlock with a few reader threads, start a writer blocking,
 * then let go the reader threads one by one.  Verify that the writer
 * thread gets, gets out, and then the rwlock can be locked for reading
 * again.
 */

pthread_rwlock_t	rw;

pthread_mutex_t m;
pthread_cond_t c;
enum
{
	UNLOCKED,
	NUM_READERS = 6,
	WRITE_STARTED,
	WRITE,
	WRITE_DONE,
} state;
time_t write_started;

static void *
reader(void *arg)
{
	int	me = *(int *)arg;
	int diff;

	pthread_mutex_lock(&m);
	assert(state < NUM_READERS);
	pthread_rwlock_rdlock(&rw);
	state++;
	printf("reader %d locked, state = %d\n", me, state);
	pthread_cond_broadcast(&c);
	pthread_mutex_unlock(&m);

	pthread_mutex_lock(&m);
	while (state < WRITE_STARTED) {
		pthread_cond_wait(&c, &m);
		printf("reader %d woken, state = %d\n", me, state);
	}

	diff = difftime(time(NULL), write_started);
	if (diff < 2)
		sleep(3 - diff);

	pthread_rwlock_unlock(&rw);
	printf("reader %d unlocked\n", me);
	sleep(1);

	pthread_mutex_unlock(&m);

	pthread_mutex_lock(&m);
	while (state >= WRITE_STARTED) {
		pthread_cond_wait(&c, &m);
		printf("reader %d woken, state = %d\n", me, state);
	}
	state++;
	printf("reader %d trying again (%d)\n", me, state);
	pthread_rwlock_rdlock(&rw);
	printf("reader %d locked again (%d)\n", me, state);
	pthread_cond_broadcast(&c);
	while (state != NUM_READERS) {
		pthread_cond_wait(&c, &m);
		printf("reader %d woken, state = %d\n", me, state);
	}
	pthread_mutex_unlock(&m);
	pthread_rwlock_unlock(&rw);

	printf("reader %d exiting\n", me);
	return NULL;
}

static void *
writer(void *arg)
{
	pthread_mutex_lock(&m);
	printf("writer started, state = %d\n", state);
	while (state != NUM_READERS) {
		pthread_cond_wait(&c, &m);
		printf("writer woken, state = %d\n", state);
	}
	state = WRITE_STARTED;
	printf("writer starting\n");
	write_started = time(NULL);
	pthread_cond_broadcast(&c);
	pthread_mutex_unlock(&m);

	pthread_rwlock_wrlock(&rw);
	printf("writer locked\n");

	pthread_mutex_lock(&m);
	state = WRITE;
	pthread_cond_broadcast(&c);

	while (state == WRITE)
		pthread_cond_wait(&c, &m);

	printf("writer unlocking\n");
	pthread_rwlock_unlock(&rw);
	state = UNLOCKED;
	pthread_cond_broadcast(&c);
	pthread_mutex_unlock(&m);

	printf("writer exiting\n");
	return NULL;
}


int
main(void)
{
	pthread_t	tr[NUM_READERS], tw;
	int	ids[NUM_READERS], i, r;

	pthread_rwlock_init(&rw, NULL);
	pthread_mutex_init(&m, NULL);
	pthread_cond_init(&c, NULL);
	state = UNLOCKED;

	for (i = 0; i < NUM_READERS; i++) {
		ids[i] = i;
		if ((r = pthread_create(&tr[i], NULL, reader, &ids[i])))
			errc(1, r, "create %d", i);
	}

	if ((r = pthread_create(&tw, NULL, writer, NULL)))
		errc(1, r, "create writer");

	pthread_mutex_lock(&m);
	while (state != WRITE)
		pthread_cond_wait(&c, &m);
	state = WRITE_DONE;
	pthread_mutex_unlock(&m);
	pthread_cond_broadcast(&c);

	pthread_join(tw, NULL);

	for (i = 0; i < NUM_READERS; i++)
		pthread_join(tr[i], NULL);
	return 0;
}

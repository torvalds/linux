/*
 * $FreeBSD$
 *
 * Test stack unwinding for mixed pthread_cleanup_push/pop and C++
 * object, both should work together.
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include <unistd.h>

#include "Test.cpp"

pthread_mutex_t mtx;
pthread_cond_t cv;

void f()
{
	Test t;

	pthread_mutex_lock(&mtx);
	pthread_cond_wait(&cv, &mtx);
	pthread_mutex_unlock(&mtx);
	printf("Bug, thread shouldn't be here.\n");
}

void g()
{
	f();
}

void *
thr(void *arg)
{
	pthread_cleanup_push(cleanup_handler, NULL);
	g();
	pthread_cleanup_pop(0);
	return (0);
}

int
main()
{
	pthread_t td;

	pthread_mutex_init(&mtx, NULL);
	pthread_cond_init(&cv, NULL);
	pthread_create(&td, NULL, thr, NULL);
	sleep(1);
	pthread_cancel(td);
	pthread_join(td, NULL);
	check_destruct2();
	return (0);
}

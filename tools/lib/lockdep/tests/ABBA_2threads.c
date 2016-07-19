#include <stdio.h>
#include <pthread.h>

pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;
pthread_barrier_t bar;

void *ba_lock(void *arg)
{
	int ret, i;

	pthread_mutex_lock(&b);

	if (pthread_barrier_wait(&bar) == PTHREAD_BARRIER_SERIAL_THREAD)
		pthread_barrier_destroy(&bar);

	pthread_mutex_lock(&a);

	pthread_mutex_unlock(&a);
	pthread_mutex_unlock(&b);
}

int main(void)
{
	pthread_t t;

	pthread_barrier_init(&bar, NULL, 2);

	if (pthread_create(&t, NULL, ba_lock, NULL)) {
		fprintf(stderr, "pthread_create() failed\n");
		return 1;
	}
	pthread_mutex_lock(&a);

	if (pthread_barrier_wait(&bar) == PTHREAD_BARRIER_SERIAL_THREAD)
		pthread_barrier_destroy(&bar);

	pthread_mutex_lock(&b);

	pthread_mutex_unlock(&b);
	pthread_mutex_unlock(&a);

	pthread_join(t, NULL);

	return 0;
}

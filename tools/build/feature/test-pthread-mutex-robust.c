// SPDX-License-Identifier: GPL-2.0
#include <pthread.h>

int main(void)
{
	pthread_mutexattr_t attr;
	pthread_mutex_t mtx;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
	pthread_mutex_init(&mtx, &attr);
	pthread_mutex_consistent(&mtx);
	pthread_mutex_destroy(&mtx);
	pthread_mutexattr_destroy(&attr);

	return 0;
}

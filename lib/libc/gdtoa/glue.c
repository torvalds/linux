/*
 * Machine-independent glue to integrate David Gay's gdtoa
 * package into libc.
 *
 * $FreeBSD$
 */

#include <pthread.h>

pthread_mutex_t __gdtoa_locks[] = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER
};

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBLOCKDEP_MUTEX_H
#define _LIBLOCKDEP_MUTEX_H

#include <pthread.h>
#include "common.h"

struct liblockdep_pthread_mutex {
	pthread_mutex_t mutex;
	struct lockdep_map dep_map;
};

typedef struct liblockdep_pthread_mutex liblockdep_pthread_mutex_t;

#define LIBLOCKDEP_PTHREAD_MUTEX_INITIALIZER(mtx)			\
		(const struct liblockdep_pthread_mutex) {		\
	.mutex = PTHREAD_MUTEX_INITIALIZER,				\
	.dep_map = STATIC_LOCKDEP_MAP_INIT(#mtx, &((&(mtx))->dep_map)),	\
}

static inline int __mutex_init(liblockdep_pthread_mutex_t *lock,
				const char *name,
				struct lock_class_key *key,
				const pthread_mutexattr_t *__mutexattr)
{
	lockdep_init_map(&lock->dep_map, name, key, 0);
	return pthread_mutex_init(&lock->mutex, __mutexattr);
}

#define liblockdep_pthread_mutex_init(mutex, mutexattr)		\
({								\
	static struct lock_class_key __key;			\
								\
	__mutex_init((mutex), #mutex, &__key, (mutexattr));	\
})

static inline int liblockdep_pthread_mutex_lock(liblockdep_pthread_mutex_t *lock)
{
	lock_acquire(&lock->dep_map, 0, 0, 0, 1, NULL, (unsigned long)_RET_IP_);
	return pthread_mutex_lock(&lock->mutex);
}

static inline int liblockdep_pthread_mutex_unlock(liblockdep_pthread_mutex_t *lock)
{
	lock_release(&lock->dep_map, 0, (unsigned long)_RET_IP_);
	return pthread_mutex_unlock(&lock->mutex);
}

static inline int liblockdep_pthread_mutex_trylock(liblockdep_pthread_mutex_t *lock)
{
	lock_acquire(&lock->dep_map, 0, 1, 0, 1, NULL, (unsigned long)_RET_IP_);
	return pthread_mutex_trylock(&lock->mutex) == 0 ? 1 : 0;
}

static inline int liblockdep_pthread_mutex_destroy(liblockdep_pthread_mutex_t *lock)
{
	lockdep_reset_lock(&lock->dep_map);
	return pthread_mutex_destroy(&lock->mutex);
}

#ifdef __USE_LIBLOCKDEP

#define pthread_mutex_t         liblockdep_pthread_mutex_t
#define pthread_mutex_init      liblockdep_pthread_mutex_init
#define pthread_mutex_lock      liblockdep_pthread_mutex_lock
#define pthread_mutex_unlock    liblockdep_pthread_mutex_unlock
#define pthread_mutex_trylock   liblockdep_pthread_mutex_trylock
#define pthread_mutex_destroy   liblockdep_pthread_mutex_destroy

#endif

#endif

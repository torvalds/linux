/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBLOCKDEP_RWLOCK_H
#define _LIBLOCKDEP_RWLOCK_H

#include <pthread.h>
#include "common.h"

struct liblockdep_pthread_rwlock {
	pthread_rwlock_t rwlock;
	struct lockdep_map dep_map;
};

typedef struct liblockdep_pthread_rwlock liblockdep_pthread_rwlock_t;

#define LIBLOCKDEP_PTHREAD_RWLOCK_INITIALIZER(rwl)			\
		(struct liblockdep_pthread_rwlock) {			\
	.rwlock = PTHREAD_RWLOCK_INITIALIZER,				\
	.dep_map = STATIC_LOCKDEP_MAP_INIT(#rwl, &((&(rwl))->dep_map)),	\
}

static inline int __rwlock_init(liblockdep_pthread_rwlock_t *lock,
				const char *name,
				struct lock_class_key *key,
				const pthread_rwlockattr_t *attr)
{
	lockdep_init_map(&lock->dep_map, name, key, 0);

	return pthread_rwlock_init(&lock->rwlock, attr);
}

#define liblockdep_pthread_rwlock_init(lock, attr)		\
({							\
	static struct lock_class_key __key;		\
							\
	__rwlock_init((lock), #lock, &__key, (attr));	\
})

static inline int liblockdep_pthread_rwlock_rdlock(liblockdep_pthread_rwlock_t *lock)
{
	lock_acquire(&lock->dep_map, 0, 0, 2, 1, NULL, (unsigned long)_RET_IP_);
	return pthread_rwlock_rdlock(&lock->rwlock);

}

static inline int liblockdep_pthread_rwlock_unlock(liblockdep_pthread_rwlock_t *lock)
{
	lock_release(&lock->dep_map, 0, (unsigned long)_RET_IP_);
	return pthread_rwlock_unlock(&lock->rwlock);
}

static inline int liblockdep_pthread_rwlock_wrlock(liblockdep_pthread_rwlock_t *lock)
{
	lock_acquire(&lock->dep_map, 0, 0, 0, 1, NULL, (unsigned long)_RET_IP_);
	return pthread_rwlock_wrlock(&lock->rwlock);
}

static inline int liblockdep_pthread_rwlock_tryrdlock(liblockdep_pthread_rwlock_t *lock)
{
	lock_acquire(&lock->dep_map, 0, 1, 2, 1, NULL, (unsigned long)_RET_IP_);
	return pthread_rwlock_tryrdlock(&lock->rwlock) == 0 ? 1 : 0;
}

static inline int liblockdep_pthread_rwlock_trywrlock(liblockdep_pthread_rwlock_t *lock)
{
	lock_acquire(&lock->dep_map, 0, 1, 0, 1, NULL, (unsigned long)_RET_IP_);
	return pthread_rwlock_trywrlock(&lock->rwlock) == 0 ? 1 : 0;
}

static inline int liblockdep_rwlock_destroy(liblockdep_pthread_rwlock_t *lock)
{
	return pthread_rwlock_destroy(&lock->rwlock);
}

#ifdef __USE_LIBLOCKDEP

#define pthread_rwlock_t		liblockdep_pthread_rwlock_t
#define pthread_rwlock_init		liblockdep_pthread_rwlock_init
#define pthread_rwlock_rdlock		liblockdep_pthread_rwlock_rdlock
#define pthread_rwlock_unlock		liblockdep_pthread_rwlock_unlock
#define pthread_rwlock_wrlock		liblockdep_pthread_rwlock_wrlock
#define pthread_rwlock_tryrdlock	liblockdep_pthread_rwlock_tryrdlock
#define pthread_rwlock_trywrlock	liblockdep_pthread_rwlock_trywrlock
#define pthread_rwlock_destroy		liblockdep_rwlock_destroy

#endif

#endif

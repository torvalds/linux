/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _TOOLS__RWSEM_H
#define _TOOLS__RWSEM_H

#include <pthread.h>

struct rw_semaphore {
	pthread_rwlock_t lock;
};

static inline int init_rwsem(struct rw_semaphore *sem)
{
	return pthread_rwlock_init(&sem->lock, NULL);
}

static inline int exit_rwsem(struct rw_semaphore *sem)
{
	return pthread_rwlock_destroy(&sem->lock);
}

static inline int down_read(struct rw_semaphore *sem)
{
	return pthread_rwlock_rdlock(&sem->lock);
}

static inline int up_read(struct rw_semaphore *sem)
{
	return pthread_rwlock_unlock(&sem->lock);
}

static inline int down_write(struct rw_semaphore *sem)
{
	return pthread_rwlock_wrlock(&sem->lock);
}

static inline int up_write(struct rw_semaphore *sem)
{
	return pthread_rwlock_unlock(&sem->lock);
}

#define down_read_nested(sem, subclass)		down_read(sem)
#define down_write_nested(sem, subclass)	down_write(sem)

#endif /* _TOOLS_RWSEM_H */

// SPDX-License-Identifier: GPL-2.0
#include "util.h"
#include "rwsem.h"

#if RWS_ERRORCHECK
#include "mutex.h"
#endif

int init_rwsem(struct rw_semaphore *sem)
{
#if RWS_ERRORCHECK
	mutex_init(&sem->mtx);
	return 0;
#else
	return pthread_rwlock_init(&sem->lock, NULL);
#endif
}

int exit_rwsem(struct rw_semaphore *sem)
{
#if RWS_ERRORCHECK
	mutex_destroy(&sem->mtx);
	return 0;
#else
	return pthread_rwlock_destroy(&sem->lock);
#endif
}

int down_read(struct rw_semaphore *sem)
{
#if RWS_ERRORCHECK
	mutex_lock(&sem->mtx);
	return 0;
#else
	return perf_singlethreaded ? 0 : pthread_rwlock_rdlock(&sem->lock);
#endif
}

int up_read(struct rw_semaphore *sem)
{
#if RWS_ERRORCHECK
	mutex_unlock(&sem->mtx);
	return 0;
#else
	return perf_singlethreaded ? 0 : pthread_rwlock_unlock(&sem->lock);
#endif
}

int down_write(struct rw_semaphore *sem)
{
#if RWS_ERRORCHECK
	mutex_lock(&sem->mtx);
	return 0;
#else
	return perf_singlethreaded ? 0 : pthread_rwlock_wrlock(&sem->lock);
#endif
}

int up_write(struct rw_semaphore *sem)
{
#if RWS_ERRORCHECK
	mutex_unlock(&sem->mtx);
	return 0;
#else
	return perf_singlethreaded ? 0 : pthread_rwlock_unlock(&sem->lock);
#endif
}

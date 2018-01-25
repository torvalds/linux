#include "util.h"
#include "rwsem.h"

int init_rwsem(struct rw_semaphore *sem)
{
	return pthread_rwlock_init(&sem->lock, NULL);
}

int exit_rwsem(struct rw_semaphore *sem)
{
	return pthread_rwlock_destroy(&sem->lock);
}

int down_read(struct rw_semaphore *sem)
{
	return perf_singlethreaded ? 0 : pthread_rwlock_rdlock(&sem->lock);
}

int up_read(struct rw_semaphore *sem)
{
	return perf_singlethreaded ? 0 : pthread_rwlock_unlock(&sem->lock);
}

int down_write(struct rw_semaphore *sem)
{
	return perf_singlethreaded ? 0 : pthread_rwlock_wrlock(&sem->lock);
}

int up_write(struct rw_semaphore *sem)
{
	return perf_singlethreaded ? 0 : pthread_rwlock_unlock(&sem->lock);
}

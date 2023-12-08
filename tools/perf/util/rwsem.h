#ifndef _PERF_RWSEM_H
#define _PERF_RWSEM_H

#include <pthread.h>

struct rw_semaphore {
	pthread_rwlock_t lock;
};

int init_rwsem(struct rw_semaphore *sem);
int exit_rwsem(struct rw_semaphore *sem);

int down_read(struct rw_semaphore *sem);
int up_read(struct rw_semaphore *sem);

int down_write(struct rw_semaphore *sem);
int up_write(struct rw_semaphore *sem);

#endif /* _PERF_RWSEM_H */

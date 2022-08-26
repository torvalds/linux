/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_MUTEX_H
#define __PERF_MUTEX_H

#include <pthread.h>
#include <stdbool.h>

/*
 * A wrapper around the mutex implementation that allows perf to error check
 * usage, etc.
 */
struct mutex {
	pthread_mutex_t lock;
};

/* A wrapper around the condition variable implementation. */
struct cond {
	pthread_cond_t cond;
};

/* Default initialize the mtx struct. */
void mutex_init(struct mutex *mtx);
/*
 * Initialize the mtx struct and set the process-shared rather than default
 * process-private attribute.
 */
void mutex_init_pshared(struct mutex *mtx);
void mutex_destroy(struct mutex *mtx);

void mutex_lock(struct mutex *mtx);
void mutex_unlock(struct mutex *mtx);
/* Tries to acquire the lock and returns true on success. */
bool mutex_trylock(struct mutex *mtx);

/* Default initialize the cond struct. */
void cond_init(struct cond *cnd);
/*
 * Initialize the cond struct and specify the process-shared rather than default
 * process-private attribute.
 */
void cond_init_pshared(struct cond *cnd);
void cond_destroy(struct cond *cnd);

void cond_wait(struct cond *cnd, struct mutex *mtx);
void cond_signal(struct cond *cnd);
void cond_broadcast(struct cond *cnd);

#endif /* __PERF_MUTEX_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_MUTEX_H
#define __PERF_MUTEX_H

#include <pthread.h>
#include <stdbool.h>

/*
 * A function-like feature checking macro that is a wrapper around
 * `__has_attribute`, which is defined by GCC 5+ and Clang and evaluates to a
 * nonzero constant integer if the attribute is supported or 0 if not.
 */
#ifdef __has_attribute
#define HAVE_ATTRIBUTE(x) __has_attribute(x)
#else
#define HAVE_ATTRIBUTE(x) 0
#endif

#if HAVE_ATTRIBUTE(guarded_by) && HAVE_ATTRIBUTE(pt_guarded_by) && \
	HAVE_ATTRIBUTE(lockable) && HAVE_ATTRIBUTE(exclusive_lock_function) && \
	HAVE_ATTRIBUTE(exclusive_trylock_function) && HAVE_ATTRIBUTE(exclusive_locks_required) && \
	HAVE_ATTRIBUTE(no_thread_safety_analysis)

/* Documents if a shared field or global variable needs to be protected by a mutex. */
#define GUARDED_BY(x) __attribute__((guarded_by(x)))

/*
 * Documents if the memory location pointed to by a pointer should be guarded by
 * a mutex when dereferencing the pointer.
 */
#define PT_GUARDED_BY(x) __attribute__((pt_guarded_by(x)))

/* Documents if a type is a lockable type. */
#define LOCKABLE __attribute__((lockable))

/* Documents a function that expects a lock not to be held prior to entry. */
#define LOCKS_EXCLUDED(...) __attribute__((locks_excluded(__VA_ARGS__)))

/* Documents a function that returns a lock. */
#define LOCK_RETURNED(x) __attribute__((lock_returned(x)))

/* Documents functions that acquire a lock in the body of a function, and do not release it. */
#define EXCLUSIVE_LOCK_FUNCTION(...)  __attribute__((exclusive_lock_function(__VA_ARGS__)))

/*
 * Documents functions that acquire a shared (reader) lock in the body of a
 * function, and do not release it.
 */
#define SHARED_LOCK_FUNCTION(...)  __attribute__((shared_lock_function(__VA_ARGS__)))

/*
 * Documents functions that expect a lock to be held on entry to the function,
 * and release it in the body of the function.
 */
#define UNLOCK_FUNCTION(...) __attribute__((unlock_function(__VA_ARGS__)))

/* Documents functions that try to acquire a lock, and return success or failure. */
#define EXCLUSIVE_TRYLOCK_FUNCTION(...) \
	__attribute__((exclusive_trylock_function(__VA_ARGS__)))

/* Documents a function that expects a mutex to be held prior to entry. */
#define EXCLUSIVE_LOCKS_REQUIRED(...) __attribute__((exclusive_locks_required(__VA_ARGS__)))

/* Documents a function that expects a shared (reader) lock to be held prior to entry. */
#define SHARED_LOCKS_REQUIRED(...) __attribute__((shared_locks_required(__VA_ARGS__)))

/* Turns off thread safety checking within the body of a particular function. */
#define NO_THREAD_SAFETY_ANALYSIS __attribute__((no_thread_safety_analysis))

#else

#define GUARDED_BY(x)
#define PT_GUARDED_BY(x)
#define LOCKABLE
#define LOCKS_EXCLUDED(...)
#define LOCK_RETURNED(x)
#define EXCLUSIVE_LOCK_FUNCTION(...)
#define SHARED_LOCK_FUNCTION(...)
#define UNLOCK_FUNCTION(...)
#define EXCLUSIVE_TRYLOCK_FUNCTION(...)
#define EXCLUSIVE_LOCKS_REQUIRED(...)
#define SHARED_LOCKS_REQUIRED(...)
#define NO_THREAD_SAFETY_ANALYSIS

#endif

/*
 * A wrapper around the mutex implementation that allows perf to error check
 * usage, etc.
 */
struct LOCKABLE mutex {
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
/* Initializes a mutex that may be recursively held on the same thread. */
void mutex_init_recursive(struct mutex *mtx);
void mutex_destroy(struct mutex *mtx);

void mutex_lock(struct mutex *mtx) EXCLUSIVE_LOCK_FUNCTION(*mtx);
void mutex_unlock(struct mutex *mtx) UNLOCK_FUNCTION(*mtx);
/* Tries to acquire the lock and returns true on success. */
bool mutex_trylock(struct mutex *mtx) EXCLUSIVE_TRYLOCK_FUNCTION(true, *mtx);

/* Default initialize the cond struct. */
void cond_init(struct cond *cnd);
/*
 * Initialize the cond struct and specify the process-shared rather than default
 * process-private attribute.
 */
void cond_init_pshared(struct cond *cnd);
void cond_destroy(struct cond *cnd);

void cond_wait(struct cond *cnd, struct mutex *mtx) EXCLUSIVE_LOCKS_REQUIRED(mtx);
void cond_signal(struct cond *cnd);
void cond_broadcast(struct cond *cnd);

#endif /* __PERF_MUTEX_H */

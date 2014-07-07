#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <sysexits.h>
#include "include/liblockdep/mutex.h"
#include "../../../include/linux/rbtree.h"

/**
 * struct lock_lookup - liblockdep's view of a single unique lock
 * @orig: pointer to the original pthread lock, used for lookups
 * @dep_map: lockdep's dep_map structure
 * @key: lockdep's key structure
 * @node: rb-tree node used to store the lock in a global tree
 * @name: a unique name for the lock
 */
struct lock_lookup {
	void *orig; /* Original pthread lock, used for lookups */
	struct lockdep_map dep_map; /* Since all locks are dynamic, we need
				     * a dep_map and a key for each lock */
	/*
	 * Wait, there's no support for key classes? Yup :(
	 * Most big projects wrap the pthread api with their own calls to
	 * be compatible with different locking methods. This means that
	 * "classes" will be brokes since the function that creates all
	 * locks will point to a generic locking function instead of the
	 * actual code that wants to do the locking.
	 */
	struct lock_class_key key;
	struct rb_node node;
#define LIBLOCKDEP_MAX_LOCK_NAME 22
	char name[LIBLOCKDEP_MAX_LOCK_NAME];
};

/* This is where we store our locks */
static struct rb_root locks = RB_ROOT;
static pthread_rwlock_t locks_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/* pthread mutex API */

#ifdef __GLIBC__
extern int __pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
extern int __pthread_mutex_lock(pthread_mutex_t *mutex);
extern int __pthread_mutex_trylock(pthread_mutex_t *mutex);
extern int __pthread_mutex_unlock(pthread_mutex_t *mutex);
extern int __pthread_mutex_destroy(pthread_mutex_t *mutex);
#else
#define __pthread_mutex_init	NULL
#define __pthread_mutex_lock	NULL
#define __pthread_mutex_trylock	NULL
#define __pthread_mutex_unlock	NULL
#define __pthread_mutex_destroy	NULL
#endif
static int (*ll_pthread_mutex_init)(pthread_mutex_t *mutex,
			const pthread_mutexattr_t *attr)	= __pthread_mutex_init;
static int (*ll_pthread_mutex_lock)(pthread_mutex_t *mutex)	= __pthread_mutex_lock;
static int (*ll_pthread_mutex_trylock)(pthread_mutex_t *mutex)	= __pthread_mutex_trylock;
static int (*ll_pthread_mutex_unlock)(pthread_mutex_t *mutex)	= __pthread_mutex_unlock;
static int (*ll_pthread_mutex_destroy)(pthread_mutex_t *mutex)	= __pthread_mutex_destroy;

/* pthread rwlock API */

#ifdef __GLIBC__
extern int __pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr);
extern int __pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
extern int __pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
extern int __pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
extern int __pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
extern int __pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
extern int __pthread_rwlock_unlock(pthread_rwlock_t *rwlock);
#else
#define __pthread_rwlock_init		NULL
#define __pthread_rwlock_destroy	NULL
#define __pthread_rwlock_wrlock		NULL
#define __pthread_rwlock_trywrlock	NULL
#define __pthread_rwlock_rdlock		NULL
#define __pthread_rwlock_tryrdlock	NULL
#define __pthread_rwlock_unlock		NULL
#endif

static int (*ll_pthread_rwlock_init)(pthread_rwlock_t *rwlock,
			const pthread_rwlockattr_t *attr)		= __pthread_rwlock_init;
static int (*ll_pthread_rwlock_destroy)(pthread_rwlock_t *rwlock)	= __pthread_rwlock_destroy;
static int (*ll_pthread_rwlock_rdlock)(pthread_rwlock_t *rwlock)	= __pthread_rwlock_rdlock;
static int (*ll_pthread_rwlock_tryrdlock)(pthread_rwlock_t *rwlock)	= __pthread_rwlock_tryrdlock;
static int (*ll_pthread_rwlock_trywrlock)(pthread_rwlock_t *rwlock)	= __pthread_rwlock_trywrlock;
static int (*ll_pthread_rwlock_wrlock)(pthread_rwlock_t *rwlock)	= __pthread_rwlock_wrlock;
static int (*ll_pthread_rwlock_unlock)(pthread_rwlock_t *rwlock)	= __pthread_rwlock_unlock;

enum { none, prepare, done, } __init_state;
static void init_preload(void);
static void try_init_preload(void)
{
	if (__init_state != done)
		init_preload();
}

static struct rb_node **__get_lock_node(void *lock, struct rb_node **parent)
{
	struct rb_node **node = &locks.rb_node;
	struct lock_lookup *l;

	*parent = NULL;

	while (*node) {
		l = rb_entry(*node, struct lock_lookup, node);

		*parent = *node;
		if (lock < l->orig)
			node = &l->node.rb_left;
		else if (lock > l->orig)
			node = &l->node.rb_right;
		else
			return node;
	}

	return node;
}

#ifndef LIBLOCKDEP_STATIC_ENTRIES
#define LIBLOCKDEP_STATIC_ENTRIES	1024
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static struct lock_lookup __locks[LIBLOCKDEP_STATIC_ENTRIES];
static int __locks_nr;

static inline bool is_static_lock(struct lock_lookup *lock)
{
	return lock >= __locks && lock < __locks + ARRAY_SIZE(__locks);
}

static struct lock_lookup *alloc_lock(void)
{
	if (__init_state != done) {
		/*
		 * Some programs attempt to initialize and use locks in their
		 * allocation path. This means that a call to malloc() would
		 * result in locks being initialized and locked.
		 *
		 * Why is it an issue for us? dlsym() below will try allocating
		 * to give us the original function. Since this allocation will
		 * result in a locking operations, we have to let pthread deal
		 * with it, but we can't! we don't have the pointer to the
		 * original API since we're inside dlsym() trying to get it
		 */

		int idx = __locks_nr++;
		if (idx >= ARRAY_SIZE(__locks)) {
			fprintf(stderr,
		"LOCKDEP error: insufficient LIBLOCKDEP_STATIC_ENTRIES\n");
			exit(EX_UNAVAILABLE);
		}
		return __locks + idx;
	}

	return malloc(sizeof(struct lock_lookup));
}

static inline void free_lock(struct lock_lookup *lock)
{
	if (likely(!is_static_lock(lock)))
		free(lock);
}

/**
 * __get_lock - find or create a lock instance
 * @lock: pointer to a pthread lock function
 *
 * Try to find an existing lock in the rbtree using the provided pointer. If
 * one wasn't found - create it.
 */
static struct lock_lookup *__get_lock(void *lock)
{
	struct rb_node **node, *parent;
	struct lock_lookup *l;

	ll_pthread_rwlock_rdlock(&locks_rwlock);
	node = __get_lock_node(lock, &parent);
	ll_pthread_rwlock_unlock(&locks_rwlock);
	if (*node) {
		return rb_entry(*node, struct lock_lookup, node);
	}

	/* We didn't find the lock, let's create it */
	l = alloc_lock();
	if (l == NULL)
		return NULL;

	l->orig = lock;
	/*
	 * Currently the name of the lock is the ptr value of the pthread lock,
	 * while not optimal, it makes debugging a bit easier.
	 *
	 * TODO: Get the real name of the lock using libdwarf
	 */
	sprintf(l->name, "%p", lock);
	lockdep_init_map(&l->dep_map, l->name, &l->key, 0);

	ll_pthread_rwlock_wrlock(&locks_rwlock);
	/* This might have changed since the last time we fetched it */
	node = __get_lock_node(lock, &parent);
	rb_link_node(&l->node, parent, node);
	rb_insert_color(&l->node, &locks);
	ll_pthread_rwlock_unlock(&locks_rwlock);

	return l;
}

static void __del_lock(struct lock_lookup *lock)
{
	ll_pthread_rwlock_wrlock(&locks_rwlock);
	rb_erase(&lock->node, &locks);
	ll_pthread_rwlock_unlock(&locks_rwlock);
	free_lock(lock);
}

int pthread_mutex_init(pthread_mutex_t *mutex,
			const pthread_mutexattr_t *attr)
{
	int r;

	/*
	 * We keep trying to init our preload module because there might be
	 * code in init sections that tries to touch locks before we are
	 * initialized, in that case we'll need to manually call preload
	 * to get us going.
	 *
	 * Funny enough, kernel's lockdep had the same issue, and used
	 * (almost) the same solution. See look_up_lock_class() in
	 * kernel/locking/lockdep.c for details.
	 */
	try_init_preload();

	r = ll_pthread_mutex_init(mutex, attr);
	if (r == 0)
		/*
		 * We do a dummy initialization here so that lockdep could
		 * warn us if something fishy is going on - such as
		 * initializing a held lock.
		 */
		__get_lock(mutex);

	return r;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	int r;

	try_init_preload();

	lock_acquire(&__get_lock(mutex)->dep_map, 0, 0, 0, 1, NULL,
			(unsigned long)_RET_IP_);
	/*
	 * Here's the thing with pthread mutexes: unlike the kernel variant,
	 * they can fail.
	 *
	 * This means that the behaviour here is a bit different from what's
	 * going on in the kernel: there we just tell lockdep that we took the
	 * lock before actually taking it, but here we must deal with the case
	 * that locking failed.
	 *
	 * To do that we'll "release" the lock if locking failed - this way
	 * we'll get lockdep doing the correct checks when we try to take
	 * the lock, and if that fails - we'll be back to the correct
	 * state by releasing it.
	 */
	r = ll_pthread_mutex_lock(mutex);
	if (r)
		lock_release(&__get_lock(mutex)->dep_map, 0, (unsigned long)_RET_IP_);

	return r;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	int r;

	try_init_preload();

	lock_acquire(&__get_lock(mutex)->dep_map, 0, 1, 0, 1, NULL, (unsigned long)_RET_IP_);
	r = ll_pthread_mutex_trylock(mutex);
	if (r)
		lock_release(&__get_lock(mutex)->dep_map, 0, (unsigned long)_RET_IP_);

	return r;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	int r;

	try_init_preload();

	lock_release(&__get_lock(mutex)->dep_map, 0, (unsigned long)_RET_IP_);
	/*
	 * Just like taking a lock, only in reverse!
	 *
	 * If we fail releasing the lock, tell lockdep we're holding it again.
	 */
	r = ll_pthread_mutex_unlock(mutex);
	if (r)
		lock_acquire(&__get_lock(mutex)->dep_map, 0, 0, 0, 1, NULL, (unsigned long)_RET_IP_);

	return r;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	try_init_preload();

	/*
	 * Let's see if we're releasing a lock that's held.
	 *
	 * TODO: Hook into free() and add that check there as well.
	 */
	debug_check_no_locks_freed(mutex, mutex + sizeof(*mutex));
	__del_lock(__get_lock(mutex));
	return ll_pthread_mutex_destroy(mutex);
}

/* This is the rwlock part, very similar to what happened with mutex above */
int pthread_rwlock_init(pthread_rwlock_t *rwlock,
			const pthread_rwlockattr_t *attr)
{
	int r;

	try_init_preload();

	r = ll_pthread_rwlock_init(rwlock, attr);
	if (r == 0)
		__get_lock(rwlock);

	return r;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
	try_init_preload();

	debug_check_no_locks_freed(rwlock, rwlock + sizeof(*rwlock));
	__del_lock(__get_lock(rwlock));
	return ll_pthread_rwlock_destroy(rwlock);
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
	int r;

        init_preload();

	lock_acquire(&__get_lock(rwlock)->dep_map, 0, 0, 2, 1, NULL, (unsigned long)_RET_IP_);
	r = ll_pthread_rwlock_rdlock(rwlock);
	if (r)
		lock_release(&__get_lock(rwlock)->dep_map, 0, (unsigned long)_RET_IP_);

	return r;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
	int r;

        init_preload();

	lock_acquire(&__get_lock(rwlock)->dep_map, 0, 1, 2, 1, NULL, (unsigned long)_RET_IP_);
	r = ll_pthread_rwlock_tryrdlock(rwlock);
	if (r)
		lock_release(&__get_lock(rwlock)->dep_map, 0, (unsigned long)_RET_IP_);

	return r;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
	int r;

        init_preload();

	lock_acquire(&__get_lock(rwlock)->dep_map, 0, 1, 0, 1, NULL, (unsigned long)_RET_IP_);
	r = ll_pthread_rwlock_trywrlock(rwlock);
	if (r)
                lock_release(&__get_lock(rwlock)->dep_map, 0, (unsigned long)_RET_IP_);

	return r;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
	int r;

        init_preload();

	lock_acquire(&__get_lock(rwlock)->dep_map, 0, 0, 0, 1, NULL, (unsigned long)_RET_IP_);
	r = ll_pthread_rwlock_wrlock(rwlock);
	if (r)
		lock_release(&__get_lock(rwlock)->dep_map, 0, (unsigned long)_RET_IP_);

	return r;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
	int r;

        init_preload();

	lock_release(&__get_lock(rwlock)->dep_map, 0, (unsigned long)_RET_IP_);
	r = ll_pthread_rwlock_unlock(rwlock);
	if (r)
		lock_acquire(&__get_lock(rwlock)->dep_map, 0, 0, 0, 1, NULL, (unsigned long)_RET_IP_);

	return r;
}

__attribute__((constructor)) static void init_preload(void)
{
	if (__init_state == done)
		return;

#ifndef __GLIBC__
	__init_state = prepare;

	ll_pthread_mutex_init = dlsym(RTLD_NEXT, "pthread_mutex_init");
	ll_pthread_mutex_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");
	ll_pthread_mutex_trylock = dlsym(RTLD_NEXT, "pthread_mutex_trylock");
	ll_pthread_mutex_unlock = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
	ll_pthread_mutex_destroy = dlsym(RTLD_NEXT, "pthread_mutex_destroy");

	ll_pthread_rwlock_init = dlsym(RTLD_NEXT, "pthread_rwlock_init");
	ll_pthread_rwlock_destroy = dlsym(RTLD_NEXT, "pthread_rwlock_destroy");
	ll_pthread_rwlock_rdlock = dlsym(RTLD_NEXT, "pthread_rwlock_rdlock");
	ll_pthread_rwlock_tryrdlock = dlsym(RTLD_NEXT, "pthread_rwlock_tryrdlock");
	ll_pthread_rwlock_wrlock = dlsym(RTLD_NEXT, "pthread_rwlock_wrlock");
	ll_pthread_rwlock_trywrlock = dlsym(RTLD_NEXT, "pthread_rwlock_trywrlock");
	ll_pthread_rwlock_unlock = dlsym(RTLD_NEXT, "pthread_rwlock_unlock");
#endif

	lockdep_init();

	__init_state = done;
}

/* $OpenBSD: rthread_libc.c,v 1.8 2025/08/07 03:40:50 dlg Exp $ */

/* PUBLIC DOMAIN: No Rights Reserved. Marco S Hyman <marc@snafu.org> */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "rthread.h"
#include "rthread_cb.h"

/*
 * A thread tag is a pointer to a structure of this type.  An opaque
 * tag is used to decouple libc from the thread library.
 */
struct _thread_tag {
	pthread_mutex_t	m;	/* the tag's mutex */
	pthread_key_t	k;	/* a key for private data */
};

/*
 * local mutex to protect against tag creation races.
 */
static pthread_mutex_t	_thread_tag_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Initialize a thread tag structure once.   This function is called
 * if the tag is null.  Allocation and initialization are controlled
 * by a mutex.   If the tag is not null when the mutex is obtained
 * the caller lost a race -- some other thread initialized the tag.
 * This function will never return NULL.
 */
static void
_thread_tag_init(void **tag, void (*dt)(void *))
{
	struct _thread_tag *tt;
	int result;

	result = pthread_mutex_lock(&_thread_tag_mutex);
	if (result == 0) {
		if (*tag == NULL) {
			tt = malloc(sizeof *tt);
			if (tt != NULL) {
				result = pthread_mutex_init(&tt->m, NULL);
				result |= pthread_key_create(&tt->k, dt ? dt :
				    free);
				*tag = tt;
			}
		}
		result |= pthread_mutex_unlock(&_thread_tag_mutex);
	}
	if (result != 0)
		_rthread_debug(1, "tag init failure");
}

/*
 * lock the mutex associated with the given tag
 */
void
_thread_tag_lock(void **tag)
{
	struct _thread_tag *tt;

	if (__isthreaded) {
		if (*tag == NULL)
			_thread_tag_init(tag, NULL);
		tt = *tag;
		if (pthread_mutex_lock(&tt->m) != 0)
			_rthread_debug(1, "tag mutex lock failure");
	}
}

/*
 * unlock the mutex associated with the given tag
 */
void
_thread_tag_unlock(void **tag)
{
	struct _thread_tag *tt;

	if (__isthreaded) {
		if (*tag == NULL)
			_thread_tag_init(tag, NULL);
		tt = *tag;
		if (pthread_mutex_unlock(&tt->m) != 0)
			_rthread_debug(1, "tag mutex unlock failure");
	}
}

/*
 * return the thread specific data for the given tag.   If there
 * is no data for this thread allocate and initialize it from 'storage'
 * or clear it for non-main threads.
 * On any error return 'err'.
 */
void *
_thread_tag_storage(void **tag, void *storage, size_t sz, void (*dt)(void *),
    void *err)
{
	struct _thread_tag *tt;
	void *ret;

	if (*tag == NULL)
		_thread_tag_init(tag, dt);
	tt = *tag;

	ret = pthread_getspecific(tt->k);
	if (ret == NULL) {
		ret = calloc(1, sz);
		if (ret == NULL)
			ret = err;
		else {
			if (pthread_setspecific(tt->k, ret) == 0) {
				if (pthread_self() == &_initial_thread)
					memcpy(ret, storage, sz);
			} else {
				free(ret);
				ret = err;
			}
		}
	}
	return ret;
}

void
_thread_mutex_lock(void **mutex)
{
	pthread_mutex_t	*pmutex = (pthread_mutex_t *)mutex;

	if (pthread_mutex_lock(pmutex) != 0)
		_rthread_debug(1, "mutex lock failure");
}

void
_thread_mutex_unlock(void **mutex)
{
	pthread_mutex_t	*pmutex = (pthread_mutex_t *)mutex;

	if (pthread_mutex_unlock(pmutex) != 0)
		_rthread_debug(1, "mutex unlock failure");
}

void
_thread_mutex_destroy(void **mutex)
{
	pthread_mutex_t	*pmutex = (pthread_mutex_t *)mutex;

	if (pthread_mutex_destroy(pmutex) != 0)
		_rthread_debug(1, "mutex destroy failure");
}

/*
 * the malloc lock
 */
#ifndef FUTEX
#define MALLOC_LOCK_INITIALIZER(n) { \
	_SPINLOCK_UNLOCKED,	\
	TAILQ_HEAD_INITIALIZER(malloc_lock[n].lockers), \
	PTHREAD_MUTEX_DEFAULT,	\
	NULL,			\
	0,			\
	-1 }
#else
#define MALLOC_LOCK_INITIALIZER(n) { \
	_SPINLOCK_UNLOCKED,	\
	PTHREAD_MUTEX_DEFAULT,	\
	NULL,			\
	0,			\
	-1 }
#endif

static struct pthread_mutex malloc_lock[_MALLOC_MUTEXES] = {
	MALLOC_LOCK_INITIALIZER(0),
	MALLOC_LOCK_INITIALIZER(1),
	MALLOC_LOCK_INITIALIZER(2),
	MALLOC_LOCK_INITIALIZER(3),
	MALLOC_LOCK_INITIALIZER(4),
	MALLOC_LOCK_INITIALIZER(5),
	MALLOC_LOCK_INITIALIZER(6),
	MALLOC_LOCK_INITIALIZER(7),
	MALLOC_LOCK_INITIALIZER(8),
	MALLOC_LOCK_INITIALIZER(9),
	MALLOC_LOCK_INITIALIZER(10),
	MALLOC_LOCK_INITIALIZER(11),
	MALLOC_LOCK_INITIALIZER(12),
	MALLOC_LOCK_INITIALIZER(13),
	MALLOC_LOCK_INITIALIZER(14),
	MALLOC_LOCK_INITIALIZER(15),
	MALLOC_LOCK_INITIALIZER(16),
	MALLOC_LOCK_INITIALIZER(17),
	MALLOC_LOCK_INITIALIZER(18),
	MALLOC_LOCK_INITIALIZER(19),
	MALLOC_LOCK_INITIALIZER(20),
	MALLOC_LOCK_INITIALIZER(21),
	MALLOC_LOCK_INITIALIZER(22),
	MALLOC_LOCK_INITIALIZER(23),
	MALLOC_LOCK_INITIALIZER(24),
	MALLOC_LOCK_INITIALIZER(25),
	MALLOC_LOCK_INITIALIZER(26),
	MALLOC_LOCK_INITIALIZER(27),
	MALLOC_LOCK_INITIALIZER(28),
	MALLOC_LOCK_INITIALIZER(29),
	MALLOC_LOCK_INITIALIZER(30),
	MALLOC_LOCK_INITIALIZER(31)
};

static pthread_mutex_t malloc_mutex[_MALLOC_MUTEXES] = {
	&malloc_lock[0],
	&malloc_lock[1],
	&malloc_lock[2],
	&malloc_lock[3],
	&malloc_lock[4],
	&malloc_lock[5],
	&malloc_lock[6],
	&malloc_lock[7],
	&malloc_lock[8],
	&malloc_lock[9],
	&malloc_lock[10],
	&malloc_lock[11],
	&malloc_lock[12],
	&malloc_lock[13],
	&malloc_lock[14],
	&malloc_lock[15],
	&malloc_lock[16],
	&malloc_lock[17],
	&malloc_lock[18],
	&malloc_lock[19],
	&malloc_lock[20],
	&malloc_lock[21],
	&malloc_lock[22],
	&malloc_lock[23],
	&malloc_lock[24],
	&malloc_lock[25],
	&malloc_lock[26],
	&malloc_lock[27],
	&malloc_lock[28],
	&malloc_lock[29],
	&malloc_lock[30],
	&malloc_lock[31]
};

void
_thread_malloc_lock(int i)
{
	pthread_mutex_lock(&malloc_mutex[i]);
}

void
_thread_malloc_unlock(int i)
{
	pthread_mutex_unlock(&malloc_mutex[i]);
}

static void
_thread_malloc_reinit(void)
{
	int i;

	for (i = 0; i < _MALLOC_MUTEXES; i++) {
		malloc_lock[i].lock = _SPINLOCK_UNLOCKED;
#ifndef FUTEX
		TAILQ_INIT(&malloc_lock[i].lockers);
#endif
		malloc_lock[i].owner = NULL;
		malloc_lock[i].count = 0;
	}
}

/*
 * atexit lock
 */
static struct __cmtx atexit_lock = __CMTX_INITIALIZER();

void
_thread_atexit_lock(void)
{
	__cmtx_enter(&atexit_lock);
}

void
_thread_atexit_unlock(void)
{
	__cmtx_leave(&atexit_lock);
}

/*
 * atfork lock
 */
static struct __cmtx atfork_lock = __CMTX_INITIALIZER();

void
_thread_atfork_lock(void)
{
	__cmtx_enter(&atfork_lock);
}

void
_thread_atfork_unlock(void)
{
	__cmtx_leave(&atfork_lock);
}

/*
 * arc4random lock
 */
static struct __cmtx arc4_lock = __CMTX_INITIALIZER();

void
_thread_arc4_lock(void)
{
	__cmtx_enter(&arc4_lock);
}

void
_thread_arc4_unlock(void)
{
	__cmtx_leave(&arc4_lock);
}

pid_t
_thread_dofork(pid_t (*sys_fork)(void))
{
	int i;
	pid_t newid;

	_thread_atexit_lock();
	for (i = 0; i < _MALLOC_MUTEXES; i++)
		_thread_malloc_lock(i);
	_thread_arc4_lock();

	newid = sys_fork();

	_thread_arc4_unlock();
	if (newid == 0)
		_thread_malloc_reinit();
	else
		for (i = 0; i < _MALLOC_MUTEXES; i++)
			_thread_malloc_unlock(i);
	_thread_atexit_unlock();

	return newid;
}


/**
 * testcode/checklocks.c - wrapper on locks that checks access.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 * 
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include <signal.h>
#include "util/locks.h"   /* include before checklocks.h */
#include "testcode/checklocks.h"

/**
 * \file
 * Locks that are checked.
 *
 * Ugly hack: uses the fact that workers start with an int thread_num, and
 * are passed to thread_create to make the thread numbers here the same as 
 * those used for logging which is nice.
 *
 * Todo: 
 *	 - debug status print, of thread lock stacks, and current waiting.
 */
#ifdef USE_THREAD_DEBUG

/** How long to wait before lock attempt is a failure. */
#define CHECK_LOCK_TIMEOUT 120 /* seconds */
/** How long to wait before join attempt is a failure. */
#define CHECK_JOIN_TIMEOUT 120 /* seconds */

/** if key has been created */
static int key_created = 0;
/** if the key was deleted, i.e. we have quit */
static int key_deleted = 0;
/** we hide the thread debug info with this key. */
static ub_thread_key_type thr_debug_key;
/** the list of threads, so all threads can be examined. NULL if unused. */
static struct thr_check* thread_infos[THRDEBUG_MAX_THREADS];
/** stored maximum lock number for threads, when a thread is restarted the
 * number is kept track of, because the new locks get new id numbers. */
static int thread_lockcount[THRDEBUG_MAX_THREADS];
/** do we check locking order */
int check_locking_order = 1;
/** the pid of this runset, reasonably unique. */
static pid_t check_lock_pid;
/** the name of the output file */
static const char* output_name = "ublocktrace";
/**
 * Should checklocks print a trace of the lock and unlock calls.
 * It uses fprintf for that because the log function uses a lock and that
 * would loop otherwise.
 */
static int verbose_locking = 0;
/**
 * Assume lock 0 0 (create_thread, create_instance), is the log lock and
 * do not print for that. Otherwise the output is full of log lock accesses.
 */
static int verbose_locking_not_loglock = 1;

/** print all possible debug info on the state of the system */
static void total_debug_info(void);
/** print pretty lock error and exit (decl for NORETURN attribute) */
static void lock_error(struct checked_lock* lock, const char* func,
	const char* file, int line, const char* err) ATTR_NORETURN;

/** print pretty lock error and exit */
static void lock_error(struct checked_lock* lock, 
	const char* func, const char* file, int line, const char* err)
{
	log_err("lock error (description follows)");
	log_err("Created at %s %s:%d", lock->create_func, 
		lock->create_file, lock->create_line);
	if(lock->holder_func && lock->holder_file)
		log_err("Previously %s %s:%d", lock->holder_func, 
			lock->holder_file, lock->holder_line);
	log_err("At %s %s:%d", func, file, line);
	log_err("Error for %s lock: %s",
		(lock->type==check_lock_mutex)?"mutex": (
		(lock->type==check_lock_spinlock)?"spinlock": (
		(lock->type==check_lock_rwlock)?"rwlock": "badtype")), err);
	log_err("complete status display:");
	total_debug_info();
	fatal_exit("bailing out");
}

/** 
 * Obtain lock on debug lock structure. This could be a deadlock by the caller.
 * The debug code itself does not deadlock. Anyway, check with timeouts. 
 * @param lock: on what to acquire lock.
 * @param func: user level caller identification.
 * @param file: user level caller identification.
 * @param line: user level caller identification.
 */
static void
acquire_locklock(struct checked_lock* lock, 
	const char* func, const char* file, int line)
{
	struct timespec to;
	int err;
	int contend = 0;
	/* first try; inc contention counter if not immediately */
	if((err = pthread_mutex_trylock(&lock->lock))) {
		if(err==EBUSY)
			contend++;
		else fatal_exit("error in mutex_trylock: %s", strerror(err));
	}
	if(!err)
		return; /* immediate success */
	to.tv_sec = time(NULL) + CHECK_LOCK_TIMEOUT;
	to.tv_nsec = 0;
	err = pthread_mutex_timedlock(&lock->lock, &to);
	if(err) {
		log_err("in acquiring locklock: %s", strerror(err));
		lock_error(lock, func, file, line, "acquire locklock");
	}
	/* since we hold the lock, we can edit the contention_count */
	lock->contention_count += contend;
}

/** add protected region */
void 
lock_protect_place(void* p, void* area, size_t size, const char* def_func,
	const char* def_file, int def_line, const char* def_area)
{
	struct checked_lock* lock = *(struct checked_lock**)p;
	struct protected_area* e = (struct protected_area*)malloc(
		sizeof(struct protected_area));
	if(!e)
		fatal_exit("lock_protect: out of memory");
	e->region = area;
	e->size = size;
	e->def_func = def_func;
	e->def_file = def_file;
	e->def_line = def_line;
	e->def_area = def_area;
	e->hold = malloc(size);
	if(!e->hold)
		fatal_exit("lock_protect: out of memory");
	memcpy(e->hold, e->region, e->size);

	acquire_locklock(lock, __func__, __FILE__, __LINE__);
	e->next = lock->prot;
	lock->prot = e;
	LOCKRET(pthread_mutex_unlock(&lock->lock));
}

/** remove protected region */
void
lock_unprotect(void* mangled, void* area)
{
	struct checked_lock* lock = *(struct checked_lock**)mangled;
	struct protected_area* p, **prevp;
	if(!lock) 
		return;
	acquire_locklock(lock, __func__, __FILE__, __LINE__);
	p = lock->prot;
	prevp = &lock->prot;
	while(p) {
		if(p->region == area) {
			*prevp = p->next;
			free(p->hold);
			free(p);
			LOCKRET(pthread_mutex_unlock(&lock->lock));
			return;
		}
		prevp = &p->next;
		p = p->next;
	}
	LOCKRET(pthread_mutex_unlock(&lock->lock));
}

/** 
 * Check protected memory region. Memory compare. Exit on error. 
 * @param lock: which lock to check.
 * @param func: location we are now (when failure is detected).
 * @param file: location we are now (when failure is detected).
 * @param line: location we are now (when failure is detected).
 */
static void 
prot_check(struct checked_lock* lock,
	const char* func, const char* file, int line)
{
	struct protected_area* p = lock->prot;
	while(p) {
		if(memcmp(p->hold, p->region, p->size) != 0) {
			log_hex("memory prev", p->hold, p->size);
			log_hex("memory here", p->region, p->size);
			log_err("lock_protect on %s %s:%d %s failed",
				p->def_func, p->def_file, p->def_line,
				p->def_area);
			lock_error(lock, func, file, line, 
				"protected area modified");
		}
		p = p->next;
	}
}

/** Copy protected memory region */
static void 
prot_store(struct checked_lock* lock)
{
	struct protected_area* p = lock->prot;
	while(p) {
		memcpy(p->hold, p->region, p->size);
		p = p->next;
	}
}

/** get memory held by lock */
size_t 
lock_get_mem(void* pp)
{
	size_t s;
	struct checked_lock* lock = *(struct checked_lock**)pp;
	struct protected_area* p;
	s = sizeof(struct checked_lock);
	acquire_locklock(lock, __func__, __FILE__, __LINE__);
	for(p = lock->prot; p; p = p->next) {
		s += sizeof(struct protected_area);
		s += p->size;
	}
	LOCKRET(pthread_mutex_unlock(&lock->lock));
	return s;
}

/** write lock trace info to file, while you hold those locks */
static void
ordercheck_locklock(struct thr_check* thr, struct checked_lock* lock)
{
	int info[4];
	if(!check_locking_order) return;
	if(!thr->holding_first) return; /* no older lock, no info */
	/* write: <lock id held> <lock id new> <file> <line> */
	info[0] = thr->holding_first->create_thread;
	info[1] = thr->holding_first->create_instance;
	info[2] = lock->create_thread;
	info[3] = lock->create_instance;
	if(fwrite(info, 4*sizeof(int), 1, thr->order_info) != 1 ||
		fwrite(lock->holder_file, strlen(lock->holder_file)+1, 1, 
		thr->order_info) != 1 ||
		fwrite(&lock->holder_line, sizeof(int), 1, 
		thr->order_info) != 1)
		log_err("fwrite: %s", strerror(errno));
}

/** write ordercheck lock creation details to file */
static void 
ordercheck_lockcreate(struct thr_check* thr, struct checked_lock* lock)
{
	/* write: <ffff = create> <lock id> <file> <line> */
	int cmd = -1;
	if(!check_locking_order) return;

	if( fwrite(&cmd, sizeof(int), 1, thr->order_info) != 1 ||
		fwrite(&lock->create_thread, sizeof(int), 1, 
			thr->order_info) != 1 ||
		fwrite(&lock->create_instance, sizeof(int), 1, 
			thr->order_info) != 1 ||
		fwrite(lock->create_file, strlen(lock->create_file)+1, 1, 
			thr->order_info) != 1 ||
		fwrite(&lock->create_line, sizeof(int), 1, 
		thr->order_info) != 1)
		log_err("fwrite: %s", strerror(errno));
}

/** alloc struct, init lock empty */
void 
checklock_init(enum check_lock_type type, struct checked_lock** lock,
        const char* func, const char* file, int line)
{
	struct checked_lock* e = (struct checked_lock*)calloc(1, 
		sizeof(struct checked_lock));
	struct thr_check *thr = (struct thr_check*)pthread_getspecific(
		thr_debug_key);
	if(!e)
		fatal_exit("%s %s %d: out of memory", func, file, line);
	if(!thr) {
		/* this is called when log_init() calls lock_init()
		 * functions, and the test check code has not yet
		 * been initialised.  But luckily, the checklock_start()
		 * routine can be called multiple times without ill effect.
		 */
		checklock_start();
		thr = (struct thr_check*)pthread_getspecific(thr_debug_key);
	}
	if(!thr)
		fatal_exit("%s %s %d: lock_init no thread info", func, file,
			line);
	*lock = e;
	e->type = type;
	e->create_func = func;
	e->create_file = file;
	e->create_line = line;
	e->create_thread = thr->num;
	e->create_instance = thr->locks_created++;
	ordercheck_lockcreate(thr, e);
	LOCKRET(pthread_mutex_init(&e->lock, NULL));
	switch(e->type) {
		case check_lock_mutex:
			LOCKRET(pthread_mutex_init(&e->u.mutex, NULL));
			break;
		case check_lock_spinlock:
			LOCKRET(pthread_spin_init(&e->u.spinlock, PTHREAD_PROCESS_PRIVATE));
			break;
		case check_lock_rwlock:
			LOCKRET(pthread_rwlock_init(&e->u.rwlock, NULL));
			break;
		default:
			log_assert(0);
	}
}

/** delete prot items */
static void 
prot_clear(struct checked_lock* lock)
{
	struct protected_area* p=lock->prot, *np;
	while(p) {
		np = p->next;
		free(p->hold);
		free(p);
		p = np;
	}
}

/** check if type is OK for the lock given */
static void 
checktype(enum check_lock_type type, struct checked_lock* lock,
        const char* func, const char* file, int line)
{
	if(!lock) 
		fatal_exit("use of null/deleted lock at %s %s:%d", 
			func, file, line);
	if(type != lock->type) {
		lock_error(lock, func, file, line, "wrong lock type");
	}
}

/** check if OK, free struct */
void 
checklock_destroy(enum check_lock_type type, struct checked_lock** lock,
        const char* func, const char* file, int line)
{
	const size_t contention_interest = 1; /* promille contented locks */
	struct checked_lock* e;
	if(!lock) 
		return;
	e = *lock;
	if(!e)
		return;
	checktype(type, e, func, file, line);

	/* check if delete is OK */
	acquire_locklock(e, func, file, line);
	if(e->hold_count != 0)
		lock_error(e, func, file, line, "delete while locked.");
	if(e->wait_count != 0)
		lock_error(e, func, file, line, "delete while waited on.");
	prot_check(e, func, file, line);
	*lock = NULL; /* use after free will fail */
	LOCKRET(pthread_mutex_unlock(&e->lock));

	/* contention, look at fraction in trouble. */
	if(e->history_count > 1 &&
	   1000*e->contention_count/e->history_count > contention_interest) {
		log_info("lock created %s %s %d has contention %u of %u (%d%%)",
			e->create_func, e->create_file, e->create_line,
			(unsigned int)e->contention_count, 
			(unsigned int)e->history_count,
			(int)(100*e->contention_count/e->history_count));
	}

	/* delete it */
	LOCKRET(pthread_mutex_destroy(&e->lock));
	prot_clear(e);
	/* since nobody holds the lock - see check above, no need to unlink 
	 * from the thread-held locks list. */
	switch(e->type) {
		case check_lock_mutex:
			LOCKRET(pthread_mutex_destroy(&e->u.mutex));
			break;
		case check_lock_spinlock:
			LOCKRET(pthread_spin_destroy(&e->u.spinlock));
			break;
		case check_lock_rwlock:
			LOCKRET(pthread_rwlock_destroy(&e->u.rwlock));
			break;
		default:
			log_assert(0);
	}
	memset(e, 0, sizeof(struct checked_lock));
	free(e);
}

/** finish acquiring lock, shared between _(rd|wr||)lock() routines */
static void 
finish_acquire_lock(struct thr_check* thr, struct checked_lock* lock,
        const char* func, const char* file, int line)
{
	thr->waiting = NULL;
	lock->wait_count --;
	lock->holder = thr;
	lock->hold_count ++;
	lock->holder_func = func;
	lock->holder_file = file;
	lock->holder_line = line;
	ordercheck_locklock(thr, lock);
	
	/* insert in thread lock list, as first */
	lock->prev_held_lock[thr->num] = NULL;
	lock->next_held_lock[thr->num] = thr->holding_first;
	if(thr->holding_first)
		/* no need to lock it, since this thread already holds the
		 * lock (since it is on this list) and we only edit thr->num
		 * member in array. So it is safe.  */
		thr->holding_first->prev_held_lock[thr->num] = lock;
	else	thr->holding_last = lock;
	thr->holding_first = lock;
}

/**
 * Locking routine.
 * @param type: as passed by user.
 * @param lock: as passed by user.
 * @param func: caller location.
 * @param file: caller location.
 * @param line: caller location.
 * @param tryfunc: the pthread_mutex_trylock or similar function.
 * @param timedfunc: the pthread_mutex_timedlock or similar function.
 *	Uses absolute timeout value.
 * @param arg: what to pass to tryfunc and timedlock.
 * @param exclusive: if lock must be exclusive (only one allowed).
 * @param getwr: if attempts to get writelock (or readlock) for rwlocks.
 */
static void 
checklock_lockit(enum check_lock_type type, struct checked_lock* lock,
        const char* func, const char* file, int line,
	int (*tryfunc)(void*), int (*timedfunc)(void*, struct timespec*),
	void* arg, int exclusive, int getwr)
{
	int err;
	int contend = 0;
	struct thr_check *thr = (struct thr_check*)pthread_getspecific(
		thr_debug_key);
	checktype(type, lock, func, file, line);
	if(!thr) lock_error(lock, func, file, line, "no thread info");
	
	acquire_locklock(lock, func, file, line);
	lock->wait_count ++;
	thr->waiting = lock;
	if(exclusive && lock->hold_count > 0 && lock->holder == thr) 
		lock_error(lock, func, file, line, "thread already owns lock");
	if(type==check_lock_rwlock && getwr && lock->writeholder == thr)
		lock_error(lock, func, file, line, "thread already has wrlock");
	LOCKRET(pthread_mutex_unlock(&lock->lock));

	/* first try; if busy increase contention counter */
	if((err=tryfunc(arg))) {
		struct timespec to;
		if(err != EBUSY) log_err("trylock: %s", strerror(err));
		to.tv_sec = time(NULL) + CHECK_LOCK_TIMEOUT;
		to.tv_nsec = 0;
		if((err=timedfunc(arg, &to))) {
			if(err == ETIMEDOUT)
				lock_error(lock, func, file, line, 
					"timeout possible deadlock");
			log_err("timedlock: %s", strerror(err));
		}
		contend ++;
	}
	/* got the lock */

	acquire_locklock(lock, func, file, line);
	lock->contention_count += contend;
	lock->history_count++;
	if(exclusive && lock->hold_count > 0)
		lock_error(lock, func, file, line, "got nonexclusive lock");
	if(type==check_lock_rwlock && getwr && lock->writeholder)
		lock_error(lock, func, file, line, "got nonexclusive wrlock");
	if(type==check_lock_rwlock && getwr)
		lock->writeholder = thr;
	/* check the memory areas for unauthorized changes,
	 * between last unlock time and current lock time.
	 * we check while holding the lock (threadsafe).
	 */
	if(getwr || exclusive)
		prot_check(lock, func, file, line);
	finish_acquire_lock(thr, lock, func, file, line);
	LOCKRET(pthread_mutex_unlock(&lock->lock));
}

/** helper for rdlock: try */
static int try_rd(void* arg)
{ return pthread_rwlock_tryrdlock((pthread_rwlock_t*)arg); }
/** helper for rdlock: timed */
static int timed_rd(void* arg, struct timespec* to)
{ return pthread_rwlock_timedrdlock((pthread_rwlock_t*)arg, to); }

/** check if OK, lock */
void 
checklock_rdlock(enum check_lock_type type, struct checked_lock* lock,
        const char* func, const char* file, int line)
{
	if(key_deleted)
		return;

	if(verbose_locking && !(verbose_locking_not_loglock &&
		lock->create_thread == 0 && lock->create_instance == 0))
		fprintf(stderr, "checklock_rdlock lock %d %d %s:%d at %s:%d\n", lock->create_thread, lock->create_instance, lock->create_file, lock->create_line, file, line);
	log_assert(type == check_lock_rwlock);
	checklock_lockit(type, lock, func, file, line,
		try_rd, timed_rd, &lock->u.rwlock, 0, 0);
}

/** helper for wrlock: try */
static int try_wr(void* arg)
{ return pthread_rwlock_trywrlock((pthread_rwlock_t*)arg); }
/** helper for wrlock: timed */
static int timed_wr(void* arg, struct timespec* to)
{ return pthread_rwlock_timedwrlock((pthread_rwlock_t*)arg, to); }

/** check if OK, lock */
void 
checklock_wrlock(enum check_lock_type type, struct checked_lock* lock,
        const char* func, const char* file, int line)
{
	if(key_deleted)
		return;
	log_assert(type == check_lock_rwlock);
	if(verbose_locking && !(verbose_locking_not_loglock &&
		lock->create_thread == 0 && lock->create_instance == 0))
		fprintf(stderr, "checklock_wrlock lock %d %d %s:%d at %s:%d\n", lock->create_thread, lock->create_instance, lock->create_file, lock->create_line, file, line);
	checklock_lockit(type, lock, func, file, line,
		try_wr, timed_wr, &lock->u.rwlock, 0, 1);
}

/** helper for lock mutex: try */
static int try_mutex(void* arg)
{ return pthread_mutex_trylock((pthread_mutex_t*)arg); }
/** helper for lock mutex: timed */
static int timed_mutex(void* arg, struct timespec* to)
{ return pthread_mutex_timedlock((pthread_mutex_t*)arg, to); }

/** helper for lock spinlock: try */
static int try_spinlock(void* arg)
{ return pthread_spin_trylock((pthread_spinlock_t*)arg); }
/** helper for lock spinlock: timed */
static int timed_spinlock(void* arg, struct timespec* to)
{
	int err;
	/* spin for 5 seconds. (ouch for the CPU, but it beats forever) */
	while( (err=try_spinlock(arg)) == EBUSY) {
#ifndef S_SPLINT_S
		if(time(NULL) >= to->tv_sec)
			return ETIMEDOUT;
		usleep(1000); /* in 1/1000000s of a second */
#endif
	}
	return err;
}

/** check if OK, lock */
void 
checklock_lock(enum check_lock_type type, struct checked_lock* lock,
        const char* func, const char* file, int line)
{
	if(key_deleted)
		return;
	log_assert(type != check_lock_rwlock);
	if(verbose_locking && !(verbose_locking_not_loglock &&
		lock->create_thread == 0 && lock->create_instance == 0))
		fprintf(stderr, "checklock_lock lock %d %d %s:%d at %s:%d\n", lock->create_thread, lock->create_instance, lock->create_file, lock->create_line, file, line);
	switch(type) {
		case check_lock_mutex:
			checklock_lockit(type, lock, func, file, line,
				try_mutex, timed_mutex, &lock->u.mutex, 1, 0);
			break;
		case check_lock_spinlock:
			/* void* cast needed because 'volatile' on some OS */
			checklock_lockit(type, lock, func, file, line,
				try_spinlock, timed_spinlock, 
				(void*)&lock->u.spinlock, 1, 0);
			break;
		default:
			log_assert(0);
	}
}

/** check if OK, unlock */
void 
checklock_unlock(enum check_lock_type type, struct checked_lock* lock,
        const char* func, const char* file, int line)
{
	struct thr_check *thr;
	if(key_deleted)
		return;
	thr = (struct thr_check*)pthread_getspecific(thr_debug_key);
	checktype(type, lock, func, file, line);
	if(!thr) lock_error(lock, func, file, line, "no thread info");

	acquire_locklock(lock, func, file, line);
	/* was this thread even holding this lock? */
	if(thr->holding_first != lock &&
		lock->prev_held_lock[thr->num] == NULL) {
		lock_error(lock, func, file, line, "unlock nonlocked lock");
	}
	if(lock->hold_count <= 0)
		lock_error(lock, func, file, line, "too many unlocks");

	if(verbose_locking && !(verbose_locking_not_loglock &&
		lock->create_thread == 0 && lock->create_instance == 0))
		fprintf(stderr, "checklock_unlock lock %d %d %s:%d at %s:%d\n", lock->create_thread, lock->create_instance, lock->create_file, lock->create_line, file, line);

	/* store this point as last touched by */
	lock->holder = thr;
	lock->hold_count --;
	lock->holder_func = func;
	lock->holder_file = file;
	lock->holder_line = line;

	/* delete from thread holder list */
	/* no need to lock other lockstructs, because they are all on the
	 * held-locks list, and this thread holds their locks.
	 * we only touch the thr->num members, so it is safe.  */
	if(thr->holding_first == lock)
		thr->holding_first = lock->next_held_lock[thr->num];
	if(thr->holding_last == lock)
		thr->holding_last = lock->prev_held_lock[thr->num];
	if(lock->next_held_lock[thr->num])
		lock->next_held_lock[thr->num]->prev_held_lock[thr->num] =
			lock->prev_held_lock[thr->num];
	if(lock->prev_held_lock[thr->num])
		lock->prev_held_lock[thr->num]->next_held_lock[thr->num] =
			lock->next_held_lock[thr->num];
	lock->next_held_lock[thr->num] = NULL;
	lock->prev_held_lock[thr->num] = NULL;

	if(type==check_lock_rwlock && lock->writeholder == thr) {
		lock->writeholder = NULL;
		prot_store(lock);
	} else if(type != check_lock_rwlock) {
		/* store memory areas that are protected, for later checks */
		prot_store(lock);
	}
	LOCKRET(pthread_mutex_unlock(&lock->lock));

	/* unlock it */
	switch(type) {
		case check_lock_mutex:
			LOCKRET(pthread_mutex_unlock(&lock->u.mutex));
			break;
		case check_lock_spinlock:
			LOCKRET(pthread_spin_unlock(&lock->u.spinlock));
			break;
		case check_lock_rwlock:
			LOCKRET(pthread_rwlock_unlock(&lock->u.rwlock));
			break;
		default:
			log_assert(0);
	}
}

void
checklock_set_output_name(const char* name)
{
	output_name = name;
}

/** open order info debug file, thr->num must be valid */
static void 
open_lockorder(struct thr_check* thr)
{
	char buf[24];
	time_t t;
	snprintf(buf, sizeof(buf), "%s.%d", output_name, thr->num);
	thr->locks_created = thread_lockcount[thr->num];
	if(thr->locks_created == 0) {
		thr->order_info = fopen(buf, "w");
		if(!thr->order_info)
			fatal_exit("could not open %s: %s", buf, strerror(errno));
	} else {
		/* There is already a file to append on with the previous
		 * thread information. */
		thr->order_info = fopen(buf, "a");
		if(!thr->order_info)
			fatal_exit("could not open for append %s: %s", buf, strerror(errno));
		return;
	}

	t = time(NULL);
	/* write: <time_stamp> <runpid> <thread_num> */
	if(fwrite(&t, sizeof(t), 1, thr->order_info) != 1 ||
		fwrite(&thr->num, sizeof(thr->num), 1, thr->order_info) != 1 || 
		fwrite(&check_lock_pid, sizeof(check_lock_pid), 1, 
		thr->order_info) != 1)
		log_err("fwrite: %s", strerror(errno));
}

/** checklock thread main, Inits thread structure */
static void* checklock_main(void* arg)
{
	struct thr_check* thr = (struct thr_check*)arg; 
	void* ret;
	thr->id = pthread_self();
	/* Hack to get same numbers as in log file */
	thr->num = *(int*)(thr->arg);
	log_assert(thr->num < THRDEBUG_MAX_THREADS);
	/* as an aside, due to this, won't work for libunbound bg thread */
	if(thread_infos[thr->num] != NULL)
		log_warn("thread warning, thr->num %d not NULL", thr->num);
	thread_infos[thr->num] = thr;
	LOCKRET(pthread_setspecific(thr_debug_key, thr));
	if(check_locking_order)
		open_lockorder(thr);
	ret = thr->func(thr->arg);
	thread_lockcount[thr->num] = thr->locks_created;
	thread_infos[thr->num] = NULL;
	if(check_locking_order)
		fclose(thr->order_info);
	free(thr);
	return ret;
}

/** init the main thread */
void checklock_start(void)
{
	if(key_deleted)
		return;
	if(!key_created) {
		struct thr_check* thisthr = (struct thr_check*)calloc(1, 
			sizeof(struct thr_check));
		if(!thisthr)
			fatal_exit("thrcreate: out of memory");
		key_created = 1;
		check_lock_pid = getpid();
		LOCKRET(pthread_key_create(&thr_debug_key, NULL));
		LOCKRET(pthread_setspecific(thr_debug_key, thisthr));
		thread_infos[0] = thisthr;
		if(check_locking_order)
			open_lockorder(thisthr);
	}
}

/** stop checklocks */
void checklock_stop(void)
{
	if(key_created) {
		int i;
		key_deleted = 1;
		if(check_locking_order)
			fclose(thread_infos[0]->order_info);
		free(thread_infos[0]);
		thread_infos[0] = NULL;
		for(i = 0; i < THRDEBUG_MAX_THREADS; i++)
			log_assert(thread_infos[i] == NULL);
			/* should have been cleaned up. */
		LOCKRET(pthread_key_delete(thr_debug_key));
		key_created = 0;
	}
}

/** allocate debug info and create thread */
void 
checklock_thrcreate(pthread_t* id, void* (*func)(void*), void* arg)
{
	struct thr_check* thr = (struct thr_check*)calloc(1, 
		sizeof(struct thr_check));
	if(!thr)
		fatal_exit("thrcreate: out of memory");
	if(!key_created) {
		checklock_start();
	}
	thr->func = func;
	thr->arg = arg;
	LOCKRET(pthread_create(id, NULL, checklock_main, thr));
}

/** count number of thread infos */
static int
count_thread_infos(void)
{
	int cnt = 0;
	int i;
	for(i=0; i<THRDEBUG_MAX_THREADS; i++)
		if(thread_infos[i])
			cnt++;
	return cnt;
}

/** print lots of info on a lock */
static void
lock_debug_info(struct checked_lock* lock)
{
	if(!lock) return;
	log_info("+++ Lock %llx, %d %d create %s %s %d",
		(unsigned long long)(size_t)lock, 
		lock->create_thread, lock->create_instance, 
		lock->create_func, lock->create_file, lock->create_line);
	log_info("lock type: %s",
		(lock->type==check_lock_mutex)?"mutex": (
		(lock->type==check_lock_spinlock)?"spinlock": (
		(lock->type==check_lock_rwlock)?"rwlock": "badtype")));
	log_info("lock contention %u, history:%u, hold:%d, wait:%d", 
		(unsigned)lock->contention_count, (unsigned)lock->history_count,
		lock->hold_count, lock->wait_count);
	log_info("last touch %s %s %d", lock->holder_func, lock->holder_file,
		lock->holder_line);
	log_info("holder thread %d, writeholder thread %d",
		lock->holder?lock->holder->num:-1,
		lock->writeholder?lock->writeholder->num:-1);
}

/** print debug locks held by a thread */
static void
held_debug_info(struct thr_check* thr, struct checked_lock* lock)
{
	if(!lock) return;
	lock_debug_info(lock);
	held_debug_info(thr, lock->next_held_lock[thr->num]);
}

/** print debug info for a thread */
static void
thread_debug_info(struct thr_check* thr)
{
	struct checked_lock* w = NULL;
	struct checked_lock* f = NULL;
	struct checked_lock* l = NULL;
	if(!thr) return;
	log_info("pthread id is %x", (int)thr->id);
	log_info("thread func is %llx", (unsigned long long)(size_t)thr->func);
	log_info("thread arg is %llx (%d)",
		(unsigned long long)(size_t)thr->arg, 
		(thr->arg?*(int*)thr->arg:0));
	log_info("thread num is %d", thr->num);
	log_info("locks created %d", thr->locks_created);
	log_info("open file for lockinfo: %s", 
		thr->order_info?"yes, flushing":"no");
	fflush(thr->order_info);
	w = thr->waiting;
	f = thr->holding_first;
	l = thr->holding_last;
	log_info("thread waiting for a lock: %s %llx", w?"yes":"no",
		(unsigned long long)(size_t)w);
	lock_debug_info(w);
	log_info("thread holding first: %s, last: %s", f?"yes":"no", 
		l?"yes":"no");
	held_debug_info(thr, f);
}

static void
total_debug_info(void)
{
	int i;
	log_info("checklocks: supervising %d threads.",
		count_thread_infos());
	if(!key_created) {
		log_info("No thread debug key created yet");
	}
	for(i=0; i<THRDEBUG_MAX_THREADS; i++) {
		if(thread_infos[i]) {
			log_info("*** Thread %d information: ***", i);
			thread_debug_info(thread_infos[i]);
		}
	}
}

/** signal handler for join timeout, Exits */
static RETSIGTYPE joinalarm(int ATTR_UNUSED(sig))
{
	log_err("join thread timeout. hangup or deadlock. Info follows.");
	total_debug_info();
	fatal_exit("join thread timeout. hangup or deadlock.");
}

/** wait for thread with a timeout */
void 
checklock_thrjoin(pthread_t thread)
{
	/* wait with a timeout */
	if(signal(SIGALRM, joinalarm) == SIG_ERR)
		fatal_exit("signal(): %s", strerror(errno));
	(void)alarm(CHECK_JOIN_TIMEOUT);
	LOCKRET(pthread_join(thread, NULL));
	(void)alarm(0);
}

#endif /* USE_THREAD_DEBUG */

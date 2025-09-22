/**
 * testcode/checklocks.h - wrapper on locks that checks access.
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

#ifndef TESTCODE_CHECK_LOCKS_H
#define TESTCODE_CHECK_LOCKS_H

/**
 * \file
 * Locks that are checked.
 *
 * Holds information per lock and per thread.
 * That information is protected by a mutex (unchecked).
 *
 * Checks:
 *      o which func, file, line created the lock.
 *      o contention count, measures amount of contention on the lock.
 *      o the memory region(s) that the lock protects are
 *        memcmp'ed to ascertain no race conditions.
 *      o checks that locks are unlocked properly (before deletion).
 *        keeps which func, file, line that locked it.
 *	o checks deadlocks with timeout so it can print errors for them.
 *
 * Limitations:
 *	o Detects unprotected memory access when the lock is locked or freed,
 *	  which detects races only if they happen, and only if in protected
 *	  memory areas.
 *	o Detects deadlocks by timeout, so approximately, as they happen.
 *	o Does not check order of locking.
 *	o Uses a lot of memory.
 *	o The checks use locks themselves, changing scheduling,
 *	  thus changing what races you see.
 */

#ifdef USE_THREAD_DEBUG
#ifndef HAVE_PTHREAD
/* we need the *timed*lock() routines to use for deadlock detection. */
#error "Need pthreads for checked locks"
#endif
/******************* THREAD DEBUG ************************/
#include <pthread.h>

/** How many threads to allocate for */
#define THRDEBUG_MAX_THREADS 32 /* threads */
/** do we check locking order */
extern int check_locking_order;

/**
 * Protection memory area.
 * It is copied to a holding buffer to compare against later.
 * Note that it may encompass the lock structure.
 */
struct protected_area {
	/** where the memory region starts */
	void* region;
	/** size of the region */
	size_t size;
	/** backbuffer that holds a copy, of same size. */
	void* hold;
	/** next protected area in list */
	struct protected_area* next;
	/** the place where the lock_protect is made, at init. */
	const char* def_func;
	/** the file where the lock_protect is made */
	const char* def_file;
	/** the line number where the lock_protect is made */
	int def_line;
	/** the text string for the area that is protected, at init call. */
	const char* def_area;
};

/**
 * Per thread information for locking debug wrappers. 
 */
struct thr_check {
	/** thread id */
	pthread_t id;
	/** real thread func */
	void* (*func)(void*);
	/** func user arg */
	void* arg;
	/** number of thread in list structure */
	int num;
	/** instance number - how many locks have been created by thread */
	int locks_created;
	/** file to write locking order information to */
	FILE* order_info;
	/** 
	 * List of locks that this thread is holding, double
	 * linked list. The first element is the most recent lock acquired.
	 * So it represents the stack of locks acquired. (of all types).
	 */
	struct checked_lock *holding_first, *holding_last;
	/** if the thread is currently waiting for a lock, which one */
	struct checked_lock* waiting;
};

/**
 * One structure for all types of locks.
 */
struct checked_lock {
	/** mutex for exclusive access to this structure */
	pthread_mutex_t lock;
	/** list of memory regions protected by this checked lock */
	struct protected_area* prot;
	/** where was this lock created */
	const char* create_func, *create_file;
	/** where was this lock created */
	int create_line;
	/** unique instance identifier */
	int create_thread, create_instance;
	/** contention count */
	size_t contention_count;
	/** number of times locked, ever */
	size_t history_count;
	/** hold count (how many threads are holding this lock) */
	int hold_count;
	/** how many threads are waiting for this lock */
	int wait_count;
	/** who touched it last */
	const char* holder_func, *holder_file;
	/** who touched it last */
	int holder_line;
	/** who owns the lock now */
	struct thr_check* holder;
	/** for rwlocks, the writelock holder */
	struct thr_check* writeholder;

	/** next lock a thread is holding (less recent) */
	struct checked_lock* next_held_lock[THRDEBUG_MAX_THREADS];
	/** prev lock a thread is holding (more recent) */
	struct checked_lock* prev_held_lock[THRDEBUG_MAX_THREADS];

	/** type of lock */
	enum check_lock_type {
		/** basic mutex */
		check_lock_mutex,
		/** fast spinlock */
		check_lock_spinlock,
		/** rwlock */
		check_lock_rwlock
	} type;
	/** the lock itself, see type to disambiguate the union */
	union {
		/** mutex */
		pthread_mutex_t mutex;
		/** spinlock */
		pthread_spinlock_t spinlock;
		/** rwlock */
		pthread_rwlock_t rwlock;
	} u;
};

/**
 * Additional call for the user to specify what areas are protected
 * @param lock: the lock that protects the area. It can be inside the area.
 *	The lock must be inited. Call with user lock. (any type).
 *	It demangles the lock itself (struct checked_lock**).
 * @param area: ptr to mem.
 * @param size: length of area.
 * @param def_func: function where the lock_protect() line is.
 * @param def_file: file where the lock_protect() line is.
 * @param def_line: line where the lock_protect() line is.
 * @param def_area: area string
 * You can call it multiple times with the same lock to give several areas.
 * Call it when you are done initializing the area, since it will be copied
 * at this time and protected right away against unauthorised changes until 
 * the next lock() call is done.
 */
void lock_protect_place(void* lock, void* area, size_t size,
	const char* def_func, const char* def_file, int def_line,
	const char* def_area);
#define lock_protect(lock, area, size) lock_protect_place(lock, area, size, __func__, __FILE__, __LINE__, #area)

/**
 * Remove protected area from lock.
 * No need to call this when deleting the lock.
 * @param lock: the lock, any type, (struct checked_lock**).
 * @param area: pointer to memory.
 */
void lock_unprotect(void* lock, void* area);

/**
 * Get memory associated with a checked lock
 * @param lock: the checked lock, any type. (struct checked_lock**).
 * @return: in bytes, including protected areas.
 */
size_t lock_get_mem(void* lock);

/**
 * Set the output name, prefix, of the lock check output file(s).
 * Call it before the checklock_start or thread creation. Pass a fixed string.
 * @param name: string to use for output data file names.
 */
void checklock_set_output_name(const char* name);

/**
 * Initialise checklock. Sets up internal debug structures.
 */
void checklock_start(void);

/**
 * Cleanup internal debug state.
 */
void checklock_stop(void);

/**
 * Init locks.
 * @param type: what type of lock this is.
 * @param lock: ptr to user alloced ptr structure. This is inited.
 *     So an alloc is done and the ptr is stored as result.
 * @param func: caller function name.
 * @param file: caller file name.
 * @param line: caller line number.
 */
void checklock_init(enum check_lock_type type, struct checked_lock** lock,
	const char* func, const char* file, int line);

/**
 * Destroy locks. Free the structure.
 * @param type: what type of lock this is.
 * @param lock: ptr to user alloced structure. This is destroyed.
 * @param func: caller function name.
 * @param file: caller file name.
 * @param line: caller line number.
 */
void checklock_destroy(enum check_lock_type type, struct checked_lock** lock,
	const char* func, const char* file, int line);

/**
 * Acquire readlock.
 * @param type: what type of lock this is. Had better be a rwlock.
 * @param lock: ptr to lock.
 * @param func: caller function name.
 * @param file: caller file name.
 * @param line: caller line number.
 */
void checklock_rdlock(enum check_lock_type type, struct checked_lock* lock,
	const char* func, const char* file, int line);

/**
 * Acquire writelock.
 * @param type: what type of lock this is. Had better be a rwlock.
 * @param lock: ptr to lock.
 * @param func: caller function name.
 * @param file: caller file name.
 * @param line: caller line number.
 */
void checklock_wrlock(enum check_lock_type type, struct checked_lock* lock,
	const char* func, const char* file, int line);

/**
 * Locks.
 * @param type: what type of lock this is. Had better be mutex or spinlock.
 * @param lock: the lock.
 * @param func: caller function name.
 * @param file: caller file name.
 * @param line: caller line number.
 */
void checklock_lock(enum check_lock_type type, struct checked_lock* lock,
	const char* func, const char* file, int line);

/**
 * Unlocks.
 * @param type: what type of lock this is.
 * @param lock: the lock.
 * @param func: caller function name.
 * @param file: caller file name.
 * @param line: caller line number.
 */
void checklock_unlock(enum check_lock_type type, struct checked_lock* lock,
	const char* func, const char* file, int line);

/**
 * Create thread.
 * @param thr: Thread id, where to store result.
 * @param func: thread start function.
 * @param arg: user argument.
 */
void checklock_thrcreate(pthread_t* thr, void* (*func)(void*), void* arg);

/**
 * Wait for thread to exit. Returns thread return value.
 * @param thread: thread to wait for.
 */
void checklock_thrjoin(pthread_t thread);

/** structures to enable compiler type checking on the locks. 
 * Also the pointer makes it so that the lock can be part of the protected
 * region without any possible problem (since the ptr will stay the same.)
 * i.e. there can be contention and readlocks stored in checked_lock, while
 * the protected area stays the same, even though it contains (ptr to) lock.
 */
struct checked_lock_rw { struct checked_lock* c_rw; };
/** structures to enable compiler type checking on the locks. */
struct checked_lock_mutex { struct checked_lock* c_m; };
/** structures to enable compiler type checking on the locks. */
struct checked_lock_spl { struct checked_lock* c_spl; };

/** debugging rwlock */
typedef struct checked_lock_rw lock_rw_type;
#define lock_rw_init(lock) checklock_init(check_lock_rwlock, &((lock)->c_rw), __func__, __FILE__, __LINE__)
#define lock_rw_destroy(lock) checklock_destroy(check_lock_rwlock, &((lock)->c_rw), __func__, __FILE__, __LINE__)
#define lock_rw_rdlock(lock) checklock_rdlock(check_lock_rwlock, (lock)->c_rw, __func__, __FILE__, __LINE__)
#define lock_rw_wrlock(lock) checklock_wrlock(check_lock_rwlock, (lock)->c_rw, __func__, __FILE__, __LINE__)
#define lock_rw_unlock(lock) checklock_unlock(check_lock_rwlock, (lock)->c_rw, __func__, __FILE__, __LINE__)

/** debugging mutex */
typedef struct checked_lock_mutex lock_basic_type;
#define lock_basic_init(lock) checklock_init(check_lock_mutex, &((lock)->c_m), __func__, __FILE__, __LINE__)
#define lock_basic_destroy(lock) checklock_destroy(check_lock_mutex, &((lock)->c_m), __func__, __FILE__, __LINE__)
#define lock_basic_lock(lock) checklock_lock(check_lock_mutex, (lock)->c_m, __func__, __FILE__, __LINE__)
#define lock_basic_unlock(lock) checklock_unlock(check_lock_mutex, (lock)->c_m, __func__, __FILE__, __LINE__)

/** debugging spinlock */
typedef struct checked_lock_spl lock_quick_type;
#define lock_quick_init(lock) checklock_init(check_lock_spinlock, &((lock)->c_spl), __func__, __FILE__, __LINE__)
#define lock_quick_destroy(lock) checklock_destroy(check_lock_spinlock, &((lock)->c_spl), __func__, __FILE__, __LINE__)
#define lock_quick_lock(lock) checklock_lock(check_lock_spinlock, (lock)->c_spl, __func__, __FILE__, __LINE__)
#define lock_quick_unlock(lock) checklock_unlock(check_lock_spinlock, (lock)->c_spl, __func__, __FILE__, __LINE__)

/** we use the pthread id, our thr_check structure is kept behind the scenes */
typedef pthread_t ub_thread_type;
#define ub_thread_create(thr, func, arg) checklock_thrcreate(thr, func, arg)
#define ub_thread_self() pthread_self()
#define ub_thread_join(thread) checklock_thrjoin(thread)

typedef pthread_key_t ub_thread_key_type;
#define ub_thread_key_create(key, f) LOCKRET(pthread_key_create(key, f))
#define ub_thread_key_set(key, v) LOCKRET(pthread_setspecific(key, v))
#define ub_thread_key_get(key) pthread_getspecific(key)

#endif /* USE_THREAD_DEBUG */
#endif /* TESTCODE_CHECK_LOCKS_H */

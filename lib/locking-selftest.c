// SPDX-License-Identifier: GPL-2.0
/*
 * lib/locking-selftest.c
 *
 * Testsuite for various locking APIs: spinlocks, rwlocks,
 * mutexes and rw-semaphores.
 *
 * It is checking both false positives and false negatives.
 *
 * Started by Ingo Molnar:
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/ww_mutex.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/delay.h>
#include <linux/lockdep.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/debug_locks.h>
#include <linux/irqflags.h>
#include <linux/rtmutex.h>
#include <linux/local_lock.h>

#ifdef CONFIG_PREEMPT_RT
# define NON_RT(...)
#else
# define NON_RT(...)	__VA_ARGS__
#endif

/*
 * Change this to 1 if you want to see the failure printouts:
 */
static unsigned int debug_locks_verbose;
unsigned int force_read_lock_recursive;

static DEFINE_WD_CLASS(ww_lockdep);

static int __init setup_debug_locks_verbose(char *str)
{
	get_option(&str, &debug_locks_verbose);

	return 1;
}

__setup("debug_locks_verbose=", setup_debug_locks_verbose);

#define FAILURE		0
#define SUCCESS		1

#define LOCKTYPE_SPIN	0x1
#define LOCKTYPE_RWLOCK	0x2
#define LOCKTYPE_MUTEX	0x4
#define LOCKTYPE_RWSEM	0x8
#define LOCKTYPE_WW	0x10
#define LOCKTYPE_RTMUTEX 0x20
#define LOCKTYPE_LL	0x40
#define LOCKTYPE_SPECIAL 0x80

static struct ww_acquire_ctx t, t2;
static struct ww_mutex o, o2, o3;

/*
 * Normal standalone locks, for the circular and irq-context
 * dependency tests:
 */
static DEFINE_SPINLOCK(lock_A);
static DEFINE_SPINLOCK(lock_B);
static DEFINE_SPINLOCK(lock_C);
static DEFINE_SPINLOCK(lock_D);

static DEFINE_RAW_SPINLOCK(raw_lock_A);
static DEFINE_RAW_SPINLOCK(raw_lock_B);

static DEFINE_RWLOCK(rwlock_A);
static DEFINE_RWLOCK(rwlock_B);
static DEFINE_RWLOCK(rwlock_C);
static DEFINE_RWLOCK(rwlock_D);

static DEFINE_MUTEX(mutex_A);
static DEFINE_MUTEX(mutex_B);
static DEFINE_MUTEX(mutex_C);
static DEFINE_MUTEX(mutex_D);

static DECLARE_RWSEM(rwsem_A);
static DECLARE_RWSEM(rwsem_B);
static DECLARE_RWSEM(rwsem_C);
static DECLARE_RWSEM(rwsem_D);

#ifdef CONFIG_RT_MUTEXES

static DEFINE_RT_MUTEX(rtmutex_A);
static DEFINE_RT_MUTEX(rtmutex_B);
static DEFINE_RT_MUTEX(rtmutex_C);
static DEFINE_RT_MUTEX(rtmutex_D);

#endif

/*
 * Locks that we initialize dynamically as well so that
 * e.g. X1 and X2 becomes two instances of the same class,
 * but X* and Y* are different classes. We do this so that
 * we do not trigger a real lockup:
 */
static DEFINE_SPINLOCK(lock_X1);
static DEFINE_SPINLOCK(lock_X2);
static DEFINE_SPINLOCK(lock_Y1);
static DEFINE_SPINLOCK(lock_Y2);
static DEFINE_SPINLOCK(lock_Z1);
static DEFINE_SPINLOCK(lock_Z2);

static DEFINE_RWLOCK(rwlock_X1);
static DEFINE_RWLOCK(rwlock_X2);
static DEFINE_RWLOCK(rwlock_Y1);
static DEFINE_RWLOCK(rwlock_Y2);
static DEFINE_RWLOCK(rwlock_Z1);
static DEFINE_RWLOCK(rwlock_Z2);

static DEFINE_MUTEX(mutex_X1);
static DEFINE_MUTEX(mutex_X2);
static DEFINE_MUTEX(mutex_Y1);
static DEFINE_MUTEX(mutex_Y2);
static DEFINE_MUTEX(mutex_Z1);
static DEFINE_MUTEX(mutex_Z2);

static DECLARE_RWSEM(rwsem_X1);
static DECLARE_RWSEM(rwsem_X2);
static DECLARE_RWSEM(rwsem_Y1);
static DECLARE_RWSEM(rwsem_Y2);
static DECLARE_RWSEM(rwsem_Z1);
static DECLARE_RWSEM(rwsem_Z2);

#ifdef CONFIG_RT_MUTEXES

static DEFINE_RT_MUTEX(rtmutex_X1);
static DEFINE_RT_MUTEX(rtmutex_X2);
static DEFINE_RT_MUTEX(rtmutex_Y1);
static DEFINE_RT_MUTEX(rtmutex_Y2);
static DEFINE_RT_MUTEX(rtmutex_Z1);
static DEFINE_RT_MUTEX(rtmutex_Z2);

#endif

static DEFINE_PER_CPU(local_lock_t, local_A);

/*
 * non-inlined runtime initializers, to let separate locks share
 * the same lock-class:
 */
#define INIT_CLASS_FUNC(class) 				\
static noinline void					\
init_class_##class(spinlock_t *lock, rwlock_t *rwlock, \
	struct mutex *mutex, struct rw_semaphore *rwsem)\
{							\
	spin_lock_init(lock);			\
	rwlock_init(rwlock);				\
	mutex_init(mutex);				\
	init_rwsem(rwsem);				\
}

INIT_CLASS_FUNC(X)
INIT_CLASS_FUNC(Y)
INIT_CLASS_FUNC(Z)

static void init_shared_classes(void)
{
#ifdef CONFIG_RT_MUTEXES
	static struct lock_class_key rt_X, rt_Y, rt_Z;

	__rt_mutex_init(&rtmutex_X1, __func__, &rt_X);
	__rt_mutex_init(&rtmutex_X2, __func__, &rt_X);
	__rt_mutex_init(&rtmutex_Y1, __func__, &rt_Y);
	__rt_mutex_init(&rtmutex_Y2, __func__, &rt_Y);
	__rt_mutex_init(&rtmutex_Z1, __func__, &rt_Z);
	__rt_mutex_init(&rtmutex_Z2, __func__, &rt_Z);
#endif

	init_class_X(&lock_X1, &rwlock_X1, &mutex_X1, &rwsem_X1);
	init_class_X(&lock_X2, &rwlock_X2, &mutex_X2, &rwsem_X2);

	init_class_Y(&lock_Y1, &rwlock_Y1, &mutex_Y1, &rwsem_Y1);
	init_class_Y(&lock_Y2, &rwlock_Y2, &mutex_Y2, &rwsem_Y2);

	init_class_Z(&lock_Z1, &rwlock_Z1, &mutex_Z1, &rwsem_Z1);
	init_class_Z(&lock_Z2, &rwlock_Z2, &mutex_Z2, &rwsem_Z2);
}

/*
 * For spinlocks and rwlocks we also do hardirq-safe / softirq-safe tests.
 * The following functions use a lock from a simulated hardirq/softirq
 * context, causing the locks to be marked as hardirq-safe/softirq-safe:
 */

#define HARDIRQ_DISABLE		local_irq_disable
#define HARDIRQ_ENABLE		local_irq_enable

#define HARDIRQ_ENTER()				\
	local_irq_disable();			\
	__irq_enter();				\
	lockdep_hardirq_threaded();		\
	WARN_ON(!in_irq());

#define HARDIRQ_EXIT()				\
	__irq_exit();				\
	local_irq_enable();

#define SOFTIRQ_DISABLE		local_bh_disable
#define SOFTIRQ_ENABLE		local_bh_enable

#define SOFTIRQ_ENTER()				\
		local_bh_disable();		\
		local_irq_disable();		\
		lockdep_softirq_enter();	\
		WARN_ON(!in_softirq());

#define SOFTIRQ_EXIT()				\
		lockdep_softirq_exit();		\
		local_irq_enable();		\
		local_bh_enable();

/*
 * Shortcuts for lock/unlock API variants, to keep
 * the testcases compact:
 */
#define L(x)			spin_lock(&lock_##x)
#define U(x)			spin_unlock(&lock_##x)
#define LU(x)			L(x); U(x)
#define SI(x)			spin_lock_init(&lock_##x)

#define WL(x)			write_lock(&rwlock_##x)
#define WU(x)			write_unlock(&rwlock_##x)
#define WLU(x)			WL(x); WU(x)

#define RL(x)			read_lock(&rwlock_##x)
#define RU(x)			read_unlock(&rwlock_##x)
#define RLU(x)			RL(x); RU(x)
#define RWI(x)			rwlock_init(&rwlock_##x)

#define ML(x)			mutex_lock(&mutex_##x)
#define MU(x)			mutex_unlock(&mutex_##x)
#define MI(x)			mutex_init(&mutex_##x)

#define RTL(x)			rt_mutex_lock(&rtmutex_##x)
#define RTU(x)			rt_mutex_unlock(&rtmutex_##x)
#define RTI(x)			rt_mutex_init(&rtmutex_##x)

#define WSL(x)			down_write(&rwsem_##x)
#define WSU(x)			up_write(&rwsem_##x)

#define RSL(x)			down_read(&rwsem_##x)
#define RSU(x)			up_read(&rwsem_##x)
#define RWSI(x)			init_rwsem(&rwsem_##x)

#ifndef CONFIG_DEBUG_WW_MUTEX_SLOWPATH
#define WWAI(x)			ww_acquire_init(x, &ww_lockdep)
#else
#define WWAI(x)			do { ww_acquire_init(x, &ww_lockdep); (x)->deadlock_inject_countdown = ~0U; } while (0)
#endif
#define WWAD(x)			ww_acquire_done(x)
#define WWAF(x)			ww_acquire_fini(x)

#define WWL(x, c)		ww_mutex_lock(x, c)
#define WWT(x)			ww_mutex_trylock(x, NULL)
#define WWL1(x)			ww_mutex_lock(x, NULL)
#define WWU(x)			ww_mutex_unlock(x)


#define LOCK_UNLOCK_2(x,y)	LOCK(x); LOCK(y); UNLOCK(y); UNLOCK(x)

/*
 * Generate different permutations of the same testcase, using
 * the same basic lock-dependency/state events:
 */

#define GENERATE_TESTCASE(name)			\
						\
static void name(void) { E(); }

#define GENERATE_PERMUTATIONS_2_EVENTS(name)	\
						\
static void name##_12(void) { E1(); E2(); }	\
static void name##_21(void) { E2(); E1(); }

#define GENERATE_PERMUTATIONS_3_EVENTS(name)		\
							\
static void name##_123(void) { E1(); E2(); E3(); }	\
static void name##_132(void) { E1(); E3(); E2(); }	\
static void name##_213(void) { E2(); E1(); E3(); }	\
static void name##_231(void) { E2(); E3(); E1(); }	\
static void name##_312(void) { E3(); E1(); E2(); }	\
static void name##_321(void) { E3(); E2(); E1(); }

/*
 * AA deadlock:
 */

#define E()					\
						\
	LOCK(X1);				\
	LOCK(X2); /* this one should fail */

/*
 * 6 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_TESTCASE(AA_spin)
#include "locking-selftest-wlock.h"
GENERATE_TESTCASE(AA_wlock)
#include "locking-selftest-rlock.h"
GENERATE_TESTCASE(AA_rlock)
#include "locking-selftest-mutex.h"
GENERATE_TESTCASE(AA_mutex)
#include "locking-selftest-wsem.h"
GENERATE_TESTCASE(AA_wsem)
#include "locking-selftest-rsem.h"
GENERATE_TESTCASE(AA_rsem)

#ifdef CONFIG_RT_MUTEXES
#include "locking-selftest-rtmutex.h"
GENERATE_TESTCASE(AA_rtmutex);
#endif

#undef E

/*
 * Special-case for read-locking, they are
 * allowed to recurse on the same lock class:
 */
static void rlock_AA1(void)
{
	RL(X1);
	RL(X1); // this one should NOT fail
}

static void rlock_AA1B(void)
{
	RL(X1);
	RL(X2); // this one should NOT fail
}

static void rsem_AA1(void)
{
	RSL(X1);
	RSL(X1); // this one should fail
}

static void rsem_AA1B(void)
{
	RSL(X1);
	RSL(X2); // this one should fail
}
/*
 * The mixing of read and write locks is not allowed:
 */
static void rlock_AA2(void)
{
	RL(X1);
	WL(X2); // this one should fail
}

static void rsem_AA2(void)
{
	RSL(X1);
	WSL(X2); // this one should fail
}

static void rlock_AA3(void)
{
	WL(X1);
	RL(X2); // this one should fail
}

static void rsem_AA3(void)
{
	WSL(X1);
	RSL(X2); // this one should fail
}

/*
 * read_lock(A)
 * spin_lock(B)
 *		spin_lock(B)
 *		write_lock(A)
 */
static void rlock_ABBA1(void)
{
	RL(X1);
	L(Y1);
	U(Y1);
	RU(X1);

	L(Y1);
	WL(X1);
	WU(X1);
	U(Y1); // should fail
}

static void rwsem_ABBA1(void)
{
	RSL(X1);
	ML(Y1);
	MU(Y1);
	RSU(X1);

	ML(Y1);
	WSL(X1);
	WSU(X1);
	MU(Y1); // should fail
}

/*
 * read_lock(A)
 * spin_lock(B)
 *		spin_lock(B)
 *		write_lock(A)
 *
 * This test case is aimed at poking whether the chain cache prevents us from
 * detecting a read-lock/lock-write deadlock: if the chain cache doesn't differ
 * read/write locks, the following case may happen
 *
 * 	{ read_lock(A)->lock(B) dependency exists }
 *
 * 	P0:
 * 	lock(B);
 * 	read_lock(A);
 *
 *	{ Not a deadlock, B -> A is added in the chain cache }
 *
 *	P1:
 *	lock(B);
 *	write_lock(A);
 *
 *	{ B->A found in chain cache, not reported as a deadlock }
 *
 */
static void rlock_chaincache_ABBA1(void)
{
	RL(X1);
	L(Y1);
	U(Y1);
	RU(X1);

	L(Y1);
	RL(X1);
	RU(X1);
	U(Y1);

	L(Y1);
	WL(X1);
	WU(X1);
	U(Y1); // should fail
}

/*
 * read_lock(A)
 * spin_lock(B)
 *		spin_lock(B)
 *		read_lock(A)
 */
static void rlock_ABBA2(void)
{
	RL(X1);
	L(Y1);
	U(Y1);
	RU(X1);

	L(Y1);
	RL(X1);
	RU(X1);
	U(Y1); // should NOT fail
}

static void rwsem_ABBA2(void)
{
	RSL(X1);
	ML(Y1);
	MU(Y1);
	RSU(X1);

	ML(Y1);
	RSL(X1);
	RSU(X1);
	MU(Y1); // should fail
}


/*
 * write_lock(A)
 * spin_lock(B)
 *		spin_lock(B)
 *		write_lock(A)
 */
static void rlock_ABBA3(void)
{
	WL(X1);
	L(Y1);
	U(Y1);
	WU(X1);

	L(Y1);
	WL(X1);
	WU(X1);
	U(Y1); // should fail
}

static void rwsem_ABBA3(void)
{
	WSL(X1);
	ML(Y1);
	MU(Y1);
	WSU(X1);

	ML(Y1);
	WSL(X1);
	WSU(X1);
	MU(Y1); // should fail
}

/*
 * ABBA deadlock:
 */

#define E()					\
						\
	LOCK_UNLOCK_2(A, B);			\
	LOCK_UNLOCK_2(B, A); /* fail */

/*
 * 6 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_TESTCASE(ABBA_spin)
#include "locking-selftest-wlock.h"
GENERATE_TESTCASE(ABBA_wlock)
#include "locking-selftest-rlock.h"
GENERATE_TESTCASE(ABBA_rlock)
#include "locking-selftest-mutex.h"
GENERATE_TESTCASE(ABBA_mutex)
#include "locking-selftest-wsem.h"
GENERATE_TESTCASE(ABBA_wsem)
#include "locking-selftest-rsem.h"
GENERATE_TESTCASE(ABBA_rsem)

#ifdef CONFIG_RT_MUTEXES
#include "locking-selftest-rtmutex.h"
GENERATE_TESTCASE(ABBA_rtmutex);
#endif

#undef E

/*
 * AB BC CA deadlock:
 */

#define E()					\
						\
	LOCK_UNLOCK_2(A, B);			\
	LOCK_UNLOCK_2(B, C);			\
	LOCK_UNLOCK_2(C, A); /* fail */

/*
 * 6 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_TESTCASE(ABBCCA_spin)
#include "locking-selftest-wlock.h"
GENERATE_TESTCASE(ABBCCA_wlock)
#include "locking-selftest-rlock.h"
GENERATE_TESTCASE(ABBCCA_rlock)
#include "locking-selftest-mutex.h"
GENERATE_TESTCASE(ABBCCA_mutex)
#include "locking-selftest-wsem.h"
GENERATE_TESTCASE(ABBCCA_wsem)
#include "locking-selftest-rsem.h"
GENERATE_TESTCASE(ABBCCA_rsem)

#ifdef CONFIG_RT_MUTEXES
#include "locking-selftest-rtmutex.h"
GENERATE_TESTCASE(ABBCCA_rtmutex);
#endif

#undef E

/*
 * AB CA BC deadlock:
 */

#define E()					\
						\
	LOCK_UNLOCK_2(A, B);			\
	LOCK_UNLOCK_2(C, A);			\
	LOCK_UNLOCK_2(B, C); /* fail */

/*
 * 6 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_TESTCASE(ABCABC_spin)
#include "locking-selftest-wlock.h"
GENERATE_TESTCASE(ABCABC_wlock)
#include "locking-selftest-rlock.h"
GENERATE_TESTCASE(ABCABC_rlock)
#include "locking-selftest-mutex.h"
GENERATE_TESTCASE(ABCABC_mutex)
#include "locking-selftest-wsem.h"
GENERATE_TESTCASE(ABCABC_wsem)
#include "locking-selftest-rsem.h"
GENERATE_TESTCASE(ABCABC_rsem)

#ifdef CONFIG_RT_MUTEXES
#include "locking-selftest-rtmutex.h"
GENERATE_TESTCASE(ABCABC_rtmutex);
#endif

#undef E

/*
 * AB BC CD DA deadlock:
 */

#define E()					\
						\
	LOCK_UNLOCK_2(A, B);			\
	LOCK_UNLOCK_2(B, C);			\
	LOCK_UNLOCK_2(C, D);			\
	LOCK_UNLOCK_2(D, A); /* fail */

/*
 * 6 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_TESTCASE(ABBCCDDA_spin)
#include "locking-selftest-wlock.h"
GENERATE_TESTCASE(ABBCCDDA_wlock)
#include "locking-selftest-rlock.h"
GENERATE_TESTCASE(ABBCCDDA_rlock)
#include "locking-selftest-mutex.h"
GENERATE_TESTCASE(ABBCCDDA_mutex)
#include "locking-selftest-wsem.h"
GENERATE_TESTCASE(ABBCCDDA_wsem)
#include "locking-selftest-rsem.h"
GENERATE_TESTCASE(ABBCCDDA_rsem)

#ifdef CONFIG_RT_MUTEXES
#include "locking-selftest-rtmutex.h"
GENERATE_TESTCASE(ABBCCDDA_rtmutex);
#endif

#undef E

/*
 * AB CD BD DA deadlock:
 */
#define E()					\
						\
	LOCK_UNLOCK_2(A, B);			\
	LOCK_UNLOCK_2(C, D);			\
	LOCK_UNLOCK_2(B, D);			\
	LOCK_UNLOCK_2(D, A); /* fail */

/*
 * 6 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_TESTCASE(ABCDBDDA_spin)
#include "locking-selftest-wlock.h"
GENERATE_TESTCASE(ABCDBDDA_wlock)
#include "locking-selftest-rlock.h"
GENERATE_TESTCASE(ABCDBDDA_rlock)
#include "locking-selftest-mutex.h"
GENERATE_TESTCASE(ABCDBDDA_mutex)
#include "locking-selftest-wsem.h"
GENERATE_TESTCASE(ABCDBDDA_wsem)
#include "locking-selftest-rsem.h"
GENERATE_TESTCASE(ABCDBDDA_rsem)

#ifdef CONFIG_RT_MUTEXES
#include "locking-selftest-rtmutex.h"
GENERATE_TESTCASE(ABCDBDDA_rtmutex);
#endif

#undef E

/*
 * AB CD BC DA deadlock:
 */
#define E()					\
						\
	LOCK_UNLOCK_2(A, B);			\
	LOCK_UNLOCK_2(C, D);			\
	LOCK_UNLOCK_2(B, C);			\
	LOCK_UNLOCK_2(D, A); /* fail */

/*
 * 6 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_TESTCASE(ABCDBCDA_spin)
#include "locking-selftest-wlock.h"
GENERATE_TESTCASE(ABCDBCDA_wlock)
#include "locking-selftest-rlock.h"
GENERATE_TESTCASE(ABCDBCDA_rlock)
#include "locking-selftest-mutex.h"
GENERATE_TESTCASE(ABCDBCDA_mutex)
#include "locking-selftest-wsem.h"
GENERATE_TESTCASE(ABCDBCDA_wsem)
#include "locking-selftest-rsem.h"
GENERATE_TESTCASE(ABCDBCDA_rsem)

#ifdef CONFIG_RT_MUTEXES
#include "locking-selftest-rtmutex.h"
GENERATE_TESTCASE(ABCDBCDA_rtmutex);
#endif

#undef E

#ifdef CONFIG_PREEMPT_RT
# define RT_PREPARE_DBL_UNLOCK()	{ migrate_disable(); rcu_read_lock(); }
#else
# define RT_PREPARE_DBL_UNLOCK()
#endif
/*
 * Double unlock:
 */
#define E()					\
						\
	LOCK(A);				\
	RT_PREPARE_DBL_UNLOCK();		\
	UNLOCK(A);				\
	UNLOCK(A); /* fail */

/*
 * 6 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_TESTCASE(double_unlock_spin)
#include "locking-selftest-wlock.h"
GENERATE_TESTCASE(double_unlock_wlock)
#include "locking-selftest-rlock.h"
GENERATE_TESTCASE(double_unlock_rlock)
#include "locking-selftest-mutex.h"
GENERATE_TESTCASE(double_unlock_mutex)
#include "locking-selftest-wsem.h"
GENERATE_TESTCASE(double_unlock_wsem)
#include "locking-selftest-rsem.h"
GENERATE_TESTCASE(double_unlock_rsem)

#ifdef CONFIG_RT_MUTEXES
#include "locking-selftest-rtmutex.h"
GENERATE_TESTCASE(double_unlock_rtmutex);
#endif

#undef E

/*
 * initializing a held lock:
 */
#define E()					\
						\
	LOCK(A);				\
	INIT(A); /* fail */

/*
 * 6 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_TESTCASE(init_held_spin)
#include "locking-selftest-wlock.h"
GENERATE_TESTCASE(init_held_wlock)
#include "locking-selftest-rlock.h"
GENERATE_TESTCASE(init_held_rlock)
#include "locking-selftest-mutex.h"
GENERATE_TESTCASE(init_held_mutex)
#include "locking-selftest-wsem.h"
GENERATE_TESTCASE(init_held_wsem)
#include "locking-selftest-rsem.h"
GENERATE_TESTCASE(init_held_rsem)

#ifdef CONFIG_RT_MUTEXES
#include "locking-selftest-rtmutex.h"
GENERATE_TESTCASE(init_held_rtmutex);
#endif

#undef E

/*
 * locking an irq-safe lock with irqs enabled:
 */
#define E1()				\
					\
	IRQ_ENTER();			\
	LOCK(A);			\
	UNLOCK(A);			\
	IRQ_EXIT();

#define E2()				\
					\
	LOCK(A);			\
	UNLOCK(A);

/*
 * Generate 24 testcases:
 */
#include "locking-selftest-spin-hardirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_hard_spin)

#include "locking-selftest-rlock-hardirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_hard_rlock)

#include "locking-selftest-wlock-hardirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_hard_wlock)

#ifndef CONFIG_PREEMPT_RT
#include "locking-selftest-spin-softirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_soft_spin)

#include "locking-selftest-rlock-softirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_soft_rlock)

#include "locking-selftest-wlock-softirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_soft_wlock)
#endif

#undef E1
#undef E2

#ifndef CONFIG_PREEMPT_RT
/*
 * Enabling hardirqs with a softirq-safe lock held:
 */
#define E1()				\
					\
	SOFTIRQ_ENTER();		\
	LOCK(A);			\
	UNLOCK(A);			\
	SOFTIRQ_EXIT();

#define E2()				\
					\
	HARDIRQ_DISABLE();		\
	LOCK(A);			\
	HARDIRQ_ENABLE();		\
	UNLOCK(A);

/*
 * Generate 12 testcases:
 */
#include "locking-selftest-spin.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2A_spin)

#include "locking-selftest-wlock.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2A_wlock)

#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2A_rlock)

#undef E1
#undef E2

#endif

/*
 * Enabling irqs with an irq-safe lock held:
 */
#define E1()				\
					\
	IRQ_ENTER();			\
	LOCK(A);			\
	UNLOCK(A);			\
	IRQ_EXIT();

#define E2()				\
					\
	IRQ_DISABLE();			\
	LOCK(A);			\
	IRQ_ENABLE();			\
	UNLOCK(A);

/*
 * Generate 24 testcases:
 */
#include "locking-selftest-spin-hardirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_hard_spin)

#include "locking-selftest-rlock-hardirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_hard_rlock)

#include "locking-selftest-wlock-hardirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_hard_wlock)

#ifndef CONFIG_PREEMPT_RT
#include "locking-selftest-spin-softirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_soft_spin)

#include "locking-selftest-rlock-softirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_soft_rlock)

#include "locking-selftest-wlock-softirq.h"
GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_soft_wlock)
#endif

#undef E1
#undef E2

/*
 * Acquiring a irq-unsafe lock while holding an irq-safe-lock:
 */
#define E1()				\
					\
	LOCK(A);			\
	LOCK(B);			\
	UNLOCK(B);			\
	UNLOCK(A);			\

#define E2()				\
					\
	LOCK(B);			\
	UNLOCK(B);

#define E3()				\
					\
	IRQ_ENTER();			\
	LOCK(A);			\
	UNLOCK(A);			\
	IRQ_EXIT();

/*
 * Generate 36 testcases:
 */
#include "locking-selftest-spin-hardirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_hard_spin)

#include "locking-selftest-rlock-hardirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_hard_rlock)

#include "locking-selftest-wlock-hardirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_hard_wlock)

#ifndef CONFIG_PREEMPT_RT
#include "locking-selftest-spin-softirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_soft_spin)

#include "locking-selftest-rlock-softirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_soft_rlock)

#include "locking-selftest-wlock-softirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_soft_wlock)
#endif

#undef E1
#undef E2
#undef E3

/*
 * If a lock turns into softirq-safe, but earlier it took
 * a softirq-unsafe lock:
 */

#define E1()				\
	IRQ_DISABLE();			\
	LOCK(A);			\
	LOCK(B);			\
	UNLOCK(B);			\
	UNLOCK(A);			\
	IRQ_ENABLE();

#define E2()				\
	LOCK(B);			\
	UNLOCK(B);

#define E3()				\
	IRQ_ENTER();			\
	LOCK(A);			\
	UNLOCK(A);			\
	IRQ_EXIT();

/*
 * Generate 36 testcases:
 */
#include "locking-selftest-spin-hardirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_hard_spin)

#include "locking-selftest-rlock-hardirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_hard_rlock)

#include "locking-selftest-wlock-hardirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_hard_wlock)

#ifndef CONFIG_PREEMPT_RT
#include "locking-selftest-spin-softirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_soft_spin)

#include "locking-selftest-rlock-softirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_soft_rlock)

#include "locking-selftest-wlock-softirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_soft_wlock)
#endif

#undef E1
#undef E2
#undef E3

/*
 * read-lock / write-lock irq inversion.
 *
 * Deadlock scenario:
 *
 * CPU#1 is at #1, i.e. it has write-locked A, but has not
 * taken B yet.
 *
 * CPU#2 is at #2, i.e. it has locked B.
 *
 * Hardirq hits CPU#2 at point #2 and is trying to read-lock A.
 *
 * The deadlock occurs because CPU#1 will spin on B, and CPU#2
 * will spin on A.
 */

#define E1()				\
					\
	IRQ_DISABLE();			\
	WL(A);				\
	LOCK(B);			\
	UNLOCK(B);			\
	WU(A);				\
	IRQ_ENABLE();

#define E2()				\
					\
	LOCK(B);			\
	UNLOCK(B);

#define E3()				\
					\
	IRQ_ENTER();			\
	RL(A);				\
	RU(A);				\
	IRQ_EXIT();

/*
 * Generate 36 testcases:
 */
#include "locking-selftest-spin-hardirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_inversion_hard_spin)

#include "locking-selftest-rlock-hardirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_inversion_hard_rlock)

#include "locking-selftest-wlock-hardirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_inversion_hard_wlock)

#ifndef CONFIG_PREEMPT_RT
#include "locking-selftest-spin-softirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_inversion_soft_spin)

#include "locking-selftest-rlock-softirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_inversion_soft_rlock)

#include "locking-selftest-wlock-softirq.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_inversion_soft_wlock)
#endif

#undef E1
#undef E2
#undef E3

/*
 * write-read / write-read / write-read deadlock even if read is recursive
 */

#define E1()				\
					\
	WL(X1);				\
	RL(Y1);				\
	RU(Y1);				\
	WU(X1);

#define E2()				\
					\
	WL(Y1);				\
	RL(Z1);				\
	RU(Z1);				\
	WU(Y1);

#define E3()				\
					\
	WL(Z1);				\
	RL(X1);				\
	RU(X1);				\
	WU(Z1);

#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(W1R2_W2R3_W3R1)

#undef E1
#undef E2
#undef E3

/*
 * write-write / read-read / write-read deadlock even if read is recursive
 */

#define E1()				\
					\
	WL(X1);				\
	WL(Y1);				\
	WU(Y1);				\
	WU(X1);

#define E2()				\
					\
	RL(Y1);				\
	RL(Z1);				\
	RU(Z1);				\
	RU(Y1);

#define E3()				\
					\
	WL(Z1);				\
	RL(X1);				\
	RU(X1);				\
	WU(Z1);

#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(W1W2_R2R3_W3R1)

#undef E1
#undef E2
#undef E3

/*
 * write-write / read-read / read-write is not deadlock when read is recursive
 */

#define E1()				\
					\
	WL(X1);				\
	WL(Y1);				\
	WU(Y1);				\
	WU(X1);

#define E2()				\
					\
	RL(Y1);				\
	RL(Z1);				\
	RU(Z1);				\
	RU(Y1);

#define E3()				\
					\
	RL(Z1);				\
	WL(X1);				\
	WU(X1);				\
	RU(Z1);

#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(W1R2_R2R3_W3W1)

#undef E1
#undef E2
#undef E3

/*
 * write-read / read-read / write-write is not deadlock when read is recursive
 */

#define E1()				\
					\
	WL(X1);				\
	RL(Y1);				\
	RU(Y1);				\
	WU(X1);

#define E2()				\
					\
	RL(Y1);				\
	RL(Z1);				\
	RU(Z1);				\
	RU(Y1);

#define E3()				\
					\
	WL(Z1);				\
	WL(X1);				\
	WU(X1);				\
	WU(Z1);

#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(W1W2_R2R3_R3W1)

#undef E1
#undef E2
#undef E3
/*
 * read-lock / write-lock recursion that is actually safe.
 */

#define E1()				\
					\
	IRQ_DISABLE();			\
	WL(A);				\
	WU(A);				\
	IRQ_ENABLE();

#define E2()				\
					\
	RL(A);				\
	RU(A);				\

#define E3()				\
					\
	IRQ_ENTER();			\
	LOCK(A);			\
	L(B);				\
	U(B);				\
	UNLOCK(A);			\
	IRQ_EXIT();

/*
 * Generate 24 testcases:
 */
#include "locking-selftest-hardirq.h"
#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion_hard_rlock)

#include "locking-selftest-wlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion_hard_wlock)

#ifndef CONFIG_PREEMPT_RT
#include "locking-selftest-softirq.h"
#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion_soft_rlock)

#include "locking-selftest-wlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion_soft_wlock)
#endif

#undef E1
#undef E2
#undef E3

/*
 * read-lock / write-lock recursion that is unsafe.
 */

#define E1()				\
					\
	IRQ_DISABLE();			\
	L(B);				\
	LOCK(A);			\
	UNLOCK(A);			\
	U(B);				\
	IRQ_ENABLE();

#define E2()				\
					\
	RL(A);				\
	RU(A);				\

#define E3()				\
					\
	IRQ_ENTER();			\
	L(B);				\
	U(B);				\
	IRQ_EXIT();

/*
 * Generate 24 testcases:
 */
#include "locking-selftest-hardirq.h"
#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion2_hard_rlock)

#include "locking-selftest-wlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion2_hard_wlock)

#ifndef CONFIG_PREEMPT_RT
#include "locking-selftest-softirq.h"
#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion2_soft_rlock)

#include "locking-selftest-wlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion2_soft_wlock)
#endif

#undef E1
#undef E2
#undef E3
/*
 * read-lock / write-lock recursion that is unsafe.
 *
 * A is a ENABLED_*_READ lock
 * B is a USED_IN_*_READ lock
 *
 * read_lock(A);
 *			write_lock(B);
 * <interrupt>
 * read_lock(B);
 * 			write_lock(A); // if this one is read_lock(), no deadlock
 */

#define E1()				\
					\
	IRQ_DISABLE();			\
	WL(B);				\
	LOCK(A);			\
	UNLOCK(A);			\
	WU(B);				\
	IRQ_ENABLE();

#define E2()				\
					\
	RL(A);				\
	RU(A);				\

#define E3()				\
					\
	IRQ_ENTER();			\
	RL(B);				\
	RU(B);				\
	IRQ_EXIT();

/*
 * Generate 24 testcases:
 */
#include "locking-selftest-hardirq.h"
#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion3_hard_rlock)

#include "locking-selftest-wlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion3_hard_wlock)

#ifndef CONFIG_PREEMPT_RT
#include "locking-selftest-softirq.h"
#include "locking-selftest-rlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion3_soft_rlock)

#include "locking-selftest-wlock.h"
GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion3_soft_wlock)
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define I_SPINLOCK(x)	lockdep_reset_lock(&lock_##x.dep_map)
# define I_RAW_SPINLOCK(x)	lockdep_reset_lock(&raw_lock_##x.dep_map)
# define I_RWLOCK(x)	lockdep_reset_lock(&rwlock_##x.dep_map)
# define I_MUTEX(x)	lockdep_reset_lock(&mutex_##x.dep_map)
# define I_RWSEM(x)	lockdep_reset_lock(&rwsem_##x.dep_map)
# define I_WW(x)	lockdep_reset_lock(&x.dep_map)
# define I_LOCAL_LOCK(x) lockdep_reset_lock(this_cpu_ptr(&local_##x.dep_map))
#ifdef CONFIG_RT_MUTEXES
# define I_RTMUTEX(x)	lockdep_reset_lock(&rtmutex_##x.dep_map)
#endif
#else
# define I_SPINLOCK(x)
# define I_RAW_SPINLOCK(x)
# define I_RWLOCK(x)
# define I_MUTEX(x)
# define I_RWSEM(x)
# define I_WW(x)
# define I_LOCAL_LOCK(x)
#endif

#ifndef I_RTMUTEX
# define I_RTMUTEX(x)
#endif

#ifdef CONFIG_RT_MUTEXES
#define I2_RTMUTEX(x)	rt_mutex_init(&rtmutex_##x)
#else
#define I2_RTMUTEX(x)
#endif

#define I1(x)					\
	do {					\
		I_SPINLOCK(x);			\
		I_RWLOCK(x);			\
		I_MUTEX(x);			\
		I_RWSEM(x);			\
		I_RTMUTEX(x);			\
	} while (0)

#define I2(x)					\
	do {					\
		spin_lock_init(&lock_##x);	\
		rwlock_init(&rwlock_##x);	\
		mutex_init(&mutex_##x);		\
		init_rwsem(&rwsem_##x);		\
		I2_RTMUTEX(x);			\
	} while (0)

static void reset_locks(void)
{
	local_irq_disable();
	lockdep_free_key_range(&ww_lockdep.acquire_key, 1);
	lockdep_free_key_range(&ww_lockdep.mutex_key, 1);

	I1(A); I1(B); I1(C); I1(D);
	I1(X1); I1(X2); I1(Y1); I1(Y2); I1(Z1); I1(Z2);
	I_WW(t); I_WW(t2); I_WW(o.base); I_WW(o2.base); I_WW(o3.base);
	I_RAW_SPINLOCK(A); I_RAW_SPINLOCK(B);
	I_LOCAL_LOCK(A);

	lockdep_reset();

	I2(A); I2(B); I2(C); I2(D);
	init_shared_classes();
	raw_spin_lock_init(&raw_lock_A);
	raw_spin_lock_init(&raw_lock_B);
	local_lock_init(this_cpu_ptr(&local_A));

	ww_mutex_init(&o, &ww_lockdep); ww_mutex_init(&o2, &ww_lockdep); ww_mutex_init(&o3, &ww_lockdep);
	memset(&t, 0, sizeof(t)); memset(&t2, 0, sizeof(t2));
	memset(&ww_lockdep.acquire_key, 0, sizeof(ww_lockdep.acquire_key));
	memset(&ww_lockdep.mutex_key, 0, sizeof(ww_lockdep.mutex_key));
	local_irq_enable();
}

#undef I

static int testcase_total;
static int testcase_successes;
static int expected_testcase_failures;
static int unexpected_testcase_failures;

static void dotest(void (*testcase_fn)(void), int expected, int lockclass_mask)
{
	int saved_preempt_count = preempt_count();
#ifdef CONFIG_PREEMPT_RT
#ifdef CONFIG_SMP
	int saved_mgd_count = current->migration_disabled;
#endif
	int saved_rcu_count = current->rcu_read_lock_nesting;
#endif

	WARN_ON(irqs_disabled());

	debug_locks_silent = !(debug_locks_verbose & lockclass_mask);

	testcase_fn();
	/*
	 * Filter out expected failures:
	 */
#ifndef CONFIG_PROVE_LOCKING
	if (expected == FAILURE && debug_locks) {
		expected_testcase_failures++;
		pr_cont("failed|");
	}
	else
#endif
	if (debug_locks != expected) {
		unexpected_testcase_failures++;
		pr_cont("FAILED|");
	} else {
		testcase_successes++;
		pr_cont("  ok  |");
	}
	testcase_total++;

	if (debug_locks_verbose & lockclass_mask)
		pr_cont(" lockclass mask: %x, debug_locks: %d, expected: %d\n",
			lockclass_mask, debug_locks, expected);
	/*
	 * Some tests (e.g. double-unlock) might corrupt the preemption
	 * count, so restore it:
	 */
	preempt_count_set(saved_preempt_count);

#ifdef CONFIG_PREEMPT_RT
#ifdef CONFIG_SMP
	while (current->migration_disabled > saved_mgd_count)
		migrate_enable();
#endif

	while (current->rcu_read_lock_nesting > saved_rcu_count)
		rcu_read_unlock();
	WARN_ON_ONCE(current->rcu_read_lock_nesting < saved_rcu_count);
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
	if (softirq_count())
		current->softirqs_enabled = 0;
	else
		current->softirqs_enabled = 1;
#endif

	reset_locks();
}

#ifdef CONFIG_RT_MUTEXES
#define dotest_rt(fn, e, m)	dotest((fn), (e), (m))
#else
#define dotest_rt(fn, e, m)
#endif

static inline void print_testname(const char *testname)
{
	printk("%33s:", testname);
}

#define DO_TESTCASE_1(desc, name, nr)				\
	print_testname(desc"/"#nr);				\
	dotest(name##_##nr, SUCCESS, LOCKTYPE_RWLOCK);		\
	pr_cont("\n");

#define DO_TESTCASE_1B(desc, name, nr)				\
	print_testname(desc"/"#nr);				\
	dotest(name##_##nr, FAILURE, LOCKTYPE_RWLOCK);		\
	pr_cont("\n");

#define DO_TESTCASE_1RR(desc, name, nr)				\
	print_testname(desc"/"#nr);				\
	pr_cont("             |");				\
	dotest(name##_##nr, SUCCESS, LOCKTYPE_RWLOCK);		\
	pr_cont("\n");

#define DO_TESTCASE_1RRB(desc, name, nr)			\
	print_testname(desc"/"#nr);				\
	pr_cont("             |");				\
	dotest(name##_##nr, FAILURE, LOCKTYPE_RWLOCK);		\
	pr_cont("\n");


#define DO_TESTCASE_3(desc, name, nr)				\
	print_testname(desc"/"#nr);				\
	dotest(name##_spin_##nr, FAILURE, LOCKTYPE_SPIN);	\
	dotest(name##_wlock_##nr, FAILURE, LOCKTYPE_RWLOCK);	\
	dotest(name##_rlock_##nr, SUCCESS, LOCKTYPE_RWLOCK);	\
	pr_cont("\n");

#define DO_TESTCASE_3RW(desc, name, nr)				\
	print_testname(desc"/"#nr);				\
	dotest(name##_spin_##nr, FAILURE, LOCKTYPE_SPIN|LOCKTYPE_RWLOCK);\
	dotest(name##_wlock_##nr, FAILURE, LOCKTYPE_RWLOCK);	\
	dotest(name##_rlock_##nr, SUCCESS, LOCKTYPE_RWLOCK);	\
	pr_cont("\n");

#define DO_TESTCASE_2RW(desc, name, nr)				\
	print_testname(desc"/"#nr);				\
	pr_cont("      |");					\
	dotest(name##_wlock_##nr, FAILURE, LOCKTYPE_RWLOCK);	\
	dotest(name##_rlock_##nr, SUCCESS, LOCKTYPE_RWLOCK);	\
	pr_cont("\n");

#define DO_TESTCASE_2x2RW(desc, name, nr)			\
	DO_TESTCASE_2RW("hard-"desc, name##_hard, nr)		\
	NON_RT(DO_TESTCASE_2RW("soft-"desc, name##_soft, nr))	\

#define DO_TESTCASE_6x2x2RW(desc, name)				\
	DO_TESTCASE_2x2RW(desc, name, 123);			\
	DO_TESTCASE_2x2RW(desc, name, 132);			\
	DO_TESTCASE_2x2RW(desc, name, 213);			\
	DO_TESTCASE_2x2RW(desc, name, 231);			\
	DO_TESTCASE_2x2RW(desc, name, 312);			\
	DO_TESTCASE_2x2RW(desc, name, 321);

#define DO_TESTCASE_6(desc, name)				\
	print_testname(desc);					\
	dotest(name##_spin, FAILURE, LOCKTYPE_SPIN);		\
	dotest(name##_wlock, FAILURE, LOCKTYPE_RWLOCK);		\
	dotest(name##_rlock, FAILURE, LOCKTYPE_RWLOCK);		\
	dotest(name##_mutex, FAILURE, LOCKTYPE_MUTEX);		\
	dotest(name##_wsem, FAILURE, LOCKTYPE_RWSEM);		\
	dotest(name##_rsem, FAILURE, LOCKTYPE_RWSEM);		\
	dotest_rt(name##_rtmutex, FAILURE, LOCKTYPE_RTMUTEX);	\
	pr_cont("\n");

#define DO_TESTCASE_6_SUCCESS(desc, name)			\
	print_testname(desc);					\
	dotest(name##_spin, SUCCESS, LOCKTYPE_SPIN);		\
	dotest(name##_wlock, SUCCESS, LOCKTYPE_RWLOCK);		\
	dotest(name##_rlock, SUCCESS, LOCKTYPE_RWLOCK);		\
	dotest(name##_mutex, SUCCESS, LOCKTYPE_MUTEX);		\
	dotest(name##_wsem, SUCCESS, LOCKTYPE_RWSEM);		\
	dotest(name##_rsem, SUCCESS, LOCKTYPE_RWSEM);		\
	dotest_rt(name##_rtmutex, SUCCESS, LOCKTYPE_RTMUTEX);	\
	pr_cont("\n");

/*
 * 'read' variant: rlocks must not trigger.
 */
#define DO_TESTCASE_6R(desc, name)				\
	print_testname(desc);					\
	dotest(name##_spin, FAILURE, LOCKTYPE_SPIN);		\
	dotest(name##_wlock, FAILURE, LOCKTYPE_RWLOCK);		\
	dotest(name##_rlock, SUCCESS, LOCKTYPE_RWLOCK);		\
	dotest(name##_mutex, FAILURE, LOCKTYPE_MUTEX);		\
	dotest(name##_wsem, FAILURE, LOCKTYPE_RWSEM);		\
	dotest(name##_rsem, FAILURE, LOCKTYPE_RWSEM);		\
	dotest_rt(name##_rtmutex, FAILURE, LOCKTYPE_RTMUTEX);	\
	pr_cont("\n");

#define DO_TESTCASE_2I(desc, name, nr)				\
	DO_TESTCASE_1("hard-"desc, name##_hard, nr);		\
	NON_RT(DO_TESTCASE_1("soft-"desc, name##_soft, nr));

#define DO_TESTCASE_2IB(desc, name, nr)				\
	DO_TESTCASE_1B("hard-"desc, name##_hard, nr);		\
	NON_RT(DO_TESTCASE_1B("soft-"desc, name##_soft, nr));

#define DO_TESTCASE_6I(desc, name, nr)				\
	DO_TESTCASE_3("hard-"desc, name##_hard, nr);		\
	NON_RT(DO_TESTCASE_3("soft-"desc, name##_soft, nr));

#define DO_TESTCASE_6IRW(desc, name, nr)			\
	DO_TESTCASE_3RW("hard-"desc, name##_hard, nr);		\
	NON_RT(DO_TESTCASE_3RW("soft-"desc, name##_soft, nr));

#define DO_TESTCASE_2x3(desc, name)				\
	DO_TESTCASE_3(desc, name, 12);				\
	DO_TESTCASE_3(desc, name, 21);

#define DO_TESTCASE_2x6(desc, name)				\
	DO_TESTCASE_6I(desc, name, 12);				\
	DO_TESTCASE_6I(desc, name, 21);

#define DO_TESTCASE_6x2(desc, name)				\
	DO_TESTCASE_2I(desc, name, 123);			\
	DO_TESTCASE_2I(desc, name, 132);			\
	DO_TESTCASE_2I(desc, name, 213);			\
	DO_TESTCASE_2I(desc, name, 231);			\
	DO_TESTCASE_2I(desc, name, 312);			\
	DO_TESTCASE_2I(desc, name, 321);

#define DO_TESTCASE_6x2B(desc, name)				\
	DO_TESTCASE_2IB(desc, name, 123);			\
	DO_TESTCASE_2IB(desc, name, 132);			\
	DO_TESTCASE_2IB(desc, name, 213);			\
	DO_TESTCASE_2IB(desc, name, 231);			\
	DO_TESTCASE_2IB(desc, name, 312);			\
	DO_TESTCASE_2IB(desc, name, 321);

#define DO_TESTCASE_6x1RR(desc, name)				\
	DO_TESTCASE_1RR(desc, name, 123);			\
	DO_TESTCASE_1RR(desc, name, 132);			\
	DO_TESTCASE_1RR(desc, name, 213);			\
	DO_TESTCASE_1RR(desc, name, 231);			\
	DO_TESTCASE_1RR(desc, name, 312);			\
	DO_TESTCASE_1RR(desc, name, 321);

#define DO_TESTCASE_6x1RRB(desc, name)				\
	DO_TESTCASE_1RRB(desc, name, 123);			\
	DO_TESTCASE_1RRB(desc, name, 132);			\
	DO_TESTCASE_1RRB(desc, name, 213);			\
	DO_TESTCASE_1RRB(desc, name, 231);			\
	DO_TESTCASE_1RRB(desc, name, 312);			\
	DO_TESTCASE_1RRB(desc, name, 321);

#define DO_TESTCASE_6x6(desc, name)				\
	DO_TESTCASE_6I(desc, name, 123);			\
	DO_TESTCASE_6I(desc, name, 132);			\
	DO_TESTCASE_6I(desc, name, 213);			\
	DO_TESTCASE_6I(desc, name, 231);			\
	DO_TESTCASE_6I(desc, name, 312);			\
	DO_TESTCASE_6I(desc, name, 321);

#define DO_TESTCASE_6x6RW(desc, name)				\
	DO_TESTCASE_6IRW(desc, name, 123);			\
	DO_TESTCASE_6IRW(desc, name, 132);			\
	DO_TESTCASE_6IRW(desc, name, 213);			\
	DO_TESTCASE_6IRW(desc, name, 231);			\
	DO_TESTCASE_6IRW(desc, name, 312);			\
	DO_TESTCASE_6IRW(desc, name, 321);

static void ww_test_fail_acquire(void)
{
	int ret;

	WWAI(&t);
	t.stamp++;

	ret = WWL(&o, &t);

	if (WARN_ON(!o.ctx) ||
	    WARN_ON(ret))
		return;

	/* No lockdep test, pure API */
	ret = WWL(&o, &t);
	WARN_ON(ret != -EALREADY);

	ret = WWT(&o);
	WARN_ON(ret);

	t2 = t;
	t2.stamp++;
	ret = WWL(&o, &t2);
	WARN_ON(ret != -EDEADLK);
	WWU(&o);

	if (WWT(&o))
		WWU(&o);
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	else
		DEBUG_LOCKS_WARN_ON(1);
#endif
}

#ifdef CONFIG_PREEMPT_RT
#define ww_mutex_base_lock(b)			rt_mutex_lock(b)
#define ww_mutex_base_trylock(b)		rt_mutex_trylock(b)
#define ww_mutex_base_lock_nest_lock(b, b2)	rt_mutex_lock_nest_lock(b, b2)
#define ww_mutex_base_lock_interruptible(b)	rt_mutex_lock_interruptible(b)
#define ww_mutex_base_lock_killable(b)		rt_mutex_lock_killable(b)
#define ww_mutex_base_unlock(b)			rt_mutex_unlock(b)
#else
#define ww_mutex_base_lock(b)			mutex_lock(b)
#define ww_mutex_base_trylock(b)		mutex_trylock(b)
#define ww_mutex_base_lock_nest_lock(b, b2)	mutex_lock_nest_lock(b, b2)
#define ww_mutex_base_lock_interruptible(b)	mutex_lock_interruptible(b)
#define ww_mutex_base_lock_killable(b)		mutex_lock_killable(b)
#define ww_mutex_base_unlock(b)			mutex_unlock(b)
#endif

static void ww_test_normal(void)
{
	int ret;

	WWAI(&t);

	/*
	 * None of the ww_mutex codepaths should be taken in the 'normal'
	 * mutex calls. The easiest way to verify this is by using the
	 * normal mutex calls, and making sure o.ctx is unmodified.
	 */

	/* mutex_lock (and indirectly, mutex_lock_nested) */
	o.ctx = (void *)~0UL;
	ww_mutex_base_lock(&o.base);
	ww_mutex_base_unlock(&o.base);
	WARN_ON(o.ctx != (void *)~0UL);

	/* mutex_lock_interruptible (and *_nested) */
	o.ctx = (void *)~0UL;
	ret = ww_mutex_base_lock_interruptible(&o.base);
	if (!ret)
		ww_mutex_base_unlock(&o.base);
	else
		WARN_ON(1);
	WARN_ON(o.ctx != (void *)~0UL);

	/* mutex_lock_killable (and *_nested) */
	o.ctx = (void *)~0UL;
	ret = ww_mutex_base_lock_killable(&o.base);
	if (!ret)
		ww_mutex_base_unlock(&o.base);
	else
		WARN_ON(1);
	WARN_ON(o.ctx != (void *)~0UL);

	/* trylock, succeeding */
	o.ctx = (void *)~0UL;
	ret = ww_mutex_base_trylock(&o.base);
	WARN_ON(!ret);
	if (ret)
		ww_mutex_base_unlock(&o.base);
	else
		WARN_ON(1);
	WARN_ON(o.ctx != (void *)~0UL);

	/* trylock, failing */
	o.ctx = (void *)~0UL;
	ww_mutex_base_lock(&o.base);
	ret = ww_mutex_base_trylock(&o.base);
	WARN_ON(ret);
	ww_mutex_base_unlock(&o.base);
	WARN_ON(o.ctx != (void *)~0UL);

	/* nest_lock */
	o.ctx = (void *)~0UL;
	ww_mutex_base_lock_nest_lock(&o.base, &t);
	ww_mutex_base_unlock(&o.base);
	WARN_ON(o.ctx != (void *)~0UL);
}

static void ww_test_two_contexts(void)
{
	WWAI(&t);
	WWAI(&t2);
}

static void ww_test_diff_class(void)
{
	WWAI(&t);
#ifdef DEBUG_WW_MUTEXES
	t.ww_class = NULL;
#endif
	WWL(&o, &t);
}

static void ww_test_context_done_twice(void)
{
	WWAI(&t);
	WWAD(&t);
	WWAD(&t);
	WWAF(&t);
}

static void ww_test_context_unlock_twice(void)
{
	WWAI(&t);
	WWAD(&t);
	WWAF(&t);
	WWAF(&t);
}

static void ww_test_context_fini_early(void)
{
	WWAI(&t);
	WWL(&o, &t);
	WWAD(&t);
	WWAF(&t);
}

static void ww_test_context_lock_after_done(void)
{
	WWAI(&t);
	WWAD(&t);
	WWL(&o, &t);
}

static void ww_test_object_unlock_twice(void)
{
	WWL1(&o);
	WWU(&o);
	WWU(&o);
}

static void ww_test_object_lock_unbalanced(void)
{
	WWAI(&t);
	WWL(&o, &t);
	t.acquired = 0;
	WWU(&o);
	WWAF(&t);
}

static void ww_test_object_lock_stale_context(void)
{
	WWAI(&t);
	o.ctx = &t2;
	WWL(&o, &t);
}

static void ww_test_edeadlk_normal(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	o2.ctx = &t2;
	mutex_release(&o2.base.dep_map, _THIS_IP_);

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);

	o2.ctx = NULL;
	mutex_acquire(&o2.base.dep_map, 0, 1, _THIS_IP_);
	ww_mutex_base_unlock(&o2.base);
	WWU(&o);

	WWL(&o2, &t);
}

static void ww_test_edeadlk_normal_slow(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	mutex_release(&o2.base.dep_map, _THIS_IP_);
	o2.ctx = &t2;

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);

	o2.ctx = NULL;
	mutex_acquire(&o2.base.dep_map, 0, 1, _THIS_IP_);
	ww_mutex_base_unlock(&o2.base);
	WWU(&o);

	ww_mutex_lock_slow(&o2, &t);
}

static void ww_test_edeadlk_no_unlock(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	o2.ctx = &t2;
	mutex_release(&o2.base.dep_map, _THIS_IP_);

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);

	o2.ctx = NULL;
	mutex_acquire(&o2.base.dep_map, 0, 1, _THIS_IP_);
	ww_mutex_base_unlock(&o2.base);

	WWL(&o2, &t);
}

static void ww_test_edeadlk_no_unlock_slow(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	mutex_release(&o2.base.dep_map, _THIS_IP_);
	o2.ctx = &t2;

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);

	o2.ctx = NULL;
	mutex_acquire(&o2.base.dep_map, 0, 1, _THIS_IP_);
	ww_mutex_base_unlock(&o2.base);

	ww_mutex_lock_slow(&o2, &t);
}

static void ww_test_edeadlk_acquire_more(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	mutex_release(&o2.base.dep_map, _THIS_IP_);
	o2.ctx = &t2;

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);

	ret = WWL(&o3, &t);
}

static void ww_test_edeadlk_acquire_more_slow(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	mutex_release(&o2.base.dep_map, _THIS_IP_);
	o2.ctx = &t2;

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);

	ww_mutex_lock_slow(&o3, &t);
}

static void ww_test_edeadlk_acquire_more_edeadlk(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	mutex_release(&o2.base.dep_map, _THIS_IP_);
	o2.ctx = &t2;

	ww_mutex_base_lock(&o3.base);
	mutex_release(&o3.base.dep_map, _THIS_IP_);
	o3.ctx = &t2;

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);

	ret = WWL(&o3, &t);
	WARN_ON(ret != -EDEADLK);
}

static void ww_test_edeadlk_acquire_more_edeadlk_slow(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	mutex_release(&o2.base.dep_map, _THIS_IP_);
	o2.ctx = &t2;

	ww_mutex_base_lock(&o3.base);
	mutex_release(&o3.base.dep_map, _THIS_IP_);
	o3.ctx = &t2;

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);

	ww_mutex_lock_slow(&o3, &t);
}

static void ww_test_edeadlk_acquire_wrong(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	mutex_release(&o2.base.dep_map, _THIS_IP_);
	o2.ctx = &t2;

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);
	if (!ret)
		WWU(&o2);

	WWU(&o);

	ret = WWL(&o3, &t);
}

static void ww_test_edeadlk_acquire_wrong_slow(void)
{
	int ret;

	ww_mutex_base_lock(&o2.base);
	mutex_release(&o2.base.dep_map, _THIS_IP_);
	o2.ctx = &t2;

	WWAI(&t);
	t2 = t;
	t2.stamp--;

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret != -EDEADLK);
	if (!ret)
		WWU(&o2);

	WWU(&o);

	ww_mutex_lock_slow(&o3, &t);
}

static void ww_test_spin_nest_unlocked(void)
{
	spin_lock_nest_lock(&lock_A, &o.base);
	U(A);
}

/* This is not a deadlock, because we have X1 to serialize Y1 and Y2 */
static void ww_test_spin_nest_lock(void)
{
	spin_lock(&lock_X1);
	spin_lock_nest_lock(&lock_Y1, &lock_X1);
	spin_lock(&lock_A);
	spin_lock_nest_lock(&lock_Y2, &lock_X1);
	spin_unlock(&lock_A);
	spin_unlock(&lock_Y2);
	spin_unlock(&lock_Y1);
	spin_unlock(&lock_X1);
}

static void ww_test_unneeded_slow(void)
{
	WWAI(&t);

	ww_mutex_lock_slow(&o, &t);
}

static void ww_test_context_block(void)
{
	int ret;

	WWAI(&t);

	ret = WWL(&o, &t);
	WARN_ON(ret);
	WWL1(&o2);
}

static void ww_test_context_try(void)
{
	int ret;

	WWAI(&t);

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWT(&o2);
	WARN_ON(!ret);
	WWU(&o2);
	WWU(&o);
}

static void ww_test_context_context(void)
{
	int ret;

	WWAI(&t);

	ret = WWL(&o, &t);
	WARN_ON(ret);

	ret = WWL(&o2, &t);
	WARN_ON(ret);

	WWU(&o2);
	WWU(&o);
}

static void ww_test_try_block(void)
{
	bool ret;

	ret = WWT(&o);
	WARN_ON(!ret);

	WWL1(&o2);
	WWU(&o2);
	WWU(&o);
}

static void ww_test_try_try(void)
{
	bool ret;

	ret = WWT(&o);
	WARN_ON(!ret);
	ret = WWT(&o2);
	WARN_ON(!ret);
	WWU(&o2);
	WWU(&o);
}

static void ww_test_try_context(void)
{
	int ret;

	ret = WWT(&o);
	WARN_ON(!ret);

	WWAI(&t);

	ret = WWL(&o2, &t);
	WARN_ON(ret);
}

static void ww_test_block_block(void)
{
	WWL1(&o);
	WWL1(&o2);
}

static void ww_test_block_try(void)
{
	bool ret;

	WWL1(&o);
	ret = WWT(&o2);
	WARN_ON(!ret);
}

static void ww_test_block_context(void)
{
	int ret;

	WWL1(&o);
	WWAI(&t);

	ret = WWL(&o2, &t);
	WARN_ON(ret);
}

static void ww_test_spin_block(void)
{
	L(A);
	U(A);

	WWL1(&o);
	L(A);
	U(A);
	WWU(&o);

	L(A);
	WWL1(&o);
	WWU(&o);
	U(A);
}

static void ww_test_spin_try(void)
{
	bool ret;

	L(A);
	U(A);

	ret = WWT(&o);
	WARN_ON(!ret);
	L(A);
	U(A);
	WWU(&o);

	L(A);
	ret = WWT(&o);
	WARN_ON(!ret);
	WWU(&o);
	U(A);
}

static void ww_test_spin_context(void)
{
	int ret;

	L(A);
	U(A);

	WWAI(&t);

	ret = WWL(&o, &t);
	WARN_ON(ret);
	L(A);
	U(A);
	WWU(&o);

	L(A);
	ret = WWL(&o, &t);
	WARN_ON(ret);
	WWU(&o);
	U(A);
}

static void ww_tests(void)
{
	printk("  --------------------------------------------------------------------------\n");
	printk("  | Wound/wait tests |\n");
	printk("  ---------------------\n");

	print_testname("ww api failures");
	dotest(ww_test_fail_acquire, SUCCESS, LOCKTYPE_WW);
	dotest(ww_test_normal, SUCCESS, LOCKTYPE_WW);
	dotest(ww_test_unneeded_slow, FAILURE, LOCKTYPE_WW);
	pr_cont("\n");

	print_testname("ww contexts mixing");
	dotest(ww_test_two_contexts, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_diff_class, FAILURE, LOCKTYPE_WW);
	pr_cont("\n");

	print_testname("finishing ww context");
	dotest(ww_test_context_done_twice, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_context_unlock_twice, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_context_fini_early, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_context_lock_after_done, FAILURE, LOCKTYPE_WW);
	pr_cont("\n");

	print_testname("locking mismatches");
	dotest(ww_test_object_unlock_twice, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_object_lock_unbalanced, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_object_lock_stale_context, FAILURE, LOCKTYPE_WW);
	pr_cont("\n");

	print_testname("EDEADLK handling");
	dotest(ww_test_edeadlk_normal, SUCCESS, LOCKTYPE_WW);
	dotest(ww_test_edeadlk_normal_slow, SUCCESS, LOCKTYPE_WW);
	dotest(ww_test_edeadlk_no_unlock, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_edeadlk_no_unlock_slow, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_edeadlk_acquire_more, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_edeadlk_acquire_more_slow, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_edeadlk_acquire_more_edeadlk, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_edeadlk_acquire_more_edeadlk_slow, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_edeadlk_acquire_wrong, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_edeadlk_acquire_wrong_slow, FAILURE, LOCKTYPE_WW);
	pr_cont("\n");

	print_testname("spinlock nest unlocked");
	dotest(ww_test_spin_nest_unlocked, FAILURE, LOCKTYPE_WW);
	pr_cont("\n");

	print_testname("spinlock nest test");
	dotest(ww_test_spin_nest_lock, SUCCESS, LOCKTYPE_WW);
	pr_cont("\n");

	printk("  -----------------------------------------------------\n");
	printk("                                 |block | try  |context|\n");
	printk("  -----------------------------------------------------\n");

	print_testname("context");
	dotest(ww_test_context_block, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_context_try, SUCCESS, LOCKTYPE_WW);
	dotest(ww_test_context_context, SUCCESS, LOCKTYPE_WW);
	pr_cont("\n");

	print_testname("try");
	dotest(ww_test_try_block, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_try_try, SUCCESS, LOCKTYPE_WW);
	dotest(ww_test_try_context, FAILURE, LOCKTYPE_WW);
	pr_cont("\n");

	print_testname("block");
	dotest(ww_test_block_block, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_block_try, SUCCESS, LOCKTYPE_WW);
	dotest(ww_test_block_context, FAILURE, LOCKTYPE_WW);
	pr_cont("\n");

	print_testname("spinlock");
	dotest(ww_test_spin_block, FAILURE, LOCKTYPE_WW);
	dotest(ww_test_spin_try, SUCCESS, LOCKTYPE_WW);
	dotest(ww_test_spin_context, FAILURE, LOCKTYPE_WW);
	pr_cont("\n");
}


/*
 * <in hardirq handler>
 * read_lock(&A);
 *			<hardirq disable>
 *			spin_lock(&B);
 * spin_lock(&B);
 *			read_lock(&A);
 *
 * is a deadlock.
 */
static void queued_read_lock_hardirq_RE_Er(void)
{
	HARDIRQ_ENTER();
	read_lock(&rwlock_A);
	LOCK(B);
	UNLOCK(B);
	read_unlock(&rwlock_A);
	HARDIRQ_EXIT();

	HARDIRQ_DISABLE();
	LOCK(B);
	read_lock(&rwlock_A);
	read_unlock(&rwlock_A);
	UNLOCK(B);
	HARDIRQ_ENABLE();
}

/*
 * <in hardirq handler>
 * spin_lock(&B);
 *			<hardirq disable>
 *			read_lock(&A);
 * read_lock(&A);
 *			spin_lock(&B);
 *
 * is not a deadlock.
 */
static void queued_read_lock_hardirq_ER_rE(void)
{
	HARDIRQ_ENTER();
	LOCK(B);
	read_lock(&rwlock_A);
	read_unlock(&rwlock_A);
	UNLOCK(B);
	HARDIRQ_EXIT();

	HARDIRQ_DISABLE();
	read_lock(&rwlock_A);
	LOCK(B);
	UNLOCK(B);
	read_unlock(&rwlock_A);
	HARDIRQ_ENABLE();
}

/*
 * <hardirq disable>
 * spin_lock(&B);
 *			read_lock(&A);
 *			<in hardirq handler>
 *			spin_lock(&B);
 * read_lock(&A);
 *
 * is a deadlock. Because the two read_lock()s are both non-recursive readers.
 */
static void queued_read_lock_hardirq_inversion(void)
{

	HARDIRQ_ENTER();
	LOCK(B);
	UNLOCK(B);
	HARDIRQ_EXIT();

	HARDIRQ_DISABLE();
	LOCK(B);
	read_lock(&rwlock_A);
	read_unlock(&rwlock_A);
	UNLOCK(B);
	HARDIRQ_ENABLE();

	read_lock(&rwlock_A);
	read_unlock(&rwlock_A);
}

static void queued_read_lock_tests(void)
{
	printk("  --------------------------------------------------------------------------\n");
	printk("  | queued read lock tests |\n");
	printk("  ---------------------------\n");
	print_testname("hardirq read-lock/lock-read");
	dotest(queued_read_lock_hardirq_RE_Er, FAILURE, LOCKTYPE_RWLOCK);
	pr_cont("\n");

	print_testname("hardirq lock-read/read-lock");
	dotest(queued_read_lock_hardirq_ER_rE, SUCCESS, LOCKTYPE_RWLOCK);
	pr_cont("\n");

	print_testname("hardirq inversion");
	dotest(queued_read_lock_hardirq_inversion, FAILURE, LOCKTYPE_RWLOCK);
	pr_cont("\n");
}

static void fs_reclaim_correct_nesting(void)
{
	fs_reclaim_acquire(GFP_KERNEL);
	might_alloc(GFP_NOFS);
	fs_reclaim_release(GFP_KERNEL);
}

static void fs_reclaim_wrong_nesting(void)
{
	fs_reclaim_acquire(GFP_KERNEL);
	might_alloc(GFP_KERNEL);
	fs_reclaim_release(GFP_KERNEL);
}

static void fs_reclaim_protected_nesting(void)
{
	unsigned int flags;

	fs_reclaim_acquire(GFP_KERNEL);
	flags = memalloc_nofs_save();
	might_alloc(GFP_KERNEL);
	memalloc_nofs_restore(flags);
	fs_reclaim_release(GFP_KERNEL);
}

static void fs_reclaim_tests(void)
{
	printk("  --------------------\n");
	printk("  | fs_reclaim tests |\n");
	printk("  --------------------\n");

	print_testname("correct nesting");
	dotest(fs_reclaim_correct_nesting, SUCCESS, 0);
	pr_cont("\n");

	print_testname("wrong nesting");
	dotest(fs_reclaim_wrong_nesting, FAILURE, 0);
	pr_cont("\n");

	print_testname("protected nesting");
	dotest(fs_reclaim_protected_nesting, SUCCESS, 0);
	pr_cont("\n");
}

/* Defines guard classes to create contexts */
DEFINE_LOCK_GUARD_0(HARDIRQ, HARDIRQ_ENTER(), HARDIRQ_EXIT())
DEFINE_LOCK_GUARD_0(NOTTHREADED_HARDIRQ,
	do {
		local_irq_disable();
		__irq_enter();
		WARN_ON(!in_irq());
	} while(0), HARDIRQ_EXIT())
DEFINE_LOCK_GUARD_0(SOFTIRQ, SOFTIRQ_ENTER(), SOFTIRQ_EXIT())

/* Define RCU guards, should go away when RCU has its own guard definitions */
DEFINE_LOCK_GUARD_0(RCU, rcu_read_lock(), rcu_read_unlock())
DEFINE_LOCK_GUARD_0(RCU_BH, rcu_read_lock_bh(), rcu_read_unlock_bh())
DEFINE_LOCK_GUARD_0(RCU_SCHED, rcu_read_lock_sched(), rcu_read_unlock_sched())


#define GENERATE_2_CONTEXT_TESTCASE(outer, outer_lock, inner, inner_lock)	\
										\
static void __maybe_unused inner##_in_##outer(void)				\
{										\
	/* Relies the reversed clean-up ordering: inner first */		\
	guard(outer)(outer_lock);						\
	guard(inner)(inner_lock);						\
}

/*
 * wait contexts (considering PREEMPT_RT)
 *
 * o: inner is allowed in outer
 * x: inner is disallowed in outer
 *
 *       \  inner |  RCU  | RAW_SPIN | SPIN | MUTEX
 * outer  \       |       |          |      |
 * ---------------+-------+----------+------+-------
 * HARDIRQ        |   o   |    o     |  o   |  x
 * ---------------+-------+----------+------+-------
 * NOTTHREADED_IRQ|   o   |    o     |  x   |  x
 * ---------------+-------+----------+------+-------
 * SOFTIRQ        |   o   |    o     |  o   |  x
 * ---------------+-------+----------+------+-------
 * RCU            |   o   |    o     |  o   |  x
 * ---------------+-------+----------+------+-------
 * RCU_BH         |   o   |    o     |  o   |  x
 * ---------------+-------+----------+------+-------
 * RCU_SCHED      |   o   |    o     |  x   |  x
 * ---------------+-------+----------+------+-------
 * RAW_SPIN       |   o   |    o     |  x   |  x
 * ---------------+-------+----------+------+-------
 * SPIN           |   o   |    o     |  o   |  x
 * ---------------+-------+----------+------+-------
 * MUTEX          |   o   |    o     |  o   |  o
 * ---------------+-------+----------+------+-------
 */

#define GENERATE_2_CONTEXT_TESTCASE_FOR_ALL_OUTER(inner, inner_lock)		\
GENERATE_2_CONTEXT_TESTCASE(HARDIRQ, , inner, inner_lock)			\
GENERATE_2_CONTEXT_TESTCASE(NOTTHREADED_HARDIRQ, , inner, inner_lock)		\
GENERATE_2_CONTEXT_TESTCASE(SOFTIRQ, , inner, inner_lock)			\
GENERATE_2_CONTEXT_TESTCASE(RCU, , inner, inner_lock)				\
GENERATE_2_CONTEXT_TESTCASE(RCU_BH, , inner, inner_lock)			\
GENERATE_2_CONTEXT_TESTCASE(RCU_SCHED, , inner, inner_lock)			\
GENERATE_2_CONTEXT_TESTCASE(raw_spinlock, &raw_lock_A, inner, inner_lock)	\
GENERATE_2_CONTEXT_TESTCASE(spinlock, &lock_A, inner, inner_lock)		\
GENERATE_2_CONTEXT_TESTCASE(mutex, &mutex_A, inner, inner_lock)

GENERATE_2_CONTEXT_TESTCASE_FOR_ALL_OUTER(RCU, )
GENERATE_2_CONTEXT_TESTCASE_FOR_ALL_OUTER(raw_spinlock, &raw_lock_B)
GENERATE_2_CONTEXT_TESTCASE_FOR_ALL_OUTER(spinlock, &lock_B)
GENERATE_2_CONTEXT_TESTCASE_FOR_ALL_OUTER(mutex, &mutex_B)

/* the outer context allows all kinds of preemption */
#define DO_CONTEXT_TESTCASE_OUTER_PREEMPTIBLE(outer)			\
	dotest(RCU_in_##outer, SUCCESS, LOCKTYPE_RWLOCK);		\
	dotest(raw_spinlock_in_##outer, SUCCESS, LOCKTYPE_SPIN);	\
	dotest(spinlock_in_##outer, SUCCESS, LOCKTYPE_SPIN);		\
	dotest(mutex_in_##outer, SUCCESS, LOCKTYPE_MUTEX);		\

/*
 * the outer context only allows the preemption introduced by spinlock_t (which
 * is a sleepable lock for PREEMPT_RT)
 */
#define DO_CONTEXT_TESTCASE_OUTER_LIMITED_PREEMPTIBLE(outer)		\
	dotest(RCU_in_##outer, SUCCESS, LOCKTYPE_RWLOCK);		\
	dotest(raw_spinlock_in_##outer, SUCCESS, LOCKTYPE_SPIN);	\
	dotest(spinlock_in_##outer, SUCCESS, LOCKTYPE_SPIN);		\
	dotest(mutex_in_##outer, FAILURE, LOCKTYPE_MUTEX);		\

/* the outer doesn't allows any kind of preemption */
#define DO_CONTEXT_TESTCASE_OUTER_NOT_PREEMPTIBLE(outer)			\
	dotest(RCU_in_##outer, SUCCESS, LOCKTYPE_RWLOCK);		\
	dotest(raw_spinlock_in_##outer, SUCCESS, LOCKTYPE_SPIN);	\
	dotest(spinlock_in_##outer, FAILURE, LOCKTYPE_SPIN);		\
	dotest(mutex_in_##outer, FAILURE, LOCKTYPE_MUTEX);		\

static void wait_context_tests(void)
{
	printk("  --------------------------------------------------------------------------\n");
	printk("  | wait context tests |\n");
	printk("  --------------------------------------------------------------------------\n");
	printk("                                 | rcu  | raw  | spin |mutex |\n");
	printk("  --------------------------------------------------------------------------\n");
	print_testname("in hardirq context");
	DO_CONTEXT_TESTCASE_OUTER_LIMITED_PREEMPTIBLE(HARDIRQ);
	pr_cont("\n");

	print_testname("in hardirq context (not threaded)");
	DO_CONTEXT_TESTCASE_OUTER_NOT_PREEMPTIBLE(NOTTHREADED_HARDIRQ);
	pr_cont("\n");

	print_testname("in softirq context");
	DO_CONTEXT_TESTCASE_OUTER_LIMITED_PREEMPTIBLE(SOFTIRQ);
	pr_cont("\n");

	print_testname("in RCU context");
	DO_CONTEXT_TESTCASE_OUTER_LIMITED_PREEMPTIBLE(RCU);
	pr_cont("\n");

	print_testname("in RCU-bh context");
	DO_CONTEXT_TESTCASE_OUTER_LIMITED_PREEMPTIBLE(RCU_BH);
	pr_cont("\n");

	print_testname("in RCU-sched context");
	DO_CONTEXT_TESTCASE_OUTER_NOT_PREEMPTIBLE(RCU_SCHED);
	pr_cont("\n");

	print_testname("in RAW_SPINLOCK context");
	DO_CONTEXT_TESTCASE_OUTER_NOT_PREEMPTIBLE(raw_spinlock);
	pr_cont("\n");

	print_testname("in SPINLOCK context");
	DO_CONTEXT_TESTCASE_OUTER_LIMITED_PREEMPTIBLE(spinlock);
	pr_cont("\n");

	print_testname("in MUTEX context");
	DO_CONTEXT_TESTCASE_OUTER_PREEMPTIBLE(mutex);
	pr_cont("\n");
}

static void local_lock_2(void)
{
	local_lock(&local_A);	/* IRQ-ON */
	local_unlock(&local_A);

	HARDIRQ_ENTER();
	spin_lock(&lock_A);		/* IN-IRQ */
	spin_unlock(&lock_A);
	HARDIRQ_EXIT()

	HARDIRQ_DISABLE();
	spin_lock(&lock_A);
	local_lock(&local_A);	/* IN-IRQ <-> IRQ-ON cycle, false */
	local_unlock(&local_A);
	spin_unlock(&lock_A);
	HARDIRQ_ENABLE();
}

static void local_lock_3A(void)
{
	local_lock(&local_A);	/* IRQ-ON */
	spin_lock(&lock_B);		/* IRQ-ON */
	spin_unlock(&lock_B);
	local_unlock(&local_A);

	HARDIRQ_ENTER();
	spin_lock(&lock_A);		/* IN-IRQ */
	spin_unlock(&lock_A);
	HARDIRQ_EXIT()

	HARDIRQ_DISABLE();
	spin_lock(&lock_A);
	local_lock(&local_A);	/* IN-IRQ <-> IRQ-ON cycle only if we count local_lock(), false */
	local_unlock(&local_A);
	spin_unlock(&lock_A);
	HARDIRQ_ENABLE();
}

static void local_lock_3B(void)
{
	local_lock(&local_A);	/* IRQ-ON */
	spin_lock(&lock_B);		/* IRQ-ON */
	spin_unlock(&lock_B);
	local_unlock(&local_A);

	HARDIRQ_ENTER();
	spin_lock(&lock_A);		/* IN-IRQ */
	spin_unlock(&lock_A);
	HARDIRQ_EXIT()

	HARDIRQ_DISABLE();
	spin_lock(&lock_A);
	local_lock(&local_A);	/* IN-IRQ <-> IRQ-ON cycle only if we count local_lock(), false */
	local_unlock(&local_A);
	spin_unlock(&lock_A);
	HARDIRQ_ENABLE();

	HARDIRQ_DISABLE();
	spin_lock(&lock_A);
	spin_lock(&lock_B);		/* IN-IRQ <-> IRQ-ON cycle, true */
	spin_unlock(&lock_B);
	spin_unlock(&lock_A);
	HARDIRQ_DISABLE();

}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static inline const char *rw_semaphore_lockdep_name(struct rw_semaphore *rwsem)
{
	return rwsem->dep_map.name;
}
#else
static inline const char *rw_semaphore_lockdep_name(struct rw_semaphore *rwsem)
{
	return NULL;
}
#endif

static void test_lockdep_set_subclass_name(void)
{
	const char *name_before = rw_semaphore_lockdep_name(&rwsem_X1);
	const char *name_after;

	lockdep_set_subclass(&rwsem_X1, 1);
	name_after = rw_semaphore_lockdep_name(&rwsem_X1);
	DEBUG_LOCKS_WARN_ON(name_before != name_after);
}

/*
 * lockdep_set_subclass() should reuse the existing lock class name instead
 * of creating a new one.
 */
static void lockdep_set_subclass_name_test(void)
{
	printk("  --------------------------------------------------------------------------\n");
	printk("  | lockdep_set_subclass() name test|\n");
	printk("  -----------------------------------\n");

	print_testname("compare name before and after");
	dotest(test_lockdep_set_subclass_name, SUCCESS, LOCKTYPE_RWSEM);
	pr_cont("\n");
}

static void local_lock_tests(void)
{
	printk("  --------------------------------------------------------------------------\n");
	printk("  | local_lock tests |\n");
	printk("  ---------------------\n");

	print_testname("local_lock inversion  2");
	dotest(local_lock_2, SUCCESS, LOCKTYPE_LL);
	pr_cont("\n");

	print_testname("local_lock inversion 3A");
	dotest(local_lock_3A, SUCCESS, LOCKTYPE_LL);
	pr_cont("\n");

	print_testname("local_lock inversion 3B");
	dotest(local_lock_3B, FAILURE, LOCKTYPE_LL);
	pr_cont("\n");
}

static void hardirq_deadlock_softirq_not_deadlock(void)
{
	/* mutex_A is hardirq-unsafe and softirq-unsafe */
	/* mutex_A -> lock_C */
	mutex_lock(&mutex_A);
	HARDIRQ_DISABLE();
	spin_lock(&lock_C);
	spin_unlock(&lock_C);
	HARDIRQ_ENABLE();
	mutex_unlock(&mutex_A);

	/* lock_A is hardirq-safe */
	HARDIRQ_ENTER();
	spin_lock(&lock_A);
	spin_unlock(&lock_A);
	HARDIRQ_EXIT();

	/* lock_A -> lock_B */
	HARDIRQ_DISABLE();
	spin_lock(&lock_A);
	spin_lock(&lock_B);
	spin_unlock(&lock_B);
	spin_unlock(&lock_A);
	HARDIRQ_ENABLE();

	/* lock_B -> lock_C */
	HARDIRQ_DISABLE();
	spin_lock(&lock_B);
	spin_lock(&lock_C);
	spin_unlock(&lock_C);
	spin_unlock(&lock_B);
	HARDIRQ_ENABLE();

	/* lock_D is softirq-safe */
	SOFTIRQ_ENTER();
	spin_lock(&lock_D);
	spin_unlock(&lock_D);
	SOFTIRQ_EXIT();

	/* And lock_D is hardirq-unsafe */
	SOFTIRQ_DISABLE();
	spin_lock(&lock_D);
	spin_unlock(&lock_D);
	SOFTIRQ_ENABLE();

	/*
	 * mutex_A -> lock_C -> lock_D is softirq-unsafe -> softirq-safe, not
	 * deadlock.
	 *
	 * lock_A -> lock_B -> lock_C -> lock_D is hardirq-safe ->
	 * hardirq-unsafe, deadlock.
	 */
	HARDIRQ_DISABLE();
	spin_lock(&lock_C);
	spin_lock(&lock_D);
	spin_unlock(&lock_D);
	spin_unlock(&lock_C);
	HARDIRQ_ENABLE();
}

void locking_selftest(void)
{
	/*
	 * Got a locking failure before the selftest ran?
	 */
	if (!debug_locks) {
		printk("----------------------------------\n");
		printk("| Locking API testsuite disabled |\n");
		printk("----------------------------------\n");
		return;
	}

	/*
	 * treats read_lock() as recursive read locks for testing purpose
	 */
	force_read_lock_recursive = 1;

	/*
	 * Run the testsuite:
	 */
	printk("------------------------\n");
	printk("| Locking API testsuite:\n");
	printk("----------------------------------------------------------------------------\n");
	printk("                                 | spin |wlock |rlock |mutex | wsem | rsem |rtmutex\n");
	printk("  --------------------------------------------------------------------------\n");

	init_shared_classes();
	lockdep_set_selftest_task(current);

	DO_TESTCASE_6R("A-A deadlock", AA);
	DO_TESTCASE_6R("A-B-B-A deadlock", ABBA);
	DO_TESTCASE_6R("A-B-B-C-C-A deadlock", ABBCCA);
	DO_TESTCASE_6R("A-B-C-A-B-C deadlock", ABCABC);
	DO_TESTCASE_6R("A-B-B-C-C-D-D-A deadlock", ABBCCDDA);
	DO_TESTCASE_6R("A-B-C-D-B-D-D-A deadlock", ABCDBDDA);
	DO_TESTCASE_6R("A-B-C-D-B-C-D-A deadlock", ABCDBCDA);
	DO_TESTCASE_6("double unlock", double_unlock);
	DO_TESTCASE_6("initialize held", init_held);

	printk("  --------------------------------------------------------------------------\n");
	print_testname("recursive read-lock");
	pr_cont("             |");
	dotest(rlock_AA1, SUCCESS, LOCKTYPE_RWLOCK);
	pr_cont("             |");
	dotest(rsem_AA1, FAILURE, LOCKTYPE_RWSEM);
	pr_cont("\n");

	print_testname("recursive read-lock #2");
	pr_cont("             |");
	dotest(rlock_AA1B, SUCCESS, LOCKTYPE_RWLOCK);
	pr_cont("             |");
	dotest(rsem_AA1B, FAILURE, LOCKTYPE_RWSEM);
	pr_cont("\n");

	print_testname("mixed read-write-lock");
	pr_cont("             |");
	dotest(rlock_AA2, FAILURE, LOCKTYPE_RWLOCK);
	pr_cont("             |");
	dotest(rsem_AA2, FAILURE, LOCKTYPE_RWSEM);
	pr_cont("\n");

	print_testname("mixed write-read-lock");
	pr_cont("             |");
	dotest(rlock_AA3, FAILURE, LOCKTYPE_RWLOCK);
	pr_cont("             |");
	dotest(rsem_AA3, FAILURE, LOCKTYPE_RWSEM);
	pr_cont("\n");

	print_testname("mixed read-lock/lock-write ABBA");
	pr_cont("             |");
	dotest(rlock_ABBA1, FAILURE, LOCKTYPE_RWLOCK);
	pr_cont("             |");
	dotest(rwsem_ABBA1, FAILURE, LOCKTYPE_RWSEM);

	print_testname("mixed read-lock/lock-read ABBA");
	pr_cont("             |");
	dotest(rlock_ABBA2, SUCCESS, LOCKTYPE_RWLOCK);
	pr_cont("             |");
	dotest(rwsem_ABBA2, FAILURE, LOCKTYPE_RWSEM);

	print_testname("mixed write-lock/lock-write ABBA");
	pr_cont("             |");
	dotest(rlock_ABBA3, FAILURE, LOCKTYPE_RWLOCK);
	pr_cont("             |");
	dotest(rwsem_ABBA3, FAILURE, LOCKTYPE_RWSEM);

	print_testname("chain cached mixed R-L/L-W ABBA");
	pr_cont("             |");
	dotest(rlock_chaincache_ABBA1, FAILURE, LOCKTYPE_RWLOCK);

	DO_TESTCASE_6x1RRB("rlock W1R2/W2R3/W3R1", W1R2_W2R3_W3R1);
	DO_TESTCASE_6x1RRB("rlock W1W2/R2R3/W3R1", W1W2_R2R3_W3R1);
	DO_TESTCASE_6x1RR("rlock W1W2/R2R3/R3W1", W1W2_R2R3_R3W1);
	DO_TESTCASE_6x1RR("rlock W1R2/R2R3/W3W1", W1R2_R2R3_W3W1);

	printk("  --------------------------------------------------------------------------\n");
	/*
	 * irq-context testcases:
	 */
	DO_TESTCASE_2x6("irqs-on + irq-safe-A", irqsafe1);
	NON_RT(DO_TESTCASE_2x3("sirq-safe-A => hirqs-on", irqsafe2A));
	DO_TESTCASE_2x6("safe-A + irqs-on", irqsafe2B);
	DO_TESTCASE_6x6("safe-A + unsafe-B #1", irqsafe3);
	DO_TESTCASE_6x6("safe-A + unsafe-B #2", irqsafe4);
	DO_TESTCASE_6x6RW("irq lock-inversion", irq_inversion);

	DO_TESTCASE_6x2x2RW("irq read-recursion", irq_read_recursion);
	DO_TESTCASE_6x2x2RW("irq read-recursion #2", irq_read_recursion2);
	DO_TESTCASE_6x2x2RW("irq read-recursion #3", irq_read_recursion3);

	ww_tests();

	force_read_lock_recursive = 0;
	/*
	 * queued_read_lock() specific test cases can be put here
	 */
	if (IS_ENABLED(CONFIG_QUEUED_RWLOCKS))
		queued_read_lock_tests();

	fs_reclaim_tests();

	/* Wait context test cases that are specific for RAW_LOCK_NESTING */
	if (IS_ENABLED(CONFIG_PROVE_RAW_LOCK_NESTING))
		wait_context_tests();

	local_lock_tests();

	print_testname("hardirq_unsafe_softirq_safe");
	dotest(hardirq_deadlock_softirq_not_deadlock, FAILURE, LOCKTYPE_SPECIAL);
	pr_cont("\n");

	lockdep_set_subclass_name_test();

	if (unexpected_testcase_failures) {
		printk("-----------------------------------------------------------------\n");
		debug_locks = 0;
		printk("BUG: %3d unexpected failures (out of %3d) - debugging disabled! |\n",
			unexpected_testcase_failures, testcase_total);
		printk("-----------------------------------------------------------------\n");
	} else if (expected_testcase_failures && testcase_successes) {
		printk("--------------------------------------------------------\n");
		printk("%3d out of %3d testcases failed, as expected. |\n",
			expected_testcase_failures, testcase_total);
		printk("----------------------------------------------------\n");
		debug_locks = 1;
	} else if (expected_testcase_failures && !testcase_successes) {
		printk("--------------------------------------------------------\n");
		printk("All %3d testcases failed, as expected. |\n",
			expected_testcase_failures);
		printk("----------------------------------------\n");
		debug_locks = 1;
	} else {
		printk("-------------------------------------------------------\n");
		printk("Good, all %3d testcases passed! |\n",
			testcase_successes);
		printk("---------------------------------\n");
		debug_locks = 1;
	}
	lockdep_set_selftest_task(NULL);
	debug_locks_silent = 0;
}

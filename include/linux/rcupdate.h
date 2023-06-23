/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update mechanism for mutual exclusion
 *
 * Copyright IBM Corporation, 2001
 *
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 *
 * Based on the original work by Paul McKenney <paulmck@vnet.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		http://lse.sourceforge.net/locking/rcupdate.html
 *
 */

#ifndef __LINUX_RCUPDATE_H
#define __LINUX_RCUPDATE_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/atomic.h>
#include <linux/irqflags.h>
#include <linux/preempt.h>
#include <linux/bottom_half.h>
#include <linux/lockdep.h>
#include <asm/processor.h>
#include <linux/cpumask.h>
#include <linux/context_tracking_irq.h>

#define ULONG_CMP_GE(a, b)	(ULONG_MAX / 2 >= (a) - (b))
#define ULONG_CMP_LT(a, b)	(ULONG_MAX / 2 < (a) - (b))
#define ulong2long(a)		(*(long *)(&(a)))
#define USHORT_CMP_GE(a, b)	(USHRT_MAX / 2 >= (unsigned short)((a) - (b)))
#define USHORT_CMP_LT(a, b)	(USHRT_MAX / 2 < (unsigned short)((a) - (b)))

/* Exported common interfaces */
void call_rcu(struct rcu_head *head, rcu_callback_t func);
void rcu_barrier_tasks(void);
void rcu_barrier_tasks_rude(void);
void synchronize_rcu(void);
unsigned long get_completed_synchronize_rcu(void);

#ifdef CONFIG_PREEMPT_RCU

void __rcu_read_lock(void);
void __rcu_read_unlock(void);

/*
 * Defined as a macro as it is a very low level header included from
 * areas that don't even know about current.  This gives the rcu_read_lock()
 * nesting depth, but makes sense only if CONFIG_PREEMPT_RCU -- in other
 * types of kernel builds, the rcu_read_lock() nesting depth is unknowable.
 */
#define rcu_preempt_depth() READ_ONCE(current->rcu_read_lock_nesting)

#else /* #ifdef CONFIG_PREEMPT_RCU */

#ifdef CONFIG_TINY_RCU
#define rcu_read_unlock_strict() do { } while (0)
#else
void rcu_read_unlock_strict(void);
#endif

static inline void __rcu_read_lock(void)
{
	preempt_disable();
}

static inline void __rcu_read_unlock(void)
{
	preempt_enable();
	if (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD))
		rcu_read_unlock_strict();
}

static inline int rcu_preempt_depth(void)
{
	return 0;
}

#endif /* #else #ifdef CONFIG_PREEMPT_RCU */

/* Internal to kernel */
void rcu_init(void);
extern int rcu_scheduler_active;
void rcu_sched_clock_irq(int user);
void rcu_report_dead(unsigned int cpu);
void rcutree_migrate_callbacks(int cpu);

#ifdef CONFIG_TASKS_RCU_GENERIC
void rcu_init_tasks_generic(void);
#else
static inline void rcu_init_tasks_generic(void) { }
#endif

#ifdef CONFIG_RCU_STALL_COMMON
void rcu_sysrq_start(void);
void rcu_sysrq_end(void);
#else /* #ifdef CONFIG_RCU_STALL_COMMON */
static inline void rcu_sysrq_start(void) { }
static inline void rcu_sysrq_end(void) { }
#endif /* #else #ifdef CONFIG_RCU_STALL_COMMON */

#if defined(CONFIG_NO_HZ_FULL) && (!defined(CONFIG_GENERIC_ENTRY) || !defined(CONFIG_KVM_XFER_TO_GUEST_WORK))
void rcu_irq_work_resched(void);
#else
static inline void rcu_irq_work_resched(void) { }
#endif

#ifdef CONFIG_RCU_NOCB_CPU
void rcu_init_nohz(void);
int rcu_nocb_cpu_offload(int cpu);
int rcu_nocb_cpu_deoffload(int cpu);
void rcu_nocb_flush_deferred_wakeup(void);
#else /* #ifdef CONFIG_RCU_NOCB_CPU */
static inline void rcu_init_nohz(void) { }
static inline int rcu_nocb_cpu_offload(int cpu) { return -EINVAL; }
static inline int rcu_nocb_cpu_deoffload(int cpu) { return 0; }
static inline void rcu_nocb_flush_deferred_wakeup(void) { }
#endif /* #else #ifdef CONFIG_RCU_NOCB_CPU */

/**
 * RCU_NONIDLE - Indicate idle-loop code that needs RCU readers
 * @a: Code that RCU needs to pay attention to.
 *
 * RCU read-side critical sections are forbidden in the inner idle loop,
 * that is, between the ct_idle_enter() and the ct_idle_exit() -- RCU
 * will happily ignore any such read-side critical sections.  However,
 * things like powertop need tracepoints in the inner idle loop.
 *
 * This macro provides the way out:  RCU_NONIDLE(do_something_with_RCU())
 * will tell RCU that it needs to pay attention, invoke its argument
 * (in this example, calling the do_something_with_RCU() function),
 * and then tell RCU to go back to ignoring this CPU.  It is permissible
 * to nest RCU_NONIDLE() wrappers, but not indefinitely (but the limit is
 * on the order of a million or so, even on 32-bit systems).  It is
 * not legal to block within RCU_NONIDLE(), nor is it permissible to
 * transfer control either into or out of RCU_NONIDLE()'s statement.
 */
#define RCU_NONIDLE(a) \
	do { \
		ct_irq_enter_irqson(); \
		do { a; } while (0); \
		ct_irq_exit_irqson(); \
	} while (0)

/*
 * Note a quasi-voluntary context switch for RCU-tasks's benefit.
 * This is a macro rather than an inline function to avoid #include hell.
 */
#ifdef CONFIG_TASKS_RCU_GENERIC

# ifdef CONFIG_TASKS_RCU
# define rcu_tasks_classic_qs(t, preempt)				\
	do {								\
		if (!(preempt) && READ_ONCE((t)->rcu_tasks_holdout))	\
			WRITE_ONCE((t)->rcu_tasks_holdout, false);	\
	} while (0)
void call_rcu_tasks(struct rcu_head *head, rcu_callback_t func);
void synchronize_rcu_tasks(void);
# else
# define rcu_tasks_classic_qs(t, preempt) do { } while (0)
# define call_rcu_tasks call_rcu
# define synchronize_rcu_tasks synchronize_rcu
# endif

# ifdef CONFIG_TASKS_TRACE_RCU
// Bits for ->trc_reader_special.b.need_qs field.
#define TRC_NEED_QS		0x1  // Task needs a quiescent state.
#define TRC_NEED_QS_CHECKED	0x2  // Task has been checked for needing quiescent state.

u8 rcu_trc_cmpxchg_need_qs(struct task_struct *t, u8 old, u8 new);
void rcu_tasks_trace_qs_blkd(struct task_struct *t);

# define rcu_tasks_trace_qs(t)							\
	do {									\
		int ___rttq_nesting = READ_ONCE((t)->trc_reader_nesting);	\
										\
		if (likely(!READ_ONCE((t)->trc_reader_special.b.need_qs)) &&	\
		    likely(!___rttq_nesting)) {					\
			rcu_trc_cmpxchg_need_qs((t), 0,	TRC_NEED_QS_CHECKED);	\
		} else if (___rttq_nesting && ___rttq_nesting != INT_MIN &&	\
			   !READ_ONCE((t)->trc_reader_special.b.blocked)) {	\
			rcu_tasks_trace_qs_blkd(t);				\
		}								\
	} while (0)
# else
# define rcu_tasks_trace_qs(t) do { } while (0)
# endif

#define rcu_tasks_qs(t, preempt)					\
do {									\
	rcu_tasks_classic_qs((t), (preempt));				\
	rcu_tasks_trace_qs(t);						\
} while (0)

# ifdef CONFIG_TASKS_RUDE_RCU
void call_rcu_tasks_rude(struct rcu_head *head, rcu_callback_t func);
void synchronize_rcu_tasks_rude(void);
# endif

#define rcu_note_voluntary_context_switch(t) rcu_tasks_qs(t, false)
void exit_tasks_rcu_start(void);
void exit_tasks_rcu_finish(void);
#else /* #ifdef CONFIG_TASKS_RCU_GENERIC */
#define rcu_tasks_classic_qs(t, preempt) do { } while (0)
#define rcu_tasks_qs(t, preempt) do { } while (0)
#define rcu_note_voluntary_context_switch(t) do { } while (0)
#define call_rcu_tasks call_rcu
#define synchronize_rcu_tasks synchronize_rcu
static inline void exit_tasks_rcu_start(void) { }
static inline void exit_tasks_rcu_finish(void) { }
#endif /* #else #ifdef CONFIG_TASKS_RCU_GENERIC */

/**
 * cond_resched_tasks_rcu_qs - Report potential quiescent states to RCU
 *
 * This macro resembles cond_resched(), except that it is defined to
 * report potential quiescent states to RCU-tasks even if the cond_resched()
 * machinery were to be shut off, as some advocate for PREEMPTION kernels.
 */
#define cond_resched_tasks_rcu_qs() \
do { \
	rcu_tasks_qs(current, false); \
	cond_resched(); \
} while (0)

/*
 * Infrastructure to implement the synchronize_() primitives in
 * TREE_RCU and rcu_barrier_() primitives in TINY_RCU.
 */

#if defined(CONFIG_TREE_RCU)
#include <linux/rcutree.h>
#elif defined(CONFIG_TINY_RCU)
#include <linux/rcutiny.h>
#else
#error "Unknown RCU implementation specified to kernel configuration"
#endif

/*
 * The init_rcu_head_on_stack() and destroy_rcu_head_on_stack() calls
 * are needed for dynamic initialization and destruction of rcu_head
 * on the stack, and init_rcu_head()/destroy_rcu_head() are needed for
 * dynamic initialization and destruction of statically allocated rcu_head
 * structures.  However, rcu_head structures allocated dynamically in the
 * heap don't need any initialization.
 */
#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
void init_rcu_head(struct rcu_head *head);
void destroy_rcu_head(struct rcu_head *head);
void init_rcu_head_on_stack(struct rcu_head *head);
void destroy_rcu_head_on_stack(struct rcu_head *head);
#else /* !CONFIG_DEBUG_OBJECTS_RCU_HEAD */
static inline void init_rcu_head(struct rcu_head *head) { }
static inline void destroy_rcu_head(struct rcu_head *head) { }
static inline void init_rcu_head_on_stack(struct rcu_head *head) { }
static inline void destroy_rcu_head_on_stack(struct rcu_head *head) { }
#endif	/* #else !CONFIG_DEBUG_OBJECTS_RCU_HEAD */

#if defined(CONFIG_HOTPLUG_CPU) && defined(CONFIG_PROVE_RCU)
bool rcu_lockdep_current_cpu_online(void);
#else /* #if defined(CONFIG_HOTPLUG_CPU) && defined(CONFIG_PROVE_RCU) */
static inline bool rcu_lockdep_current_cpu_online(void) { return true; }
#endif /* #else #if defined(CONFIG_HOTPLUG_CPU) && defined(CONFIG_PROVE_RCU) */

extern struct lockdep_map rcu_lock_map;
extern struct lockdep_map rcu_bh_lock_map;
extern struct lockdep_map rcu_sched_lock_map;
extern struct lockdep_map rcu_callback_map;

#ifdef CONFIG_DEBUG_LOCK_ALLOC

static inline void rcu_lock_acquire(struct lockdep_map *map)
{
	lock_acquire(map, 0, 0, 2, 0, NULL, _THIS_IP_);
}

static inline void rcu_lock_release(struct lockdep_map *map)
{
	lock_release(map, _THIS_IP_);
}

int debug_lockdep_rcu_enabled(void);
int rcu_read_lock_held(void);
int rcu_read_lock_bh_held(void);
int rcu_read_lock_sched_held(void);
int rcu_read_lock_any_held(void);

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

# define rcu_lock_acquire(a)		do { } while (0)
# define rcu_lock_release(a)		do { } while (0)

static inline int rcu_read_lock_held(void)
{
	return 1;
}

static inline int rcu_read_lock_bh_held(void)
{
	return 1;
}

static inline int rcu_read_lock_sched_held(void)
{
	return !preemptible();
}

static inline int rcu_read_lock_any_held(void)
{
	return !preemptible();
}

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

#ifdef CONFIG_PROVE_RCU

/**
 * RCU_LOCKDEP_WARN - emit lockdep splat if specified condition is met
 * @c: condition to check
 * @s: informative message
 */
#define RCU_LOCKDEP_WARN(c, s)						\
	do {								\
		static bool __section(".data.unlikely") __warned;	\
		if ((c) && debug_lockdep_rcu_enabled() && !__warned) {	\
			__warned = true;				\
			lockdep_rcu_suspicious(__FILE__, __LINE__, s);	\
		}							\
	} while (0)

#if defined(CONFIG_PROVE_RCU) && !defined(CONFIG_PREEMPT_RCU)
static inline void rcu_preempt_sleep_check(void)
{
	RCU_LOCKDEP_WARN(lock_is_held(&rcu_lock_map),
			 "Illegal context switch in RCU read-side critical section");
}
#else /* #ifdef CONFIG_PROVE_RCU */
static inline void rcu_preempt_sleep_check(void) { }
#endif /* #else #ifdef CONFIG_PROVE_RCU */

#define rcu_sleep_check()						\
	do {								\
		rcu_preempt_sleep_check();				\
		if (!IS_ENABLED(CONFIG_PREEMPT_RT))			\
		    RCU_LOCKDEP_WARN(lock_is_held(&rcu_bh_lock_map),	\
				 "Illegal context switch in RCU-bh read-side critical section"); \
		RCU_LOCKDEP_WARN(lock_is_held(&rcu_sched_lock_map),	\
				 "Illegal context switch in RCU-sched read-side critical section"); \
	} while (0)

#else /* #ifdef CONFIG_PROVE_RCU */

#define RCU_LOCKDEP_WARN(c, s) do { } while (0 && (c))
#define rcu_sleep_check() do { } while (0)

#endif /* #else #ifdef CONFIG_PROVE_RCU */

/*
 * Helper functions for rcu_dereference_check(), rcu_dereference_protected()
 * and rcu_assign_pointer().  Some of these could be folded into their
 * callers, but they are left separate in order to ease introduction of
 * multiple pointers markings to match different RCU implementations
 * (e.g., __srcu), should this make sense in the future.
 */

#ifdef __CHECKER__
#define rcu_check_sparse(p, space) \
	((void)(((typeof(*p) space *)p) == p))
#else /* #ifdef __CHECKER__ */
#define rcu_check_sparse(p, space)
#endif /* #else #ifdef __CHECKER__ */

#define __unrcu_pointer(p, local)					\
({									\
	typeof(*p) *local = (typeof(*p) *__force)(p);			\
	rcu_check_sparse(p, __rcu);					\
	((typeof(*p) __force __kernel *)(local)); 			\
})
/**
 * unrcu_pointer - mark a pointer as not being RCU protected
 * @p: pointer needing to lose its __rcu property
 *
 * Converts @p from an __rcu pointer to a __kernel pointer.
 * This allows an __rcu pointer to be used with xchg() and friends.
 */
#define unrcu_pointer(p) __unrcu_pointer(p, __UNIQUE_ID(rcu))

#define __rcu_access_pointer(p, local, space) \
({ \
	typeof(*p) *local = (typeof(*p) *__force)READ_ONCE(p); \
	rcu_check_sparse(p, space); \
	((typeof(*p) __force __kernel *)(local)); \
})
#define __rcu_dereference_check(p, local, c, space) \
({ \
	/* Dependency order vs. p above. */ \
	typeof(*p) *local = (typeof(*p) *__force)READ_ONCE(p); \
	RCU_LOCKDEP_WARN(!(c), "suspicious rcu_dereference_check() usage"); \
	rcu_check_sparse(p, space); \
	((typeof(*p) __force __kernel *)(local)); \
})
#define __rcu_dereference_protected(p, local, c, space) \
({ \
	RCU_LOCKDEP_WARN(!(c), "suspicious rcu_dereference_protected() usage"); \
	rcu_check_sparse(p, space); \
	((typeof(*p) __force __kernel *)(p)); \
})
#define __rcu_dereference_raw(p, local) \
({ \
	/* Dependency order vs. p above. */ \
	typeof(p) local = READ_ONCE(p); \
	((typeof(*p) __force __kernel *)(local)); \
})
#define rcu_dereference_raw(p) __rcu_dereference_raw(p, __UNIQUE_ID(rcu))

/**
 * RCU_INITIALIZER() - statically initialize an RCU-protected global variable
 * @v: The value to statically initialize with.
 */
#define RCU_INITIALIZER(v) (typeof(*(v)) __force __rcu *)(v)

/**
 * rcu_assign_pointer() - assign to RCU-protected pointer
 * @p: pointer to assign to
 * @v: value to assign (publish)
 *
 * Assigns the specified value to the specified RCU-protected
 * pointer, ensuring that any concurrent RCU readers will see
 * any prior initialization.
 *
 * Inserts memory barriers on architectures that require them
 * (which is most of them), and also prevents the compiler from
 * reordering the code that initializes the structure after the pointer
 * assignment.  More importantly, this call documents which pointers
 * will be dereferenced by RCU read-side code.
 *
 * In some special cases, you may use RCU_INIT_POINTER() instead
 * of rcu_assign_pointer().  RCU_INIT_POINTER() is a bit faster due
 * to the fact that it does not constrain either the CPU or the compiler.
 * That said, using RCU_INIT_POINTER() when you should have used
 * rcu_assign_pointer() is a very bad thing that results in
 * impossible-to-diagnose memory corruption.  So please be careful.
 * See the RCU_INIT_POINTER() comment header for details.
 *
 * Note that rcu_assign_pointer() evaluates each of its arguments only
 * once, appearances notwithstanding.  One of the "extra" evaluations
 * is in typeof() and the other visible only to sparse (__CHECKER__),
 * neither of which actually execute the argument.  As with most cpp
 * macros, this execute-arguments-only-once property is important, so
 * please be careful when making changes to rcu_assign_pointer() and the
 * other macros that it invokes.
 */
#define rcu_assign_pointer(p, v)					      \
do {									      \
	uintptr_t _r_a_p__v = (uintptr_t)(v);				      \
	rcu_check_sparse(p, __rcu);					      \
									      \
	if (__builtin_constant_p(v) && (_r_a_p__v) == (uintptr_t)NULL)	      \
		WRITE_ONCE((p), (typeof(p))(_r_a_p__v));		      \
	else								      \
		smp_store_release(&p, RCU_INITIALIZER((typeof(p))_r_a_p__v)); \
} while (0)

/**
 * rcu_replace_pointer() - replace an RCU pointer, returning its old value
 * @rcu_ptr: RCU pointer, whose old value is returned
 * @ptr: regular pointer
 * @c: the lockdep conditions under which the dereference will take place
 *
 * Perform a replacement, where @rcu_ptr is an RCU-annotated
 * pointer and @c is the lockdep argument that is passed to the
 * rcu_dereference_protected() call used to read that pointer.  The old
 * value of @rcu_ptr is returned, and @rcu_ptr is set to @ptr.
 */
#define rcu_replace_pointer(rcu_ptr, ptr, c)				\
({									\
	typeof(ptr) __tmp = rcu_dereference_protected((rcu_ptr), (c));	\
	rcu_assign_pointer((rcu_ptr), (ptr));				\
	__tmp;								\
})

/**
 * rcu_access_pointer() - fetch RCU pointer with no dereferencing
 * @p: The pointer to read
 *
 * Return the value of the specified RCU-protected pointer, but omit the
 * lockdep checks for being in an RCU read-side critical section.  This is
 * useful when the value of this pointer is accessed, but the pointer is
 * not dereferenced, for example, when testing an RCU-protected pointer
 * against NULL.  Although rcu_access_pointer() may also be used in cases
 * where update-side locks prevent the value of the pointer from changing,
 * you should instead use rcu_dereference_protected() for this use case.
 *
 * It is also permissible to use rcu_access_pointer() when read-side
 * access to the pointer was removed at least one grace period ago, as
 * is the case in the context of the RCU callback that is freeing up
 * the data, or after a synchronize_rcu() returns.  This can be useful
 * when tearing down multi-linked structures after a grace period
 * has elapsed.
 */
#define rcu_access_pointer(p) __rcu_access_pointer((p), __UNIQUE_ID(rcu), __rcu)

/**
 * rcu_dereference_check() - rcu_dereference with debug checking
 * @p: The pointer to read, prior to dereferencing
 * @c: The conditions under which the dereference will take place
 *
 * Do an rcu_dereference(), but check that the conditions under which the
 * dereference will take place are correct.  Typically the conditions
 * indicate the various locking conditions that should be held at that
 * point.  The check should return true if the conditions are satisfied.
 * An implicit check for being in an RCU read-side critical section
 * (rcu_read_lock()) is included.
 *
 * For example:
 *
 *	bar = rcu_dereference_check(foo->bar, lockdep_is_held(&foo->lock));
 *
 * could be used to indicate to lockdep that foo->bar may only be dereferenced
 * if either rcu_read_lock() is held, or that the lock required to replace
 * the bar struct at foo->bar is held.
 *
 * Note that the list of conditions may also include indications of when a lock
 * need not be held, for example during initialisation or destruction of the
 * target struct:
 *
 *	bar = rcu_dereference_check(foo->bar, lockdep_is_held(&foo->lock) ||
 *					      atomic_read(&foo->usage) == 0);
 *
 * Inserts memory barriers on architectures that require them
 * (currently only the Alpha), prevents the compiler from refetching
 * (and from merging fetches), and, more importantly, documents exactly
 * which pointers are protected by RCU and checks that the pointer is
 * annotated as __rcu.
 */
#define rcu_dereference_check(p, c) \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), \
				(c) || rcu_read_lock_held(), __rcu)

/**
 * rcu_dereference_bh_check() - rcu_dereference_bh with debug checking
 * @p: The pointer to read, prior to dereferencing
 * @c: The conditions under which the dereference will take place
 *
 * This is the RCU-bh counterpart to rcu_dereference_check().  However,
 * please note that starting in v5.0 kernels, vanilla RCU grace periods
 * wait for local_bh_disable() regions of code in addition to regions of
 * code demarked by rcu_read_lock() and rcu_read_unlock().  This means
 * that synchronize_rcu(), call_rcu, and friends all take not only
 * rcu_read_lock() but also rcu_read_lock_bh() into account.
 */
#define rcu_dereference_bh_check(p, c) \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), \
				(c) || rcu_read_lock_bh_held(), __rcu)

/**
 * rcu_dereference_sched_check() - rcu_dereference_sched with debug checking
 * @p: The pointer to read, prior to dereferencing
 * @c: The conditions under which the dereference will take place
 *
 * This is the RCU-sched counterpart to rcu_dereference_check().
 * However, please note that starting in v5.0 kernels, vanilla RCU grace
 * periods wait for preempt_disable() regions of code in addition to
 * regions of code demarked by rcu_read_lock() and rcu_read_unlock().
 * This means that synchronize_rcu(), call_rcu, and friends all take not
 * only rcu_read_lock() but also rcu_read_lock_sched() into account.
 */
#define rcu_dereference_sched_check(p, c) \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), \
				(c) || rcu_read_lock_sched_held(), \
				__rcu)

/*
 * The tracing infrastructure traces RCU (we want that), but unfortunately
 * some of the RCU checks causes tracing to lock up the system.
 *
 * The no-tracing version of rcu_dereference_raw() must not call
 * rcu_read_lock_held().
 */
#define rcu_dereference_raw_check(p) \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), 1, __rcu)

/**
 * rcu_dereference_protected() - fetch RCU pointer when updates prevented
 * @p: The pointer to read, prior to dereferencing
 * @c: The conditions under which the dereference will take place
 *
 * Return the value of the specified RCU-protected pointer, but omit
 * the READ_ONCE().  This is useful in cases where update-side locks
 * prevent the value of the pointer from changing.  Please note that this
 * primitive does *not* prevent the compiler from repeating this reference
 * or combining it with other references, so it should not be used without
 * protection of appropriate locks.
 *
 * This function is only for update-side use.  Using this function
 * when protected only by rcu_read_lock() will result in infrequent
 * but very ugly failures.
 */
#define rcu_dereference_protected(p, c) \
	__rcu_dereference_protected((p), __UNIQUE_ID(rcu), (c), __rcu)


/**
 * rcu_dereference() - fetch RCU-protected pointer for dereferencing
 * @p: The pointer to read, prior to dereferencing
 *
 * This is a simple wrapper around rcu_dereference_check().
 */
#define rcu_dereference(p) rcu_dereference_check(p, 0)

/**
 * rcu_dereference_bh() - fetch an RCU-bh-protected pointer for dereferencing
 * @p: The pointer to read, prior to dereferencing
 *
 * Makes rcu_dereference_check() do the dirty work.
 */
#define rcu_dereference_bh(p) rcu_dereference_bh_check(p, 0)

/**
 * rcu_dereference_sched() - fetch RCU-sched-protected pointer for dereferencing
 * @p: The pointer to read, prior to dereferencing
 *
 * Makes rcu_dereference_check() do the dirty work.
 */
#define rcu_dereference_sched(p) rcu_dereference_sched_check(p, 0)

/**
 * rcu_pointer_handoff() - Hand off a pointer from RCU to other mechanism
 * @p: The pointer to hand off
 *
 * This is simply an identity function, but it documents where a pointer
 * is handed off from RCU to some other synchronization mechanism, for
 * example, reference counting or locking.  In C11, it would map to
 * kill_dependency().  It could be used as follows::
 *
 *	rcu_read_lock();
 *	p = rcu_dereference(gp);
 *	long_lived = is_long_lived(p);
 *	if (long_lived) {
 *		if (!atomic_inc_not_zero(p->refcnt))
 *			long_lived = false;
 *		else
 *			p = rcu_pointer_handoff(p);
 *	}
 *	rcu_read_unlock();
 */
#define rcu_pointer_handoff(p) (p)

/**
 * rcu_read_lock() - mark the beginning of an RCU read-side critical section
 *
 * When synchronize_rcu() is invoked on one CPU while other CPUs
 * are within RCU read-side critical sections, then the
 * synchronize_rcu() is guaranteed to block until after all the other
 * CPUs exit their critical sections.  Similarly, if call_rcu() is invoked
 * on one CPU while other CPUs are within RCU read-side critical
 * sections, invocation of the corresponding RCU callback is deferred
 * until after the all the other CPUs exit their critical sections.
 *
 * In v5.0 and later kernels, synchronize_rcu() and call_rcu() also
 * wait for regions of code with preemption disabled, including regions of
 * code with interrupts or softirqs disabled.  In pre-v5.0 kernels, which
 * define synchronize_sched(), only code enclosed within rcu_read_lock()
 * and rcu_read_unlock() are guaranteed to be waited for.
 *
 * Note, however, that RCU callbacks are permitted to run concurrently
 * with new RCU read-side critical sections.  One way that this can happen
 * is via the following sequence of events: (1) CPU 0 enters an RCU
 * read-side critical section, (2) CPU 1 invokes call_rcu() to register
 * an RCU callback, (3) CPU 0 exits the RCU read-side critical section,
 * (4) CPU 2 enters a RCU read-side critical section, (5) the RCU
 * callback is invoked.  This is legal, because the RCU read-side critical
 * section that was running concurrently with the call_rcu() (and which
 * therefore might be referencing something that the corresponding RCU
 * callback would free up) has completed before the corresponding
 * RCU callback is invoked.
 *
 * RCU read-side critical sections may be nested.  Any deferred actions
 * will be deferred until the outermost RCU read-side critical section
 * completes.
 *
 * You can avoid reading and understanding the next paragraph by
 * following this rule: don't put anything in an rcu_read_lock() RCU
 * read-side critical section that would block in a !PREEMPTION kernel.
 * But if you want the full story, read on!
 *
 * In non-preemptible RCU implementations (pure TREE_RCU and TINY_RCU),
 * it is illegal to block while in an RCU read-side critical section.
 * In preemptible RCU implementations (PREEMPT_RCU) in CONFIG_PREEMPTION
 * kernel builds, RCU read-side critical sections may be preempted,
 * but explicit blocking is illegal.  Finally, in preemptible RCU
 * implementations in real-time (with -rt patchset) kernel builds, RCU
 * read-side critical sections may be preempted and they may also block, but
 * only when acquiring spinlocks that are subject to priority inheritance.
 */
static __always_inline void rcu_read_lock(void)
{
	__rcu_read_lock();
	__acquire(RCU);
	rcu_lock_acquire(&rcu_lock_map);
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_lock() used illegally while idle");
}

/*
 * So where is rcu_write_lock()?  It does not exist, as there is no
 * way for writers to lock out RCU readers.  This is a feature, not
 * a bug -- this property is what provides RCU's performance benefits.
 * Of course, writers must coordinate with each other.  The normal
 * spinlock primitives work well for this, but any other technique may be
 * used as well.  RCU does not care how the writers keep out of each
 * others' way, as long as they do so.
 */

/**
 * rcu_read_unlock() - marks the end of an RCU read-side critical section.
 *
 * In almost all situations, rcu_read_unlock() is immune from deadlock.
 * In recent kernels that have consolidated synchronize_sched() and
 * synchronize_rcu_bh() into synchronize_rcu(), this deadlock immunity
 * also extends to the scheduler's runqueue and priority-inheritance
 * spinlocks, courtesy of the quiescent-state deferral that is carried
 * out when rcu_read_unlock() is invoked with interrupts disabled.
 *
 * See rcu_read_lock() for more information.
 */
static inline void rcu_read_unlock(void)
{
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_unlock() used illegally while idle");
	__release(RCU);
	__rcu_read_unlock();
	rcu_lock_release(&rcu_lock_map); /* Keep acq info for rls diags. */
}

/**
 * rcu_read_lock_bh() - mark the beginning of an RCU-bh critical section
 *
 * This is equivalent to rcu_read_lock(), but also disables softirqs.
 * Note that anything else that disables softirqs can also serve as an RCU
 * read-side critical section.  However, please note that this equivalence
 * applies only to v5.0 and later.  Before v5.0, rcu_read_lock() and
 * rcu_read_lock_bh() were unrelated.
 *
 * Note that rcu_read_lock_bh() and the matching rcu_read_unlock_bh()
 * must occur in the same context, for example, it is illegal to invoke
 * rcu_read_unlock_bh() from one task if the matching rcu_read_lock_bh()
 * was invoked from some other task.
 */
static inline void rcu_read_lock_bh(void)
{
	local_bh_disable();
	__acquire(RCU_BH);
	rcu_lock_acquire(&rcu_bh_lock_map);
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_lock_bh() used illegally while idle");
}

/**
 * rcu_read_unlock_bh() - marks the end of a softirq-only RCU critical section
 *
 * See rcu_read_lock_bh() for more information.
 */
static inline void rcu_read_unlock_bh(void)
{
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_unlock_bh() used illegally while idle");
	rcu_lock_release(&rcu_bh_lock_map);
	__release(RCU_BH);
	local_bh_enable();
}

/**
 * rcu_read_lock_sched() - mark the beginning of a RCU-sched critical section
 *
 * This is equivalent to rcu_read_lock(), but also disables preemption.
 * Read-side critical sections can also be introduced by anything else that
 * disables preemption, including local_irq_disable() and friends.  However,
 * please note that the equivalence to rcu_read_lock() applies only to
 * v5.0 and later.  Before v5.0, rcu_read_lock() and rcu_read_lock_sched()
 * were unrelated.
 *
 * Note that rcu_read_lock_sched() and the matching rcu_read_unlock_sched()
 * must occur in the same context, for example, it is illegal to invoke
 * rcu_read_unlock_sched() from process context if the matching
 * rcu_read_lock_sched() was invoked from an NMI handler.
 */
static inline void rcu_read_lock_sched(void)
{
	preempt_disable();
	__acquire(RCU_SCHED);
	rcu_lock_acquire(&rcu_sched_lock_map);
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_lock_sched() used illegally while idle");
}

/* Used by lockdep and tracing: cannot be traced, cannot call lockdep. */
static inline notrace void rcu_read_lock_sched_notrace(void)
{
	preempt_disable_notrace();
	__acquire(RCU_SCHED);
}

/**
 * rcu_read_unlock_sched() - marks the end of a RCU-classic critical section
 *
 * See rcu_read_lock_sched() for more information.
 */
static inline void rcu_read_unlock_sched(void)
{
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_unlock_sched() used illegally while idle");
	rcu_lock_release(&rcu_sched_lock_map);
	__release(RCU_SCHED);
	preempt_enable();
}

/* Used by lockdep and tracing: cannot be traced, cannot call lockdep. */
static inline notrace void rcu_read_unlock_sched_notrace(void)
{
	__release(RCU_SCHED);
	preempt_enable_notrace();
}

/**
 * RCU_INIT_POINTER() - initialize an RCU protected pointer
 * @p: The pointer to be initialized.
 * @v: The value to initialized the pointer to.
 *
 * Initialize an RCU-protected pointer in special cases where readers
 * do not need ordering constraints on the CPU or the compiler.  These
 * special cases are:
 *
 * 1.	This use of RCU_INIT_POINTER() is NULLing out the pointer *or*
 * 2.	The caller has taken whatever steps are required to prevent
 *	RCU readers from concurrently accessing this pointer *or*
 * 3.	The referenced data structure has already been exposed to
 *	readers either at compile time or via rcu_assign_pointer() *and*
 *
 *	a.	You have not made *any* reader-visible changes to
 *		this structure since then *or*
 *	b.	It is OK for readers accessing this structure from its
 *		new location to see the old state of the structure.  (For
 *		example, the changes were to statistical counters or to
 *		other state where exact synchronization is not required.)
 *
 * Failure to follow these rules governing use of RCU_INIT_POINTER() will
 * result in impossible-to-diagnose memory corruption.  As in the structures
 * will look OK in crash dumps, but any concurrent RCU readers might
 * see pre-initialized values of the referenced data structure.  So
 * please be very careful how you use RCU_INIT_POINTER()!!!
 *
 * If you are creating an RCU-protected linked structure that is accessed
 * by a single external-to-structure RCU-protected pointer, then you may
 * use RCU_INIT_POINTER() to initialize the internal RCU-protected
 * pointers, but you must use rcu_assign_pointer() to initialize the
 * external-to-structure pointer *after* you have completely initialized
 * the reader-accessible portions of the linked structure.
 *
 * Note that unlike rcu_assign_pointer(), RCU_INIT_POINTER() provides no
 * ordering guarantees for either the CPU or the compiler.
 */
#define RCU_INIT_POINTER(p, v) \
	do { \
		rcu_check_sparse(p, __rcu); \
		WRITE_ONCE(p, RCU_INITIALIZER(v)); \
	} while (0)

/**
 * RCU_POINTER_INITIALIZER() - statically initialize an RCU protected pointer
 * @p: The pointer to be initialized.
 * @v: The value to initialized the pointer to.
 *
 * GCC-style initialization for an RCU-protected pointer in a structure field.
 */
#define RCU_POINTER_INITIALIZER(p, v) \
		.p = RCU_INITIALIZER(v)

/*
 * Does the specified offset indicate that the corresponding rcu_head
 * structure can be handled by kvfree_rcu()?
 */
#define __is_kvfree_rcu_offset(offset) ((offset) < 4096)

/**
 * kfree_rcu() - kfree an object after a grace period.
 * @ptr: pointer to kfree for both single- and double-argument invocations.
 * @rhf: the name of the struct rcu_head within the type of @ptr,
 *       but only for double-argument invocations.
 *
 * Many rcu callbacks functions just call kfree() on the base structure.
 * These functions are trivial, but their size adds up, and furthermore
 * when they are used in a kernel module, that module must invoke the
 * high-latency rcu_barrier() function at module-unload time.
 *
 * The kfree_rcu() function handles this issue.  Rather than encoding a
 * function address in the embedded rcu_head structure, kfree_rcu() instead
 * encodes the offset of the rcu_head structure within the base structure.
 * Because the functions are not allowed in the low-order 4096 bytes of
 * kernel virtual memory, offsets up to 4095 bytes can be accommodated.
 * If the offset is larger than 4095 bytes, a compile-time error will
 * be generated in kvfree_rcu_arg_2(). If this error is triggered, you can
 * either fall back to use of call_rcu() or rearrange the structure to
 * position the rcu_head structure into the first 4096 bytes.
 *
 * Note that the allowable offset might decrease in the future, for example,
 * to allow something like kmem_cache_free_rcu().
 *
 * The BUILD_BUG_ON check must not involve any function calls, hence the
 * checks are done in macros here.
 */
#define kfree_rcu(ptr, rhf...) kvfree_rcu(ptr, ## rhf)

/**
 * kvfree_rcu() - kvfree an object after a grace period.
 *
 * This macro consists of one or two arguments and it is
 * based on whether an object is head-less or not. If it
 * has a head then a semantic stays the same as it used
 * to be before:
 *
 *     kvfree_rcu(ptr, rhf);
 *
 * where @ptr is a pointer to kvfree(), @rhf is the name
 * of the rcu_head structure within the type of @ptr.
 *
 * When it comes to head-less variant, only one argument
 * is passed and that is just a pointer which has to be
 * freed after a grace period. Therefore the semantic is
 *
 *     kvfree_rcu(ptr);
 *
 * where @ptr is the pointer to be freed by kvfree().
 *
 * Please note, head-less way of freeing is permitted to
 * use from a context that has to follow might_sleep()
 * annotation. Otherwise, please switch and embed the
 * rcu_head structure within the type of @ptr.
 */
#define kvfree_rcu(...) KVFREE_GET_MACRO(__VA_ARGS__,		\
	kvfree_rcu_arg_2, kvfree_rcu_arg_1)(__VA_ARGS__)

#define KVFREE_GET_MACRO(_1, _2, NAME, ...) NAME
#define kvfree_rcu_arg_2(ptr, rhf)					\
do {									\
	typeof (ptr) ___p = (ptr);					\
									\
	if (___p) {									\
		BUILD_BUG_ON(!__is_kvfree_rcu_offset(offsetof(typeof(*(ptr)), rhf)));	\
		kvfree_call_rcu(&((___p)->rhf), (rcu_callback_t)(unsigned long)		\
			(offsetof(typeof(*(ptr)), rhf)));				\
	}										\
} while (0)

#define kvfree_rcu_arg_1(ptr)					\
do {								\
	typeof(ptr) ___p = (ptr);				\
								\
	if (___p)						\
		kvfree_call_rcu(NULL, (rcu_callback_t) (___p));	\
} while (0)

/*
 * Place this after a lock-acquisition primitive to guarantee that
 * an UNLOCK+LOCK pair acts as a full barrier.  This guarantee applies
 * if the UNLOCK and LOCK are executed by the same CPU or if the
 * UNLOCK and LOCK operate on the same lock variable.
 */
#ifdef CONFIG_ARCH_WEAK_RELEASE_ACQUIRE
#define smp_mb__after_unlock_lock()	smp_mb()  /* Full ordering for lock. */
#else /* #ifdef CONFIG_ARCH_WEAK_RELEASE_ACQUIRE */
#define smp_mb__after_unlock_lock()	do { } while (0)
#endif /* #else #ifdef CONFIG_ARCH_WEAK_RELEASE_ACQUIRE */


/* Has the specified rcu_head structure been handed to call_rcu()? */

/**
 * rcu_head_init - Initialize rcu_head for rcu_head_after_call_rcu()
 * @rhp: The rcu_head structure to initialize.
 *
 * If you intend to invoke rcu_head_after_call_rcu() to test whether a
 * given rcu_head structure has already been passed to call_rcu(), then
 * you must also invoke this rcu_head_init() function on it just after
 * allocating that structure.  Calls to this function must not race with
 * calls to call_rcu(), rcu_head_after_call_rcu(), or callback invocation.
 */
static inline void rcu_head_init(struct rcu_head *rhp)
{
	rhp->func = (rcu_callback_t)~0L;
}

/**
 * rcu_head_after_call_rcu() - Has this rcu_head been passed to call_rcu()?
 * @rhp: The rcu_head structure to test.
 * @f: The function passed to call_rcu() along with @rhp.
 *
 * Returns @true if the @rhp has been passed to call_rcu() with @func,
 * and @false otherwise.  Emits a warning in any other case, including
 * the case where @rhp has already been invoked after a grace period.
 * Calls to this function must not race with callback invocation.  One way
 * to avoid such races is to enclose the call to rcu_head_after_call_rcu()
 * in an RCU read-side critical section that includes a read-side fetch
 * of the pointer to the structure containing @rhp.
 */
static inline bool
rcu_head_after_call_rcu(struct rcu_head *rhp, rcu_callback_t f)
{
	rcu_callback_t func = READ_ONCE(rhp->func);

	if (func == f)
		return true;
	WARN_ON_ONCE(func != (rcu_callback_t)~0L);
	return false;
}

/* kernel/ksysfs.c definitions */
extern int rcu_expedited;
extern int rcu_normal;

#endif /* __LINUX_RCUPDATE_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_WAIT_BIT_H
#define _LINUX_WAIT_BIT_H

/*
 * Linux wait-bit related types and methods:
 */
#include <linux/wait.h>

struct wait_bit_key {
	unsigned long		*flags;
	int			bit_nr;
	unsigned long		timeout;
};

struct wait_bit_queue_entry {
	struct wait_bit_key	key;
	struct wait_queue_entry	wq_entry;
};

#define __WAIT_BIT_KEY_INITIALIZER(word, bit)					\
	{ .flags = word, .bit_nr = bit, }

typedef int wait_bit_action_f(struct wait_bit_key *key, int mode);

void __wake_up_bit(struct wait_queue_head *wq_head, unsigned long *word, int bit);
int __wait_on_bit(struct wait_queue_head *wq_head, struct wait_bit_queue_entry *wbq_entry, wait_bit_action_f *action, unsigned int mode);
int __wait_on_bit_lock(struct wait_queue_head *wq_head, struct wait_bit_queue_entry *wbq_entry, wait_bit_action_f *action, unsigned int mode);
void wake_up_bit(unsigned long *word, int bit);
int out_of_line_wait_on_bit(unsigned long *word, int, wait_bit_action_f *action, unsigned int mode);
int out_of_line_wait_on_bit_timeout(unsigned long *word, int, wait_bit_action_f *action, unsigned int mode, unsigned long timeout);
int out_of_line_wait_on_bit_lock(unsigned long *word, int, wait_bit_action_f *action, unsigned int mode);
struct wait_queue_head *bit_waitqueue(unsigned long *word, int bit);
extern void __init wait_bit_init(void);

int wake_bit_function(struct wait_queue_entry *wq_entry, unsigned mode, int sync, void *key);

#define DEFINE_WAIT_BIT(name, word, bit)					\
	struct wait_bit_queue_entry name = {					\
		.key = __WAIT_BIT_KEY_INITIALIZER(word, bit),			\
		.wq_entry = {							\
			.private	= current,				\
			.func		= wake_bit_function,			\
			.entry		=					\
				LIST_HEAD_INIT((name).wq_entry.entry),		\
		},								\
	}

extern int bit_wait(struct wait_bit_key *key, int mode);
extern int bit_wait_io(struct wait_bit_key *key, int mode);
extern int bit_wait_timeout(struct wait_bit_key *key, int mode);

/**
 * wait_on_bit - wait for a bit to be cleared
 * @word: the address containing the bit being waited on
 * @bit: the bit at that address being waited on
 * @mode: the task state to sleep in
 *
 * Wait for the given bit in an unsigned long or bitmap (see DECLARE_BITMAP())
 * to be cleared.  The clearing of the bit must be signalled with
 * wake_up_bit(), often as clear_and_wake_up_bit().
 *
 * The process will wait on a waitqueue selected by hash from a shared
 * pool.  It will only be woken on a wake_up for the target bit, even
 * if other processes on the same queue are waiting for other bits.
 *
 * Returned value will be zero if the bit was cleared in which case the
 * call has ACQUIRE semantics, or %-EINTR if the process received a
 * signal and the mode permitted wake up on that signal.
 */
static inline int
wait_on_bit(unsigned long *word, int bit, unsigned mode)
{
	might_sleep();
	if (!test_bit_acquire(bit, word))
		return 0;
	return out_of_line_wait_on_bit(word, bit,
				       bit_wait,
				       mode);
}

/**
 * wait_on_bit_io - wait for a bit to be cleared
 * @word: the address containing the bit being waited on
 * @bit: the bit at that address being waited on
 * @mode: the task state to sleep in
 *
 * Wait for the given bit in an unsigned long or bitmap (see DECLARE_BITMAP())
 * to be cleared.  The clearing of the bit must be signalled with
 * wake_up_bit(), often as clear_and_wake_up_bit().
 *
 * This is similar to wait_on_bit(), but calls io_schedule() instead of
 * schedule() for the actual waiting.
 *
 * Returned value will be zero if the bit was cleared in which case the
 * call has ACQUIRE semantics, or %-EINTR if the process received a
 * signal and the mode permitted wake up on that signal.
 */
static inline int
wait_on_bit_io(unsigned long *word, int bit, unsigned mode)
{
	might_sleep();
	if (!test_bit_acquire(bit, word))
		return 0;
	return out_of_line_wait_on_bit(word, bit,
				       bit_wait_io,
				       mode);
}

/**
 * wait_on_bit_timeout - wait for a bit to be cleared or a timeout to elapse
 * @word: the address containing the bit being waited on
 * @bit: the bit at that address being waited on
 * @mode: the task state to sleep in
 * @timeout: timeout, in jiffies
 *
 * Wait for the given bit in an unsigned long or bitmap (see
 * DECLARE_BITMAP()) to be cleared, or for a timeout to expire.  The
 * clearing of the bit must be signalled with wake_up_bit(), often as
 * clear_and_wake_up_bit().
 *
 * This is similar to wait_on_bit(), except it also takes a timeout
 * parameter.
 *
 * Returned value will be zero if the bit was cleared in which case the
 * call has ACQUIRE semantics, or %-EINTR if the process received a
 * signal and the mode permitted wake up on that signal, or %-EAGAIN if the
 * timeout elapsed.
 */
static inline int
wait_on_bit_timeout(unsigned long *word, int bit, unsigned mode,
		    unsigned long timeout)
{
	might_sleep();
	if (!test_bit_acquire(bit, word))
		return 0;
	return out_of_line_wait_on_bit_timeout(word, bit,
					       bit_wait_timeout,
					       mode, timeout);
}

/**
 * wait_on_bit_action - wait for a bit to be cleared
 * @word: the address containing the bit waited on
 * @bit: the bit at that address being waited on
 * @action: the function used to sleep, which may take special actions
 * @mode: the task state to sleep in
 *
 * Wait for the given bit in an unsigned long or bitmap (see DECLARE_BITMAP())
 * to be cleared.  The clearing of the bit must be signalled with
 * wake_up_bit(), often as clear_and_wake_up_bit().
 *
 * This is similar to wait_on_bit(), but calls @action() instead of
 * schedule() for the actual waiting.
 *
 * Returned value will be zero if the bit was cleared in which case the
 * call has ACQUIRE semantics, or the error code returned by @action if
 * that call returned non-zero.
 */
static inline int
wait_on_bit_action(unsigned long *word, int bit, wait_bit_action_f *action,
		   unsigned mode)
{
	might_sleep();
	if (!test_bit_acquire(bit, word))
		return 0;
	return out_of_line_wait_on_bit(word, bit, action, mode);
}

/**
 * wait_on_bit_lock - wait for a bit to be cleared, then set it
 * @word: the address containing the bit being waited on
 * @bit: the bit of the word being waited on and set
 * @mode: the task state to sleep in
 *
 * Wait for the given bit in an unsigned long or bitmap (see
 * DECLARE_BITMAP()) to be cleared.  The clearing of the bit must be
 * signalled with wake_up_bit(), often as clear_and_wake_up_bit().  As
 * soon as it is clear, atomically set it and return.
 *
 * This is similar to wait_on_bit(), but sets the bit before returning.
 *
 * Returned value will be zero if the bit was successfully set in which
 * case the call has the same memory sequencing semantics as
 * test_and_clear_bit(), or %-EINTR if the process received a signal and
 * the mode permitted wake up on that signal.
 */
static inline int
wait_on_bit_lock(unsigned long *word, int bit, unsigned mode)
{
	might_sleep();
	if (!test_and_set_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit_lock(word, bit, bit_wait, mode);
}

/**
 * wait_on_bit_lock_io - wait for a bit to be cleared, then set it
 * @word: the address containing the bit being waited on
 * @bit: the bit of the word being waited on and set
 * @mode: the task state to sleep in
 *
 * Wait for the given bit in an unsigned long or bitmap (see
 * DECLARE_BITMAP()) to be cleared.  The clearing of the bit must be
 * signalled with wake_up_bit(), often as clear_and_wake_up_bit().  As
 * soon as it is clear, atomically set it and return.
 *
 * This is similar to wait_on_bit_lock(), but calls io_schedule() instead
 * of schedule().
 *
 * Returns zero if the bit was (eventually) found to be clear and was
 * set.  Returns non-zero if a signal was delivered to the process and
 * the @mode allows that signal to wake the process.
 */
static inline int
wait_on_bit_lock_io(unsigned long *word, int bit, unsigned mode)
{
	might_sleep();
	if (!test_and_set_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit_lock(word, bit, bit_wait_io, mode);
}

/**
 * wait_on_bit_lock_action - wait for a bit to be cleared, then set it
 * @word: the address containing the bit being waited on
 * @bit: the bit of the word being waited on and set
 * @action: the function used to sleep, which may take special actions
 * @mode: the task state to sleep in
 *
 * This is similar to wait_on_bit_lock(), but calls @action() instead of
 * schedule() for the actual waiting.
 *
 * Returned value will be zero if the bit was successfully set in which
 * case the call has the same memory sequencing semantics as
 * test_and_clear_bit(), or the error code returned by @action if that
 * call returned non-zero.
 */
static inline int
wait_on_bit_lock_action(unsigned long *word, int bit, wait_bit_action_f *action,
			unsigned mode)
{
	might_sleep();
	if (!test_and_set_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit_lock(word, bit, action, mode);
}

extern void init_wait_var_entry(struct wait_bit_queue_entry *wbq_entry, void *var, int flags);
extern void wake_up_var(void *var);
extern wait_queue_head_t *__var_waitqueue(void *p);

#define ___wait_var_event(var, condition, state, exclusive, ret, cmd)	\
({									\
	__label__ __out;						\
	struct wait_queue_head *__wq_head = __var_waitqueue(var);	\
	struct wait_bit_queue_entry __wbq_entry;			\
	long __ret = ret; /* explicit shadow */				\
									\
	init_wait_var_entry(&__wbq_entry, var,				\
			    exclusive ? WQ_FLAG_EXCLUSIVE : 0);		\
	for (;;) {							\
		long __int = prepare_to_wait_event(__wq_head,		\
						   &__wbq_entry.wq_entry, \
						   state);		\
		if (condition)						\
			break;						\
									\
		if (___wait_is_interruptible(state) && __int) {		\
			__ret = __int;					\
			goto __out;					\
		}							\
									\
		cmd;							\
	}								\
	finish_wait(__wq_head, &__wbq_entry.wq_entry);			\
__out:	__ret;								\
})

#define __wait_var_event(var, condition)				\
	___wait_var_event(var, condition, TASK_UNINTERRUPTIBLE, 0, 0,	\
			  schedule())
#define __wait_var_event_io(var, condition)				\
	___wait_var_event(var, condition, TASK_UNINTERRUPTIBLE, 0, 0,	\
			  io_schedule())

/**
 * wait_var_event - wait for a variable to be updated and notified
 * @var: the address of variable being waited on
 * @condition: the condition to wait for
 *
 * Wait for a @condition to be true, only re-checking when a wake up is
 * received for the given @var (an arbitrary kernel address which need
 * not be directly related to the given condition, but usually is).
 *
 * The process will wait on a waitqueue selected by hash from a shared
 * pool.  It will only be woken on a wake_up for the given address.
 *
 * The condition should normally use smp_load_acquire() or a similarly
 * ordered access to ensure that any changes to memory made before the
 * condition became true will be visible after the wait completes.
 */
#define wait_var_event(var, condition)					\
do {									\
	might_sleep();							\
	if (condition)							\
		break;							\
	__wait_var_event(var, condition);				\
} while (0)

/**
 * wait_var_event_io - wait for a variable to be updated and notified
 * @var: the address of variable being waited on
 * @condition: the condition to wait for
 *
 * Wait for an IO related @condition to be true, only re-checking when a
 * wake up is received for the given @var (an arbitrary kernel address
 * which need not be directly related to the given condition, but
 * usually is).
 *
 * The process will wait on a waitqueue selected by hash from a shared
 * pool.  It will only be woken on a wake_up for the given address.
 *
 * This is similar to wait_var_event(), but calls io_schedule() instead
 * of schedule().
 *
 * The condition should normally use smp_load_acquire() or a similarly
 * ordered access to ensure that any changes to memory made before the
 * condition became true will be visible after the wait completes.
 */
#define wait_var_event_io(var, condition)				\
do {									\
	might_sleep();							\
	if (condition)							\
		break;							\
	__wait_var_event_io(var, condition);				\
} while (0)

#define __wait_var_event_killable(var, condition)			\
	___wait_var_event(var, condition, TASK_KILLABLE, 0, 0,		\
			  schedule())

/**
 * wait_var_event_killable - wait for a variable to be updated and notified
 * @var: the address of variable being waited on
 * @condition: the condition to wait for
 *
 * Wait for a @condition to be true or a fatal signal to be received,
 * only re-checking the condition when a wake up is received for the given
 * @var (an arbitrary kernel address which need not be directly related
 * to the given condition, but usually is).
 *
 * This is similar to wait_var_event() but returns a value which is
 * 0 if the condition became true, or %-ERESTARTSYS if a fatal signal
 * was received.
 *
 * The condition should normally use smp_load_acquire() or a similarly
 * ordered access to ensure that any changes to memory made before the
 * condition became true will be visible after the wait completes.
 */
#define wait_var_event_killable(var, condition)				\
({									\
	int __ret = 0;							\
	might_sleep();							\
	if (!(condition))						\
		__ret = __wait_var_event_killable(var, condition);	\
	__ret;								\
})

#define __wait_var_event_timeout(var, condition, timeout)		\
	___wait_var_event(var, ___wait_cond_timeout(condition),		\
			  TASK_UNINTERRUPTIBLE, 0, timeout,		\
			  __ret = schedule_timeout(__ret))

/**
 * wait_var_event_timeout - wait for a variable to be updated or a timeout to expire
 * @var: the address of variable being waited on
 * @condition: the condition to wait for
 * @timeout: maximum time to wait in jiffies
 *
 * Wait for a @condition to be true or a timeout to expire, only
 * re-checking the condition when a wake up is received for the given
 * @var (an arbitrary kernel address which need not be directly related
 * to the given condition, but usually is).
 *
 * This is similar to wait_var_event() but returns a value which is 0 if
 * the timeout expired and the condition was still false, or the
 * remaining time left in the timeout (but at least 1) if the condition
 * was found to be true.
 *
 * The condition should normally use smp_load_acquire() or a similarly
 * ordered access to ensure that any changes to memory made before the
 * condition became true will be visible after the wait completes.
 */
#define wait_var_event_timeout(var, condition, timeout)			\
({									\
	long __ret = timeout;						\
	might_sleep();							\
	if (!___wait_cond_timeout(condition))				\
		__ret = __wait_var_event_timeout(var, condition, timeout); \
	__ret;								\
})

#define __wait_var_event_interruptible(var, condition)			\
	___wait_var_event(var, condition, TASK_INTERRUPTIBLE, 0, 0,	\
			  schedule())

/**
 * wait_var_event_killable - wait for a variable to be updated and notified
 * @var: the address of variable being waited on
 * @condition: the condition to wait for
 *
 * Wait for a @condition to be true or a signal to be received, only
 * re-checking the condition when a wake up is received for the given
 * @var (an arbitrary kernel address which need not be directly related
 * to the given condition, but usually is).
 *
 * This is similar to wait_var_event() but returns a value which is 0 if
 * the condition became true, or %-ERESTARTSYS if a signal was received.
 *
 * The condition should normally use smp_load_acquire() or a similarly
 * ordered access to ensure that any changes to memory made before the
 * condition became true will be visible after the wait completes.
 */
#define wait_var_event_interruptible(var, condition)			\
({									\
	int __ret = 0;							\
	might_sleep();							\
	if (!(condition))						\
		__ret = __wait_var_event_interruptible(var, condition);	\
	__ret;								\
})

/**
 * wait_var_event_any_lock - wait for a variable to be updated under a lock
 * @var: the address of the variable being waited on
 * @condition: condition to wait for
 * @lock: the object that is locked to protect updates to the variable
 * @type: prefix on lock and unlock operations
 * @state: waiting state, %TASK_UNINTERRUPTIBLE etc.
 *
 * Wait for a condition which can only be reliably tested while holding
 * a lock.  The variables assessed in the condition will normal be updated
 * under the same lock, and the wake up should be signalled with
 * wake_up_var_locked() under the same lock.
 *
 * This is similar to wait_var_event(), but assumes a lock is held
 * while calling this function and while updating the variable.
 *
 * This must be called while the given lock is held and the lock will be
 * dropped when schedule() is called to wait for a wake up, and will be
 * reclaimed before testing the condition again.  The functions used to
 * unlock and lock the object are constructed by appending _unlock and _lock
 * to @type.
 *
 * Return %-ERESTARTSYS if a signal arrives which is allowed to interrupt
 * the wait according to @state.
 */
#define wait_var_event_any_lock(var, condition, lock, type, state)	\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__ret = ___wait_var_event(var, condition, state, 0, 0,	\
					  type ## _unlock(lock);	\
					  schedule();			\
					  type ## _lock(lock));		\
	__ret;								\
})

/**
 * wait_var_event_spinlock - wait for a variable to be updated under a spinlock
 * @var: the address of the variable being waited on
 * @condition: condition to wait for
 * @lock: the spinlock which protects updates to the variable
 *
 * Wait for a condition which can only be reliably tested while holding
 * a spinlock.  The variables assessed in the condition will normal be updated
 * under the same spinlock, and the wake up should be signalled with
 * wake_up_var_locked() under the same spinlock.
 *
 * This is similar to wait_var_event(), but assumes a spinlock is held
 * while calling this function and while updating the variable.
 *
 * This must be called while the given lock is held and the lock will be
 * dropped when schedule() is called to wait for a wake up, and will be
 * reclaimed before testing the condition again.
 */
#define wait_var_event_spinlock(var, condition, lock)			\
	wait_var_event_any_lock(var, condition, lock, spin, TASK_UNINTERRUPTIBLE)

/**
 * wait_var_event_mutex - wait for a variable to be updated under a mutex
 * @var: the address of the variable being waited on
 * @condition: condition to wait for
 * @mutex: the mutex which protects updates to the variable
 *
 * Wait for a condition which can only be reliably tested while holding
 * a mutex.  The variables assessed in the condition will normal be
 * updated under the same mutex, and the wake up should be signalled
 * with wake_up_var_locked() under the same mutex.
 *
 * This is similar to wait_var_event(), but assumes a mutex is held
 * while calling this function and while updating the variable.
 *
 * This must be called while the given mutex is held and the mutex will be
 * dropped when schedule() is called to wait for a wake up, and will be
 * reclaimed before testing the condition again.
 */
#define wait_var_event_mutex(var, condition, lock)			\
	wait_var_event_any_lock(var, condition, lock, mutex, TASK_UNINTERRUPTIBLE)

/**
 * wake_up_var_protected - wake up waiters for a variable asserting that it is safe
 * @var: the address of the variable being waited on
 * @cond: the condition which afirms this is safe
 *
 * When waking waiters which use wait_var_event_any_lock() the waker must be
 * holding the reelvant lock to avoid races.  This version of wake_up_var()
 * asserts that the relevant lock is held and so no barrier is needed.
 * The @cond is only tested when CONFIG_LOCKDEP is enabled.
 */
#define wake_up_var_protected(var, cond)				\
do {									\
	lockdep_assert(cond);						\
	wake_up_var(var);						\
} while (0)

/**
 * wake_up_var_locked - wake up waiters for a variable while holding a spinlock or mutex
 * @var: the address of the variable being waited on
 * @lock: The spinlock or mutex what protects the variable
 *
 * Send a wake up for the given variable which should be waited for with
 * wait_var_event_spinlock() or wait_var_event_mutex().  Unlike wake_up_var(),
 * no extra barriers are needed as the locking provides sufficient sequencing.
 */
#define wake_up_var_locked(var, lock)					\
	wake_up_var_protected(var, lockdep_is_held(lock))

/**
 * clear_and_wake_up_bit - clear a bit and wake up anyone waiting on that bit
 * @bit: the bit of the word being waited on
 * @word: the address containing the bit being waited on
 *
 * The designated bit is cleared and any tasks waiting in wait_on_bit()
 * or similar will be woken.  This call has RELEASE semantics so that
 * any changes to memory made before this call are guaranteed to be visible
 * after the corresponding wait_on_bit() completes.
 */
static inline void clear_and_wake_up_bit(int bit, unsigned long *word)
{
	clear_bit_unlock(bit, word);
	/* See wake_up_bit() for which memory barrier you need to use. */
	smp_mb__after_atomic();
	wake_up_bit(word, bit);
}

/**
 * test_and_clear_wake_up_bit - clear a bit if it was set: wake up anyone waiting on that bit
 * @bit: the bit of the word being waited on
 * @word: the address of memory containing that bit
 *
 * If the bit is set and can be atomically cleared, any tasks waiting in
 * wait_on_bit() or similar will be woken.  This call has the same
 * complete ordering semantics as test_and_clear_bit().  Any changes to
 * memory made before this call are guaranteed to be visible after the
 * corresponding wait_on_bit() completes.
 *
 * Returns %true if the bit was successfully set and the wake up was sent.
 */
static inline bool test_and_clear_wake_up_bit(int bit, unsigned long *word)
{
	if (!test_and_clear_bit(bit, word))
		return false;
	/* no extra barrier required */
	wake_up_bit(word, bit);
	return true;
}

/**
 * atomic_dec_and_wake_up - decrement an atomic_t and if zero, wake up waiters
 * @var: the variable to dec and test
 *
 * Decrements the atomic variable and if it reaches zero, send a wake_up to any
 * processes waiting on the variable.
 *
 * This function has the same complete ordering semantics as atomic_dec_and_test.
 *
 * Returns %true is the variable reaches zero and the wake up was sent.
 */

static inline bool atomic_dec_and_wake_up(atomic_t *var)
{
	if (!atomic_dec_and_test(var))
		return false;
	/* No extra barrier required */
	wake_up_var(var);
	return true;
}

/**
 * store_release_wake_up - update a variable and send a wake_up
 * @var: the address of the variable to be updated and woken
 * @val: the value to store in the variable.
 *
 * Store the given value in the variable send a wake up to any tasks
 * waiting on the variable.  All necessary barriers are included to ensure
 * the task calling wait_var_event() sees the new value and all values
 * written to memory before this call.
 */
#define store_release_wake_up(var, val)					\
do {									\
	smp_store_release(var, val);					\
	smp_mb();							\
	wake_up_var(var);						\
} while (0)

#endif /* _LINUX_WAIT_BIT_H */

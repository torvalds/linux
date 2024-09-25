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
extern int bit_wait_io_timeout(struct wait_bit_key *key, int mode);

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

#define wait_var_event(var, condition)					\
do {									\
	might_sleep();							\
	if (condition)							\
		break;							\
	__wait_var_event(var, condition);				\
} while (0)

#define __wait_var_event_killable(var, condition)			\
	___wait_var_event(var, condition, TASK_KILLABLE, 0, 0,		\
			  schedule())

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

#define wait_var_event_interruptible(var, condition)			\
({									\
	int __ret = 0;							\
	might_sleep();							\
	if (!(condition))						\
		__ret = __wait_var_event_interruptible(var, condition);	\
	__ret;								\
})

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

#endif /* _LINUX_WAIT_BIT_H */

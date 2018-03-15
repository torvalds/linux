/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_WAIT_BIT_H
#define _LINUX_WAIT_BIT_H

/*
 * Linux wait-bit related types and methods:
 */
#include <linux/wait.h>

struct wait_bit_key {
	void			*flags;
	int			bit_nr;
#define WAIT_ATOMIC_T_BIT_NR	-1
	unsigned long		timeout;
};

struct wait_bit_queue_entry {
	struct wait_bit_key	key;
	struct wait_queue_entry	wq_entry;
};

#define __WAIT_BIT_KEY_INITIALIZER(word, bit)					\
	{ .flags = word, .bit_nr = bit, }

#define __WAIT_ATOMIC_T_KEY_INITIALIZER(p)					\
	{ .flags = p, .bit_nr = WAIT_ATOMIC_T_BIT_NR, }

typedef int wait_bit_action_f(struct wait_bit_key *key, int mode);
typedef int wait_atomic_t_action_f(atomic_t *counter, unsigned int mode);

void __wake_up_bit(struct wait_queue_head *wq_head, void *word, int bit);
int __wait_on_bit(struct wait_queue_head *wq_head, struct wait_bit_queue_entry *wbq_entry, wait_bit_action_f *action, unsigned int mode);
int __wait_on_bit_lock(struct wait_queue_head *wq_head, struct wait_bit_queue_entry *wbq_entry, wait_bit_action_f *action, unsigned int mode);
void wake_up_bit(void *word, int bit);
void wake_up_atomic_t(atomic_t *p);
int out_of_line_wait_on_bit(void *word, int, wait_bit_action_f *action, unsigned int mode);
int out_of_line_wait_on_bit_timeout(void *word, int, wait_bit_action_f *action, unsigned int mode, unsigned long timeout);
int out_of_line_wait_on_bit_lock(void *word, int, wait_bit_action_f *action, unsigned int mode);
int out_of_line_wait_on_atomic_t(atomic_t *p, wait_atomic_t_action_f action, unsigned int mode);
struct wait_queue_head *bit_waitqueue(void *word, int bit);
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
extern int atomic_t_wait(atomic_t *counter, unsigned int mode);

/**
 * wait_on_bit - wait for a bit to be cleared
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 * @mode: the task state to sleep in
 *
 * There is a standard hashed waitqueue table for generic use. This
 * is the part of the hashtable's accessor API that waits on a bit.
 * For instance, if one were to have waiters on a bitflag, one would
 * call wait_on_bit() in threads waiting for the bit to clear.
 * One uses wait_on_bit() where one is waiting for the bit to clear,
 * but has no intention of setting it.
 * Returned value will be zero if the bit was cleared, or non-zero
 * if the process received a signal and the mode permitted wakeup
 * on that signal.
 */
static inline int
wait_on_bit(unsigned long *word, int bit, unsigned mode)
{
	might_sleep();
	if (!test_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit(word, bit,
				       bit_wait,
				       mode);
}

/**
 * wait_on_bit_io - wait for a bit to be cleared
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 * @mode: the task state to sleep in
 *
 * Use the standard hashed waitqueue table to wait for a bit
 * to be cleared.  This is similar to wait_on_bit(), but calls
 * io_schedule() instead of schedule() for the actual waiting.
 *
 * Returned value will be zero if the bit was cleared, or non-zero
 * if the process received a signal and the mode permitted wakeup
 * on that signal.
 */
static inline int
wait_on_bit_io(unsigned long *word, int bit, unsigned mode)
{
	might_sleep();
	if (!test_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit(word, bit,
				       bit_wait_io,
				       mode);
}

/**
 * wait_on_bit_timeout - wait for a bit to be cleared or a timeout elapses
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 * @mode: the task state to sleep in
 * @timeout: timeout, in jiffies
 *
 * Use the standard hashed waitqueue table to wait for a bit
 * to be cleared. This is similar to wait_on_bit(), except also takes a
 * timeout parameter.
 *
 * Returned value will be zero if the bit was cleared before the
 * @timeout elapsed, or non-zero if the @timeout elapsed or process
 * received a signal and the mode permitted wakeup on that signal.
 */
static inline int
wait_on_bit_timeout(unsigned long *word, int bit, unsigned mode,
		    unsigned long timeout)
{
	might_sleep();
	if (!test_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit_timeout(word, bit,
					       bit_wait_timeout,
					       mode, timeout);
}

/**
 * wait_on_bit_action - wait for a bit to be cleared
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 * @action: the function used to sleep, which may take special actions
 * @mode: the task state to sleep in
 *
 * Use the standard hashed waitqueue table to wait for a bit
 * to be cleared, and allow the waiting action to be specified.
 * This is like wait_on_bit() but allows fine control of how the waiting
 * is done.
 *
 * Returned value will be zero if the bit was cleared, or non-zero
 * if the process received a signal and the mode permitted wakeup
 * on that signal.
 */
static inline int
wait_on_bit_action(unsigned long *word, int bit, wait_bit_action_f *action,
		   unsigned mode)
{
	might_sleep();
	if (!test_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit(word, bit, action, mode);
}

/**
 * wait_on_bit_lock - wait for a bit to be cleared, when wanting to set it
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 * @mode: the task state to sleep in
 *
 * There is a standard hashed waitqueue table for generic use. This
 * is the part of the hashtable's accessor API that waits on a bit
 * when one intends to set it, for instance, trying to lock bitflags.
 * For instance, if one were to have waiters trying to set bitflag
 * and waiting for it to clear before setting it, one would call
 * wait_on_bit() in threads waiting to be able to set the bit.
 * One uses wait_on_bit_lock() where one is waiting for the bit to
 * clear with the intention of setting it, and when done, clearing it.
 *
 * Returns zero if the bit was (eventually) found to be clear and was
 * set.  Returns non-zero if a signal was delivered to the process and
 * the @mode allows that signal to wake the process.
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
 * wait_on_bit_lock_io - wait for a bit to be cleared, when wanting to set it
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 * @mode: the task state to sleep in
 *
 * Use the standard hashed waitqueue table to wait for a bit
 * to be cleared and then to atomically set it.  This is similar
 * to wait_on_bit(), but calls io_schedule() instead of schedule()
 * for the actual waiting.
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
 * wait_on_bit_lock_action - wait for a bit to be cleared, when wanting to set it
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 * @action: the function used to sleep, which may take special actions
 * @mode: the task state to sleep in
 *
 * Use the standard hashed waitqueue table to wait for a bit
 * to be cleared and then to set it, and allow the waiting action
 * to be specified.
 * This is like wait_on_bit() but allows fine control of how the waiting
 * is done.
 *
 * Returns zero if the bit was (eventually) found to be clear and was
 * set.  Returns non-zero if a signal was delivered to the process and
 * the @mode allows that signal to wake the process.
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

/**
 * wait_on_atomic_t - Wait for an atomic_t to become 0
 * @val: The atomic value being waited on, a kernel virtual address
 * @action: the function used to sleep, which may take special actions
 * @mode: the task state to sleep in
 *
 * Wait for an atomic_t to become 0.  We abuse the bit-wait waitqueue table for
 * the purpose of getting a waitqueue, but we set the key to a bit number
 * outside of the target 'word'.
 */
static inline
int wait_on_atomic_t(atomic_t *val, wait_atomic_t_action_f action, unsigned mode)
{
	might_sleep();
	if (atomic_read(val) == 0)
		return 0;
	return out_of_line_wait_on_atomic_t(val, action, mode);
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

#endif /* _LINUX_WAIT_BIT_H */

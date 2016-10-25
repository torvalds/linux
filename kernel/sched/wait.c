/*
 * Generic waiting primitives.
 *
 * (C) 2004 Nadia Yvette Chambers, Oracle
 */
#include <linux/init.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/hash.h>
#include <linux/kthread.h>

void __init_waitqueue_head(wait_queue_head_t *q, const char *name, struct lock_class_key *key)
{
	spin_lock_init(&q->lock);
	lockdep_set_class_and_name(&q->lock, key, name);
	INIT_LIST_HEAD(&q->task_list);
}

EXPORT_SYMBOL(__init_waitqueue_head);

void add_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	wait->flags &= ~WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(add_wait_queue);

void add_wait_queue_exclusive(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	wait->flags |= WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue_tail(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(add_wait_queue_exclusive);

void remove_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	__remove_wait_queue(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(remove_wait_queue);


/*
 * The core wakeup function. Non-exclusive wakeups (nr_exclusive == 0) just
 * wake everything up. If it's an exclusive wakeup (nr_exclusive == small +ve
 * number) then we wake all the non-exclusive tasks and one exclusive task.
 *
 * There are circumstances in which we can try to wake a task which has already
 * started to run but is not in state TASK_RUNNING. try_to_wake_up() returns
 * zero in this (rare) case, and we handle it by continuing to scan the queue.
 */
static void __wake_up_common(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, int wake_flags, void *key)
{
	wait_queue_t *curr, *next;

	list_for_each_entry_safe(curr, next, &q->task_list, task_list) {
		unsigned flags = curr->flags;

		if (curr->func(curr, mode, wake_flags, key) &&
				(flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
			break;
	}
}

/**
 * __wake_up - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 * @key: is directly passed to the wakeup function
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
void __wake_up(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, 0, key);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(__wake_up);

/*
 * Same as __wake_up but called with the spinlock in wait_queue_head_t held.
 */
void __wake_up_locked(wait_queue_head_t *q, unsigned int mode, int nr)
{
	__wake_up_common(q, mode, nr, 0, NULL);
}
EXPORT_SYMBOL_GPL(__wake_up_locked);

void __wake_up_locked_key(wait_queue_head_t *q, unsigned int mode, void *key)
{
	__wake_up_common(q, mode, 1, 0, key);
}
EXPORT_SYMBOL_GPL(__wake_up_locked_key);

/**
 * __wake_up_sync_key - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 * @key: opaque value to be passed to wakeup targets
 *
 * The sync wakeup differs that the waker knows that it will schedule
 * away soon, so while the target thread will be woken up, it will not
 * be migrated to another CPU - ie. the two threads are 'synchronized'
 * with each other. This can prevent needless bouncing between CPUs.
 *
 * On UP it can prevent extra preemption.
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
void __wake_up_sync_key(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
	unsigned long flags;
	int wake_flags = 1; /* XXX WF_SYNC */

	if (unlikely(!q))
		return;

	if (unlikely(nr_exclusive != 1))
		wake_flags = 0;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, wake_flags, key);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL_GPL(__wake_up_sync_key);

/*
 * __wake_up_sync - see __wake_up_sync_key()
 */
void __wake_up_sync(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
{
	__wake_up_sync_key(q, mode, nr_exclusive, NULL);
}
EXPORT_SYMBOL_GPL(__wake_up_sync);	/* For internal use only */

/*
 * Note: we use "set_current_state()" _after_ the wait-queue add,
 * because we need a memory barrier there on SMP, so that any
 * wake-function that tests for the wait-queue being active
 * will be guaranteed to see waitqueue addition _or_ subsequent
 * tests in this thread will see the wakeup having taken place.
 *
 * The spin_unlock() itself is semi-permeable and only protects
 * one way (it only protects stuff inside the critical region and
 * stops them from bleeding out - it would still allow subsequent
 * loads to move into the critical region).
 */
void
prepare_to_wait(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	unsigned long flags;

	wait->flags &= ~WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	if (list_empty(&wait->task_list))
		__add_wait_queue(q, wait);
	set_current_state(state);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(prepare_to_wait);

void
prepare_to_wait_exclusive(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	unsigned long flags;

	wait->flags |= WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	if (list_empty(&wait->task_list))
		__add_wait_queue_tail(q, wait);
	set_current_state(state);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(prepare_to_wait_exclusive);

void init_wait_entry(wait_queue_t *wait, int flags)
{
	wait->flags = flags;
	wait->private = current;
	wait->func = autoremove_wake_function;
	INIT_LIST_HEAD(&wait->task_list);
}
EXPORT_SYMBOL(init_wait_entry);

long prepare_to_wait_event(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	unsigned long flags;
	long ret = 0;

	spin_lock_irqsave(&q->lock, flags);
	if (unlikely(signal_pending_state(state, current))) {
		/*
		 * Exclusive waiter must not fail if it was selected by wakeup,
		 * it should "consume" the condition we were waiting for.
		 *
		 * The caller will recheck the condition and return success if
		 * we were already woken up, we can not miss the event because
		 * wakeup locks/unlocks the same q->lock.
		 *
		 * But we need to ensure that set-condition + wakeup after that
		 * can't see us, it should wake up another exclusive waiter if
		 * we fail.
		 */
		list_del_init(&wait->task_list);
		ret = -ERESTARTSYS;
	} else {
		if (list_empty(&wait->task_list)) {
			if (wait->flags & WQ_FLAG_EXCLUSIVE)
				__add_wait_queue_tail(q, wait);
			else
				__add_wait_queue(q, wait);
		}
		set_current_state(state);
	}
	spin_unlock_irqrestore(&q->lock, flags);

	return ret;
}
EXPORT_SYMBOL(prepare_to_wait_event);

/**
 * finish_wait - clean up after waiting in a queue
 * @q: waitqueue waited on
 * @wait: wait descriptor
 *
 * Sets current thread back to running state and removes
 * the wait descriptor from the given waitqueue if still
 * queued.
 */
void finish_wait(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	__set_current_state(TASK_RUNNING);
	/*
	 * We can check for list emptiness outside the lock
	 * IFF:
	 *  - we use the "careful" check that verifies both
	 *    the next and prev pointers, so that there cannot
	 *    be any half-pending updates in progress on other
	 *    CPU's that we haven't seen yet (and that might
	 *    still change the stack area.
	 * and
	 *  - all other users take the lock (ie we can only
	 *    have _one_ other CPU that looks at or modifies
	 *    the list).
	 */
	if (!list_empty_careful(&wait->task_list)) {
		spin_lock_irqsave(&q->lock, flags);
		list_del_init(&wait->task_list);
		spin_unlock_irqrestore(&q->lock, flags);
	}
}
EXPORT_SYMBOL(finish_wait);

int autoremove_wake_function(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	int ret = default_wake_function(wait, mode, sync, key);

	if (ret)
		list_del_init(&wait->task_list);
	return ret;
}
EXPORT_SYMBOL(autoremove_wake_function);

static inline bool is_kthread_should_stop(void)
{
	return (current->flags & PF_KTHREAD) && kthread_should_stop();
}

/*
 * DEFINE_WAIT_FUNC(wait, woken_wake_func);
 *
 * add_wait_queue(&wq, &wait);
 * for (;;) {
 *     if (condition)
 *         break;
 *
 *     p->state = mode;				condition = true;
 *     smp_mb(); // A				smp_wmb(); // C
 *     if (!wait->flags & WQ_FLAG_WOKEN)	wait->flags |= WQ_FLAG_WOKEN;
 *         schedule()				try_to_wake_up();
 *     p->state = TASK_RUNNING;		    ~~~~~~~~~~~~~~~~~~
 *     wait->flags &= ~WQ_FLAG_WOKEN;		condition = true;
 *     smp_mb() // B				smp_wmb(); // C
 *						wait->flags |= WQ_FLAG_WOKEN;
 * }
 * remove_wait_queue(&wq, &wait);
 *
 */
long wait_woken(wait_queue_t *wait, unsigned mode, long timeout)
{
	set_current_state(mode); /* A */
	/*
	 * The above implies an smp_mb(), which matches with the smp_wmb() from
	 * woken_wake_function() such that if we observe WQ_FLAG_WOKEN we must
	 * also observe all state before the wakeup.
	 */
	if (!(wait->flags & WQ_FLAG_WOKEN) && !is_kthread_should_stop())
		timeout = schedule_timeout(timeout);
	__set_current_state(TASK_RUNNING);

	/*
	 * The below implies an smp_mb(), it too pairs with the smp_wmb() from
	 * woken_wake_function() such that we must either observe the wait
	 * condition being true _OR_ WQ_FLAG_WOKEN such that we will not miss
	 * an event.
	 */
	smp_store_mb(wait->flags, wait->flags & ~WQ_FLAG_WOKEN); /* B */

	return timeout;
}
EXPORT_SYMBOL(wait_woken);

int woken_wake_function(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	/*
	 * Although this function is called under waitqueue lock, LOCK
	 * doesn't imply write barrier and the users expects write
	 * barrier semantics on wakeup functions.  The following
	 * smp_wmb() is equivalent to smp_wmb() in try_to_wake_up()
	 * and is paired with smp_store_mb() in wait_woken().
	 */
	smp_wmb(); /* C */
	wait->flags |= WQ_FLAG_WOKEN;

	return default_wake_function(wait, mode, sync, key);
}
EXPORT_SYMBOL(woken_wake_function);

int wake_bit_function(wait_queue_t *wait, unsigned mode, int sync, void *arg)
{
	struct wait_bit_key *key = arg;
	struct wait_bit_queue *wait_bit
		= container_of(wait, struct wait_bit_queue, wait);

	if (wait_bit->key.flags != key->flags ||
			wait_bit->key.bit_nr != key->bit_nr ||
			test_bit(key->bit_nr, key->flags))
		return 0;
	else
		return autoremove_wake_function(wait, mode, sync, key);
}
EXPORT_SYMBOL(wake_bit_function);

/*
 * To allow interruptible waiting and asynchronous (i.e. nonblocking)
 * waiting, the actions of __wait_on_bit() and __wait_on_bit_lock() are
 * permitted return codes. Nonzero return codes halt waiting and return.
 */
int __sched
__wait_on_bit(wait_queue_head_t *wq, struct wait_bit_queue *q,
	      wait_bit_action_f *action, unsigned mode)
{
	int ret = 0;

	do {
		prepare_to_wait(wq, &q->wait, mode);
		if (test_bit(q->key.bit_nr, q->key.flags))
			ret = (*action)(&q->key, mode);
	} while (test_bit(q->key.bit_nr, q->key.flags) && !ret);
	finish_wait(wq, &q->wait);
	return ret;
}
EXPORT_SYMBOL(__wait_on_bit);

int __sched out_of_line_wait_on_bit(void *word, int bit,
				    wait_bit_action_f *action, unsigned mode)
{
	wait_queue_head_t *wq = bit_waitqueue(word, bit);
	DEFINE_WAIT_BIT(wait, word, bit);

	return __wait_on_bit(wq, &wait, action, mode);
}
EXPORT_SYMBOL(out_of_line_wait_on_bit);

int __sched out_of_line_wait_on_bit_timeout(
	void *word, int bit, wait_bit_action_f *action,
	unsigned mode, unsigned long timeout)
{
	wait_queue_head_t *wq = bit_waitqueue(word, bit);
	DEFINE_WAIT_BIT(wait, word, bit);

	wait.key.timeout = jiffies + timeout;
	return __wait_on_bit(wq, &wait, action, mode);
}
EXPORT_SYMBOL_GPL(out_of_line_wait_on_bit_timeout);

int __sched
__wait_on_bit_lock(wait_queue_head_t *wq, struct wait_bit_queue *q,
			wait_bit_action_f *action, unsigned mode)
{
	int ret = 0;

	for (;;) {
		prepare_to_wait_exclusive(wq, &q->wait, mode);
		if (test_bit(q->key.bit_nr, q->key.flags)) {
			ret = action(&q->key, mode);
			/*
			 * See the comment in prepare_to_wait_event().
			 * finish_wait() does not necessarily takes wq->lock,
			 * but test_and_set_bit() implies mb() which pairs with
			 * smp_mb__after_atomic() before wake_up_page().
			 */
			if (ret)
				finish_wait(wq, &q->wait);
		}
		if (!test_and_set_bit(q->key.bit_nr, q->key.flags)) {
			if (!ret)
				finish_wait(wq, &q->wait);
			return 0;
		} else if (ret) {
			return ret;
		}
	}
}
EXPORT_SYMBOL(__wait_on_bit_lock);

int __sched out_of_line_wait_on_bit_lock(void *word, int bit,
					 wait_bit_action_f *action, unsigned mode)
{
	wait_queue_head_t *wq = bit_waitqueue(word, bit);
	DEFINE_WAIT_BIT(wait, word, bit);

	return __wait_on_bit_lock(wq, &wait, action, mode);
}
EXPORT_SYMBOL(out_of_line_wait_on_bit_lock);

void __wake_up_bit(wait_queue_head_t *wq, void *word, int bit)
{
	struct wait_bit_key key = __WAIT_BIT_KEY_INITIALIZER(word, bit);
	if (waitqueue_active(wq))
		__wake_up(wq, TASK_NORMAL, 1, &key);
}
EXPORT_SYMBOL(__wake_up_bit);

/**
 * wake_up_bit - wake up a waiter on a bit
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 *
 * There is a standard hashed waitqueue table for generic use. This
 * is the part of the hashtable's accessor API that wakes up waiters
 * on a bit. For instance, if one were to have waiters on a bitflag,
 * one would call wake_up_bit() after clearing the bit.
 *
 * In order for this to function properly, as it uses waitqueue_active()
 * internally, some kind of memory barrier must be done prior to calling
 * this. Typically, this will be smp_mb__after_atomic(), but in some
 * cases where bitflags are manipulated non-atomically under a lock, one
 * may need to use a less regular barrier, such fs/inode.c's smp_mb(),
 * because spin_unlock() does not guarantee a memory barrier.
 */
void wake_up_bit(void *word, int bit)
{
	__wake_up_bit(bit_waitqueue(word, bit), word, bit);
}
EXPORT_SYMBOL(wake_up_bit);

wait_queue_head_t *bit_waitqueue(void *word, int bit)
{
	const int shift = BITS_PER_LONG == 32 ? 5 : 6;
	const struct zone *zone = page_zone(virt_to_page(word));
	unsigned long val = (unsigned long)word << shift | bit;

	return &zone->wait_table[hash_long(val, zone->wait_table_bits)];
}
EXPORT_SYMBOL(bit_waitqueue);

/*
 * Manipulate the atomic_t address to produce a better bit waitqueue table hash
 * index (we're keying off bit -1, but that would produce a horrible hash
 * value).
 */
static inline wait_queue_head_t *atomic_t_waitqueue(atomic_t *p)
{
	if (BITS_PER_LONG == 64) {
		unsigned long q = (unsigned long)p;
		return bit_waitqueue((void *)(q & ~1), q & 1);
	}
	return bit_waitqueue(p, 0);
}

static int wake_atomic_t_function(wait_queue_t *wait, unsigned mode, int sync,
				  void *arg)
{
	struct wait_bit_key *key = arg;
	struct wait_bit_queue *wait_bit
		= container_of(wait, struct wait_bit_queue, wait);
	atomic_t *val = key->flags;

	if (wait_bit->key.flags != key->flags ||
	    wait_bit->key.bit_nr != key->bit_nr ||
	    atomic_read(val) != 0)
		return 0;
	return autoremove_wake_function(wait, mode, sync, key);
}

/*
 * To allow interruptible waiting and asynchronous (i.e. nonblocking) waiting,
 * the actions of __wait_on_atomic_t() are permitted return codes.  Nonzero
 * return codes halt waiting and return.
 */
static __sched
int __wait_on_atomic_t(wait_queue_head_t *wq, struct wait_bit_queue *q,
		       int (*action)(atomic_t *), unsigned mode)
{
	atomic_t *val;
	int ret = 0;

	do {
		prepare_to_wait(wq, &q->wait, mode);
		val = q->key.flags;
		if (atomic_read(val) == 0)
			break;
		ret = (*action)(val);
	} while (!ret && atomic_read(val) != 0);
	finish_wait(wq, &q->wait);
	return ret;
}

#define DEFINE_WAIT_ATOMIC_T(name, p)					\
	struct wait_bit_queue name = {					\
		.key = __WAIT_ATOMIC_T_KEY_INITIALIZER(p),		\
		.wait	= {						\
			.private	= current,			\
			.func		= wake_atomic_t_function,	\
			.task_list	=				\
				LIST_HEAD_INIT((name).wait.task_list),	\
		},							\
	}

__sched int out_of_line_wait_on_atomic_t(atomic_t *p, int (*action)(atomic_t *),
					 unsigned mode)
{
	wait_queue_head_t *wq = atomic_t_waitqueue(p);
	DEFINE_WAIT_ATOMIC_T(wait, p);

	return __wait_on_atomic_t(wq, &wait, action, mode);
}
EXPORT_SYMBOL(out_of_line_wait_on_atomic_t);

/**
 * wake_up_atomic_t - Wake up a waiter on a atomic_t
 * @p: The atomic_t being waited on, a kernel virtual address
 *
 * Wake up anyone waiting for the atomic_t to go to zero.
 *
 * Abuse the bit-waker function and its waitqueue hash table set (the atomic_t
 * check is done by the waiter's wake function, not the by the waker itself).
 */
void wake_up_atomic_t(atomic_t *p)
{
	__wake_up_bit(atomic_t_waitqueue(p), p, WAIT_ATOMIC_T_BIT_NR);
}
EXPORT_SYMBOL(wake_up_atomic_t);

__sched int bit_wait(struct wait_bit_key *word, int mode)
{
	schedule();
	if (signal_pending_state(mode, current))
		return -EINTR;
	return 0;
}
EXPORT_SYMBOL(bit_wait);

__sched int bit_wait_io(struct wait_bit_key *word, int mode)
{
	io_schedule();
	if (signal_pending_state(mode, current))
		return -EINTR;
	return 0;
}
EXPORT_SYMBOL(bit_wait_io);

__sched int bit_wait_timeout(struct wait_bit_key *word, int mode)
{
	unsigned long now = READ_ONCE(jiffies);
	if (time_after_eq(now, word->timeout))
		return -EAGAIN;
	schedule_timeout(word->timeout - now);
	if (signal_pending_state(mode, current))
		return -EINTR;
	return 0;
}
EXPORT_SYMBOL_GPL(bit_wait_timeout);

__sched int bit_wait_io_timeout(struct wait_bit_key *word, int mode)
{
	unsigned long now = READ_ONCE(jiffies);
	if (time_after_eq(now, word->timeout))
		return -EAGAIN;
	io_schedule_timeout(word->timeout - now);
	if (signal_pending_state(mode, current))
		return -EINTR;
	return 0;
}
EXPORT_SYMBOL_GPL(bit_wait_io_timeout);

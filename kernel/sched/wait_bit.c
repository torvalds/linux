// SPDX-License-Identifier: GPL-2.0-only

/*
 * The implementation of the wait_bit*() and related waiting APIs:
 */

#define WAIT_TABLE_BITS 8
#define WAIT_TABLE_SIZE (1 << WAIT_TABLE_BITS)

static wait_queue_head_t bit_wait_table[WAIT_TABLE_SIZE] __cacheline_aligned;

wait_queue_head_t *bit_waitqueue(void *word, int bit)
{
	const int shift = BITS_PER_LONG == 32 ? 5 : 6;
	unsigned long val = (unsigned long)word << shift | bit;

	return bit_wait_table + hash_long(val, WAIT_TABLE_BITS);
}
EXPORT_SYMBOL(bit_waitqueue);

int wake_bit_function(struct wait_queue_entry *wq_entry, unsigned mode, int sync, void *arg)
{
	struct wait_bit_key *key = arg;
	struct wait_bit_queue_entry *wait_bit = container_of(wq_entry, struct wait_bit_queue_entry, wq_entry);

	if (wait_bit->key.flags != key->flags ||
			wait_bit->key.bit_nr != key->bit_nr ||
			test_bit(key->bit_nr, key->flags))
		return 0;

	return autoremove_wake_function(wq_entry, mode, sync, key);
}
EXPORT_SYMBOL(wake_bit_function);

/*
 * To allow interruptible waiting and asynchronous (i.e. nonblocking)
 * waiting, the actions of __wait_on_bit() and __wait_on_bit_lock() are
 * permitted return codes. Nonzero return codes halt waiting and return.
 */
int __sched
__wait_on_bit(struct wait_queue_head *wq_head, struct wait_bit_queue_entry *wbq_entry,
	      wait_bit_action_f *action, unsigned mode)
{
	int ret = 0;

	do {
		prepare_to_wait(wq_head, &wbq_entry->wq_entry, mode);
		if (test_bit(wbq_entry->key.bit_nr, wbq_entry->key.flags))
			ret = (*action)(&wbq_entry->key, mode);
	} while (test_bit_acquire(wbq_entry->key.bit_nr, wbq_entry->key.flags) && !ret);

	finish_wait(wq_head, &wbq_entry->wq_entry);

	return ret;
}
EXPORT_SYMBOL(__wait_on_bit);

int __sched out_of_line_wait_on_bit(void *word, int bit,
				    wait_bit_action_f *action, unsigned mode)
{
	struct wait_queue_head *wq_head = bit_waitqueue(word, bit);
	DEFINE_WAIT_BIT(wq_entry, word, bit);

	return __wait_on_bit(wq_head, &wq_entry, action, mode);
}
EXPORT_SYMBOL(out_of_line_wait_on_bit);

int __sched out_of_line_wait_on_bit_timeout(
	void *word, int bit, wait_bit_action_f *action,
	unsigned mode, unsigned long timeout)
{
	struct wait_queue_head *wq_head = bit_waitqueue(word, bit);
	DEFINE_WAIT_BIT(wq_entry, word, bit);

	wq_entry.key.timeout = jiffies + timeout;

	return __wait_on_bit(wq_head, &wq_entry, action, mode);
}
EXPORT_SYMBOL_GPL(out_of_line_wait_on_bit_timeout);

int __sched
__wait_on_bit_lock(struct wait_queue_head *wq_head, struct wait_bit_queue_entry *wbq_entry,
			wait_bit_action_f *action, unsigned mode)
{
	int ret = 0;

	for (;;) {
		prepare_to_wait_exclusive(wq_head, &wbq_entry->wq_entry, mode);
		if (test_bit(wbq_entry->key.bit_nr, wbq_entry->key.flags)) {
			ret = action(&wbq_entry->key, mode);
			/*
			 * See the comment in prepare_to_wait_event().
			 * finish_wait() does not necessarily takes wwq_head->lock,
			 * but test_and_set_bit() implies mb() which pairs with
			 * smp_mb__after_atomic() before wake_up_page().
			 */
			if (ret)
				finish_wait(wq_head, &wbq_entry->wq_entry);
		}
		if (!test_and_set_bit(wbq_entry->key.bit_nr, wbq_entry->key.flags)) {
			if (!ret)
				finish_wait(wq_head, &wbq_entry->wq_entry);
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
	struct wait_queue_head *wq_head = bit_waitqueue(word, bit);
	DEFINE_WAIT_BIT(wq_entry, word, bit);

	return __wait_on_bit_lock(wq_head, &wq_entry, action, mode);
}
EXPORT_SYMBOL(out_of_line_wait_on_bit_lock);

void __wake_up_bit(struct wait_queue_head *wq_head, void *word, int bit)
{
	struct wait_bit_key key = __WAIT_BIT_KEY_INITIALIZER(word, bit);

	if (waitqueue_active(wq_head))
		__wake_up(wq_head, TASK_NORMAL, 1, &key);
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

wait_queue_head_t *__var_waitqueue(void *p)
{
	return bit_wait_table + hash_ptr(p, WAIT_TABLE_BITS);
}
EXPORT_SYMBOL(__var_waitqueue);

static int
var_wake_function(struct wait_queue_entry *wq_entry, unsigned int mode,
		  int sync, void *arg)
{
	struct wait_bit_key *key = arg;
	struct wait_bit_queue_entry *wbq_entry =
		container_of(wq_entry, struct wait_bit_queue_entry, wq_entry);

	if (wbq_entry->key.flags != key->flags ||
	    wbq_entry->key.bit_nr != key->bit_nr)
		return 0;

	return autoremove_wake_function(wq_entry, mode, sync, key);
}

void init_wait_var_entry(struct wait_bit_queue_entry *wbq_entry, void *var, int flags)
{
	*wbq_entry = (struct wait_bit_queue_entry){
		.key = {
			.flags	= (var),
			.bit_nr = -1,
		},
		.wq_entry = {
			.flags	 = flags,
			.private = current,
			.func	 = var_wake_function,
			.entry	 = LIST_HEAD_INIT(wbq_entry->wq_entry.entry),
		},
	};
}
EXPORT_SYMBOL(init_wait_var_entry);

void wake_up_var(void *var)
{
	__wake_up_bit(__var_waitqueue(var), var, -1);
}
EXPORT_SYMBOL(wake_up_var);

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

void __init wait_bit_init(void)
{
	int i;

	for (i = 0; i < WAIT_TABLE_SIZE; i++)
		init_waitqueue_head(bit_wait_table + i);
}

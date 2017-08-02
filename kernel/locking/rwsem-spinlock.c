/* rwsem-spinlock.c: R/W semaphores: contention handling functions for
 * generic spinlock implementation
 *
 * Copyright (c) 2001   David Howells (dhowells@redhat.com).
 * - Derived partially from idea by Andrea Arcangeli <andrea@suse.de>
 * - Derived also from comments by Linus
 */
#include <linux/rwsem.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/export.h>

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
};

int rwsem_is_locked(struct rw_semaphore *sem)
{
	int ret = 1;
	unsigned long flags;

	if (raw_spin_trylock_irqsave(&sem->wait_lock, flags)) {
		ret = (sem->count != 0);
		raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
	}
	return ret;
}
EXPORT_SYMBOL(rwsem_is_locked);

/*
 * initialise the semaphore
 */
void __init_rwsem(struct rw_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held semaphore:
	 */
	debug_check_no_locks_freed((void *)sem, sizeof(*sem));
	lockdep_init_map(&sem->dep_map, name, key, 0);
#endif
	sem->count = 0;
	raw_spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
}
EXPORT_SYMBOL(__init_rwsem);

/*
 * handle the lock release when processes blocked on it that can now run
 * - if we come here, then:
 *   - the 'active count' _reached_ zero
 *   - the 'waiting count' is non-zero
 * - the spinlock must be held by the caller
 * - woken process blocks are discarded from the list after having task zeroed
 * - writers are only woken if wakewrite is non-zero
 */
static inline struct rw_semaphore *
__rwsem_do_wake(struct rw_semaphore *sem, int wakewrite)
{
	struct rwsem_waiter *waiter;
	struct task_struct *tsk;
	int woken;

	waiter = list_entry(sem->wait_list.next, struct rwsem_waiter, list);

	if (waiter->type == RWSEM_WAITING_FOR_WRITE) {
		if (wakewrite)
			/* Wake up a writer. Note that we do not grant it the
			 * lock - it will have to acquire it when it runs. */
			wake_up_process(waiter->task);
		goto out;
	}

	/* grant an infinite number of read locks to the front of the queue */
	woken = 0;
	do {
		struct list_head *next = waiter->list.next;

		list_del(&waiter->list);
		tsk = waiter->task;
		/*
		 * Make sure we do not wakeup the next reader before
		 * setting the nil condition to grant the next reader;
		 * otherwise we could miss the wakeup on the other
		 * side and end up sleeping again. See the pairing
		 * in rwsem_down_read_failed().
		 */
		smp_mb();
		waiter->task = NULL;
		wake_up_process(tsk);
		put_task_struct(tsk);
		woken++;
		if (next == &sem->wait_list)
			break;
		waiter = list_entry(next, struct rwsem_waiter, list);
	} while (waiter->type != RWSEM_WAITING_FOR_WRITE);

	sem->count += woken;

 out:
	return sem;
}

/*
 * wake a single writer
 */
static inline struct rw_semaphore *
__rwsem_wake_one_writer(struct rw_semaphore *sem)
{
	struct rwsem_waiter *waiter;

	waiter = list_entry(sem->wait_list.next, struct rwsem_waiter, list);
	wake_up_process(waiter->task);

	return sem;
}

/*
 * get a read lock on the semaphore
 */
void __sched __down_read(struct rw_semaphore *sem)
{
	struct rwsem_waiter waiter;
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	if (sem->count >= 0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->count++;
		raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
		goto out;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);

	/* set up my own style of waitqueue */
	waiter.task = current;
	waiter.type = RWSEM_WAITING_FOR_READ;
	get_task_struct(current);

	list_add_tail(&waiter.list, &sem->wait_list);

	/* we don't need to touch the semaphore struct anymore */
	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.task)
			break;
		schedule();
		set_current_state(TASK_UNINTERRUPTIBLE);
	}

	__set_current_state(TASK_RUNNING);
 out:
	;
}

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
int __down_read_trylock(struct rw_semaphore *sem)
{
	unsigned long flags;
	int ret = 0;


	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	if (sem->count >= 0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->count++;
		ret = 1;
	}

	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);

	return ret;
}

/*
 * get a write lock on the semaphore
 */
int __sched __down_write_common(struct rw_semaphore *sem, int state)
{
	struct rwsem_waiter waiter;
	unsigned long flags;
	int ret = 0;

	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	/* set up my own style of waitqueue */
	waiter.task = current;
	waiter.type = RWSEM_WAITING_FOR_WRITE;
	list_add_tail(&waiter.list, &sem->wait_list);

	/* wait for someone to release the lock */
	for (;;) {
		/*
		 * That is the key to support write lock stealing: allows the
		 * task already on CPU to get the lock soon rather than put
		 * itself into sleep and waiting for system woke it or someone
		 * else in the head of the wait list up.
		 */
		if (sem->count == 0)
			break;
		if (signal_pending_state(state, current))
			goto out_nolock;

		set_current_state(state);
		raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
		schedule();
		raw_spin_lock_irqsave(&sem->wait_lock, flags);
	}
	/* got the lock */
	sem->count = -1;
	list_del(&waiter.list);

	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);

	return ret;

out_nolock:
	list_del(&waiter.list);
	if (!list_empty(&sem->wait_list) && sem->count >= 0)
		__rwsem_do_wake(sem, 0);
	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);

	return -EINTR;
}

void __sched __down_write(struct rw_semaphore *sem)
{
	__down_write_common(sem, TASK_UNINTERRUPTIBLE);
}

int __sched __down_write_killable(struct rw_semaphore *sem)
{
	return __down_write_common(sem, TASK_KILLABLE);
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
int __down_write_trylock(struct rw_semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	if (sem->count == 0) {
		/* got the lock */
		sem->count = -1;
		ret = 1;
	}

	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);

	return ret;
}

/*
 * release a read lock on the semaphore
 */
void __up_read(struct rw_semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	if (--sem->count == 0 && !list_empty(&sem->wait_list))
		sem = __rwsem_wake_one_writer(sem);

	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
}

/*
 * release a write lock on the semaphore
 */
void __up_write(struct rw_semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	sem->count = 0;
	if (!list_empty(&sem->wait_list))
		sem = __rwsem_do_wake(sem, 1);

	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
}

/*
 * downgrade a write lock into a read lock
 * - just wake up any readers at the front of the queue
 */
void __downgrade_write(struct rw_semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	sem->count = 1;
	if (!list_empty(&sem->wait_list))
		sem = __rwsem_do_wake(sem, 0);

	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
}


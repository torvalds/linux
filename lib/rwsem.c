/* rwsem.c: R/W semaphores: contention handling functions
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from arch/i386/kernel/semaphore.c
 */
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	unsigned int flags;
#define RWSEM_WAITING_FOR_READ	0x00000001
#define RWSEM_WAITING_FOR_WRITE	0x00000002
};

#if RWSEM_DEBUG
#undef rwsemtrace
void rwsemtrace(struct rw_semaphore *sem, const char *str)
{
	printk("sem=%p\n", sem);
	printk("(sem)=%08lx\n", sem->count);
	if (sem->debug)
		printk("[%d] %s({%08lx})\n", current->pid, str, sem->count);
}
#endif

/*
 * handle the lock release when processes blocked on it that can now run
 * - if we come here from up_xxxx(), then:
 *   - the 'active part' of count (&0x0000ffff) reached 0 (but may have changed)
 *   - the 'waiting part' of count (&0xffff0000) is -ve (and will still be so)
 *   - there must be someone on the queue
 * - the spinlock must be held by the caller
 * - woken process blocks are discarded from the list after having task zeroed
 * - writers are only woken if downgrading is false
 */
static inline struct rw_semaphore *
__rwsem_do_wake(struct rw_semaphore *sem, int downgrading)
{
	struct rwsem_waiter *waiter;
	struct task_struct *tsk;
	struct list_head *next;
	signed long oldcount, woken, loop;

	rwsemtrace(sem, "Entering __rwsem_do_wake");

	if (downgrading)
		goto dont_wake_writers;

	/* if we came through an up_xxxx() call, we only only wake someone up
	 * if we can transition the active part of the count from 0 -> 1
	 */
 try_again:
	oldcount = rwsem_atomic_update(RWSEM_ACTIVE_BIAS, sem)
						- RWSEM_ACTIVE_BIAS;
	if (oldcount & RWSEM_ACTIVE_MASK)
		goto undo;

	waiter = list_entry(sem->wait_list.next, struct rwsem_waiter, list);

	/* try to grant a single write lock if there's a writer at the front
	 * of the queue - note we leave the 'active part' of the count
	 * incremented by 1 and the waiting part incremented by 0x00010000
	 */
	if (!(waiter->flags & RWSEM_WAITING_FOR_WRITE))
		goto readers_only;

	/* We must be careful not to touch 'waiter' after we set ->task = NULL.
	 * It is an allocated on the waiter's stack and may become invalid at
	 * any time after that point (due to a wakeup from another source).
	 */
	list_del(&waiter->list);
	tsk = waiter->task;
	smp_mb();
	waiter->task = NULL;
	wake_up_process(tsk);
	put_task_struct(tsk);
	goto out;

	/* don't want to wake any writers */
 dont_wake_writers:
	waiter = list_entry(sem->wait_list.next, struct rwsem_waiter, list);
	if (waiter->flags & RWSEM_WAITING_FOR_WRITE)
		goto out;

	/* grant an infinite number of read locks to the readers at the front
	 * of the queue
	 * - note we increment the 'active part' of the count by the number of
	 *   readers before waking any processes up
	 */
 readers_only:
	woken = 0;
	do {
		woken++;

		if (waiter->list.next == &sem->wait_list)
			break;

		waiter = list_entry(waiter->list.next,
					struct rwsem_waiter, list);

	} while (waiter->flags & RWSEM_WAITING_FOR_READ);

	loop = woken;
	woken *= RWSEM_ACTIVE_BIAS - RWSEM_WAITING_BIAS;
	if (!downgrading)
		/* we'd already done one increment earlier */
		woken -= RWSEM_ACTIVE_BIAS;

	rwsem_atomic_add(woken, sem);

	next = sem->wait_list.next;
	for (; loop > 0; loop--) {
		waiter = list_entry(next, struct rwsem_waiter, list);
		next = waiter->list.next;
		tsk = waiter->task;
		smp_mb();
		waiter->task = NULL;
		wake_up_process(tsk);
		put_task_struct(tsk);
	}

	sem->wait_list.next = next;
	next->prev = &sem->wait_list;

 out:
	rwsemtrace(sem, "Leaving __rwsem_do_wake");
	return sem;

	/* undo the change to count, but check for a transition 1->0 */
 undo:
	if (rwsem_atomic_update(-RWSEM_ACTIVE_BIAS, sem) != 0)
		goto out;
	goto try_again;
}

/*
 * wait for a lock to be granted
 */
static inline struct rw_semaphore *
rwsem_down_failed_common(struct rw_semaphore *sem,
			struct rwsem_waiter *waiter, signed long adjustment)
{
	struct task_struct *tsk = current;
	signed long count;

	set_task_state(tsk, TASK_UNINTERRUPTIBLE);

	/* set up my own style of waitqueue */
	spin_lock_irq(&sem->wait_lock);
	waiter->task = tsk;
	get_task_struct(tsk);

	list_add_tail(&waiter->list, &sem->wait_list);

	/* we're now waiting on the lock, but no longer actively read-locking */
	count = rwsem_atomic_update(adjustment, sem);

	/* if there are no active locks, wake the front queued process(es) up */
	if (!(count & RWSEM_ACTIVE_MASK))
		sem = __rwsem_do_wake(sem, 0);

	spin_unlock_irq(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter->task)
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	tsk->state = TASK_RUNNING;

	return sem;
}

/*
 * wait for the read lock to be granted
 */
struct rw_semaphore fastcall __sched *
rwsem_down_read_failed(struct rw_semaphore *sem)
{
	struct rwsem_waiter waiter;

	rwsemtrace(sem, "Entering rwsem_down_read_failed");

	waiter.flags = RWSEM_WAITING_FOR_READ;
	rwsem_down_failed_common(sem, &waiter,
				RWSEM_WAITING_BIAS - RWSEM_ACTIVE_BIAS);

	rwsemtrace(sem, "Leaving rwsem_down_read_failed");
	return sem;
}

/*
 * wait for the write lock to be granted
 */
struct rw_semaphore fastcall __sched *
rwsem_down_write_failed(struct rw_semaphore *sem)
{
	struct rwsem_waiter waiter;

	rwsemtrace(sem, "Entering rwsem_down_write_failed");

	waiter.flags = RWSEM_WAITING_FOR_WRITE;
	rwsem_down_failed_common(sem, &waiter, -RWSEM_ACTIVE_BIAS);

	rwsemtrace(sem, "Leaving rwsem_down_write_failed");
	return sem;
}

/*
 * handle waking up a waiter on the semaphore
 * - up_read/up_write has decremented the active part of count if we come here
 */
struct rw_semaphore fastcall *rwsem_wake(struct rw_semaphore *sem)
{
	unsigned long flags;

	rwsemtrace(sem, "Entering rwsem_wake");

	spin_lock_irqsave(&sem->wait_lock, flags);

	/* do nothing if list empty */
	if (!list_empty(&sem->wait_list))
		sem = __rwsem_do_wake(sem, 0);

	spin_unlock_irqrestore(&sem->wait_lock, flags);

	rwsemtrace(sem, "Leaving rwsem_wake");

	return sem;
}

/*
 * downgrade a write lock into a read lock
 * - caller incremented waiting part of count and discovered it still negative
 * - just wake up any readers at the front of the queue
 */
struct rw_semaphore fastcall *rwsem_downgrade_wake(struct rw_semaphore *sem)
{
	unsigned long flags;

	rwsemtrace(sem, "Entering rwsem_downgrade_wake");

	spin_lock_irqsave(&sem->wait_lock, flags);

	/* do nothing if list empty */
	if (!list_empty(&sem->wait_list))
		sem = __rwsem_do_wake(sem, 1);

	spin_unlock_irqrestore(&sem->wait_lock, flags);

	rwsemtrace(sem, "Leaving rwsem_downgrade_wake");
	return sem;
}

EXPORT_SYMBOL(rwsem_down_read_failed);
EXPORT_SYMBOL(rwsem_down_write_failed);
EXPORT_SYMBOL(rwsem_wake);
EXPORT_SYMBOL(rwsem_downgrade_wake);
#if RWSEM_DEBUG
EXPORT_SYMBOL(rwsemtrace);
#endif

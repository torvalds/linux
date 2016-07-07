#include <linux/atomic.h>
#include <linux/rwsem.h>
#include <linux/percpu.h>
#include <linux/wait.h>
#include <linux/lockdep.h>
#include <linux/percpu-rwsem.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/errno.h>

int __percpu_init_rwsem(struct percpu_rw_semaphore *brw,
			const char *name, struct lock_class_key *rwsem_key)
{
	brw->fast_read_ctr = alloc_percpu(int);
	if (unlikely(!brw->fast_read_ctr))
		return -ENOMEM;

	/* ->rw_sem represents the whole percpu_rw_semaphore for lockdep */
	__init_rwsem(&brw->rw_sem, name, rwsem_key);
	rcu_sync_init(&brw->rss, RCU_SCHED_SYNC);
	atomic_set(&brw->slow_read_ctr, 0);
	init_waitqueue_head(&brw->write_waitq);
	return 0;
}
EXPORT_SYMBOL_GPL(__percpu_init_rwsem);

void percpu_free_rwsem(struct percpu_rw_semaphore *brw)
{
	/*
	 * XXX: temporary kludge. The error path in alloc_super()
	 * assumes that percpu_free_rwsem() is safe after kzalloc().
	 */
	if (!brw->fast_read_ctr)
		return;

	rcu_sync_dtor(&brw->rss);
	free_percpu(brw->fast_read_ctr);
	brw->fast_read_ctr = NULL; /* catch use after free bugs */
}
EXPORT_SYMBOL_GPL(percpu_free_rwsem);

/*
 * This is the fast-path for down_read/up_read. If it succeeds we rely
 * on the barriers provided by rcu_sync_enter/exit; see the comments in
 * percpu_down_write() and percpu_up_write().
 *
 * If this helper fails the callers rely on the normal rw_semaphore and
 * atomic_dec_and_test(), so in this case we have the necessary barriers.
 */
static bool update_fast_ctr(struct percpu_rw_semaphore *brw, unsigned int val)
{
	bool success;

	preempt_disable();
	success = rcu_sync_is_idle(&brw->rss);
	if (likely(success))
		__this_cpu_add(*brw->fast_read_ctr, val);
	preempt_enable();

	return success;
}

/*
 * Like the normal down_read() this is not recursive, the writer can
 * come after the first percpu_down_read() and create the deadlock.
 *
 * Note: returns with lock_is_held(brw->rw_sem) == T for lockdep,
 * percpu_up_read() does rwsem_release(). This pairs with the usage
 * of ->rw_sem in percpu_down/up_write().
 */
void percpu_down_read(struct percpu_rw_semaphore *brw)
{
	might_sleep();
	rwsem_acquire_read(&brw->rw_sem.dep_map, 0, 0, _RET_IP_);

	if (likely(update_fast_ctr(brw, +1)))
		return;

	/* Avoid rwsem_acquire_read() and rwsem_release() */
	__down_read(&brw->rw_sem);
	atomic_inc(&brw->slow_read_ctr);
	__up_read(&brw->rw_sem);
}
EXPORT_SYMBOL_GPL(percpu_down_read);

int percpu_down_read_trylock(struct percpu_rw_semaphore *brw)
{
	if (unlikely(!update_fast_ctr(brw, +1))) {
		if (!__down_read_trylock(&brw->rw_sem))
			return 0;
		atomic_inc(&brw->slow_read_ctr);
		__up_read(&brw->rw_sem);
	}

	rwsem_acquire_read(&brw->rw_sem.dep_map, 0, 1, _RET_IP_);
	return 1;
}

void percpu_up_read(struct percpu_rw_semaphore *brw)
{
	rwsem_release(&brw->rw_sem.dep_map, 1, _RET_IP_);

	if (likely(update_fast_ctr(brw, -1)))
		return;

	/* false-positive is possible but harmless */
	if (atomic_dec_and_test(&brw->slow_read_ctr))
		wake_up_all(&brw->write_waitq);
}
EXPORT_SYMBOL_GPL(percpu_up_read);

static int clear_fast_ctr(struct percpu_rw_semaphore *brw)
{
	unsigned int sum = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		sum += per_cpu(*brw->fast_read_ctr, cpu);
		per_cpu(*brw->fast_read_ctr, cpu) = 0;
	}

	return sum;
}

void percpu_down_write(struct percpu_rw_semaphore *brw)
{
	/*
	 * Make rcu_sync_is_idle() == F and thus disable the fast-path in
	 * percpu_down_read() and percpu_up_read(), and wait for gp pass.
	 *
	 * The latter synchronises us with the preceding readers which used
	 * the fast-past, so we can not miss the result of __this_cpu_add()
	 * or anything else inside their criticial sections.
	 */
	rcu_sync_enter(&brw->rss);

	/* exclude other writers, and block the new readers completely */
	down_write(&brw->rw_sem);

	/* nobody can use fast_read_ctr, move its sum into slow_read_ctr */
	atomic_add(clear_fast_ctr(brw), &brw->slow_read_ctr);

	/* wait for all readers to complete their percpu_up_read() */
	wait_event(brw->write_waitq, !atomic_read(&brw->slow_read_ctr));
}
EXPORT_SYMBOL_GPL(percpu_down_write);

void percpu_up_write(struct percpu_rw_semaphore *brw)
{
	/* release the lock, but the readers can't use the fast-path */
	up_write(&brw->rw_sem);
	/*
	 * Enable the fast-path in percpu_down_read() and percpu_up_read()
	 * but only after another gp pass; this adds the necessary barrier
	 * to ensure the reader can't miss the changes done by us.
	 */
	rcu_sync_exit(&brw->rss);
}
EXPORT_SYMBOL_GPL(percpu_up_write);

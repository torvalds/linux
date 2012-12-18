#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/percpu.h>
#include <linux/wait.h>
#include <linux/percpu-rwsem.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/errno.h>

int percpu_init_rwsem(struct percpu_rw_semaphore *brw)
{
	brw->fast_read_ctr = alloc_percpu(int);
	if (unlikely(!brw->fast_read_ctr))
		return -ENOMEM;

	mutex_init(&brw->writer_mutex);
	init_rwsem(&brw->rw_sem);
	atomic_set(&brw->slow_read_ctr, 0);
	init_waitqueue_head(&brw->write_waitq);
	return 0;
}

void percpu_free_rwsem(struct percpu_rw_semaphore *brw)
{
	free_percpu(brw->fast_read_ctr);
	brw->fast_read_ctr = NULL; /* catch use after free bugs */
}

/*
 * This is the fast-path for down_read/up_read, it only needs to ensure
 * there is no pending writer (!mutex_is_locked() check) and inc/dec the
 * fast per-cpu counter. The writer uses synchronize_sched_expedited() to
 * serialize with the preempt-disabled section below.
 *
 * The nontrivial part is that we should guarantee acquire/release semantics
 * in case when
 *
 *	R_W: down_write() comes after up_read(), the writer should see all
 *	     changes done by the reader
 * or
 *	W_R: down_read() comes after up_write(), the reader should see all
 *	     changes done by the writer
 *
 * If this helper fails the callers rely on the normal rw_semaphore and
 * atomic_dec_and_test(), so in this case we have the necessary barriers.
 *
 * But if it succeeds we do not have any barriers, mutex_is_locked() or
 * __this_cpu_add() below can be reordered with any LOAD/STORE done by the
 * reader inside the critical section. See the comments in down_write and
 * up_write below.
 */
static bool update_fast_ctr(struct percpu_rw_semaphore *brw, unsigned int val)
{
	bool success = false;

	preempt_disable();
	if (likely(!mutex_is_locked(&brw->writer_mutex))) {
		__this_cpu_add(*brw->fast_read_ctr, val);
		success = true;
	}
	preempt_enable();

	return success;
}

/*
 * Like the normal down_read() this is not recursive, the writer can
 * come after the first percpu_down_read() and create the deadlock.
 */
void percpu_down_read(struct percpu_rw_semaphore *brw)
{
	if (likely(update_fast_ctr(brw, +1)))
		return;

	down_read(&brw->rw_sem);
	atomic_inc(&brw->slow_read_ctr);
	up_read(&brw->rw_sem);
}

void percpu_up_read(struct percpu_rw_semaphore *brw)
{
	if (likely(update_fast_ctr(brw, -1)))
		return;

	/* false-positive is possible but harmless */
	if (atomic_dec_and_test(&brw->slow_read_ctr))
		wake_up_all(&brw->write_waitq);
}

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

/*
 * A writer takes ->writer_mutex to exclude other writers and to force the
 * readers to switch to the slow mode, note the mutex_is_locked() check in
 * update_fast_ctr().
 *
 * After that the readers can only inc/dec the slow ->slow_read_ctr counter,
 * ->fast_read_ctr is stable. Once the writer moves its sum into the slow
 * counter it represents the number of active readers.
 *
 * Finally the writer takes ->rw_sem for writing and blocks the new readers,
 * then waits until the slow counter becomes zero.
 */
void percpu_down_write(struct percpu_rw_semaphore *brw)
{
	/* also blocks update_fast_ctr() which checks mutex_is_locked() */
	mutex_lock(&brw->writer_mutex);

	/*
	 * 1. Ensures mutex_is_locked() is visible to any down_read/up_read
	 *    so that update_fast_ctr() can't succeed.
	 *
	 * 2. Ensures we see the result of every previous this_cpu_add() in
	 *    update_fast_ctr().
	 *
	 * 3. Ensures that if any reader has exited its critical section via
	 *    fast-path, it executes a full memory barrier before we return.
	 *    See R_W case in the comment above update_fast_ctr().
	 */
	synchronize_sched_expedited();

	/* nobody can use fast_read_ctr, move its sum into slow_read_ctr */
	atomic_add(clear_fast_ctr(brw), &brw->slow_read_ctr);

	/* block the new readers completely */
	down_write(&brw->rw_sem);

	/* wait for all readers to complete their percpu_up_read() */
	wait_event(brw->write_waitq, !atomic_read(&brw->slow_read_ctr));
}

void percpu_up_write(struct percpu_rw_semaphore *brw)
{
	/* allow the new readers, but only the slow-path */
	up_write(&brw->rw_sem);

	/*
	 * Insert the barrier before the next fast-path in down_read,
	 * see W_R case in the comment above update_fast_ctr().
	 */
	synchronize_sched_expedited();
	mutex_unlock(&brw->writer_mutex);
}

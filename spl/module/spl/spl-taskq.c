/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Solaris Porting Layer (SPL) Task Queue Implementation.
 */

#include <sys/taskq.h>
#include <sys/kmem.h>
#include <sys/tsd.h>

int spl_taskq_thread_bind = 0;
module_param(spl_taskq_thread_bind, int, 0644);
MODULE_PARM_DESC(spl_taskq_thread_bind, "Bind taskq thread to CPU by default");


int spl_taskq_thread_dynamic = 1;
module_param(spl_taskq_thread_dynamic, int, 0644);
MODULE_PARM_DESC(spl_taskq_thread_dynamic, "Allow dynamic taskq threads");

int spl_taskq_thread_priority = 1;
module_param(spl_taskq_thread_priority, int, 0644);
MODULE_PARM_DESC(spl_taskq_thread_priority,
	"Allow non-default priority for taskq threads");

int spl_taskq_thread_sequential = 4;
module_param(spl_taskq_thread_sequential, int, 0644);
MODULE_PARM_DESC(spl_taskq_thread_sequential,
	"Create new taskq threads after N sequential tasks");

/* Global system-wide dynamic task queue available for all consumers */
taskq_t *system_taskq;
EXPORT_SYMBOL(system_taskq);
/* Global dynamic task queue for long delay */
taskq_t *system_delay_taskq;
EXPORT_SYMBOL(system_delay_taskq);

/* Private dedicated taskq for creating new taskq threads on demand. */
static taskq_t *dynamic_taskq;
static taskq_thread_t *taskq_thread_create(taskq_t *);

/* List of all taskqs */
LIST_HEAD(tq_list);
DECLARE_RWSEM(tq_list_sem);
static uint_t taskq_tsd;

static int
task_km_flags(uint_t flags)
{
	if (flags & TQ_NOSLEEP)
		return (KM_NOSLEEP);

	if (flags & TQ_PUSHPAGE)
		return (KM_PUSHPAGE);

	return (KM_SLEEP);
}

/*
 * taskq_find_by_name - Find the largest instance number of a named taskq.
 */
static int
taskq_find_by_name(const char *name)
{
	struct list_head *tql;
	taskq_t *tq;

	list_for_each_prev(tql, &tq_list) {
		tq = list_entry(tql, taskq_t, tq_taskqs);
		if (strcmp(name, tq->tq_name) == 0)
			return (tq->tq_instance);
	}
	return (-1);
}

/*
 * NOTE: Must be called with tq->tq_lock held, returns a list_t which
 * is not attached to the free, work, or pending taskq lists.
 */
static taskq_ent_t *
task_alloc(taskq_t *tq, uint_t flags, unsigned long *irqflags)
{
	taskq_ent_t *t;
	int count = 0;

	ASSERT(tq);
retry:
	/* Acquire taskq_ent_t's from free list if available */
	if (!list_empty(&tq->tq_free_list) && !(flags & TQ_NEW)) {
		t = list_entry(tq->tq_free_list.next, taskq_ent_t, tqent_list);

		ASSERT(!(t->tqent_flags & TQENT_FLAG_PREALLOC));
		ASSERT(!(t->tqent_flags & TQENT_FLAG_CANCEL));
		ASSERT(!timer_pending(&t->tqent_timer));

		list_del_init(&t->tqent_list);
		return (t);
	}

	/* Free list is empty and memory allocations are prohibited */
	if (flags & TQ_NOALLOC)
		return (NULL);

	/* Hit maximum taskq_ent_t pool size */
	if (tq->tq_nalloc >= tq->tq_maxalloc) {
		if (flags & TQ_NOSLEEP)
			return (NULL);

		/*
		 * Sleep periodically polling the free list for an available
		 * taskq_ent_t. Dispatching with TQ_SLEEP should always succeed
		 * but we cannot block forever waiting for an taskq_ent_t to
		 * show up in the free list, otherwise a deadlock can happen.
		 *
		 * Therefore, we need to allocate a new task even if the number
		 * of allocated tasks is above tq->tq_maxalloc, but we still
		 * end up delaying the task allocation by one second, thereby
		 * throttling the task dispatch rate.
		 */
		spin_unlock_irqrestore(&tq->tq_lock, *irqflags);
		schedule_timeout(HZ / 100);
		spin_lock_irqsave_nested(&tq->tq_lock, *irqflags,
		    tq->tq_lock_class);
		if (count < 100) {
			count++;
			goto retry;
		}
	}

	spin_unlock_irqrestore(&tq->tq_lock, *irqflags);
	t = kmem_alloc(sizeof (taskq_ent_t), task_km_flags(flags));
	spin_lock_irqsave_nested(&tq->tq_lock, *irqflags, tq->tq_lock_class);

	if (t) {
		taskq_init_ent(t);
		tq->tq_nalloc++;
	}

	return (t);
}

/*
 * NOTE: Must be called with tq->tq_lock held, expects the taskq_ent_t
 * to already be removed from the free, work, or pending taskq lists.
 */
static void
task_free(taskq_t *tq, taskq_ent_t *t)
{
	ASSERT(tq);
	ASSERT(t);
	ASSERT(list_empty(&t->tqent_list));
	ASSERT(!timer_pending(&t->tqent_timer));

	kmem_free(t, sizeof (taskq_ent_t));
	tq->tq_nalloc--;
}

/*
 * NOTE: Must be called with tq->tq_lock held, either destroys the
 * taskq_ent_t if too many exist or moves it to the free list for later use.
 */
static void
task_done(taskq_t *tq, taskq_ent_t *t)
{
	ASSERT(tq);
	ASSERT(t);

	/* Wake tasks blocked in taskq_wait_id() */
	wake_up_all(&t->tqent_waitq);

	list_del_init(&t->tqent_list);

	if (tq->tq_nalloc <= tq->tq_minalloc) {
		t->tqent_id = TASKQID_INVALID;
		t->tqent_func = NULL;
		t->tqent_arg = NULL;
		t->tqent_flags = 0;

		list_add_tail(&t->tqent_list, &tq->tq_free_list);
	} else {
		task_free(tq, t);
	}
}

/*
 * When a delayed task timer expires remove it from the delay list and
 * add it to the priority list in order for immediate processing.
 */
static void
task_expire_impl(taskq_ent_t *t)
{
	taskq_ent_t *w;
	taskq_t *tq = t->tqent_taskq;
	struct list_head *l;
	unsigned long flags;

	spin_lock_irqsave_nested(&tq->tq_lock, flags, tq->tq_lock_class);

	if (t->tqent_flags & TQENT_FLAG_CANCEL) {
		ASSERT(list_empty(&t->tqent_list));
		spin_unlock_irqrestore(&tq->tq_lock, flags);
		return;
	}

	t->tqent_birth = jiffies;
	/*
	 * The priority list must be maintained in strict task id order
	 * from lowest to highest for lowest_id to be easily calculable.
	 */
	list_del(&t->tqent_list);
	list_for_each_prev(l, &tq->tq_prio_list) {
		w = list_entry(l, taskq_ent_t, tqent_list);
		if (w->tqent_id < t->tqent_id) {
			list_add(&t->tqent_list, l);
			break;
		}
	}
	if (l == &tq->tq_prio_list)
		list_add(&t->tqent_list, &tq->tq_prio_list);

	spin_unlock_irqrestore(&tq->tq_lock, flags);

	wake_up(&tq->tq_work_waitq);
}

#ifdef HAVE_KERNEL_TIMER_FUNCTION_TIMER_LIST
static void
task_expire(struct timer_list *tl)
{
	taskq_ent_t *t = from_timer(t, tl, tqent_timer);
	task_expire_impl(t);
}
#else
static void
task_expire(unsigned long data)
{
	task_expire_impl((taskq_ent_t *)data);
}
#endif

/*
 * Returns the lowest incomplete taskqid_t.  The taskqid_t may
 * be queued on the pending list, on the priority list, on the
 * delay list, or on the work list currently being handled, but
 * it is not 100% complete yet.
 */
static taskqid_t
taskq_lowest_id(taskq_t *tq)
{
	taskqid_t lowest_id = tq->tq_next_id;
	taskq_ent_t *t;
	taskq_thread_t *tqt;

	ASSERT(tq);

	if (!list_empty(&tq->tq_pend_list)) {
		t = list_entry(tq->tq_pend_list.next, taskq_ent_t, tqent_list);
		lowest_id = MIN(lowest_id, t->tqent_id);
	}

	if (!list_empty(&tq->tq_prio_list)) {
		t = list_entry(tq->tq_prio_list.next, taskq_ent_t, tqent_list);
		lowest_id = MIN(lowest_id, t->tqent_id);
	}

	if (!list_empty(&tq->tq_delay_list)) {
		t = list_entry(tq->tq_delay_list.next, taskq_ent_t, tqent_list);
		lowest_id = MIN(lowest_id, t->tqent_id);
	}

	if (!list_empty(&tq->tq_active_list)) {
		tqt = list_entry(tq->tq_active_list.next, taskq_thread_t,
		    tqt_active_list);
		ASSERT(tqt->tqt_id != TASKQID_INVALID);
		lowest_id = MIN(lowest_id, tqt->tqt_id);
	}

	return (lowest_id);
}

/*
 * Insert a task into a list keeping the list sorted by increasing taskqid.
 */
static void
taskq_insert_in_order(taskq_t *tq, taskq_thread_t *tqt)
{
	taskq_thread_t *w;
	struct list_head *l;

	ASSERT(tq);
	ASSERT(tqt);

	list_for_each_prev(l, &tq->tq_active_list) {
		w = list_entry(l, taskq_thread_t, tqt_active_list);
		if (w->tqt_id < tqt->tqt_id) {
			list_add(&tqt->tqt_active_list, l);
			break;
		}
	}
	if (l == &tq->tq_active_list)
		list_add(&tqt->tqt_active_list, &tq->tq_active_list);
}

/*
 * Find and return a task from the given list if it exists.  The list
 * must be in lowest to highest task id order.
 */
static taskq_ent_t *
taskq_find_list(taskq_t *tq, struct list_head *lh, taskqid_t id)
{
	struct list_head *l;
	taskq_ent_t *t;

	list_for_each(l, lh) {
		t = list_entry(l, taskq_ent_t, tqent_list);

		if (t->tqent_id == id)
			return (t);

		if (t->tqent_id > id)
			break;
	}

	return (NULL);
}

/*
 * Find an already dispatched task given the task id regardless of what
 * state it is in.  If a task is still pending it will be returned.
 * If a task is executing, then -EBUSY will be returned instead.
 * If the task has already been run then NULL is returned.
 */
static taskq_ent_t *
taskq_find(taskq_t *tq, taskqid_t id)
{
	taskq_thread_t *tqt;
	struct list_head *l;
	taskq_ent_t *t;

	t = taskq_find_list(tq, &tq->tq_delay_list, id);
	if (t)
		return (t);

	t = taskq_find_list(tq, &tq->tq_prio_list, id);
	if (t)
		return (t);

	t = taskq_find_list(tq, &tq->tq_pend_list, id);
	if (t)
		return (t);

	list_for_each(l, &tq->tq_active_list) {
		tqt = list_entry(l, taskq_thread_t, tqt_active_list);
		if (tqt->tqt_id == id) {
			/*
			 * Instead of returning tqt_task, we just return a non
			 * NULL value to prevent misuse, since tqt_task only
			 * has two valid fields.
			 */
			return (ERR_PTR(-EBUSY));
		}
	}

	return (NULL);
}

/*
 * Theory for the taskq_wait_id(), taskq_wait_outstanding(), and
 * taskq_wait() functions below.
 *
 * Taskq waiting is accomplished by tracking the lowest outstanding task
 * id and the next available task id.  As tasks are dispatched they are
 * added to the tail of the pending, priority, or delay lists.  As worker
 * threads become available the tasks are removed from the heads of these
 * lists and linked to the worker threads.  This ensures the lists are
 * kept sorted by lowest to highest task id.
 *
 * Therefore the lowest outstanding task id can be quickly determined by
 * checking the head item from all of these lists.  This value is stored
 * with the taskq as the lowest id.  It only needs to be recalculated when
 * either the task with the current lowest id completes or is canceled.
 *
 * By blocking until the lowest task id exceeds the passed task id the
 * taskq_wait_outstanding() function can be easily implemented.  Similarly,
 * by blocking until the lowest task id matches the next task id taskq_wait()
 * can be implemented.
 *
 * Callers should be aware that when there are multiple worked threads it
 * is possible for larger task ids to complete before smaller ones.  Also
 * when the taskq contains delay tasks with small task ids callers may
 * block for a considerable length of time waiting for them to expire and
 * execute.
 */
static int
taskq_wait_id_check(taskq_t *tq, taskqid_t id)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave_nested(&tq->tq_lock, flags, tq->tq_lock_class);
	rc = (taskq_find(tq, id) == NULL);
	spin_unlock_irqrestore(&tq->tq_lock, flags);

	return (rc);
}

/*
 * The taskq_wait_id() function blocks until the passed task id completes.
 * This does not guarantee that all lower task ids have completed.
 */
void
taskq_wait_id(taskq_t *tq, taskqid_t id)
{
	wait_event(tq->tq_wait_waitq, taskq_wait_id_check(tq, id));
}
EXPORT_SYMBOL(taskq_wait_id);

static int
taskq_wait_outstanding_check(taskq_t *tq, taskqid_t id)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave_nested(&tq->tq_lock, flags, tq->tq_lock_class);
	rc = (id < tq->tq_lowest_id);
	spin_unlock_irqrestore(&tq->tq_lock, flags);

	return (rc);
}

/*
 * The taskq_wait_outstanding() function will block until all tasks with a
 * lower taskqid than the passed 'id' have been completed.  Note that all
 * task id's are assigned monotonically at dispatch time.  Zero may be
 * passed for the id to indicate all tasks dispatch up to this point,
 * but not after, should be waited for.
 */
void
taskq_wait_outstanding(taskq_t *tq, taskqid_t id)
{
	id = id ? id : tq->tq_next_id - 1;
	wait_event(tq->tq_wait_waitq, taskq_wait_outstanding_check(tq, id));
}
EXPORT_SYMBOL(taskq_wait_outstanding);

static int
taskq_wait_check(taskq_t *tq)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave_nested(&tq->tq_lock, flags, tq->tq_lock_class);
	rc = (tq->tq_lowest_id == tq->tq_next_id);
	spin_unlock_irqrestore(&tq->tq_lock, flags);

	return (rc);
}

/*
 * The taskq_wait() function will block until the taskq is empty.
 * This means that if a taskq re-dispatches work to itself taskq_wait()
 * callers will block indefinitely.
 */
void
taskq_wait(taskq_t *tq)
{
	wait_event(tq->tq_wait_waitq, taskq_wait_check(tq));
}
EXPORT_SYMBOL(taskq_wait);

int
taskq_member(taskq_t *tq, kthread_t *t)
{
	return (tq == (taskq_t *)tsd_get_by_thread(taskq_tsd, t));
}
EXPORT_SYMBOL(taskq_member);

/*
 * Cancel an already dispatched task given the task id.  Still pending tasks
 * will be immediately canceled, and if the task is active the function will
 * block until it completes.  Preallocated tasks which are canceled must be
 * freed by the caller.
 */
int
taskq_cancel_id(taskq_t *tq, taskqid_t id)
{
	taskq_ent_t *t;
	int rc = ENOENT;
	unsigned long flags;

	ASSERT(tq);

	spin_lock_irqsave_nested(&tq->tq_lock, flags, tq->tq_lock_class);
	t = taskq_find(tq, id);
	if (t && t != ERR_PTR(-EBUSY)) {
		list_del_init(&t->tqent_list);
		t->tqent_flags |= TQENT_FLAG_CANCEL;

		/*
		 * When canceling the lowest outstanding task id we
		 * must recalculate the new lowest outstanding id.
		 */
		if (tq->tq_lowest_id == t->tqent_id) {
			tq->tq_lowest_id = taskq_lowest_id(tq);
			ASSERT3S(tq->tq_lowest_id, >, t->tqent_id);
		}

		/*
		 * The task_expire() function takes the tq->tq_lock so drop
		 * drop the lock before synchronously cancelling the timer.
		 */
		if (timer_pending(&t->tqent_timer)) {
			spin_unlock_irqrestore(&tq->tq_lock, flags);
			del_timer_sync(&t->tqent_timer);
			spin_lock_irqsave_nested(&tq->tq_lock, flags,
			    tq->tq_lock_class);
		}

		if (!(t->tqent_flags & TQENT_FLAG_PREALLOC))
			task_done(tq, t);

		rc = 0;
	}
	spin_unlock_irqrestore(&tq->tq_lock, flags);

	if (t == ERR_PTR(-EBUSY)) {
		taskq_wait_id(tq, id);
		rc = EBUSY;
	}

	return (rc);
}
EXPORT_SYMBOL(taskq_cancel_id);

static int taskq_thread_spawn(taskq_t *tq);

taskqid_t
taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, uint_t flags)
{
	taskq_ent_t *t;
	taskqid_t rc = TASKQID_INVALID;
	unsigned long irqflags;

	ASSERT(tq);
	ASSERT(func);

	spin_lock_irqsave_nested(&tq->tq_lock, irqflags, tq->tq_lock_class);

	/* Taskq being destroyed and all tasks drained */
	if (!(tq->tq_flags & TASKQ_ACTIVE))
		goto out;

	/* Do not queue the task unless there is idle thread for it */
	ASSERT(tq->tq_nactive <= tq->tq_nthreads);
	if ((flags & TQ_NOQUEUE) && (tq->tq_nactive == tq->tq_nthreads)) {
		/* Dynamic taskq may be able to spawn another thread */
		if (!(tq->tq_flags & TASKQ_DYNAMIC) ||
		    taskq_thread_spawn(tq) == 0)
			goto out;
	}

	if ((t = task_alloc(tq, flags, &irqflags)) == NULL)
		goto out;

	spin_lock(&t->tqent_lock);

	/* Queue to the front of the list to enforce TQ_NOQUEUE semantics */
	if (flags & TQ_NOQUEUE)
		list_add(&t->tqent_list, &tq->tq_prio_list);
	/* Queue to the priority list instead of the pending list */
	else if (flags & TQ_FRONT)
		list_add_tail(&t->tqent_list, &tq->tq_prio_list);
	else
		list_add_tail(&t->tqent_list, &tq->tq_pend_list);

	t->tqent_id = rc = tq->tq_next_id;
	tq->tq_next_id++;
	t->tqent_func = func;
	t->tqent_arg = arg;
	t->tqent_taskq = tq;
#ifndef HAVE_KERNEL_TIMER_FUNCTION_TIMER_LIST
	t->tqent_timer.data = 0;
#endif
	t->tqent_timer.function = NULL;
	t->tqent_timer.expires = 0;
	t->tqent_birth = jiffies;

	ASSERT(!(t->tqent_flags & TQENT_FLAG_PREALLOC));

	spin_unlock(&t->tqent_lock);

	wake_up(&tq->tq_work_waitq);
out:
	/* Spawn additional taskq threads if required. */
	if (!(flags & TQ_NOQUEUE) && tq->tq_nactive == tq->tq_nthreads)
		(void) taskq_thread_spawn(tq);

	spin_unlock_irqrestore(&tq->tq_lock, irqflags);
	return (rc);
}
EXPORT_SYMBOL(taskq_dispatch);

taskqid_t
taskq_dispatch_delay(taskq_t *tq, task_func_t func, void *arg,
    uint_t flags, clock_t expire_time)
{
	taskqid_t rc = TASKQID_INVALID;
	taskq_ent_t *t;
	unsigned long irqflags;

	ASSERT(tq);
	ASSERT(func);

	spin_lock_irqsave_nested(&tq->tq_lock, irqflags, tq->tq_lock_class);

	/* Taskq being destroyed and all tasks drained */
	if (!(tq->tq_flags & TASKQ_ACTIVE))
		goto out;

	if ((t = task_alloc(tq, flags, &irqflags)) == NULL)
		goto out;

	spin_lock(&t->tqent_lock);

	/* Queue to the delay list for subsequent execution */
	list_add_tail(&t->tqent_list, &tq->tq_delay_list);

	t->tqent_id = rc = tq->tq_next_id;
	tq->tq_next_id++;
	t->tqent_func = func;
	t->tqent_arg = arg;
	t->tqent_taskq = tq;
#ifndef HAVE_KERNEL_TIMER_FUNCTION_TIMER_LIST
	t->tqent_timer.data = (unsigned long)t;
#endif
	t->tqent_timer.function = task_expire;
	t->tqent_timer.expires = (unsigned long)expire_time;
	add_timer(&t->tqent_timer);

	ASSERT(!(t->tqent_flags & TQENT_FLAG_PREALLOC));

	spin_unlock(&t->tqent_lock);
out:
	/* Spawn additional taskq threads if required. */
	if (tq->tq_nactive == tq->tq_nthreads)
		(void) taskq_thread_spawn(tq);
	spin_unlock_irqrestore(&tq->tq_lock, irqflags);
	return (rc);
}
EXPORT_SYMBOL(taskq_dispatch_delay);

void
taskq_dispatch_ent(taskq_t *tq, task_func_t func, void *arg, uint_t flags,
    taskq_ent_t *t)
{
	unsigned long irqflags;
	ASSERT(tq);
	ASSERT(func);

	spin_lock_irqsave_nested(&tq->tq_lock, irqflags,
	    tq->tq_lock_class);

	/* Taskq being destroyed and all tasks drained */
	if (!(tq->tq_flags & TASKQ_ACTIVE)) {
		t->tqent_id = TASKQID_INVALID;
		goto out;
	}

	if ((flags & TQ_NOQUEUE) && (tq->tq_nactive == tq->tq_nthreads)) {
		/* Dynamic taskq may be able to spawn another thread */
		if (!(tq->tq_flags & TASKQ_DYNAMIC) ||
		    taskq_thread_spawn(tq) == 0)
			goto out2;
		flags |= TQ_FRONT;
	}

	spin_lock(&t->tqent_lock);

	/*
	 * Make sure the entry is not on some other taskq; it is important to
	 * ASSERT() under lock
	 */
	ASSERT(taskq_empty_ent(t));

	/*
	 * Mark it as a prealloc'd task.  This is important
	 * to ensure that we don't free it later.
	 */
	t->tqent_flags |= TQENT_FLAG_PREALLOC;

	/* Queue to the priority list instead of the pending list */
	if (flags & TQ_FRONT)
		list_add_tail(&t->tqent_list, &tq->tq_prio_list);
	else
		list_add_tail(&t->tqent_list, &tq->tq_pend_list);

	t->tqent_id = tq->tq_next_id;
	tq->tq_next_id++;
	t->tqent_func = func;
	t->tqent_arg = arg;
	t->tqent_taskq = tq;
	t->tqent_birth = jiffies;

	spin_unlock(&t->tqent_lock);

	wake_up(&tq->tq_work_waitq);
out:
	/* Spawn additional taskq threads if required. */
	if (tq->tq_nactive == tq->tq_nthreads)
		(void) taskq_thread_spawn(tq);
out2:
	spin_unlock_irqrestore(&tq->tq_lock, irqflags);
}
EXPORT_SYMBOL(taskq_dispatch_ent);

int
taskq_empty_ent(taskq_ent_t *t)
{
	return (list_empty(&t->tqent_list));
}
EXPORT_SYMBOL(taskq_empty_ent);

void
taskq_init_ent(taskq_ent_t *t)
{
	spin_lock_init(&t->tqent_lock);
	init_waitqueue_head(&t->tqent_waitq);
#ifdef HAVE_KERNEL_TIMER_FUNCTION_TIMER_LIST
	timer_setup(&t->tqent_timer, NULL, 0);
#else
	init_timer(&t->tqent_timer);
#endif
	INIT_LIST_HEAD(&t->tqent_list);
	t->tqent_id = 0;
	t->tqent_func = NULL;
	t->tqent_arg = NULL;
	t->tqent_flags = 0;
	t->tqent_taskq = NULL;
}
EXPORT_SYMBOL(taskq_init_ent);

/*
 * Return the next pending task, preference is given to tasks on the
 * priority list which were dispatched with TQ_FRONT.
 */
static taskq_ent_t *
taskq_next_ent(taskq_t *tq)
{
	struct list_head *list;

	if (!list_empty(&tq->tq_prio_list))
		list = &tq->tq_prio_list;
	else if (!list_empty(&tq->tq_pend_list))
		list = &tq->tq_pend_list;
	else
		return (NULL);

	return (list_entry(list->next, taskq_ent_t, tqent_list));
}

/*
 * Spawns a new thread for the specified taskq.
 */
static void
taskq_thread_spawn_task(void *arg)
{
	taskq_t *tq = (taskq_t *)arg;
	unsigned long flags;

	if (taskq_thread_create(tq) == NULL) {
		/* restore spawning count if failed */
		spin_lock_irqsave_nested(&tq->tq_lock, flags,
		    tq->tq_lock_class);
		tq->tq_nspawn--;
		spin_unlock_irqrestore(&tq->tq_lock, flags);
	}
}

/*
 * Spawn addition threads for dynamic taskqs (TASKQ_DYNAMIC) the current
 * number of threads is insufficient to handle the pending tasks.  These
 * new threads must be created by the dedicated dynamic_taskq to avoid
 * deadlocks between thread creation and memory reclaim.  The system_taskq
 * which is also a dynamic taskq cannot be safely used for this.
 */
static int
taskq_thread_spawn(taskq_t *tq)
{
	int spawning = 0;

	if (!(tq->tq_flags & TASKQ_DYNAMIC))
		return (0);

	if ((tq->tq_nthreads + tq->tq_nspawn < tq->tq_maxthreads) &&
	    (tq->tq_flags & TASKQ_ACTIVE)) {
		spawning = (++tq->tq_nspawn);
		taskq_dispatch(dynamic_taskq, taskq_thread_spawn_task,
		    tq, TQ_NOSLEEP);
	}

	return (spawning);
}

/*
 * Threads in a dynamic taskq should only exit once it has been completely
 * drained and no other threads are actively servicing tasks.  This prevents
 * threads from being created and destroyed more than is required.
 *
 * The first thread is the thread list is treated as the primary thread.
 * There is nothing special about the primary thread but in order to avoid
 * all the taskq pids from changing we opt to make it long running.
 */
static int
taskq_thread_should_stop(taskq_t *tq, taskq_thread_t *tqt)
{
	if (!(tq->tq_flags & TASKQ_DYNAMIC))
		return (0);

	if (list_first_entry(&(tq->tq_thread_list), taskq_thread_t,
	    tqt_thread_list) == tqt)
		return (0);

	return
	    ((tq->tq_nspawn == 0) &&	/* No threads are being spawned */
	    (tq->tq_nactive == 0) &&	/* No threads are handling tasks */
	    (tq->tq_nthreads > 1) &&	/* More than 1 thread is running */
	    (!taskq_next_ent(tq)) &&	/* There are no pending tasks */
	    (spl_taskq_thread_dynamic)); /* Dynamic taskqs are allowed */
}

static int
taskq_thread(void *args)
{
	DECLARE_WAITQUEUE(wait, current);
	sigset_t blocked;
	taskq_thread_t *tqt = args;
	taskq_t *tq;
	taskq_ent_t *t;
	int seq_tasks = 0;
	unsigned long flags;
	taskq_ent_t dup_task = {};

	ASSERT(tqt);
	ASSERT(tqt->tqt_tq);
	tq = tqt->tqt_tq;
	current->flags |= PF_NOFREEZE;

	(void) spl_fstrans_mark();

	sigfillset(&blocked);
	sigprocmask(SIG_BLOCK, &blocked, NULL);
	flush_signals(current);

	tsd_set(taskq_tsd, tq);
	spin_lock_irqsave_nested(&tq->tq_lock, flags, tq->tq_lock_class);
	/*
	 * If we are dynamically spawned, decrease spawning count. Note that
	 * we could be created during taskq_create, in which case we shouldn't
	 * do the decrement. But it's fine because taskq_create will reset
	 * tq_nspawn later.
	 */
	if (tq->tq_flags & TASKQ_DYNAMIC)
		tq->tq_nspawn--;

	/* Immediately exit if more threads than allowed were created. */
	if (tq->tq_nthreads >= tq->tq_maxthreads)
		goto error;

	tq->tq_nthreads++;
	list_add_tail(&tqt->tqt_thread_list, &tq->tq_thread_list);
	wake_up(&tq->tq_wait_waitq);
	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {

		if (list_empty(&tq->tq_pend_list) &&
		    list_empty(&tq->tq_prio_list)) {

			if (taskq_thread_should_stop(tq, tqt)) {
				wake_up_all(&tq->tq_wait_waitq);
				break;
			}

			add_wait_queue_exclusive(&tq->tq_work_waitq, &wait);
			spin_unlock_irqrestore(&tq->tq_lock, flags);

			schedule();
			seq_tasks = 0;

			spin_lock_irqsave_nested(&tq->tq_lock, flags,
			    tq->tq_lock_class);
			remove_wait_queue(&tq->tq_work_waitq, &wait);
		} else {
			__set_current_state(TASK_RUNNING);
		}

		if ((t = taskq_next_ent(tq)) != NULL) {
			list_del_init(&t->tqent_list);

			/*
			 * A TQENT_FLAG_PREALLOC task may be reused or freed
			 * during the task function call. Store tqent_id and
			 * tqent_flags here.
			 *
			 * Also use an on stack taskq_ent_t for tqt_task
			 * assignment in this case. We only populate the two
			 * fields used by the only user in taskq proc file.
			 */
			tqt->tqt_id = t->tqent_id;
			tqt->tqt_flags = t->tqent_flags;

			if (t->tqent_flags & TQENT_FLAG_PREALLOC) {
				dup_task.tqent_func = t->tqent_func;
				dup_task.tqent_arg = t->tqent_arg;
				t = &dup_task;
			}
			tqt->tqt_task = t;

			taskq_insert_in_order(tq, tqt);
			tq->tq_nactive++;
			spin_unlock_irqrestore(&tq->tq_lock, flags);

			/* Perform the requested task */
			t->tqent_func(t->tqent_arg);

			spin_lock_irqsave_nested(&tq->tq_lock, flags,
			    tq->tq_lock_class);
			tq->tq_nactive--;
			list_del_init(&tqt->tqt_active_list);
			tqt->tqt_task = NULL;

			/* For prealloc'd tasks, we don't free anything. */
			if (!(tqt->tqt_flags & TQENT_FLAG_PREALLOC))
				task_done(tq, t);

			/*
			 * When the current lowest outstanding taskqid is
			 * done calculate the new lowest outstanding id
			 */
			if (tq->tq_lowest_id == tqt->tqt_id) {
				tq->tq_lowest_id = taskq_lowest_id(tq);
				ASSERT3S(tq->tq_lowest_id, >, tqt->tqt_id);
			}

			/* Spawn additional taskq threads if required. */
			if ((++seq_tasks) > spl_taskq_thread_sequential &&
			    taskq_thread_spawn(tq))
				seq_tasks = 0;

			tqt->tqt_id = TASKQID_INVALID;
			tqt->tqt_flags = 0;
			wake_up_all(&tq->tq_wait_waitq);
		} else {
			if (taskq_thread_should_stop(tq, tqt))
				break;
		}

		set_current_state(TASK_INTERRUPTIBLE);

	}

	__set_current_state(TASK_RUNNING);
	tq->tq_nthreads--;
	list_del_init(&tqt->tqt_thread_list);
error:
	kmem_free(tqt, sizeof (taskq_thread_t));
	spin_unlock_irqrestore(&tq->tq_lock, flags);

	tsd_set(taskq_tsd, NULL);

	return (0);
}

static taskq_thread_t *
taskq_thread_create(taskq_t *tq)
{
	static int last_used_cpu = 0;
	taskq_thread_t *tqt;

	tqt = kmem_alloc(sizeof (*tqt), KM_PUSHPAGE);
	INIT_LIST_HEAD(&tqt->tqt_thread_list);
	INIT_LIST_HEAD(&tqt->tqt_active_list);
	tqt->tqt_tq = tq;
	tqt->tqt_id = TASKQID_INVALID;

	tqt->tqt_thread = spl_kthread_create(taskq_thread, tqt,
	    "%s", tq->tq_name);
	if (tqt->tqt_thread == NULL) {
		kmem_free(tqt, sizeof (taskq_thread_t));
		return (NULL);
	}

	if (spl_taskq_thread_bind) {
		last_used_cpu = (last_used_cpu + 1) % num_online_cpus();
		kthread_bind(tqt->tqt_thread, last_used_cpu);
	}

	if (spl_taskq_thread_priority)
		set_user_nice(tqt->tqt_thread, PRIO_TO_NICE(tq->tq_pri));

	wake_up_process(tqt->tqt_thread);

	return (tqt);
}

taskq_t *
taskq_create(const char *name, int nthreads, pri_t pri,
    int minalloc, int maxalloc, uint_t flags)
{
	taskq_t *tq;
	taskq_thread_t *tqt;
	int count = 0, rc = 0, i;
	unsigned long irqflags;

	ASSERT(name != NULL);
	ASSERT(minalloc >= 0);
	ASSERT(maxalloc <= INT_MAX);
	ASSERT(!(flags & (TASKQ_CPR_SAFE))); /* Unsupported */

	/* Scale the number of threads using nthreads as a percentage */
	if (flags & TASKQ_THREADS_CPU_PCT) {
		ASSERT(nthreads <= 100);
		ASSERT(nthreads >= 0);
		nthreads = MIN(nthreads, 100);
		nthreads = MAX(nthreads, 0);
		nthreads = MAX((num_online_cpus() * nthreads) / 100, 1);
	}

	tq = kmem_alloc(sizeof (*tq), KM_PUSHPAGE);
	if (tq == NULL)
		return (NULL);

	spin_lock_init(&tq->tq_lock);
	INIT_LIST_HEAD(&tq->tq_thread_list);
	INIT_LIST_HEAD(&tq->tq_active_list);
	tq->tq_name = strdup(name);
	tq->tq_nactive = 0;
	tq->tq_nthreads = 0;
	tq->tq_nspawn = 0;
	tq->tq_maxthreads = nthreads;
	tq->tq_pri = pri;
	tq->tq_minalloc = minalloc;
	tq->tq_maxalloc = maxalloc;
	tq->tq_nalloc = 0;
	tq->tq_flags = (flags | TASKQ_ACTIVE);
	tq->tq_next_id = TASKQID_INITIAL;
	tq->tq_lowest_id = TASKQID_INITIAL;
	INIT_LIST_HEAD(&tq->tq_free_list);
	INIT_LIST_HEAD(&tq->tq_pend_list);
	INIT_LIST_HEAD(&tq->tq_prio_list);
	INIT_LIST_HEAD(&tq->tq_delay_list);
	init_waitqueue_head(&tq->tq_work_waitq);
	init_waitqueue_head(&tq->tq_wait_waitq);
	tq->tq_lock_class = TQ_LOCK_GENERAL;
	INIT_LIST_HEAD(&tq->tq_taskqs);

	if (flags & TASKQ_PREPOPULATE) {
		spin_lock_irqsave_nested(&tq->tq_lock, irqflags,
		    tq->tq_lock_class);

		for (i = 0; i < minalloc; i++)
			task_done(tq, task_alloc(tq, TQ_PUSHPAGE | TQ_NEW,
			    &irqflags));

		spin_unlock_irqrestore(&tq->tq_lock, irqflags);
	}

	if ((flags & TASKQ_DYNAMIC) && spl_taskq_thread_dynamic)
		nthreads = 1;

	for (i = 0; i < nthreads; i++) {
		tqt = taskq_thread_create(tq);
		if (tqt == NULL)
			rc = 1;
		else
			count++;
	}

	/* Wait for all threads to be started before potential destroy */
	wait_event(tq->tq_wait_waitq, tq->tq_nthreads == count);
	/*
	 * taskq_thread might have touched nspawn, but we don't want them to
	 * because they're not dynamically spawned. So we reset it to 0
	 */
	tq->tq_nspawn = 0;

	if (rc) {
		taskq_destroy(tq);
		tq = NULL;
	} else {
		down_write(&tq_list_sem);
		tq->tq_instance = taskq_find_by_name(name) + 1;
		list_add_tail(&tq->tq_taskqs, &tq_list);
		up_write(&tq_list_sem);
	}

	return (tq);
}
EXPORT_SYMBOL(taskq_create);

void
taskq_destroy(taskq_t *tq)
{
	struct task_struct *thread;
	taskq_thread_t *tqt;
	taskq_ent_t *t;
	unsigned long flags;

	ASSERT(tq);
	spin_lock_irqsave_nested(&tq->tq_lock, flags, tq->tq_lock_class);
	tq->tq_flags &= ~TASKQ_ACTIVE;
	spin_unlock_irqrestore(&tq->tq_lock, flags);

	/*
	 * When TASKQ_ACTIVE is clear new tasks may not be added nor may
	 * new worker threads be spawned for dynamic taskq.
	 */
	if (dynamic_taskq != NULL)
		taskq_wait_outstanding(dynamic_taskq, 0);

	taskq_wait(tq);

	/* remove taskq from global list used by the kstats */
	down_write(&tq_list_sem);
	list_del(&tq->tq_taskqs);
	up_write(&tq_list_sem);

	spin_lock_irqsave_nested(&tq->tq_lock, flags, tq->tq_lock_class);
	/* wait for spawning threads to insert themselves to the list */
	while (tq->tq_nspawn) {
		spin_unlock_irqrestore(&tq->tq_lock, flags);
		schedule_timeout_interruptible(1);
		spin_lock_irqsave_nested(&tq->tq_lock, flags,
		    tq->tq_lock_class);
	}

	/*
	 * Signal each thread to exit and block until it does.  Each thread
	 * is responsible for removing itself from the list and freeing its
	 * taskq_thread_t.  This allows for idle threads to opt to remove
	 * themselves from the taskq.  They can be recreated as needed.
	 */
	while (!list_empty(&tq->tq_thread_list)) {
		tqt = list_entry(tq->tq_thread_list.next,
		    taskq_thread_t, tqt_thread_list);
		thread = tqt->tqt_thread;
		spin_unlock_irqrestore(&tq->tq_lock, flags);

		kthread_stop(thread);

		spin_lock_irqsave_nested(&tq->tq_lock, flags,
		    tq->tq_lock_class);
	}

	while (!list_empty(&tq->tq_free_list)) {
		t = list_entry(tq->tq_free_list.next, taskq_ent_t, tqent_list);

		ASSERT(!(t->tqent_flags & TQENT_FLAG_PREALLOC));

		list_del_init(&t->tqent_list);
		task_free(tq, t);
	}

	ASSERT0(tq->tq_nthreads);
	ASSERT0(tq->tq_nalloc);
	ASSERT0(tq->tq_nspawn);
	ASSERT(list_empty(&tq->tq_thread_list));
	ASSERT(list_empty(&tq->tq_active_list));
	ASSERT(list_empty(&tq->tq_free_list));
	ASSERT(list_empty(&tq->tq_pend_list));
	ASSERT(list_empty(&tq->tq_prio_list));
	ASSERT(list_empty(&tq->tq_delay_list));

	spin_unlock_irqrestore(&tq->tq_lock, flags);

	strfree(tq->tq_name);
	kmem_free(tq, sizeof (taskq_t));
}
EXPORT_SYMBOL(taskq_destroy);


static unsigned int spl_taskq_kick = 0;

/*
 * 2.6.36 API Change
 * module_param_cb is introduced to take kernel_param_ops and
 * module_param_call is marked as obsolete. Also set and get operations
 * were changed to take a 'const struct kernel_param *'.
 */
static int
#ifdef module_param_cb
param_set_taskq_kick(const char *val, const struct kernel_param *kp)
#else
param_set_taskq_kick(const char *val, struct kernel_param *kp)
#endif
{
	int ret;
	taskq_t *tq;
	taskq_ent_t *t;
	unsigned long flags;

	ret = param_set_uint(val, kp);
	if (ret < 0 || !spl_taskq_kick)
		return (ret);
	/* reset value */
	spl_taskq_kick = 0;

	down_read(&tq_list_sem);
	list_for_each_entry(tq, &tq_list, tq_taskqs) {
		spin_lock_irqsave_nested(&tq->tq_lock, flags,
		    tq->tq_lock_class);
		/* Check if the first pending is older than 5 seconds */
		t = taskq_next_ent(tq);
		if (t && time_after(jiffies, t->tqent_birth + 5*HZ)) {
			(void) taskq_thread_spawn(tq);
			printk(KERN_INFO "spl: Kicked taskq %s/%d\n",
			    tq->tq_name, tq->tq_instance);
		}
		spin_unlock_irqrestore(&tq->tq_lock, flags);
	}
	up_read(&tq_list_sem);
	return (ret);
}

#ifdef module_param_cb
static const struct kernel_param_ops param_ops_taskq_kick = {
	.set = param_set_taskq_kick,
	.get = param_get_uint,
};
module_param_cb(spl_taskq_kick, &param_ops_taskq_kick, &spl_taskq_kick, 0644);
#else
module_param_call(spl_taskq_kick, param_set_taskq_kick, param_get_uint,
	&spl_taskq_kick, 0644);
#endif
MODULE_PARM_DESC(spl_taskq_kick,
	"Write nonzero to kick stuck taskqs to spawn more threads");

int
spl_taskq_init(void)
{
	tsd_create(&taskq_tsd, NULL);

	system_taskq = taskq_create("spl_system_taskq", MAX(boot_ncpus, 64),
	    maxclsyspri, boot_ncpus, INT_MAX, TASKQ_PREPOPULATE|TASKQ_DYNAMIC);
	if (system_taskq == NULL)
		return (1);

	system_delay_taskq = taskq_create("spl_delay_taskq", MAX(boot_ncpus, 4),
	    maxclsyspri, boot_ncpus, INT_MAX, TASKQ_PREPOPULATE|TASKQ_DYNAMIC);
	if (system_delay_taskq == NULL) {
		taskq_destroy(system_taskq);
		return (1);
	}

	dynamic_taskq = taskq_create("spl_dynamic_taskq", 1,
	    maxclsyspri, boot_ncpus, INT_MAX, TASKQ_PREPOPULATE);
	if (dynamic_taskq == NULL) {
		taskq_destroy(system_taskq);
		taskq_destroy(system_delay_taskq);
		return (1);
	}

	/*
	 * This is used to annotate tq_lock, so
	 *   taskq_dispatch -> taskq_thread_spawn -> taskq_dispatch
	 * does not trigger a lockdep warning re: possible recursive locking
	 */
	dynamic_taskq->tq_lock_class = TQ_LOCK_DYNAMIC;

	return (0);
}

void
spl_taskq_fini(void)
{
	taskq_destroy(dynamic_taskq);
	dynamic_taskq = NULL;

	taskq_destroy(system_delay_taskq);
	system_delay_taskq = NULL;

	taskq_destroy(system_taskq);
	system_taskq = NULL;

	tsd_destroy(&taskq_tsd);
}

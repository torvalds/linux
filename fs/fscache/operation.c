/* FS-Cache worker operation management routines
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * See Documentation/filesystems/caching/operations.txt
 */

#define FSCACHE_DEBUG_LEVEL OPERATION
#include <linux/module.h>
#include <linux/seq_file.h>
#include "internal.h"

atomic_t fscache_op_debug_id;
EXPORT_SYMBOL(fscache_op_debug_id);

/**
 * fscache_enqueue_operation - Enqueue an operation for processing
 * @op: The operation to enqueue
 *
 * Enqueue an operation for processing by the FS-Cache thread pool.
 *
 * This will get its own ref on the object.
 */
void fscache_enqueue_operation(struct fscache_operation *op)
{
	_enter("{OBJ%x OP%x,%u}",
	       op->object->debug_id, op->debug_id, atomic_read(&op->usage));

	fscache_set_op_state(op, "EnQ");

	ASSERT(list_empty(&op->pend_link));
	ASSERT(op->processor != NULL);
	ASSERTCMP(op->object->state, >=, FSCACHE_OBJECT_AVAILABLE);
	ASSERTCMP(atomic_read(&op->usage), >, 0);

	fscache_stat(&fscache_n_op_enqueue);
	switch (op->flags & FSCACHE_OP_TYPE) {
	case FSCACHE_OP_FAST:
		_debug("queue fast");
		atomic_inc(&op->usage);
		if (!schedule_work(&op->fast_work))
			fscache_put_operation(op);
		break;
	case FSCACHE_OP_SLOW:
		_debug("queue slow");
		slow_work_enqueue(&op->slow_work);
		break;
	case FSCACHE_OP_MYTHREAD:
		_debug("queue for caller's attention");
		break;
	default:
		printk(KERN_ERR "FS-Cache: Unexpected op type %lx",
		       op->flags);
		BUG();
		break;
	}
}
EXPORT_SYMBOL(fscache_enqueue_operation);

/*
 * start an op running
 */
static void fscache_run_op(struct fscache_object *object,
			   struct fscache_operation *op)
{
	fscache_set_op_state(op, "Run");

	object->n_in_progress++;
	if (test_and_clear_bit(FSCACHE_OP_WAITING, &op->flags))
		wake_up_bit(&op->flags, FSCACHE_OP_WAITING);
	if (op->processor)
		fscache_enqueue_operation(op);
	fscache_stat(&fscache_n_op_run);
}

/*
 * submit an exclusive operation for an object
 * - other ops are excluded from running simultaneously with this one
 * - this gets any extra refs it needs on an op
 */
int fscache_submit_exclusive_op(struct fscache_object *object,
				struct fscache_operation *op)
{
	int ret;

	_enter("{OBJ%x OP%x},", object->debug_id, op->debug_id);

	fscache_set_op_state(op, "SubmitX");

	spin_lock(&object->lock);
	ASSERTCMP(object->n_ops, >=, object->n_in_progress);
	ASSERTCMP(object->n_ops, >=, object->n_exclusive);
	ASSERT(list_empty(&op->pend_link));

	ret = -ENOBUFS;
	if (fscache_object_is_active(object)) {
		op->object = object;
		object->n_ops++;
		object->n_exclusive++;	/* reads and writes must wait */

		if (object->n_ops > 0) {
			atomic_inc(&op->usage);
			list_add_tail(&op->pend_link, &object->pending_ops);
			fscache_stat(&fscache_n_op_pend);
		} else if (!list_empty(&object->pending_ops)) {
			atomic_inc(&op->usage);
			list_add_tail(&op->pend_link, &object->pending_ops);
			fscache_stat(&fscache_n_op_pend);
			fscache_start_operations(object);
		} else {
			ASSERTCMP(object->n_in_progress, ==, 0);
			fscache_run_op(object, op);
		}

		/* need to issue a new write op after this */
		clear_bit(FSCACHE_OBJECT_PENDING_WRITE, &object->flags);
		ret = 0;
	} else if (object->state == FSCACHE_OBJECT_CREATING) {
		op->object = object;
		object->n_ops++;
		object->n_exclusive++;	/* reads and writes must wait */
		atomic_inc(&op->usage);
		list_add_tail(&op->pend_link, &object->pending_ops);
		fscache_stat(&fscache_n_op_pend);
		ret = 0;
	} else {
		/* not allowed to submit ops in any other state */
		BUG();
	}

	spin_unlock(&object->lock);
	return ret;
}

/*
 * report an unexpected submission
 */
static void fscache_report_unexpected_submission(struct fscache_object *object,
						 struct fscache_operation *op,
						 unsigned long ostate)
{
	static bool once_only;
	struct fscache_operation *p;
	unsigned n;

	if (once_only)
		return;
	once_only = true;

	kdebug("unexpected submission OP%x [OBJ%x %s]",
	       op->debug_id, object->debug_id,
	       fscache_object_states[object->state]);
	kdebug("objstate=%s [%s]",
	       fscache_object_states[object->state],
	       fscache_object_states[ostate]);
	kdebug("objflags=%lx", object->flags);
	kdebug("objevent=%lx [%lx]", object->events, object->event_mask);
	kdebug("ops=%u inp=%u exc=%u",
	       object->n_ops, object->n_in_progress, object->n_exclusive);

	if (!list_empty(&object->pending_ops)) {
		n = 0;
		list_for_each_entry(p, &object->pending_ops, pend_link) {
			ASSERTCMP(p->object, ==, object);
			kdebug("%p %p", op->processor, op->release);
			n++;
		}

		kdebug("n=%u", n);
	}

	dump_stack();
}

/*
 * submit an operation for an object
 * - objects may be submitted only in the following states:
 *   - during object creation (write ops may be submitted)
 *   - whilst the object is active
 *   - after an I/O error incurred in one of the two above states (op rejected)
 * - this gets any extra refs it needs on an op
 */
int fscache_submit_op(struct fscache_object *object,
		      struct fscache_operation *op)
{
	unsigned long ostate;
	int ret;

	_enter("{OBJ%x OP%x},{%u}",
	       object->debug_id, op->debug_id, atomic_read(&op->usage));

	ASSERTCMP(atomic_read(&op->usage), >, 0);

	fscache_set_op_state(op, "Submit");

	spin_lock(&object->lock);
	ASSERTCMP(object->n_ops, >=, object->n_in_progress);
	ASSERTCMP(object->n_ops, >=, object->n_exclusive);
	ASSERT(list_empty(&op->pend_link));

	ostate = object->state;
	smp_rmb();

	if (fscache_object_is_active(object)) {
		op->object = object;
		object->n_ops++;

		if (object->n_exclusive > 0) {
			atomic_inc(&op->usage);
			list_add_tail(&op->pend_link, &object->pending_ops);
			fscache_stat(&fscache_n_op_pend);
		} else if (!list_empty(&object->pending_ops)) {
			atomic_inc(&op->usage);
			list_add_tail(&op->pend_link, &object->pending_ops);
			fscache_stat(&fscache_n_op_pend);
			fscache_start_operations(object);
		} else {
			ASSERTCMP(object->n_exclusive, ==, 0);
			fscache_run_op(object, op);
		}
		ret = 0;
	} else if (object->state == FSCACHE_OBJECT_CREATING) {
		op->object = object;
		object->n_ops++;
		atomic_inc(&op->usage);
		list_add_tail(&op->pend_link, &object->pending_ops);
		fscache_stat(&fscache_n_op_pend);
		ret = 0;
	} else if (object->state == FSCACHE_OBJECT_DYING ||
		   object->state == FSCACHE_OBJECT_LC_DYING ||
		   object->state == FSCACHE_OBJECT_WITHDRAWING) {
		fscache_stat(&fscache_n_op_rejected);
		ret = -ENOBUFS;
	} else if (!test_bit(FSCACHE_IOERROR, &object->cache->flags)) {
		fscache_report_unexpected_submission(object, op, ostate);
		ASSERT(!fscache_object_is_active(object));
		ret = -ENOBUFS;
	} else {
		ret = -ENOBUFS;
	}

	spin_unlock(&object->lock);
	return ret;
}

/*
 * queue an object for withdrawal on error, aborting all following asynchronous
 * operations
 */
void fscache_abort_object(struct fscache_object *object)
{
	_enter("{OBJ%x}", object->debug_id);

	fscache_raise_event(object, FSCACHE_OBJECT_EV_ERROR);
}

/*
 * jump start the operation processing on an object
 * - caller must hold object->lock
 */
void fscache_start_operations(struct fscache_object *object)
{
	struct fscache_operation *op;
	bool stop = false;

	while (!list_empty(&object->pending_ops) && !stop) {
		op = list_entry(object->pending_ops.next,
				struct fscache_operation, pend_link);

		if (test_bit(FSCACHE_OP_EXCLUSIVE, &op->flags)) {
			if (object->n_in_progress > 0)
				break;
			stop = true;
		}
		list_del_init(&op->pend_link);
		fscache_run_op(object, op);

		/* the pending queue was holding a ref on the object */
		fscache_put_operation(op);
	}

	ASSERTCMP(object->n_in_progress, <=, object->n_ops);

	_debug("woke %d ops on OBJ%x",
	       object->n_in_progress, object->debug_id);
}

/*
 * cancel an operation that's pending on an object
 */
int fscache_cancel_op(struct fscache_operation *op)
{
	struct fscache_object *object = op->object;
	int ret;

	_enter("OBJ%x OP%x}", op->object->debug_id, op->debug_id);

	spin_lock(&object->lock);

	ret = -EBUSY;
	if (!list_empty(&op->pend_link)) {
		fscache_stat(&fscache_n_op_cancelled);
		list_del_init(&op->pend_link);
		object->n_ops--;
		if (test_bit(FSCACHE_OP_EXCLUSIVE, &op->flags))
			object->n_exclusive--;
		if (test_and_clear_bit(FSCACHE_OP_WAITING, &op->flags))
			wake_up_bit(&op->flags, FSCACHE_OP_WAITING);
		fscache_put_operation(op);
		ret = 0;
	}

	spin_unlock(&object->lock);
	_leave(" = %d", ret);
	return ret;
}

/*
 * release an operation
 * - queues pending ops if this is the last in-progress op
 */
void fscache_put_operation(struct fscache_operation *op)
{
	struct fscache_object *object;
	struct fscache_cache *cache;

	_enter("{OBJ%x OP%x,%d}",
	       op->object->debug_id, op->debug_id, atomic_read(&op->usage));

	ASSERTCMP(atomic_read(&op->usage), >, 0);

	if (!atomic_dec_and_test(&op->usage))
		return;

	fscache_set_op_state(op, "Put");

	_debug("PUT OP");
	if (test_and_set_bit(FSCACHE_OP_DEAD, &op->flags))
		BUG();

	fscache_stat(&fscache_n_op_release);

	if (op->release) {
		op->release(op);
		op->release = NULL;
	}

	object = op->object;

	if (test_bit(FSCACHE_OP_DEC_READ_CNT, &op->flags))
		atomic_dec(&object->n_reads);

	/* now... we may get called with the object spinlock held, so we
	 * complete the cleanup here only if we can immediately acquire the
	 * lock, and defer it otherwise */
	if (!spin_trylock(&object->lock)) {
		_debug("defer put");
		fscache_stat(&fscache_n_op_deferred_release);

		cache = object->cache;
		spin_lock(&cache->op_gc_list_lock);
		list_add_tail(&op->pend_link, &cache->op_gc_list);
		spin_unlock(&cache->op_gc_list_lock);
		schedule_work(&cache->op_gc);
		_leave(" [defer]");
		return;
	}

	if (test_bit(FSCACHE_OP_EXCLUSIVE, &op->flags)) {
		ASSERTCMP(object->n_exclusive, >, 0);
		object->n_exclusive--;
	}

	ASSERTCMP(object->n_in_progress, >, 0);
	object->n_in_progress--;
	if (object->n_in_progress == 0)
		fscache_start_operations(object);

	ASSERTCMP(object->n_ops, >, 0);
	object->n_ops--;
	if (object->n_ops == 0)
		fscache_raise_event(object, FSCACHE_OBJECT_EV_CLEARED);

	spin_unlock(&object->lock);

	kfree(op);
	_leave(" [done]");
}
EXPORT_SYMBOL(fscache_put_operation);

/*
 * garbage collect operations that have had their release deferred
 */
void fscache_operation_gc(struct work_struct *work)
{
	struct fscache_operation *op;
	struct fscache_object *object;
	struct fscache_cache *cache =
		container_of(work, struct fscache_cache, op_gc);
	int count = 0;

	_enter("");

	do {
		spin_lock(&cache->op_gc_list_lock);
		if (list_empty(&cache->op_gc_list)) {
			spin_unlock(&cache->op_gc_list_lock);
			break;
		}

		op = list_entry(cache->op_gc_list.next,
				struct fscache_operation, pend_link);
		list_del(&op->pend_link);
		spin_unlock(&cache->op_gc_list_lock);

		object = op->object;

		_debug("GC DEFERRED REL OBJ%x OP%x",
		       object->debug_id, op->debug_id);
		fscache_stat(&fscache_n_op_gc);

		ASSERTCMP(atomic_read(&op->usage), ==, 0);

		spin_lock(&object->lock);
		if (test_bit(FSCACHE_OP_EXCLUSIVE, &op->flags)) {
			ASSERTCMP(object->n_exclusive, >, 0);
			object->n_exclusive--;
		}

		ASSERTCMP(object->n_in_progress, >, 0);
		object->n_in_progress--;
		if (object->n_in_progress == 0)
			fscache_start_operations(object);

		ASSERTCMP(object->n_ops, >, 0);
		object->n_ops--;
		if (object->n_ops == 0)
			fscache_raise_event(object, FSCACHE_OBJECT_EV_CLEARED);

		spin_unlock(&object->lock);

	} while (count++ < 20);

	if (!list_empty(&cache->op_gc_list))
		schedule_work(&cache->op_gc);

	_leave("");
}

/*
 * allow the slow work item processor to get a ref on an operation
 */
static int fscache_op_get_ref(struct slow_work *work)
{
	struct fscache_operation *op =
		container_of(work, struct fscache_operation, slow_work);

	atomic_inc(&op->usage);
	return 0;
}

/*
 * allow the slow work item processor to discard a ref on an operation
 */
static void fscache_op_put_ref(struct slow_work *work)
{
	struct fscache_operation *op =
		container_of(work, struct fscache_operation, slow_work);

	fscache_put_operation(op);
}

/*
 * execute an operation using the slow thread pool to provide processing context
 * - the caller holds a ref to this object, so we don't need to hold one
 */
static void fscache_op_execute(struct slow_work *work)
{
	struct fscache_operation *op =
		container_of(work, struct fscache_operation, slow_work);
	unsigned long start;

	_enter("{OBJ%x OP%x,%d}",
	       op->object->debug_id, op->debug_id, atomic_read(&op->usage));

	ASSERT(op->processor != NULL);
	start = jiffies;
	op->processor(op);
	fscache_hist(fscache_ops_histogram, start);

	_leave("");
}

/*
 * describe an operation for slow-work debugging
 */
#ifdef CONFIG_SLOW_WORK_PROC
static void fscache_op_desc(struct slow_work *work, struct seq_file *m)
{
	struct fscache_operation *op =
		container_of(work, struct fscache_operation, slow_work);

	seq_printf(m, "FSC: OBJ%x OP%x: %s/%s fl=%lx",
		   op->object->debug_id, op->debug_id,
		   op->name, op->state, op->flags);
}
#endif

const struct slow_work_ops fscache_op_slow_work_ops = {
	.owner		= THIS_MODULE,
	.get_ref	= fscache_op_get_ref,
	.put_ref	= fscache_op_put_ref,
	.execute	= fscache_op_execute,
#ifdef CONFIG_SLOW_WORK_PROC
	.desc		= fscache_op_desc,
#endif
};

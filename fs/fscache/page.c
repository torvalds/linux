/* Cache page management and data I/O routines
 *
 * Copyright (C) 2004-2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define FSCACHE_DEBUG_LEVEL PAGE
#include <linux/module.h>
#include <linux/fscache-cache.h>
#include <linux/buffer_head.h>
#include <linux/pagevec.h>
#include "internal.h"

/*
 * check to see if a page is being written to the cache
 */
bool __fscache_check_page_write(struct fscache_cookie *cookie, struct page *page)
{
	void *val;

	rcu_read_lock();
	val = radix_tree_lookup(&cookie->stores, page->index);
	rcu_read_unlock();

	return val != NULL;
}
EXPORT_SYMBOL(__fscache_check_page_write);

/*
 * wait for a page to finish being written to the cache
 */
void __fscache_wait_on_page_write(struct fscache_cookie *cookie, struct page *page)
{
	wait_queue_head_t *wq = bit_waitqueue(&cookie->flags, 0);

	wait_event(*wq, !__fscache_check_page_write(cookie, page));
}
EXPORT_SYMBOL(__fscache_wait_on_page_write);

/*
 * decide whether a page can be released, possibly by cancelling a store to it
 * - we're allowed to sleep if __GFP_WAIT is flagged
 */
bool __fscache_maybe_release_page(struct fscache_cookie *cookie,
				  struct page *page,
				  gfp_t gfp)
{
	struct page *xpage;
	void *val;

	_enter("%p,%p,%x", cookie, page, gfp);

	rcu_read_lock();
	val = radix_tree_lookup(&cookie->stores, page->index);
	if (!val) {
		rcu_read_unlock();
		fscache_stat(&fscache_n_store_vmscan_not_storing);
		__fscache_uncache_page(cookie, page);
		return true;
	}

	/* see if the page is actually undergoing storage - if so we can't get
	 * rid of it till the cache has finished with it */
	if (radix_tree_tag_get(&cookie->stores, page->index,
			       FSCACHE_COOKIE_STORING_TAG)) {
		rcu_read_unlock();
		goto page_busy;
	}

	/* the page is pending storage, so we attempt to cancel the store and
	 * discard the store request so that the page can be reclaimed */
	spin_lock(&cookie->stores_lock);
	rcu_read_unlock();

	if (radix_tree_tag_get(&cookie->stores, page->index,
			       FSCACHE_COOKIE_STORING_TAG)) {
		/* the page started to undergo storage whilst we were looking,
		 * so now we can only wait or return */
		spin_unlock(&cookie->stores_lock);
		goto page_busy;
	}

	xpage = radix_tree_delete(&cookie->stores, page->index);
	spin_unlock(&cookie->stores_lock);

	if (xpage) {
		fscache_stat(&fscache_n_store_vmscan_cancelled);
		fscache_stat(&fscache_n_store_radix_deletes);
		ASSERTCMP(xpage, ==, page);
	} else {
		fscache_stat(&fscache_n_store_vmscan_gone);
	}

	wake_up_bit(&cookie->flags, 0);
	if (xpage)
		page_cache_release(xpage);
	__fscache_uncache_page(cookie, page);
	return true;

page_busy:
	/* we might want to wait here, but that could deadlock the allocator as
	 * the slow-work threads writing to the cache may all end up sleeping
	 * on memory allocation */
	fscache_stat(&fscache_n_store_vmscan_busy);
	return false;
}
EXPORT_SYMBOL(__fscache_maybe_release_page);

/*
 * note that a page has finished being written to the cache
 */
static void fscache_end_page_write(struct fscache_object *object,
				   struct page *page)
{
	struct fscache_cookie *cookie;
	struct page *xpage = NULL;

	spin_lock(&object->lock);
	cookie = object->cookie;
	if (cookie) {
		/* delete the page from the tree if it is now no longer
		 * pending */
		spin_lock(&cookie->stores_lock);
		radix_tree_tag_clear(&cookie->stores, page->index,
				     FSCACHE_COOKIE_STORING_TAG);
		if (!radix_tree_tag_get(&cookie->stores, page->index,
					FSCACHE_COOKIE_PENDING_TAG)) {
			fscache_stat(&fscache_n_store_radix_deletes);
			xpage = radix_tree_delete(&cookie->stores, page->index);
		}
		spin_unlock(&cookie->stores_lock);
		wake_up_bit(&cookie->flags, 0);
	}
	spin_unlock(&object->lock);
	if (xpage)
		page_cache_release(xpage);
}

/*
 * actually apply the changed attributes to a cache object
 */
static void fscache_attr_changed_op(struct fscache_operation *op)
{
	struct fscache_object *object = op->object;
	int ret;

	_enter("{OBJ%x OP%x}", object->debug_id, op->debug_id);

	fscache_stat(&fscache_n_attr_changed_calls);

	if (fscache_object_is_active(object)) {
		fscache_set_op_state(op, "CallFS");
		fscache_stat(&fscache_n_cop_attr_changed);
		ret = object->cache->ops->attr_changed(object);
		fscache_stat_d(&fscache_n_cop_attr_changed);
		fscache_set_op_state(op, "Done");
		if (ret < 0)
			fscache_abort_object(object);
	}

	_leave("");
}

/*
 * notification that the attributes on an object have changed
 */
int __fscache_attr_changed(struct fscache_cookie *cookie)
{
	struct fscache_operation *op;
	struct fscache_object *object;

	_enter("%p", cookie);

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);

	fscache_stat(&fscache_n_attr_changed);

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op) {
		fscache_stat(&fscache_n_attr_changed_nomem);
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	fscache_operation_init(op, NULL);
	fscache_operation_init_slow(op, fscache_attr_changed_op);
	op->flags = FSCACHE_OP_SLOW | (1 << FSCACHE_OP_EXCLUSIVE);
	fscache_set_op_name(op, "Attr");

	spin_lock(&cookie->lock);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs;
	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);

	if (fscache_submit_exclusive_op(object, op) < 0)
		goto nobufs;
	spin_unlock(&cookie->lock);
	fscache_stat(&fscache_n_attr_changed_ok);
	fscache_put_operation(op);
	_leave(" = 0");
	return 0;

nobufs:
	spin_unlock(&cookie->lock);
	kfree(op);
	fscache_stat(&fscache_n_attr_changed_nobufs);
	_leave(" = %d", -ENOBUFS);
	return -ENOBUFS;
}
EXPORT_SYMBOL(__fscache_attr_changed);

/*
 * handle secondary execution given to a retrieval op on behalf of the
 * cache
 */
static void fscache_retrieval_work(struct work_struct *work)
{
	struct fscache_retrieval *op =
		container_of(work, struct fscache_retrieval, op.fast_work);
	unsigned long start;

	_enter("{OP%x}", op->op.debug_id);

	start = jiffies;
	op->op.processor(&op->op);
	fscache_hist(fscache_ops_histogram, start);
	fscache_put_operation(&op->op);
}

/*
 * release a retrieval op reference
 */
static void fscache_release_retrieval_op(struct fscache_operation *_op)
{
	struct fscache_retrieval *op =
		container_of(_op, struct fscache_retrieval, op);

	_enter("{OP%x}", op->op.debug_id);

	fscache_hist(fscache_retrieval_histogram, op->start_time);
	if (op->context)
		fscache_put_context(op->op.object->cookie, op->context);

	_leave("");
}

/*
 * allocate a retrieval op
 */
static struct fscache_retrieval *fscache_alloc_retrieval(
	struct address_space *mapping,
	fscache_rw_complete_t end_io_func,
	void *context)
{
	struct fscache_retrieval *op;

	/* allocate a retrieval operation and attempt to submit it */
	op = kzalloc(sizeof(*op), GFP_NOIO);
	if (!op) {
		fscache_stat(&fscache_n_retrievals_nomem);
		return NULL;
	}

	fscache_operation_init(&op->op, fscache_release_retrieval_op);
	op->op.flags	= FSCACHE_OP_MYTHREAD | (1 << FSCACHE_OP_WAITING);
	op->mapping	= mapping;
	op->end_io_func	= end_io_func;
	op->context	= context;
	op->start_time	= jiffies;
	INIT_WORK(&op->op.fast_work, fscache_retrieval_work);
	INIT_LIST_HEAD(&op->to_do);
	fscache_set_op_name(&op->op, "Retr");
	return op;
}

/*
 * wait for a deferred lookup to complete
 */
static int fscache_wait_for_deferred_lookup(struct fscache_cookie *cookie)
{
	unsigned long jif;

	_enter("");

	if (!test_bit(FSCACHE_COOKIE_LOOKING_UP, &cookie->flags)) {
		_leave(" = 0 [imm]");
		return 0;
	}

	fscache_stat(&fscache_n_retrievals_wait);

	jif = jiffies;
	if (wait_on_bit(&cookie->flags, FSCACHE_COOKIE_LOOKING_UP,
			fscache_wait_bit_interruptible,
			TASK_INTERRUPTIBLE) != 0) {
		fscache_stat(&fscache_n_retrievals_intr);
		_leave(" = -ERESTARTSYS");
		return -ERESTARTSYS;
	}

	ASSERT(!test_bit(FSCACHE_COOKIE_LOOKING_UP, &cookie->flags));

	smp_rmb();
	fscache_hist(fscache_retrieval_delay_histogram, jif);
	_leave(" = 0 [dly]");
	return 0;
}

/*
 * wait for an object to become active (or dead)
 */
static int fscache_wait_for_retrieval_activation(struct fscache_object *object,
						 struct fscache_retrieval *op,
						 atomic_t *stat_op_waits,
						 atomic_t *stat_object_dead)
{
	int ret;

	if (!test_bit(FSCACHE_OP_WAITING, &op->op.flags))
		goto check_if_dead;

	_debug(">>> WT");
	fscache_stat(stat_op_waits);
	if (wait_on_bit(&op->op.flags, FSCACHE_OP_WAITING,
			fscache_wait_bit_interruptible,
			TASK_INTERRUPTIBLE) < 0) {
		ret = fscache_cancel_op(&op->op);
		if (ret == 0)
			return -ERESTARTSYS;

		/* it's been removed from the pending queue by another party,
		 * so we should get to run shortly */
		wait_on_bit(&op->op.flags, FSCACHE_OP_WAITING,
			    fscache_wait_bit, TASK_UNINTERRUPTIBLE);
	}
	_debug("<<< GO");

check_if_dead:
	if (unlikely(fscache_object_is_dead(object))) {
		fscache_stat(stat_object_dead);
		return -ENOBUFS;
	}
	return 0;
}

/*
 * read a page from the cache or allocate a block in which to store it
 * - we return:
 *   -ENOMEM	- out of memory, nothing done
 *   -ERESTARTSYS - interrupted
 *   -ENOBUFS	- no backing object available in which to cache the block
 *   -ENODATA	- no data available in the backing object for this block
 *   0		- dispatched a read - it'll call end_io_func() when finished
 */
int __fscache_read_or_alloc_page(struct fscache_cookie *cookie,
				 struct page *page,
				 fscache_rw_complete_t end_io_func,
				 void *context,
				 gfp_t gfp)
{
	struct fscache_retrieval *op;
	struct fscache_object *object;
	int ret;

	_enter("%p,%p,,,", cookie, page);

	fscache_stat(&fscache_n_retrievals);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs;

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);
	ASSERTCMP(page, !=, NULL);

	if (fscache_wait_for_deferred_lookup(cookie) < 0)
		return -ERESTARTSYS;

	op = fscache_alloc_retrieval(page->mapping, end_io_func, context);
	if (!op) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}
	fscache_set_op_name(&op->op, "RetrRA1");

	spin_lock(&cookie->lock);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs_unlock;
	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);

	ASSERTCMP(object->state, >, FSCACHE_OBJECT_LOOKING_UP);

	atomic_inc(&object->n_reads);
	set_bit(FSCACHE_OP_DEC_READ_CNT, &op->op.flags);

	if (fscache_submit_op(object, &op->op) < 0)
		goto nobufs_unlock;
	spin_unlock(&cookie->lock);

	fscache_stat(&fscache_n_retrieval_ops);

	/* pin the netfs read context in case we need to do the actual netfs
	 * read because we've encountered a cache read failure */
	fscache_get_context(object->cookie, op->context);

	/* we wait for the operation to become active, and then process it
	 * *here*, in this thread, and not in the thread pool */
	ret = fscache_wait_for_retrieval_activation(
		object, op,
		__fscache_stat(&fscache_n_retrieval_op_waits),
		__fscache_stat(&fscache_n_retrievals_object_dead));
	if (ret < 0)
		goto error;

	/* ask the cache to honour the operation */
	if (test_bit(FSCACHE_COOKIE_NO_DATA_YET, &object->cookie->flags)) {
		fscache_stat(&fscache_n_cop_allocate_page);
		ret = object->cache->ops->allocate_page(op, page, gfp);
		fscache_stat_d(&fscache_n_cop_allocate_page);
		if (ret == 0)
			ret = -ENODATA;
	} else {
		fscache_stat(&fscache_n_cop_read_or_alloc_page);
		ret = object->cache->ops->read_or_alloc_page(op, page, gfp);
		fscache_stat_d(&fscache_n_cop_read_or_alloc_page);
	}

error:
	if (ret == -ENOMEM)
		fscache_stat(&fscache_n_retrievals_nomem);
	else if (ret == -ERESTARTSYS)
		fscache_stat(&fscache_n_retrievals_intr);
	else if (ret == -ENODATA)
		fscache_stat(&fscache_n_retrievals_nodata);
	else if (ret < 0)
		fscache_stat(&fscache_n_retrievals_nobufs);
	else
		fscache_stat(&fscache_n_retrievals_ok);

	fscache_put_retrieval(op);
	_leave(" = %d", ret);
	return ret;

nobufs_unlock:
	spin_unlock(&cookie->lock);
	kfree(op);
nobufs:
	fscache_stat(&fscache_n_retrievals_nobufs);
	_leave(" = -ENOBUFS");
	return -ENOBUFS;
}
EXPORT_SYMBOL(__fscache_read_or_alloc_page);

/*
 * read a list of page from the cache or allocate a block in which to store
 * them
 * - we return:
 *   -ENOMEM	- out of memory, some pages may be being read
 *   -ERESTARTSYS - interrupted, some pages may be being read
 *   -ENOBUFS	- no backing object or space available in which to cache any
 *                pages not being read
 *   -ENODATA	- no data available in the backing object for some or all of
 *                the pages
 *   0		- dispatched a read on all pages
 *
 * end_io_func() will be called for each page read from the cache as it is
 * finishes being read
 *
 * any pages for which a read is dispatched will be removed from pages and
 * nr_pages
 */
int __fscache_read_or_alloc_pages(struct fscache_cookie *cookie,
				  struct address_space *mapping,
				  struct list_head *pages,
				  unsigned *nr_pages,
				  fscache_rw_complete_t end_io_func,
				  void *context,
				  gfp_t gfp)
{
	struct fscache_retrieval *op;
	struct fscache_object *object;
	int ret;

	_enter("%p,,%d,,,", cookie, *nr_pages);

	fscache_stat(&fscache_n_retrievals);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs;

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);
	ASSERTCMP(*nr_pages, >, 0);
	ASSERT(!list_empty(pages));

	if (fscache_wait_for_deferred_lookup(cookie) < 0)
		return -ERESTARTSYS;

	op = fscache_alloc_retrieval(mapping, end_io_func, context);
	if (!op)
		return -ENOMEM;
	fscache_set_op_name(&op->op, "RetrRAN");

	spin_lock(&cookie->lock);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs_unlock;
	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);

	atomic_inc(&object->n_reads);
	set_bit(FSCACHE_OP_DEC_READ_CNT, &op->op.flags);

	if (fscache_submit_op(object, &op->op) < 0)
		goto nobufs_unlock;
	spin_unlock(&cookie->lock);

	fscache_stat(&fscache_n_retrieval_ops);

	/* pin the netfs read context in case we need to do the actual netfs
	 * read because we've encountered a cache read failure */
	fscache_get_context(object->cookie, op->context);

	/* we wait for the operation to become active, and then process it
	 * *here*, in this thread, and not in the thread pool */
	ret = fscache_wait_for_retrieval_activation(
		object, op,
		__fscache_stat(&fscache_n_retrieval_op_waits),
		__fscache_stat(&fscache_n_retrievals_object_dead));
	if (ret < 0)
		goto error;

	/* ask the cache to honour the operation */
	if (test_bit(FSCACHE_COOKIE_NO_DATA_YET, &object->cookie->flags)) {
		fscache_stat(&fscache_n_cop_allocate_pages);
		ret = object->cache->ops->allocate_pages(
			op, pages, nr_pages, gfp);
		fscache_stat_d(&fscache_n_cop_allocate_pages);
	} else {
		fscache_stat(&fscache_n_cop_read_or_alloc_pages);
		ret = object->cache->ops->read_or_alloc_pages(
			op, pages, nr_pages, gfp);
		fscache_stat_d(&fscache_n_cop_read_or_alloc_pages);
	}

error:
	if (ret == -ENOMEM)
		fscache_stat(&fscache_n_retrievals_nomem);
	else if (ret == -ERESTARTSYS)
		fscache_stat(&fscache_n_retrievals_intr);
	else if (ret == -ENODATA)
		fscache_stat(&fscache_n_retrievals_nodata);
	else if (ret < 0)
		fscache_stat(&fscache_n_retrievals_nobufs);
	else
		fscache_stat(&fscache_n_retrievals_ok);

	fscache_put_retrieval(op);
	_leave(" = %d", ret);
	return ret;

nobufs_unlock:
	spin_unlock(&cookie->lock);
	kfree(op);
nobufs:
	fscache_stat(&fscache_n_retrievals_nobufs);
	_leave(" = -ENOBUFS");
	return -ENOBUFS;
}
EXPORT_SYMBOL(__fscache_read_or_alloc_pages);

/*
 * allocate a block in the cache on which to store a page
 * - we return:
 *   -ENOMEM	- out of memory, nothing done
 *   -ERESTARTSYS - interrupted
 *   -ENOBUFS	- no backing object available in which to cache the block
 *   0		- block allocated
 */
int __fscache_alloc_page(struct fscache_cookie *cookie,
			 struct page *page,
			 gfp_t gfp)
{
	struct fscache_retrieval *op;
	struct fscache_object *object;
	int ret;

	_enter("%p,%p,,,", cookie, page);

	fscache_stat(&fscache_n_allocs);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs;

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);
	ASSERTCMP(page, !=, NULL);

	if (fscache_wait_for_deferred_lookup(cookie) < 0)
		return -ERESTARTSYS;

	op = fscache_alloc_retrieval(page->mapping, NULL, NULL);
	if (!op)
		return -ENOMEM;
	fscache_set_op_name(&op->op, "RetrAL1");

	spin_lock(&cookie->lock);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs_unlock;
	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);

	if (fscache_submit_op(object, &op->op) < 0)
		goto nobufs_unlock;
	spin_unlock(&cookie->lock);

	fscache_stat(&fscache_n_alloc_ops);

	ret = fscache_wait_for_retrieval_activation(
		object, op,
		__fscache_stat(&fscache_n_alloc_op_waits),
		__fscache_stat(&fscache_n_allocs_object_dead));
	if (ret < 0)
		goto error;

	/* ask the cache to honour the operation */
	fscache_stat(&fscache_n_cop_allocate_page);
	ret = object->cache->ops->allocate_page(op, page, gfp);
	fscache_stat_d(&fscache_n_cop_allocate_page);

error:
	if (ret == -ERESTARTSYS)
		fscache_stat(&fscache_n_allocs_intr);
	else if (ret < 0)
		fscache_stat(&fscache_n_allocs_nobufs);
	else
		fscache_stat(&fscache_n_allocs_ok);

	fscache_put_retrieval(op);
	_leave(" = %d", ret);
	return ret;

nobufs_unlock:
	spin_unlock(&cookie->lock);
	kfree(op);
nobufs:
	fscache_stat(&fscache_n_allocs_nobufs);
	_leave(" = -ENOBUFS");
	return -ENOBUFS;
}
EXPORT_SYMBOL(__fscache_alloc_page);

/*
 * release a write op reference
 */
static void fscache_release_write_op(struct fscache_operation *_op)
{
	_enter("{OP%x}", _op->debug_id);
}

/*
 * perform the background storage of a page into the cache
 */
static void fscache_write_op(struct fscache_operation *_op)
{
	struct fscache_storage *op =
		container_of(_op, struct fscache_storage, op);
	struct fscache_object *object = op->op.object;
	struct fscache_cookie *cookie;
	struct page *page;
	unsigned n;
	void *results[1];
	int ret;

	_enter("{OP%x,%d}", op->op.debug_id, atomic_read(&op->op.usage));

	fscache_set_op_state(&op->op, "GetPage");

	spin_lock(&object->lock);
	cookie = object->cookie;

	if (!fscache_object_is_active(object) || !cookie) {
		spin_unlock(&object->lock);
		_leave("");
		return;
	}

	spin_lock(&cookie->stores_lock);

	fscache_stat(&fscache_n_store_calls);

	/* find a page to store */
	page = NULL;
	n = radix_tree_gang_lookup_tag(&cookie->stores, results, 0, 1,
				       FSCACHE_COOKIE_PENDING_TAG);
	if (n != 1)
		goto superseded;
	page = results[0];
	_debug("gang %d [%lx]", n, page->index);
	if (page->index > op->store_limit) {
		fscache_stat(&fscache_n_store_pages_over_limit);
		goto superseded;
	}

	if (page) {
		radix_tree_tag_set(&cookie->stores, page->index,
				   FSCACHE_COOKIE_STORING_TAG);
		radix_tree_tag_clear(&cookie->stores, page->index,
				     FSCACHE_COOKIE_PENDING_TAG);
	}

	spin_unlock(&cookie->stores_lock);
	spin_unlock(&object->lock);

	if (page) {
		fscache_set_op_state(&op->op, "Store");
		fscache_stat(&fscache_n_store_pages);
		fscache_stat(&fscache_n_cop_write_page);
		ret = object->cache->ops->write_page(op, page);
		fscache_stat_d(&fscache_n_cop_write_page);
		fscache_set_op_state(&op->op, "EndWrite");
		fscache_end_page_write(object, page);
		if (ret < 0) {
			fscache_set_op_state(&op->op, "Abort");
			fscache_abort_object(object);
		} else {
			fscache_enqueue_operation(&op->op);
		}
	}

	_leave("");
	return;

superseded:
	/* this writer is going away and there aren't any more things to
	 * write */
	_debug("cease");
	spin_unlock(&cookie->stores_lock);
	clear_bit(FSCACHE_OBJECT_PENDING_WRITE, &object->flags);
	spin_unlock(&object->lock);
	_leave("");
}

/*
 * request a page be stored in the cache
 * - returns:
 *   -ENOMEM	- out of memory, nothing done
 *   -ENOBUFS	- no backing object available in which to cache the page
 *   0		- dispatched a write - it'll call end_io_func() when finished
 *
 * if the cookie still has a backing object at this point, that object can be
 * in one of a few states with respect to storage processing:
 *
 *  (1) negative lookup, object not yet created (FSCACHE_COOKIE_CREATING is
 *      set)
 *
 *	(a) no writes yet (set FSCACHE_COOKIE_PENDING_FILL and queue deferred
 *	    fill op)
 *
 *	(b) writes deferred till post-creation (mark page for writing and
 *	    return immediately)
 *
 *  (2) negative lookup, object created, initial fill being made from netfs
 *      (FSCACHE_COOKIE_INITIAL_FILL is set)
 *
 *	(a) fill point not yet reached this page (mark page for writing and
 *          return)
 *
 *	(b) fill point passed this page (queue op to store this page)
 *
 *  (3) object extant (queue op to store this page)
 *
 * any other state is invalid
 */
int __fscache_write_page(struct fscache_cookie *cookie,
			 struct page *page,
			 gfp_t gfp)
{
	struct fscache_storage *op;
	struct fscache_object *object;
	int ret;

	_enter("%p,%x,", cookie, (u32) page->flags);

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);
	ASSERT(PageFsCache(page));

	fscache_stat(&fscache_n_stores);

	op = kzalloc(sizeof(*op), GFP_NOIO);
	if (!op)
		goto nomem;

	fscache_operation_init(&op->op, fscache_release_write_op);
	fscache_operation_init_slow(&op->op, fscache_write_op);
	op->op.flags = FSCACHE_OP_SLOW | (1 << FSCACHE_OP_WAITING);
	fscache_set_op_name(&op->op, "Write1");

	ret = radix_tree_preload(gfp & ~__GFP_HIGHMEM);
	if (ret < 0)
		goto nomem_free;

	ret = -ENOBUFS;
	spin_lock(&cookie->lock);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs;
	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);
	if (test_bit(FSCACHE_IOERROR, &object->cache->flags))
		goto nobufs;

	/* add the page to the pending-storage radix tree on the backing
	 * object */
	spin_lock(&object->lock);
	spin_lock(&cookie->stores_lock);

	_debug("store limit %llx", (unsigned long long) object->store_limit);

	ret = radix_tree_insert(&cookie->stores, page->index, page);
	if (ret < 0) {
		if (ret == -EEXIST)
			goto already_queued;
		_debug("insert failed %d", ret);
		goto nobufs_unlock_obj;
	}

	radix_tree_tag_set(&cookie->stores, page->index,
			   FSCACHE_COOKIE_PENDING_TAG);
	page_cache_get(page);

	/* we only want one writer at a time, but we do need to queue new
	 * writers after exclusive ops */
	if (test_and_set_bit(FSCACHE_OBJECT_PENDING_WRITE, &object->flags))
		goto already_pending;

	spin_unlock(&cookie->stores_lock);
	spin_unlock(&object->lock);

	op->op.debug_id	= atomic_inc_return(&fscache_op_debug_id);
	op->store_limit = object->store_limit;

	if (fscache_submit_op(object, &op->op) < 0)
		goto submit_failed;

	spin_unlock(&cookie->lock);
	radix_tree_preload_end();
	fscache_stat(&fscache_n_store_ops);
	fscache_stat(&fscache_n_stores_ok);

	/* the slow work queue now carries its own ref on the object */
	fscache_put_operation(&op->op);
	_leave(" = 0");
	return 0;

already_queued:
	fscache_stat(&fscache_n_stores_again);
already_pending:
	spin_unlock(&cookie->stores_lock);
	spin_unlock(&object->lock);
	spin_unlock(&cookie->lock);
	radix_tree_preload_end();
	kfree(op);
	fscache_stat(&fscache_n_stores_ok);
	_leave(" = 0");
	return 0;

submit_failed:
	spin_lock(&cookie->stores_lock);
	radix_tree_delete(&cookie->stores, page->index);
	spin_unlock(&cookie->stores_lock);
	page_cache_release(page);
	ret = -ENOBUFS;
	goto nobufs;

nobufs_unlock_obj:
	spin_unlock(&cookie->stores_lock);
	spin_unlock(&object->lock);
nobufs:
	spin_unlock(&cookie->lock);
	radix_tree_preload_end();
	kfree(op);
	fscache_stat(&fscache_n_stores_nobufs);
	_leave(" = -ENOBUFS");
	return -ENOBUFS;

nomem_free:
	kfree(op);
nomem:
	fscache_stat(&fscache_n_stores_oom);
	_leave(" = -ENOMEM");
	return -ENOMEM;
}
EXPORT_SYMBOL(__fscache_write_page);

/*
 * remove a page from the cache
 */
void __fscache_uncache_page(struct fscache_cookie *cookie, struct page *page)
{
	struct fscache_object *object;

	_enter(",%p", page);

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);
	ASSERTCMP(page, !=, NULL);

	fscache_stat(&fscache_n_uncaches);

	/* cache withdrawal may beat us to it */
	if (!PageFsCache(page))
		goto done;

	/* get the object */
	spin_lock(&cookie->lock);

	if (hlist_empty(&cookie->backing_objects)) {
		ClearPageFsCache(page);
		goto done_unlock;
	}

	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);

	/* there might now be stuff on disk we could read */
	clear_bit(FSCACHE_COOKIE_NO_DATA_YET, &cookie->flags);

	/* only invoke the cache backend if we managed to mark the page
	 * uncached here; this deals with synchronisation vs withdrawal */
	if (TestClearPageFsCache(page) &&
	    object->cache->ops->uncache_page) {
		/* the cache backend releases the cookie lock */
		fscache_stat(&fscache_n_cop_uncache_page);
		object->cache->ops->uncache_page(object, page);
		fscache_stat_d(&fscache_n_cop_uncache_page);
		goto done;
	}

done_unlock:
	spin_unlock(&cookie->lock);
done:
	_leave("");
}
EXPORT_SYMBOL(__fscache_uncache_page);

/**
 * fscache_mark_pages_cached - Mark pages as being cached
 * @op: The retrieval op pages are being marked for
 * @pagevec: The pages to be marked
 *
 * Mark a bunch of netfs pages as being cached.  After this is called,
 * the netfs must call fscache_uncache_page() to remove the mark.
 */
void fscache_mark_pages_cached(struct fscache_retrieval *op,
			       struct pagevec *pagevec)
{
	struct fscache_cookie *cookie = op->op.object->cookie;
	unsigned long loop;

#ifdef CONFIG_FSCACHE_STATS
	atomic_add(pagevec->nr, &fscache_n_marks);
#endif

	for (loop = 0; loop < pagevec->nr; loop++) {
		struct page *page = pagevec->pages[loop];

		_debug("- mark %p{%lx}", page, page->index);
		if (TestSetPageFsCache(page)) {
			static bool once_only;
			if (!once_only) {
				once_only = true;
				printk(KERN_WARNING "FS-Cache:"
				       " Cookie type %s marked page %lx"
				       " multiple times\n",
				       cookie->def->name, page->index);
			}
		}
	}

	if (cookie->def->mark_pages_cached)
		cookie->def->mark_pages_cached(cookie->netfs_data,
					       op->mapping, pagevec);
	pagevec_reinit(pagevec);
}
EXPORT_SYMBOL(fscache_mark_pages_cached);

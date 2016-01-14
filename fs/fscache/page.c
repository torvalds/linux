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
#include <linux/slab.h>
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
 * wait for a page to finish being written to the cache. Put a timeout here
 * since we might be called recursively via parent fs.
 */
static
bool release_page_wait_timeout(struct fscache_cookie *cookie, struct page *page)
{
	wait_queue_head_t *wq = bit_waitqueue(&cookie->flags, 0);

	return wait_event_timeout(*wq, !__fscache_check_page_write(cookie, page),
				  HZ);
}

/*
 * decide whether a page can be released, possibly by cancelling a store to it
 * - we're allowed to sleep if __GFP_DIRECT_RECLAIM is flagged
 */
bool __fscache_maybe_release_page(struct fscache_cookie *cookie,
				  struct page *page,
				  gfp_t gfp)
{
	struct page *xpage;
	void *val;

	_enter("%p,%p,%x", cookie, page, gfp);

try_again:
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
	/* We will wait here if we're allowed to, but that could deadlock the
	 * allocator as the work threads writing to the cache may all end up
	 * sleeping on memory allocation, so we may need to impose a timeout
	 * too. */
	if (!(gfp & __GFP_DIRECT_RECLAIM) || !(gfp & __GFP_FS)) {
		fscache_stat(&fscache_n_store_vmscan_busy);
		return false;
	}

	fscache_stat(&fscache_n_store_vmscan_wait);
	if (!release_page_wait_timeout(cookie, page))
		_debug("fscache writeout timeout page: %p{%lx}",
			page, page->index);

	gfp &= ~__GFP_DIRECT_RECLAIM;
	goto try_again;
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
		fscache_stat(&fscache_n_cop_attr_changed);
		ret = object->cache->ops->attr_changed(object);
		fscache_stat_d(&fscache_n_cop_attr_changed);
		if (ret < 0)
			fscache_abort_object(object);
	}

	fscache_op_complete(op, true);
	_leave("");
}

/*
 * notification that the attributes on an object have changed
 */
int __fscache_attr_changed(struct fscache_cookie *cookie)
{
	struct fscache_operation *op;
	struct fscache_object *object;
	bool wake_cookie = false;

	_enter("%p", cookie);

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);

	fscache_stat(&fscache_n_attr_changed);

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op) {
		fscache_stat(&fscache_n_attr_changed_nomem);
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	fscache_operation_init(op, fscache_attr_changed_op, NULL, NULL);
	op->flags = FSCACHE_OP_ASYNC |
		(1 << FSCACHE_OP_EXCLUSIVE) |
		(1 << FSCACHE_OP_UNUSE_COOKIE);

	spin_lock(&cookie->lock);

	if (!fscache_cookie_enabled(cookie) ||
	    hlist_empty(&cookie->backing_objects))
		goto nobufs;
	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);

	__fscache_use_cookie(cookie);
	if (fscache_submit_exclusive_op(object, op) < 0)
		goto nobufs_dec;
	spin_unlock(&cookie->lock);
	fscache_stat(&fscache_n_attr_changed_ok);
	fscache_put_operation(op);
	_leave(" = 0");
	return 0;

nobufs_dec:
	wake_cookie = __fscache_unuse_cookie(cookie);
nobufs:
	spin_unlock(&cookie->lock);
	fscache_put_operation(op);
	if (wake_cookie)
		__fscache_wake_unused_cookie(cookie);
	fscache_stat(&fscache_n_attr_changed_nobufs);
	_leave(" = %d", -ENOBUFS);
	return -ENOBUFS;
}
EXPORT_SYMBOL(__fscache_attr_changed);

/*
 * Handle cancellation of a pending retrieval op
 */
static void fscache_do_cancel_retrieval(struct fscache_operation *_op)
{
	struct fscache_retrieval *op =
		container_of(_op, struct fscache_retrieval, op);

	atomic_set(&op->n_pages, 0);
}

/*
 * release a retrieval op reference
 */
static void fscache_release_retrieval_op(struct fscache_operation *_op)
{
	struct fscache_retrieval *op =
		container_of(_op, struct fscache_retrieval, op);

	_enter("{OP%x}", op->op.debug_id);

	ASSERTIFCMP(op->op.state != FSCACHE_OP_ST_INITIALISED,
		    atomic_read(&op->n_pages), ==, 0);

	fscache_hist(fscache_retrieval_histogram, op->start_time);
	if (op->context)
		fscache_put_context(op->cookie, op->context);

	_leave("");
}

/*
 * allocate a retrieval op
 */
static struct fscache_retrieval *fscache_alloc_retrieval(
	struct fscache_cookie *cookie,
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

	fscache_operation_init(&op->op, NULL,
			       fscache_do_cancel_retrieval,
			       fscache_release_retrieval_op);
	op->op.flags	= FSCACHE_OP_MYTHREAD |
		(1UL << FSCACHE_OP_WAITING) |
		(1UL << FSCACHE_OP_UNUSE_COOKIE);
	op->cookie	= cookie;
	op->mapping	= mapping;
	op->end_io_func	= end_io_func;
	op->context	= context;
	op->start_time	= jiffies;
	INIT_LIST_HEAD(&op->to_do);

	/* Pin the netfs read context in case we need to do the actual netfs
	 * read because we've encountered a cache read failure.
	 */
	if (context)
		fscache_get_context(op->cookie, context);
	return op;
}

/*
 * wait for a deferred lookup to complete
 */
int fscache_wait_for_deferred_lookup(struct fscache_cookie *cookie)
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
int fscache_wait_for_operation_activation(struct fscache_object *object,
					  struct fscache_operation *op,
					  atomic_t *stat_op_waits,
					  atomic_t *stat_object_dead)
{
	int ret;

	if (!test_bit(FSCACHE_OP_WAITING, &op->flags))
		goto check_if_dead;

	_debug(">>> WT");
	if (stat_op_waits)
		fscache_stat(stat_op_waits);
	if (wait_on_bit(&op->flags, FSCACHE_OP_WAITING,
			TASK_INTERRUPTIBLE) != 0) {
		ret = fscache_cancel_op(op, false);
		if (ret == 0)
			return -ERESTARTSYS;

		/* it's been removed from the pending queue by another party,
		 * so we should get to run shortly */
		wait_on_bit(&op->flags, FSCACHE_OP_WAITING,
			    TASK_UNINTERRUPTIBLE);
	}
	_debug("<<< GO");

check_if_dead:
	if (op->state == FSCACHE_OP_ST_CANCELLED) {
		if (stat_object_dead)
			fscache_stat(stat_object_dead);
		_leave(" = -ENOBUFS [cancelled]");
		return -ENOBUFS;
	}
	if (unlikely(fscache_object_is_dying(object) ||
		     fscache_cache_is_broken(object))) {
		enum fscache_operation_state state = op->state;
		fscache_cancel_op(op, true);
		if (stat_object_dead)
			fscache_stat(stat_object_dead);
		_leave(" = -ENOBUFS [obj dead %d]", state);
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
	bool wake_cookie = false;
	int ret;

	_enter("%p,%p,,,", cookie, page);

	fscache_stat(&fscache_n_retrievals);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs;

	if (test_bit(FSCACHE_COOKIE_INVALIDATING, &cookie->flags)) {
		_leave(" = -ENOBUFS [invalidating]");
		return -ENOBUFS;
	}

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);
	ASSERTCMP(page, !=, NULL);

	if (fscache_wait_for_deferred_lookup(cookie) < 0)
		return -ERESTARTSYS;

	op = fscache_alloc_retrieval(cookie, page->mapping,
				     end_io_func, context);
	if (!op) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}
	atomic_set(&op->n_pages, 1);

	spin_lock(&cookie->lock);

	if (!fscache_cookie_enabled(cookie) ||
	    hlist_empty(&cookie->backing_objects))
		goto nobufs_unlock;
	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);

	ASSERT(test_bit(FSCACHE_OBJECT_IS_LOOKED_UP, &object->flags));

	__fscache_use_cookie(cookie);
	atomic_inc(&object->n_reads);
	__set_bit(FSCACHE_OP_DEC_READ_CNT, &op->op.flags);

	if (fscache_submit_op(object, &op->op) < 0)
		goto nobufs_unlock_dec;
	spin_unlock(&cookie->lock);

	fscache_stat(&fscache_n_retrieval_ops);

	/* we wait for the operation to become active, and then process it
	 * *here*, in this thread, and not in the thread pool */
	ret = fscache_wait_for_operation_activation(
		object, &op->op,
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

nobufs_unlock_dec:
	atomic_dec(&object->n_reads);
	wake_cookie = __fscache_unuse_cookie(cookie);
nobufs_unlock:
	spin_unlock(&cookie->lock);
	if (wake_cookie)
		__fscache_wake_unused_cookie(cookie);
	fscache_put_retrieval(op);
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
	bool wake_cookie = false;
	int ret;

	_enter("%p,,%d,,,", cookie, *nr_pages);

	fscache_stat(&fscache_n_retrievals);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs;

	if (test_bit(FSCACHE_COOKIE_INVALIDATING, &cookie->flags)) {
		_leave(" = -ENOBUFS [invalidating]");
		return -ENOBUFS;
	}

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);
	ASSERTCMP(*nr_pages, >, 0);
	ASSERT(!list_empty(pages));

	if (fscache_wait_for_deferred_lookup(cookie) < 0)
		return -ERESTARTSYS;

	op = fscache_alloc_retrieval(cookie, mapping, end_io_func, context);
	if (!op)
		return -ENOMEM;
	atomic_set(&op->n_pages, *nr_pages);

	spin_lock(&cookie->lock);

	if (!fscache_cookie_enabled(cookie) ||
	    hlist_empty(&cookie->backing_objects))
		goto nobufs_unlock;
	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);

	__fscache_use_cookie(cookie);
	atomic_inc(&object->n_reads);
	__set_bit(FSCACHE_OP_DEC_READ_CNT, &op->op.flags);

	if (fscache_submit_op(object, &op->op) < 0)
		goto nobufs_unlock_dec;
	spin_unlock(&cookie->lock);

	fscache_stat(&fscache_n_retrieval_ops);

	/* we wait for the operation to become active, and then process it
	 * *here*, in this thread, and not in the thread pool */
	ret = fscache_wait_for_operation_activation(
		object, &op->op,
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

nobufs_unlock_dec:
	atomic_dec(&object->n_reads);
	wake_cookie = __fscache_unuse_cookie(cookie);
nobufs_unlock:
	spin_unlock(&cookie->lock);
	fscache_put_retrieval(op);
	if (wake_cookie)
		__fscache_wake_unused_cookie(cookie);
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
	bool wake_cookie = false;
	int ret;

	_enter("%p,%p,,,", cookie, page);

	fscache_stat(&fscache_n_allocs);

	if (hlist_empty(&cookie->backing_objects))
		goto nobufs;

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);
	ASSERTCMP(page, !=, NULL);

	if (test_bit(FSCACHE_COOKIE_INVALIDATING, &cookie->flags)) {
		_leave(" = -ENOBUFS [invalidating]");
		return -ENOBUFS;
	}

	if (fscache_wait_for_deferred_lookup(cookie) < 0)
		return -ERESTARTSYS;

	op = fscache_alloc_retrieval(cookie, page->mapping, NULL, NULL);
	if (!op)
		return -ENOMEM;
	atomic_set(&op->n_pages, 1);

	spin_lock(&cookie->lock);

	if (!fscache_cookie_enabled(cookie) ||
	    hlist_empty(&cookie->backing_objects))
		goto nobufs_unlock;
	object = hlist_entry(cookie->backing_objects.first,
			     struct fscache_object, cookie_link);

	__fscache_use_cookie(cookie);
	if (fscache_submit_op(object, &op->op) < 0)
		goto nobufs_unlock_dec;
	spin_unlock(&cookie->lock);

	fscache_stat(&fscache_n_alloc_ops);

	ret = fscache_wait_for_operation_activation(
		object, &op->op,
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

nobufs_unlock_dec:
	wake_cookie = __fscache_unuse_cookie(cookie);
nobufs_unlock:
	spin_unlock(&cookie->lock);
	fscache_put_retrieval(op);
	if (wake_cookie)
		__fscache_wake_unused_cookie(cookie);
nobufs:
	fscache_stat(&fscache_n_allocs_nobufs);
	_leave(" = -ENOBUFS");
	return -ENOBUFS;
}
EXPORT_SYMBOL(__fscache_alloc_page);

/*
 * Unmark pages allocate in the readahead code path (via:
 * fscache_readpages_or_alloc) after delegating to the base filesystem
 */
void __fscache_readpages_cancel(struct fscache_cookie *cookie,
				struct list_head *pages)
{
	struct page *page;

	list_for_each_entry(page, pages, lru) {
		if (PageFsCache(page))
			__fscache_uncache_page(cookie, page);
	}
}
EXPORT_SYMBOL(__fscache_readpages_cancel);

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

	spin_lock(&object->lock);
	cookie = object->cookie;

	if (!fscache_object_is_active(object)) {
		/* If we get here, then the on-disk cache object likely longer
		 * exists, so we should just cancel this write operation.
		 */
		spin_unlock(&object->lock);
		fscache_op_complete(&op->op, false);
		_leave(" [inactive]");
		return;
	}

	if (!cookie) {
		/* If we get here, then the cookie belonging to the object was
		 * detached, probably by the cookie being withdrawn due to
		 * memory pressure, which means that the pages we might write
		 * to the cache from no longer exist - therefore, we can just
		 * cancel this write operation.
		 */
		spin_unlock(&object->lock);
		fscache_op_complete(&op->op, false);
		_leave(" [cancel] op{f=%lx s=%u} obj{s=%s f=%lx}",
		       _op->flags, _op->state, object->state->short_name,
		       object->flags);
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
	if (page->index >= op->store_limit) {
		fscache_stat(&fscache_n_store_pages_over_limit);
		goto superseded;
	}

	radix_tree_tag_set(&cookie->stores, page->index,
			   FSCACHE_COOKIE_STORING_TAG);
	radix_tree_tag_clear(&cookie->stores, page->index,
			     FSCACHE_COOKIE_PENDING_TAG);

	spin_unlock(&cookie->stores_lock);
	spin_unlock(&object->lock);

	fscache_stat(&fscache_n_store_pages);
	fscache_stat(&fscache_n_cop_write_page);
	ret = object->cache->ops->write_page(op, page);
	fscache_stat_d(&fscache_n_cop_write_page);
	fscache_end_page_write(object, page);
	if (ret < 0) {
		fscache_abort_object(object);
		fscache_op_complete(&op->op, true);
	} else {
		fscache_enqueue_operation(&op->op);
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
	fscache_op_complete(&op->op, true);
	_leave("");
}

/*
 * Clear the pages pending writing for invalidation
 */
void fscache_invalidate_writes(struct fscache_cookie *cookie)
{
	struct page *page;
	void *results[16];
	int n, i;

	_enter("");

	for (;;) {
		spin_lock(&cookie->stores_lock);
		n = radix_tree_gang_lookup_tag(&cookie->stores, results, 0,
					       ARRAY_SIZE(results),
					       FSCACHE_COOKIE_PENDING_TAG);
		if (n == 0) {
			spin_unlock(&cookie->stores_lock);
			break;
		}

		for (i = n - 1; i >= 0; i--) {
			page = results[i];
			radix_tree_delete(&cookie->stores, page->index);
		}

		spin_unlock(&cookie->stores_lock);

		for (i = n - 1; i >= 0; i--)
			page_cache_release(results[i]);
	}

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
 *	(a) no writes yet
 *
 *	(b) writes deferred till post-creation (mark page for writing and
 *	    return immediately)
 *
 *  (2) negative lookup, object created, initial fill being made from netfs
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
	bool wake_cookie = false;
	int ret;

	_enter("%p,%x,", cookie, (u32) page->flags);

	ASSERTCMP(cookie->def->type, !=, FSCACHE_COOKIE_TYPE_INDEX);
	ASSERT(PageFsCache(page));

	fscache_stat(&fscache_n_stores);

	if (test_bit(FSCACHE_COOKIE_INVALIDATING, &cookie->flags)) {
		_leave(" = -ENOBUFS [invalidating]");
		return -ENOBUFS;
	}

	op = kzalloc(sizeof(*op), GFP_NOIO | __GFP_NOMEMALLOC | __GFP_NORETRY);
	if (!op)
		goto nomem;

	fscache_operation_init(&op->op, fscache_write_op, NULL,
			       fscache_release_write_op);
	op->op.flags = FSCACHE_OP_ASYNC |
		(1 << FSCACHE_OP_WAITING) |
		(1 << FSCACHE_OP_UNUSE_COOKIE);

	ret = radix_tree_maybe_preload(gfp & ~__GFP_HIGHMEM);
	if (ret < 0)
		goto nomem_free;

	ret = -ENOBUFS;
	spin_lock(&cookie->lock);

	if (!fscache_cookie_enabled(cookie) ||
	    hlist_empty(&cookie->backing_objects))
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

	__fscache_use_cookie(cookie);
	if (fscache_submit_op(object, &op->op) < 0)
		goto submit_failed;

	spin_unlock(&cookie->lock);
	radix_tree_preload_end();
	fscache_stat(&fscache_n_store_ops);
	fscache_stat(&fscache_n_stores_ok);

	/* the work queue now carries its own ref on the object */
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
	fscache_put_operation(&op->op);
	fscache_stat(&fscache_n_stores_ok);
	_leave(" = 0");
	return 0;

submit_failed:
	spin_lock(&cookie->stores_lock);
	radix_tree_delete(&cookie->stores, page->index);
	spin_unlock(&cookie->stores_lock);
	wake_cookie = __fscache_unuse_cookie(cookie);
	page_cache_release(page);
	ret = -ENOBUFS;
	goto nobufs;

nobufs_unlock_obj:
	spin_unlock(&cookie->stores_lock);
	spin_unlock(&object->lock);
nobufs:
	spin_unlock(&cookie->lock);
	radix_tree_preload_end();
	fscache_put_operation(&op->op);
	if (wake_cookie)
		__fscache_wake_unused_cookie(cookie);
	fscache_stat(&fscache_n_stores_nobufs);
	_leave(" = -ENOBUFS");
	return -ENOBUFS;

nomem_free:
	fscache_put_operation(&op->op);
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
 * fscache_mark_page_cached - Mark a page as being cached
 * @op: The retrieval op pages are being marked for
 * @page: The page to be marked
 *
 * Mark a netfs page as being cached.  After this is called, the netfs
 * must call fscache_uncache_page() to remove the mark.
 */
void fscache_mark_page_cached(struct fscache_retrieval *op, struct page *page)
{
	struct fscache_cookie *cookie = op->op.object->cookie;

#ifdef CONFIG_FSCACHE_STATS
	atomic_inc(&fscache_n_marks);
#endif

	_debug("- mark %p{%lx}", page, page->index);
	if (TestSetPageFsCache(page)) {
		static bool once_only;
		if (!once_only) {
			once_only = true;
			pr_warn("Cookie type %s marked page %lx multiple times\n",
				cookie->def->name, page->index);
		}
	}

	if (cookie->def->mark_page_cached)
		cookie->def->mark_page_cached(cookie->netfs_data,
					      op->mapping, page);
}
EXPORT_SYMBOL(fscache_mark_page_cached);

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
	unsigned long loop;

	for (loop = 0; loop < pagevec->nr; loop++)
		fscache_mark_page_cached(op, pagevec->pages[loop]);

	pagevec_reinit(pagevec);
}
EXPORT_SYMBOL(fscache_mark_pages_cached);

/*
 * Uncache all the pages in an inode that are marked PG_fscache, assuming them
 * to be associated with the given cookie.
 */
void __fscache_uncache_all_inode_pages(struct fscache_cookie *cookie,
				       struct inode *inode)
{
	struct address_space *mapping = inode->i_mapping;
	struct pagevec pvec;
	pgoff_t next;
	int i;

	_enter("%p,%p", cookie, inode);

	if (!mapping || mapping->nrpages == 0) {
		_leave(" [no pages]");
		return;
	}

	pagevec_init(&pvec, 0);
	next = 0;
	do {
		if (!pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE))
			break;
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];
			next = page->index;
			if (PageFsCache(page)) {
				__fscache_wait_on_page_write(cookie, page);
				__fscache_uncache_page(cookie, page);
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	} while (++next);

	_leave("");
}
EXPORT_SYMBOL(__fscache_uncache_all_inode_pages);

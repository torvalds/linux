/*
 * Copyright (c) 2006-2007 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_mru_cache.h"

/*
 * The MRU Cache data structure consists of a data store, an array of lists and
 * a lock to protect its internal state.  At initialisation time, the client
 * supplies an element lifetime in milliseconds and a group count, as well as a
 * function pointer to call when deleting elements.  A data structure for
 * queueing up work in the form of timed callbacks is also included.
 *
 * The group count controls how many lists are created, and thereby how finely
 * the elements are grouped in time.  When reaping occurs, all the elements in
 * all the lists whose time has expired are deleted.
 *
 * To give an example of how this works in practice, consider a client that
 * initialises an MRU Cache with a lifetime of ten seconds and a group count of
 * five.  Five internal lists will be created, each representing a two second
 * period in time.  When the first element is added, time zero for the data
 * structure is initialised to the current time.
 *
 * All the elements added in the first two seconds are appended to the first
 * list.  Elements added in the third second go into the second list, and so on.
 * If an element is accessed at any point, it is removed from its list and
 * inserted at the head of the current most-recently-used list.
 *
 * The reaper function will have nothing to do until at least twelve seconds
 * have elapsed since the first element was added.  The reason for this is that
 * if it were called at t=11s, there could be elements in the first list that
 * have only been inactive for nine seconds, so it still does nothing.  If it is
 * called anywhere between t=12 and t=14 seconds, it will delete all the
 * elements that remain in the first list.  It's therefore possible for elements
 * to remain in the data store even after they've been inactive for up to
 * (t + t/g) seconds, where t is the inactive element lifetime and g is the
 * number of groups.
 *
 * The above example assumes that the reaper function gets called at least once
 * every (t/g) seconds.  If it is called less frequently, unused elements will
 * accumulate in the reap list until the reaper function is eventually called.
 * The current implementation uses work queue callbacks to carefully time the
 * reaper function calls, so this should happen rarely, if at all.
 *
 * From a design perspective, the primary reason for the choice of a list array
 * representing discrete time intervals is that it's only practical to reap
 * expired elements in groups of some appreciable size.  This automatically
 * introduces a granularity to element lifetimes, so there's no point storing an
 * individual timeout with each element that specifies a more precise reap time.
 * The bonus is a saving of sizeof(long) bytes of memory per element stored.
 *
 * The elements could have been stored in just one list, but an array of
 * counters or pointers would need to be maintained to allow them to be divided
 * up into discrete time groups.  More critically, the process of touching or
 * removing an element would involve walking large portions of the entire list,
 * which would have a detrimental effect on performance.  The additional memory
 * requirement for the array of list heads is minimal.
 *
 * When an element is touched or deleted, it needs to be removed from its
 * current list.  Doubly linked lists are used to make the list maintenance
 * portion of these operations O(1).  Since reaper timing can be imprecise,
 * inserts and lookups can occur when there are no free lists available.  When
 * this happens, all the elements on the LRU list need to be migrated to the end
 * of the reap list.  To keep the list maintenance portion of these operations
 * O(1) also, list tails need to be accessible without walking the entire list.
 * This is the reason why doubly linked list heads are used.
 */

/*
 * An MRU Cache is a dynamic data structure that stores its elements in a way
 * that allows efficient lookups, but also groups them into discrete time
 * intervals based on insertion time.  This allows elements to be efficiently
 * and automatically reaped after a fixed period of inactivity.
 *
 * When a client data pointer is stored in the MRU Cache it needs to be added to
 * both the data store and to one of the lists.  It must also be possible to
 * access each of these entries via the other, i.e. to:
 *
 *    a) Walk a list, removing the corresponding data store entry for each item.
 *    b) Look up a data store entry, then access its list entry directly.
 *
 * To achieve both of these goals, each entry must contain both a list entry and
 * a key, in addition to the user's data pointer.  Note that it's not a good
 * idea to have the client embed one of these structures at the top of their own
 * data structure, because inserting the same item more than once would most
 * likely result in a loop in one of the lists.  That's a sure-fire recipe for
 * an infinite loop in the code.
 */
struct xfs_mru_cache {
	struct radix_tree_root	store;     /* Core storage data structure.  */
	struct list_head	*lists;    /* Array of lists, one per grp.  */
	struct list_head	reap_list; /* Elements overdue for reaping. */
	spinlock_t		lock;      /* Lock to protect this struct.  */
	unsigned int		grp_count; /* Number of discrete groups.    */
	unsigned int		grp_time;  /* Time period spanned by grps.  */
	unsigned int		lru_grp;   /* Group containing time zero.   */
	unsigned long		time_zero; /* Time first element was added. */
	xfs_mru_cache_free_func_t free_func; /* Function pointer for freeing. */
	struct delayed_work	work;      /* Workqueue data for reaping.   */
	unsigned int		queued;	   /* work has been queued */
};

static struct workqueue_struct	*xfs_mru_reap_wq;

/*
 * When inserting, destroying or reaping, it's first necessary to update the
 * lists relative to a particular time.  In the case of destroying, that time
 * will be well in the future to ensure that all items are moved to the reap
 * list.  In all other cases though, the time will be the current time.
 *
 * This function enters a loop, moving the contents of the LRU list to the reap
 * list again and again until either a) the lists are all empty, or b) time zero
 * has been advanced sufficiently to be within the immediate element lifetime.
 *
 * Case a) above is detected by counting how many groups are migrated and
 * stopping when they've all been moved.  Case b) is detected by monitoring the
 * time_zero field, which is updated as each group is migrated.
 *
 * The return value is the earliest time that more migration could be needed, or
 * zero if there's no need to schedule more work because the lists are empty.
 */
STATIC unsigned long
_xfs_mru_cache_migrate(
	struct xfs_mru_cache	*mru,
	unsigned long		now)
{
	unsigned int		grp;
	unsigned int		migrated = 0;
	struct list_head	*lru_list;

	/* Nothing to do if the data store is empty. */
	if (!mru->time_zero)
		return 0;

	/* While time zero is older than the time spanned by all the lists. */
	while (mru->time_zero <= now - mru->grp_count * mru->grp_time) {

		/*
		 * If the LRU list isn't empty, migrate its elements to the tail
		 * of the reap list.
		 */
		lru_list = mru->lists + mru->lru_grp;
		if (!list_empty(lru_list))
			list_splice_init(lru_list, mru->reap_list.prev);

		/*
		 * Advance the LRU group number, freeing the old LRU list to
		 * become the new MRU list; advance time zero accordingly.
		 */
		mru->lru_grp = (mru->lru_grp + 1) % mru->grp_count;
		mru->time_zero += mru->grp_time;

		/*
		 * If reaping is so far behind that all the elements on all the
		 * lists have been migrated to the reap list, it's now empty.
		 */
		if (++migrated == mru->grp_count) {
			mru->lru_grp = 0;
			mru->time_zero = 0;
			return 0;
		}
	}

	/* Find the first non-empty list from the LRU end. */
	for (grp = 0; grp < mru->grp_count; grp++) {

		/* Check the grp'th list from the LRU end. */
		lru_list = mru->lists + ((mru->lru_grp + grp) % mru->grp_count);
		if (!list_empty(lru_list))
			return mru->time_zero +
			       (mru->grp_count + grp) * mru->grp_time;
	}

	/* All the lists must be empty. */
	mru->lru_grp = 0;
	mru->time_zero = 0;
	return 0;
}

/*
 * When inserting or doing a lookup, an element needs to be inserted into the
 * MRU list.  The lists must be migrated first to ensure that they're
 * up-to-date, otherwise the new element could be given a shorter lifetime in
 * the cache than it should.
 */
STATIC void
_xfs_mru_cache_list_insert(
	struct xfs_mru_cache	*mru,
	struct xfs_mru_cache_elem *elem)
{
	unsigned int		grp = 0;
	unsigned long		now = jiffies;

	/*
	 * If the data store is empty, initialise time zero, leave grp set to
	 * zero and start the work queue timer if necessary.  Otherwise, set grp
	 * to the number of group times that have elapsed since time zero.
	 */
	if (!_xfs_mru_cache_migrate(mru, now)) {
		mru->time_zero = now;
		if (!mru->queued) {
			mru->queued = 1;
			queue_delayed_work(xfs_mru_reap_wq, &mru->work,
			                   mru->grp_count * mru->grp_time);
		}
	} else {
		grp = (now - mru->time_zero) / mru->grp_time;
		grp = (mru->lru_grp + grp) % mru->grp_count;
	}

	/* Insert the element at the tail of the corresponding list. */
	list_add_tail(&elem->list_node, mru->lists + grp);
}

/*
 * When destroying or reaping, all the elements that were migrated to the reap
 * list need to be deleted.  For each element this involves removing it from the
 * data store, removing it from the reap list, calling the client's free
 * function and deleting the element from the element zone.
 *
 * We get called holding the mru->lock, which we drop and then reacquire.
 * Sparse need special help with this to tell it we know what we are doing.
 */
STATIC void
_xfs_mru_cache_clear_reap_list(
	struct xfs_mru_cache	*mru)
		__releases(mru->lock) __acquires(mru->lock)
{
	struct xfs_mru_cache_elem *elem, *next;
	struct list_head	tmp;

	INIT_LIST_HEAD(&tmp);
	list_for_each_entry_safe(elem, next, &mru->reap_list, list_node) {

		/* Remove the element from the data store. */
		radix_tree_delete(&mru->store, elem->key);

		/*
		 * remove to temp list so it can be freed without
		 * needing to hold the lock
		 */
		list_move(&elem->list_node, &tmp);
	}
	spin_unlock(&mru->lock);

	list_for_each_entry_safe(elem, next, &tmp, list_node) {
		list_del_init(&elem->list_node);
		mru->free_func(elem);
	}

	spin_lock(&mru->lock);
}

/*
 * We fire the reap timer every group expiry interval so
 * we always have a reaper ready to run. This makes shutdown
 * and flushing of the reaper easy to do. Hence we need to
 * keep when the next reap must occur so we can determine
 * at each interval whether there is anything we need to do.
 */
STATIC void
_xfs_mru_cache_reap(
	struct work_struct	*work)
{
	struct xfs_mru_cache	*mru =
		container_of(work, struct xfs_mru_cache, work.work);
	unsigned long		now, next;

	ASSERT(mru && mru->lists);
	if (!mru || !mru->lists)
		return;

	spin_lock(&mru->lock);
	next = _xfs_mru_cache_migrate(mru, jiffies);
	_xfs_mru_cache_clear_reap_list(mru);

	mru->queued = next;
	if ((mru->queued > 0)) {
		now = jiffies;
		if (next <= now)
			next = 0;
		else
			next -= now;
		queue_delayed_work(xfs_mru_reap_wq, &mru->work, next);
	}

	spin_unlock(&mru->lock);
}

int
xfs_mru_cache_init(void)
{
	xfs_mru_reap_wq = alloc_workqueue("xfs_mru_cache",
				WQ_MEM_RECLAIM|WQ_FREEZABLE, 1);
	if (!xfs_mru_reap_wq)
		return -ENOMEM;
	return 0;
}

void
xfs_mru_cache_uninit(void)
{
	destroy_workqueue(xfs_mru_reap_wq);
}

/*
 * To initialise a struct xfs_mru_cache pointer, call xfs_mru_cache_create()
 * with the address of the pointer, a lifetime value in milliseconds, a group
 * count and a free function to use when deleting elements.  This function
 * returns 0 if the initialisation was successful.
 */
int
xfs_mru_cache_create(
	struct xfs_mru_cache	**mrup,
	unsigned int		lifetime_ms,
	unsigned int		grp_count,
	xfs_mru_cache_free_func_t free_func)
{
	struct xfs_mru_cache	*mru = NULL;
	int			err = 0, grp;
	unsigned int		grp_time;

	if (mrup)
		*mrup = NULL;

	if (!mrup || !grp_count || !lifetime_ms || !free_func)
		return -EINVAL;

	if (!(grp_time = msecs_to_jiffies(lifetime_ms) / grp_count))
		return -EINVAL;

	if (!(mru = kmem_zalloc(sizeof(*mru), KM_SLEEP)))
		return -ENOMEM;

	/* An extra list is needed to avoid reaping up to a grp_time early. */
	mru->grp_count = grp_count + 1;
	mru->lists = kmem_zalloc(mru->grp_count * sizeof(*mru->lists), KM_SLEEP);

	if (!mru->lists) {
		err = -ENOMEM;
		goto exit;
	}

	for (grp = 0; grp < mru->grp_count; grp++)
		INIT_LIST_HEAD(mru->lists + grp);

	/*
	 * We use GFP_KERNEL radix tree preload and do inserts under a
	 * spinlock so GFP_ATOMIC is appropriate for the radix tree itself.
	 */
	INIT_RADIX_TREE(&mru->store, GFP_ATOMIC);
	INIT_LIST_HEAD(&mru->reap_list);
	spin_lock_init(&mru->lock);
	INIT_DELAYED_WORK(&mru->work, _xfs_mru_cache_reap);

	mru->grp_time  = grp_time;
	mru->free_func = free_func;

	*mrup = mru;

exit:
	if (err && mru && mru->lists)
		kmem_free(mru->lists);
	if (err && mru)
		kmem_free(mru);

	return err;
}

/*
 * Call xfs_mru_cache_flush() to flush out all cached entries, calling their
 * free functions as they're deleted.  When this function returns, the caller is
 * guaranteed that all the free functions for all the elements have finished
 * executing and the reaper is not running.
 */
static void
xfs_mru_cache_flush(
	struct xfs_mru_cache	*mru)
{
	if (!mru || !mru->lists)
		return;

	spin_lock(&mru->lock);
	if (mru->queued) {
		spin_unlock(&mru->lock);
		cancel_delayed_work_sync(&mru->work);
		spin_lock(&mru->lock);
	}

	_xfs_mru_cache_migrate(mru, jiffies + mru->grp_count * mru->grp_time);
	_xfs_mru_cache_clear_reap_list(mru);

	spin_unlock(&mru->lock);
}

void
xfs_mru_cache_destroy(
	struct xfs_mru_cache	*mru)
{
	if (!mru || !mru->lists)
		return;

	xfs_mru_cache_flush(mru);

	kmem_free(mru->lists);
	kmem_free(mru);
}

/*
 * To insert an element, call xfs_mru_cache_insert() with the data store, the
 * element's key and the client data pointer.  This function returns 0 on
 * success or ENOMEM if memory for the data element couldn't be allocated.
 */
int
xfs_mru_cache_insert(
	struct xfs_mru_cache	*mru,
	unsigned long		key,
	struct xfs_mru_cache_elem *elem)
{
	int			error;

	ASSERT(mru && mru->lists);
	if (!mru || !mru->lists)
		return -EINVAL;

	if (radix_tree_preload(GFP_NOFS))
		return -ENOMEM;

	INIT_LIST_HEAD(&elem->list_node);
	elem->key = key;

	spin_lock(&mru->lock);
	error = radix_tree_insert(&mru->store, key, elem);
	radix_tree_preload_end();
	if (!error)
		_xfs_mru_cache_list_insert(mru, elem);
	spin_unlock(&mru->lock);

	return error;
}

/*
 * To remove an element without calling the free function, call
 * xfs_mru_cache_remove() with the data store and the element's key.  On success
 * the client data pointer for the removed element is returned, otherwise this
 * function will return a NULL pointer.
 */
struct xfs_mru_cache_elem *
xfs_mru_cache_remove(
	struct xfs_mru_cache	*mru,
	unsigned long		key)
{
	struct xfs_mru_cache_elem *elem;

	ASSERT(mru && mru->lists);
	if (!mru || !mru->lists)
		return NULL;

	spin_lock(&mru->lock);
	elem = radix_tree_delete(&mru->store, key);
	if (elem)
		list_del(&elem->list_node);
	spin_unlock(&mru->lock);

	return elem;
}

/*
 * To remove and element and call the free function, call xfs_mru_cache_delete()
 * with the data store and the element's key.
 */
void
xfs_mru_cache_delete(
	struct xfs_mru_cache	*mru,
	unsigned long		key)
{
	struct xfs_mru_cache_elem *elem;

	elem = xfs_mru_cache_remove(mru, key);
	if (elem)
		mru->free_func(elem);
}

/*
 * To look up an element using its key, call xfs_mru_cache_lookup() with the
 * data store and the element's key.  If found, the element will be moved to the
 * head of the MRU list to indicate that it's been touched.
 *
 * The internal data structures are protected by a spinlock that is STILL HELD
 * when this function returns.  Call xfs_mru_cache_done() to release it.  Note
 * that it is not safe to call any function that might sleep in the interim.
 *
 * The implementation could have used reference counting to avoid this
 * restriction, but since most clients simply want to get, set or test a member
 * of the returned data structure, the extra per-element memory isn't warranted.
 *
 * If the element isn't found, this function returns NULL and the spinlock is
 * released.  xfs_mru_cache_done() should NOT be called when this occurs.
 *
 * Because sparse isn't smart enough to know about conditional lock return
 * status, we need to help it get it right by annotating the path that does
 * not release the lock.
 */
struct xfs_mru_cache_elem *
xfs_mru_cache_lookup(
	struct xfs_mru_cache	*mru,
	unsigned long		key)
{
	struct xfs_mru_cache_elem *elem;

	ASSERT(mru && mru->lists);
	if (!mru || !mru->lists)
		return NULL;

	spin_lock(&mru->lock);
	elem = radix_tree_lookup(&mru->store, key);
	if (elem) {
		list_del(&elem->list_node);
		_xfs_mru_cache_list_insert(mru, elem);
		__release(mru_lock); /* help sparse not be stupid */
	} else
		spin_unlock(&mru->lock);

	return elem;
}

/*
 * To release the internal data structure spinlock after having performed an
 * xfs_mru_cache_lookup() or an xfs_mru_cache_peek(), call xfs_mru_cache_done()
 * with the data store pointer.
 */
void
xfs_mru_cache_done(
	struct xfs_mru_cache	*mru)
		__releases(mru->lock)
{
	spin_unlock(&mru->lock);
}

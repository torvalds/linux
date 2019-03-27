/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/multilist.h>

/* needed for spa_get_random() */
#include <sys/spa.h>

/*
 * This overrides the number of sublists in each multilist_t, which defaults
 * to the number of CPUs in the system (see multilist_create()).
 */
int zfs_multilist_num_sublists = 0;

/*
 * Given the object contained on the list, return a pointer to the
 * object's multilist_node_t structure it contains.
 */
static multilist_node_t *
multilist_d2l(multilist_t *ml, void *obj)
{
	return ((multilist_node_t *)((char *)obj + ml->ml_offset));
}

/*
 * Initialize a new mutlilist using the parameters specified.
 *
 *  - 'size' denotes the size of the structure containing the
 *     multilist_node_t.
 *  - 'offset' denotes the byte offset of the mutlilist_node_t within
 *     the structure that contains it.
 *  - 'num' specifies the number of internal sublists to create.
 *  - 'index_func' is used to determine which sublist to insert into
 *     when the multilist_insert() function is called; as well as which
 *     sublist to remove from when multilist_remove() is called. The
 *     requirements this function must meet, are the following:
 *
 *      - It must always return the same value when called on the same
 *        object (to ensure the object is removed from the list it was
 *        inserted into).
 *
 *      - It must return a value in the range [0, number of sublists).
 *        The multilist_get_num_sublists() function may be used to
 *        determine the number of sublists in the multilist.
 *
 *     Also, in order to reduce internal contention between the sublists
 *     during insertion and removal, this function should choose evenly
 *     between all available sublists when inserting. This isn't a hard
 *     requirement, but a general rule of thumb in order to garner the
 *     best multi-threaded performance out of the data structure.
 */
static multilist_t *
multilist_create_impl(size_t size, size_t offset,
    unsigned int num, multilist_sublist_index_func_t *index_func)
{
	ASSERT3U(size, >, 0);
	ASSERT3U(size, >=, offset + sizeof (multilist_node_t));
	ASSERT3U(num, >, 0);
	ASSERT3P(index_func, !=, NULL);

	multilist_t *ml = kmem_alloc(sizeof (*ml), KM_SLEEP);
	ml->ml_offset = offset;
	ml->ml_num_sublists = num;
	ml->ml_index_func = index_func;

	ml->ml_sublists = kmem_zalloc(sizeof (multilist_sublist_t) *
	    ml->ml_num_sublists, KM_SLEEP);

	ASSERT3P(ml->ml_sublists, !=, NULL);

	for (int i = 0; i < ml->ml_num_sublists; i++) {
		multilist_sublist_t *mls = &ml->ml_sublists[i];
		mutex_init(&mls->mls_lock, NULL, MUTEX_DEFAULT, NULL);
		list_create(&mls->mls_list, size, offset);
	}
	return (ml);
}

/*
 * Allocate a new multilist, using the default number of sublists
 * (the number of CPUs, or at least 4, or the tunable
 * zfs_multilist_num_sublists).
 */
multilist_t *
multilist_create(size_t size, size_t offset,
    multilist_sublist_index_func_t *index_func)
{
	int num_sublists;

	if (zfs_multilist_num_sublists > 0) {
		num_sublists = zfs_multilist_num_sublists;
	} else {
		num_sublists = MAX(max_ncpus, 4);
	}

	return (multilist_create_impl(size, offset, num_sublists, index_func));
}

/*
 * Destroy the given multilist object, and free up any memory it holds.
 */
void
multilist_destroy(multilist_t *ml)
{
	ASSERT(multilist_is_empty(ml));

	for (int i = 0; i < ml->ml_num_sublists; i++) {
		multilist_sublist_t *mls = &ml->ml_sublists[i];

		ASSERT(list_is_empty(&mls->mls_list));

		list_destroy(&mls->mls_list);
		mutex_destroy(&mls->mls_lock);
	}

	ASSERT3P(ml->ml_sublists, !=, NULL);
	kmem_free(ml->ml_sublists,
	    sizeof (multilist_sublist_t) * ml->ml_num_sublists);

	ml->ml_num_sublists = 0;
	ml->ml_offset = 0;
	kmem_free(ml, sizeof (multilist_t));
}

/*
 * Insert the given object into the multilist.
 *
 * This function will insert the object specified into the sublist
 * determined using the function given at multilist creation time.
 *
 * The sublist locks are automatically acquired if not already held, to
 * ensure consistency when inserting and removing from multiple threads.
 */
void
multilist_insert(multilist_t *ml, void *obj)
{
	unsigned int sublist_idx = ml->ml_index_func(ml, obj);
	multilist_sublist_t *mls;
	boolean_t need_lock;

	DTRACE_PROBE3(multilist__insert, multilist_t *, ml,
	    unsigned int, sublist_idx, void *, obj);

	ASSERT3U(sublist_idx, <, ml->ml_num_sublists);

	mls = &ml->ml_sublists[sublist_idx];

	/*
	 * Note: Callers may already hold the sublist lock by calling
	 * multilist_sublist_lock().  Here we rely on MUTEX_HELD()
	 * returning TRUE if and only if the current thread holds the
	 * lock.  While it's a little ugly to make the lock recursive in
	 * this way, it works and allows the calling code to be much
	 * simpler -- otherwise it would have to pass around a flag
	 * indicating that it already has the lock.
	 */
	need_lock = !MUTEX_HELD(&mls->mls_lock);

	if (need_lock)
		mutex_enter(&mls->mls_lock);

	ASSERT(!multilist_link_active(multilist_d2l(ml, obj)));

	multilist_sublist_insert_head(mls, obj);

	if (need_lock)
		mutex_exit(&mls->mls_lock);
}

/*
 * Remove the given object from the multilist.
 *
 * This function will remove the object specified from the sublist
 * determined using the function given at multilist creation time.
 *
 * The necessary sublist locks are automatically acquired, to ensure
 * consistency when inserting and removing from multiple threads.
 */
void
multilist_remove(multilist_t *ml, void *obj)
{
	unsigned int sublist_idx = ml->ml_index_func(ml, obj);
	multilist_sublist_t *mls;
	boolean_t need_lock;

	DTRACE_PROBE3(multilist__remove, multilist_t *, ml,
	    unsigned int, sublist_idx, void *, obj);

	ASSERT3U(sublist_idx, <, ml->ml_num_sublists);

	mls = &ml->ml_sublists[sublist_idx];
	/* See comment in multilist_insert(). */
	need_lock = !MUTEX_HELD(&mls->mls_lock);

	if (need_lock)
		mutex_enter(&mls->mls_lock);

	ASSERT(multilist_link_active(multilist_d2l(ml, obj)));

	multilist_sublist_remove(mls, obj);

	if (need_lock)
		mutex_exit(&mls->mls_lock);
}

/*
 * Check to see if this multilist object is empty.
 *
 * This will return TRUE if it finds all of the sublists of this
 * multilist to be empty, and FALSE otherwise. Each sublist lock will be
 * automatically acquired as necessary.
 *
 * If concurrent insertions and removals are occurring, the semantics
 * of this function become a little fuzzy. Instead of locking all
 * sublists for the entire call time of the function, each sublist is
 * only locked as it is individually checked for emptiness. Thus, it's
 * possible for this function to return TRUE with non-empty sublists at
 * the time the function returns. This would be due to another thread
 * inserting into a given sublist, after that specific sublist was check
 * and deemed empty, but before all sublists have been checked.
 */
int
multilist_is_empty(multilist_t *ml)
{
	for (int i = 0; i < ml->ml_num_sublists; i++) {
		multilist_sublist_t *mls = &ml->ml_sublists[i];
		/* See comment in multilist_insert(). */
		boolean_t need_lock = !MUTEX_HELD(&mls->mls_lock);

		if (need_lock)
			mutex_enter(&mls->mls_lock);

		if (!list_is_empty(&mls->mls_list)) {
			if (need_lock)
				mutex_exit(&mls->mls_lock);

			return (FALSE);
		}

		if (need_lock)
			mutex_exit(&mls->mls_lock);
	}

	return (TRUE);
}

/* Return the number of sublists composing this multilist */
unsigned int
multilist_get_num_sublists(multilist_t *ml)
{
	return (ml->ml_num_sublists);
}

/* Return a randomly selected, valid sublist index for this multilist */
unsigned int
multilist_get_random_index(multilist_t *ml)
{
	return (spa_get_random(ml->ml_num_sublists));
}

/* Lock and return the sublist specified at the given index */
multilist_sublist_t *
multilist_sublist_lock(multilist_t *ml, unsigned int sublist_idx)
{
	multilist_sublist_t *mls;

	ASSERT3U(sublist_idx, <, ml->ml_num_sublists);
	mls = &ml->ml_sublists[sublist_idx];
	mutex_enter(&mls->mls_lock);

	return (mls);
}

/* Lock and return the sublist that would be used to store the specified obj */
multilist_sublist_t *
multilist_sublist_lock_obj(multilist_t *ml, void *obj)
{
	return (multilist_sublist_lock(ml, ml->ml_index_func(ml, obj)));
}

void
multilist_sublist_unlock(multilist_sublist_t *mls)
{
	mutex_exit(&mls->mls_lock);
}

/*
 * We're allowing any object to be inserted into this specific sublist,
 * but this can lead to trouble if multilist_remove() is called to
 * remove this object. Specifically, if calling ml_index_func on this
 * object returns an index for sublist different than what is passed as
 * a parameter here, any call to multilist_remove() with this newly
 * inserted object is undefined! (the call to multilist_remove() will
 * remove the object from a list that it isn't contained in)
 */
void
multilist_sublist_insert_head(multilist_sublist_t *mls, void *obj)
{
	ASSERT(MUTEX_HELD(&mls->mls_lock));
	list_insert_head(&mls->mls_list, obj);
}

/* please see comment above multilist_sublist_insert_head */
void
multilist_sublist_insert_tail(multilist_sublist_t *mls, void *obj)
{
	ASSERT(MUTEX_HELD(&mls->mls_lock));
	list_insert_tail(&mls->mls_list, obj);
}

/*
 * Move the object one element forward in the list.
 *
 * This function will move the given object forward in the list (towards
 * the head) by one object. So, in essence, it will swap its position in
 * the list with its "prev" pointer. If the given object is already at the
 * head of the list, it cannot be moved forward any more than it already
 * is, so no action is taken.
 *
 * NOTE: This function **must not** remove any object from the list other
 *       than the object given as the parameter. This is relied upon in
 *       arc_evict_state_impl().
 */
void
multilist_sublist_move_forward(multilist_sublist_t *mls, void *obj)
{
	void *prev = list_prev(&mls->mls_list, obj);

	ASSERT(MUTEX_HELD(&mls->mls_lock));
	ASSERT(!list_is_empty(&mls->mls_list));

	/* 'obj' must be at the head of the list, nothing to do */
	if (prev == NULL)
		return;

	list_remove(&mls->mls_list, obj);
	list_insert_before(&mls->mls_list, prev, obj);
}

void
multilist_sublist_remove(multilist_sublist_t *mls, void *obj)
{
	ASSERT(MUTEX_HELD(&mls->mls_lock));
	list_remove(&mls->mls_list, obj);
}

void *
multilist_sublist_head(multilist_sublist_t *mls)
{
	ASSERT(MUTEX_HELD(&mls->mls_lock));
	return (list_head(&mls->mls_list));
}

void *
multilist_sublist_tail(multilist_sublist_t *mls)
{
	ASSERT(MUTEX_HELD(&mls->mls_lock));
	return (list_tail(&mls->mls_list));
}

void *
multilist_sublist_next(multilist_sublist_t *mls, void *obj)
{
	ASSERT(MUTEX_HELD(&mls->mls_lock));
	return (list_next(&mls->mls_list, obj));
}

void *
multilist_sublist_prev(multilist_sublist_t *mls, void *obj)
{
	ASSERT(MUTEX_HELD(&mls->mls_lock));
	return (list_prev(&mls->mls_list, obj));
}

void
multilist_link_init(multilist_node_t *link)
{
	list_link_init(link);
}

int
multilist_link_active(multilist_node_t *link)
{
	return (list_link_active(link));
}

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/refcount.h>

#ifdef	ZFS_DEBUG

#ifdef _KERNEL
int reference_tracking_enable = FALSE; /* runs out of memory too easily */
SYSCTL_DECL(_vfs_zfs);
SYSCTL_INT(_vfs_zfs, OID_AUTO, reference_tracking_enable, CTLFLAG_RDTUN,
    &reference_tracking_enable, 0,
    "Track reference holders to refcount_t objects, used mostly by ZFS");
#else
int reference_tracking_enable = TRUE;
#endif
int reference_history = 3; /* tunable */

static kmem_cache_t *reference_cache;
static kmem_cache_t *reference_history_cache;

void
refcount_sysinit(void)
{
	reference_cache = kmem_cache_create("reference_cache",
	    sizeof (reference_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	reference_history_cache = kmem_cache_create("reference_history_cache",
	    sizeof (uint64_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
refcount_fini(void)
{
	kmem_cache_destroy(reference_cache);
	kmem_cache_destroy(reference_history_cache);
}

void
refcount_create(refcount_t *rc)
{
	mutex_init(&rc->rc_mtx, NULL, MUTEX_DEFAULT, NULL);
	list_create(&rc->rc_list, sizeof (reference_t),
	    offsetof(reference_t, ref_link));
	list_create(&rc->rc_removed, sizeof (reference_t),
	    offsetof(reference_t, ref_link));
	rc->rc_count = 0;
	rc->rc_removed_count = 0;
	rc->rc_tracked = reference_tracking_enable;
}

void
refcount_create_tracked(refcount_t *rc)
{
	refcount_create(rc);
	rc->rc_tracked = B_TRUE;
}

void
refcount_create_untracked(refcount_t *rc)
{
	refcount_create(rc);
	rc->rc_tracked = B_FALSE;
}

void
refcount_destroy_many(refcount_t *rc, uint64_t number)
{
	reference_t *ref;

	ASSERT(rc->rc_count == number);
	while (ref = list_head(&rc->rc_list)) {
		list_remove(&rc->rc_list, ref);
		kmem_cache_free(reference_cache, ref);
	}
	list_destroy(&rc->rc_list);

	while (ref = list_head(&rc->rc_removed)) {
		list_remove(&rc->rc_removed, ref);
		kmem_cache_free(reference_history_cache, ref->ref_removed);
		kmem_cache_free(reference_cache, ref);
	}
	list_destroy(&rc->rc_removed);
	mutex_destroy(&rc->rc_mtx);
}

void
refcount_destroy(refcount_t *rc)
{
	refcount_destroy_many(rc, 0);
}

int
refcount_is_zero(refcount_t *rc)
{
	return (rc->rc_count == 0);
}

int64_t
refcount_count(refcount_t *rc)
{
	return (rc->rc_count);
}

int64_t
refcount_add_many(refcount_t *rc, uint64_t number, void *holder)
{
	reference_t *ref = NULL;
	int64_t count;

	if (rc->rc_tracked) {
		ref = kmem_cache_alloc(reference_cache, KM_SLEEP);
		ref->ref_holder = holder;
		ref->ref_number = number;
	}
	mutex_enter(&rc->rc_mtx);
	ASSERT(rc->rc_count >= 0);
	if (rc->rc_tracked)
		list_insert_head(&rc->rc_list, ref);
	rc->rc_count += number;
	count = rc->rc_count;
	mutex_exit(&rc->rc_mtx);

	return (count);
}

int64_t
refcount_add(refcount_t *rc, void *holder)
{
	return (refcount_add_many(rc, 1, holder));
}

int64_t
refcount_remove_many(refcount_t *rc, uint64_t number, void *holder)
{
	reference_t *ref;
	int64_t count;

	mutex_enter(&rc->rc_mtx);
	ASSERT(rc->rc_count >= number);

	if (!rc->rc_tracked) {
		rc->rc_count -= number;
		count = rc->rc_count;
		mutex_exit(&rc->rc_mtx);
		return (count);
	}

	for (ref = list_head(&rc->rc_list); ref;
	    ref = list_next(&rc->rc_list, ref)) {
		if (ref->ref_holder == holder && ref->ref_number == number) {
			list_remove(&rc->rc_list, ref);
			if (reference_history > 0) {
				ref->ref_removed =
				    kmem_cache_alloc(reference_history_cache,
				    KM_SLEEP);
				list_insert_head(&rc->rc_removed, ref);
				rc->rc_removed_count++;
				if (rc->rc_removed_count > reference_history) {
					ref = list_tail(&rc->rc_removed);
					list_remove(&rc->rc_removed, ref);
					kmem_cache_free(reference_history_cache,
					    ref->ref_removed);
					kmem_cache_free(reference_cache, ref);
					rc->rc_removed_count--;
				}
			} else {
				kmem_cache_free(reference_cache, ref);
			}
			rc->rc_count -= number;
			count = rc->rc_count;
			mutex_exit(&rc->rc_mtx);
			return (count);
		}
	}
	panic("No such hold %p on refcount %llx", holder,
	    (u_longlong_t)(uintptr_t)rc);
	return (-1);
}

int64_t
refcount_remove(refcount_t *rc, void *holder)
{
	return (refcount_remove_many(rc, 1, holder));
}

void
refcount_transfer(refcount_t *dst, refcount_t *src)
{
	int64_t count, removed_count;
	list_t list, removed;

	list_create(&list, sizeof (reference_t),
	    offsetof(reference_t, ref_link));
	list_create(&removed, sizeof (reference_t),
	    offsetof(reference_t, ref_link));

	mutex_enter(&src->rc_mtx);
	count = src->rc_count;
	removed_count = src->rc_removed_count;
	src->rc_count = 0;
	src->rc_removed_count = 0;
	list_move_tail(&list, &src->rc_list);
	list_move_tail(&removed, &src->rc_removed);
	mutex_exit(&src->rc_mtx);

	mutex_enter(&dst->rc_mtx);
	dst->rc_count += count;
	dst->rc_removed_count += removed_count;
	list_move_tail(&dst->rc_list, &list);
	list_move_tail(&dst->rc_removed, &removed);
	mutex_exit(&dst->rc_mtx);

	list_destroy(&list);
	list_destroy(&removed);
}

void
refcount_transfer_ownership(refcount_t *rc, void *current_holder,
    void *new_holder)
{
	reference_t *ref;
	boolean_t found = B_FALSE;

	mutex_enter(&rc->rc_mtx);
	if (!rc->rc_tracked) {
		mutex_exit(&rc->rc_mtx);
		return;
	}

	for (ref = list_head(&rc->rc_list); ref;
	    ref = list_next(&rc->rc_list, ref)) {
		if (ref->ref_holder == current_holder) {
			ref->ref_holder = new_holder;
			found = B_TRUE;
			break;
		}
	}
	ASSERT(found);
	mutex_exit(&rc->rc_mtx);
}

/*
 * If tracking is enabled, return true if a reference exists that matches
 * the "holder" tag. If tracking is disabled, then return true if a reference
 * might be held.
 */
boolean_t
refcount_held(refcount_t *rc, void *holder)
{
	reference_t *ref;

	mutex_enter(&rc->rc_mtx);

	if (!rc->rc_tracked) {
		mutex_exit(&rc->rc_mtx);
		return (rc->rc_count > 0);
	}

	for (ref = list_head(&rc->rc_list); ref;
	    ref = list_next(&rc->rc_list, ref)) {
		if (ref->ref_holder == holder) {
			mutex_exit(&rc->rc_mtx);
			return (B_TRUE);
		}
	}
	mutex_exit(&rc->rc_mtx);
	return (B_FALSE);
}

/*
 * If tracking is enabled, return true if a reference does not exist that
 * matches the "holder" tag. If tracking is disabled, always return true
 * since the reference might not be held.
 */
boolean_t
refcount_not_held(refcount_t *rc, void *holder)
{
	reference_t *ref;

	mutex_enter(&rc->rc_mtx);

	if (!rc->rc_tracked) {
		mutex_exit(&rc->rc_mtx);
		return (B_TRUE);
	}

	for (ref = list_head(&rc->rc_list); ref;
	    ref = list_next(&rc->rc_list, ref)) {
		if (ref->ref_holder == holder) {
			mutex_exit(&rc->rc_mtx);
			return (B_FALSE);
		}
	}
	mutex_exit(&rc->rc_mtx);
	return (B_TRUE);
}
#endif	/* ZFS_DEBUG */

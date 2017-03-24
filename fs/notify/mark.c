/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * fsnotify inode mark locking/lifetime/and refcnting
 *
 * REFCNT:
 * The group->recnt and mark->refcnt tell how many "things" in the kernel
 * currently are referencing the objects. Both kind of objects typically will
 * live inside the kernel with a refcnt of 2, one for its creation and one for
 * the reference a group and a mark hold to each other.
 * If you are holding the appropriate locks, you can take a reference and the
 * object itself is guaranteed to survive until the reference is dropped.
 *
 * LOCKING:
 * There are 3 locks involved with fsnotify inode marks and they MUST be taken
 * in order as follows:
 *
 * group->mark_mutex
 * mark->lock
 * inode->i_lock
 *
 * group->mark_mutex protects the marks_list anchored inside a given group and
 * each mark is hooked via the g_list.  It also protects the groups private
 * data (i.e group limits).

 * mark->lock protects the marks attributes like its masks and flags.
 * Furthermore it protects the access to a reference of the group that the mark
 * is assigned to as well as the access to a reference of the inode/vfsmount
 * that is being watched by the mark.
 *
 * inode->i_lock protects the i_fsnotify_marks list anchored inside a
 * given inode and each mark is hooked via the i_list. (and sorta the
 * free_i_list)
 *
 *
 * LIFETIME:
 * Inode marks survive between when they are added to an inode and when their
 * refcnt==0.
 *
 * The inode mark can be cleared for a number of different reasons including:
 * - The inode is unlinked for the last time.  (fsnotify_inode_remove)
 * - The inode is being evicted from cache. (fsnotify_inode_delete)
 * - The fs the inode is on is unmounted.  (fsnotify_inode_delete/fsnotify_unmount_inodes)
 * - Something explicitly requests that it be removed.  (fsnotify_destroy_mark)
 * - The fsnotify_group associated with the mark is going away and all such marks
 *   need to be cleaned up. (fsnotify_clear_marks_by_group)
 *
 * Worst case we are given an inode and need to clean up all the marks on that
 * inode.  We take i_lock and walk the i_fsnotify_marks safely.  For each
 * mark on the list we take a reference (so the mark can't disappear under us).
 * We remove that mark form the inode's list of marks and we add this mark to a
 * private list anchored on the stack using i_free_list; we walk i_free_list
 * and before we destroy the mark we make sure that we dont race with a
 * concurrent destroy_group by getting a ref to the marks group and taking the
 * groups mutex.

 * Very similarly for freeing by group, except we use free_g_list.
 *
 * This has the very interesting property of being able to run concurrently with
 * any (or all) other directions.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>

#include <linux/atomic.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"

#define FSNOTIFY_REAPER_DELAY	(1)	/* 1 jiffy */

struct srcu_struct fsnotify_mark_srcu;
static DEFINE_SPINLOCK(destroy_lock);
static LIST_HEAD(destroy_list);

static void fsnotify_mark_destroy_workfn(struct work_struct *work);
static DECLARE_DELAYED_WORK(reaper_work, fsnotify_mark_destroy_workfn);

void fsnotify_get_mark(struct fsnotify_mark *mark)
{
	atomic_inc(&mark->refcnt);
}

void fsnotify_put_mark(struct fsnotify_mark *mark)
{
	if (atomic_dec_and_test(&mark->refcnt)) {
		if (mark->group)
			fsnotify_put_group(mark->group);
		mark->free_mark(mark);
	}
}

/* Calculate mask of events for a list of marks */
u32 fsnotify_recalc_mask(struct hlist_head *head)
{
	u32 new_mask = 0;
	struct fsnotify_mark *mark;

	hlist_for_each_entry(mark, head, obj_list)
		new_mask |= mark->mask;
	return new_mask;
}

/*
 * Remove mark from inode / vfsmount list, group list, drop inode reference
 * if we got one.
 *
 * Must be called with group->mark_mutex held.
 */
void fsnotify_detach_mark(struct fsnotify_mark *mark)
{
	struct inode *inode = NULL;
	struct fsnotify_group *group = mark->group;

	BUG_ON(!mutex_is_locked(&group->mark_mutex));

	spin_lock(&mark->lock);

	/* something else already called this function on this mark */
	if (!(mark->flags & FSNOTIFY_MARK_FLAG_ATTACHED)) {
		spin_unlock(&mark->lock);
		return;
	}

	mark->flags &= ~FSNOTIFY_MARK_FLAG_ATTACHED;

	if (mark->flags & FSNOTIFY_MARK_FLAG_INODE) {
		inode = mark->inode;
		fsnotify_destroy_inode_mark(mark);
	} else if (mark->flags & FSNOTIFY_MARK_FLAG_VFSMOUNT)
		fsnotify_destroy_vfsmount_mark(mark);
	else
		BUG();
	/*
	 * Note that we didn't update flags telling whether inode cares about
	 * what's happening with children. We update these flags from
	 * __fsnotify_parent() lazily when next event happens on one of our
	 * children.
	 */

	list_del_init(&mark->g_list);

	spin_unlock(&mark->lock);

	if (inode && (mark->flags & FSNOTIFY_MARK_FLAG_OBJECT_PINNED))
		iput(inode);

	atomic_dec(&group->num_marks);
}

/*
 * Prepare mark for freeing and add it to the list of marks prepared for
 * freeing. The actual freeing must happen after SRCU period ends and the
 * caller is responsible for this.
 *
 * The function returns true if the mark was added to the list of marks for
 * freeing. The function returns false if someone else has already called
 * __fsnotify_free_mark() for the mark.
 */
static bool __fsnotify_free_mark(struct fsnotify_mark *mark)
{
	struct fsnotify_group *group = mark->group;

	spin_lock(&mark->lock);
	/* something else already called this function on this mark */
	if (!(mark->flags & FSNOTIFY_MARK_FLAG_ALIVE)) {
		spin_unlock(&mark->lock);
		return false;
	}
	mark->flags &= ~FSNOTIFY_MARK_FLAG_ALIVE;
	spin_unlock(&mark->lock);

	/*
	 * Some groups like to know that marks are being freed.  This is a
	 * callback to the group function to let it know that this mark
	 * is being freed.
	 */
	if (group->ops->freeing_mark)
		group->ops->freeing_mark(mark, group);

	spin_lock(&destroy_lock);
	list_add(&mark->g_list, &destroy_list);
	spin_unlock(&destroy_lock);

	return true;
}

/*
 * Free fsnotify mark. The freeing is actually happening from a workqueue which
 * first waits for srcu period end. Caller must have a reference to the mark
 * or be protected by fsnotify_mark_srcu.
 */
void fsnotify_free_mark(struct fsnotify_mark *mark)
{
	if (__fsnotify_free_mark(mark)) {
		queue_delayed_work(system_unbound_wq, &reaper_work,
				   FSNOTIFY_REAPER_DELAY);
	}
}

void fsnotify_destroy_mark(struct fsnotify_mark *mark,
			   struct fsnotify_group *group)
{
	mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
	fsnotify_detach_mark(mark);
	mutex_unlock(&group->mark_mutex);
	fsnotify_free_mark(mark);
}

void fsnotify_destroy_marks(struct hlist_head *head, spinlock_t *lock)
{
	struct fsnotify_mark *mark;

	while (1) {
		/*
		 * We have to be careful since we can race with e.g.
		 * fsnotify_clear_marks_by_group() and once we drop 'lock',
		 * mark can get removed from the obj_list and destroyed. But
		 * we are holding mark reference so mark cannot be freed and
		 * calling fsnotify_destroy_mark() more than once is fine.
		 */
		spin_lock(lock);
		if (hlist_empty(head)) {
			spin_unlock(lock);
			break;
		}
		mark = hlist_entry(head->first, struct fsnotify_mark, obj_list);
		/*
		 * We don't update i_fsnotify_mask / mnt_fsnotify_mask here
		 * since inode / mount is going away anyway. So just remove
		 * mark from the list.
		 */
		hlist_del_init_rcu(&mark->obj_list);
		fsnotify_get_mark(mark);
		spin_unlock(lock);
		fsnotify_destroy_mark(mark, mark->group);
		fsnotify_put_mark(mark);
	}
}

void fsnotify_set_mark_mask_locked(struct fsnotify_mark *mark, __u32 mask)
{
	assert_spin_locked(&mark->lock);

	mark->mask = mask;

	if (mark->flags & FSNOTIFY_MARK_FLAG_INODE)
		fsnotify_set_inode_mark_mask_locked(mark, mask);
}

void fsnotify_set_mark_ignored_mask_locked(struct fsnotify_mark *mark, __u32 mask)
{
	assert_spin_locked(&mark->lock);

	mark->ignored_mask = mask;
}

/*
 * Sorting function for lists of fsnotify marks.
 *
 * Fanotify supports different notification classes (reflected as priority of
 * notification group). Events shall be passed to notification groups in
 * decreasing priority order. To achieve this marks in notification lists for
 * inodes and vfsmounts are sorted so that priorities of corresponding groups
 * are descending.
 *
 * Furthermore correct handling of the ignore mask requires processing inode
 * and vfsmount marks of each group together. Using the group address as
 * further sort criterion provides a unique sorting order and thus we can
 * merge inode and vfsmount lists of marks in linear time and find groups
 * present in both lists.
 *
 * A return value of 1 signifies that b has priority over a.
 * A return value of 0 signifies that the two marks have to be handled together.
 * A return value of -1 signifies that a has priority over b.
 */
int fsnotify_compare_groups(struct fsnotify_group *a, struct fsnotify_group *b)
{
	if (a == b)
		return 0;
	if (!a)
		return 1;
	if (!b)
		return -1;
	if (a->priority < b->priority)
		return 1;
	if (a->priority > b->priority)
		return -1;
	if (a < b)
		return 1;
	return -1;
}

/* Add mark into proper place in given list of marks */
int fsnotify_add_mark_list(struct hlist_head *head, struct fsnotify_mark *mark,
			   int allow_dups)
{
	struct fsnotify_mark *lmark, *last = NULL;
	int cmp;

	/* is mark the first mark? */
	if (hlist_empty(head)) {
		hlist_add_head_rcu(&mark->obj_list, head);
		return 0;
	}

	/* should mark be in the middle of the current list? */
	hlist_for_each_entry(lmark, head, obj_list) {
		last = lmark;

		if ((lmark->group == mark->group) && !allow_dups)
			return -EEXIST;

		cmp = fsnotify_compare_groups(lmark->group, mark->group);
		if (cmp >= 0) {
			hlist_add_before_rcu(&mark->obj_list, &lmark->obj_list);
			return 0;
		}
	}

	BUG_ON(last == NULL);
	/* mark should be the last entry.  last is the current last entry */
	hlist_add_behind_rcu(&mark->obj_list, &last->obj_list);
	return 0;
}

/*
 * Attach an initialized mark to a given group and fs object.
 * These marks may be used for the fsnotify backend to determine which
 * event types should be delivered to which group.
 */
int fsnotify_add_mark_locked(struct fsnotify_mark *mark,
			     struct fsnotify_group *group, struct inode *inode,
			     struct vfsmount *mnt, int allow_dups)
{
	int ret = 0;

	BUG_ON(inode && mnt);
	BUG_ON(!inode && !mnt);
	BUG_ON(!mutex_is_locked(&group->mark_mutex));

	/*
	 * LOCKING ORDER!!!!
	 * group->mark_mutex
	 * mark->lock
	 * inode->i_lock
	 */
	spin_lock(&mark->lock);
	mark->flags |= FSNOTIFY_MARK_FLAG_ALIVE | FSNOTIFY_MARK_FLAG_ATTACHED;

	fsnotify_get_group(group);
	mark->group = group;
	list_add(&mark->g_list, &group->marks_list);
	atomic_inc(&group->num_marks);
	fsnotify_get_mark(mark); /* for i_list and g_list */

	if (inode) {
		ret = fsnotify_add_inode_mark(mark, group, inode, allow_dups);
		if (ret)
			goto err;
	} else if (mnt) {
		ret = fsnotify_add_vfsmount_mark(mark, group, mnt, allow_dups);
		if (ret)
			goto err;
	} else {
		BUG();
	}

	/* this will pin the object if appropriate */
	fsnotify_set_mark_mask_locked(mark, mark->mask);
	spin_unlock(&mark->lock);

	if (inode)
		__fsnotify_update_child_dentry_flags(inode);

	return ret;
err:
	mark->flags &= ~FSNOTIFY_MARK_FLAG_ALIVE;
	list_del_init(&mark->g_list);
	fsnotify_put_group(group);
	mark->group = NULL;
	atomic_dec(&group->num_marks);

	spin_unlock(&mark->lock);

	spin_lock(&destroy_lock);
	list_add(&mark->g_list, &destroy_list);
	spin_unlock(&destroy_lock);
	queue_delayed_work(system_unbound_wq, &reaper_work,
				FSNOTIFY_REAPER_DELAY);

	return ret;
}

int fsnotify_add_mark(struct fsnotify_mark *mark, struct fsnotify_group *group,
		      struct inode *inode, struct vfsmount *mnt, int allow_dups)
{
	int ret;
	mutex_lock(&group->mark_mutex);
	ret = fsnotify_add_mark_locked(mark, group, inode, mnt, allow_dups);
	mutex_unlock(&group->mark_mutex);
	return ret;
}

/*
 * Given a list of marks, find the mark associated with given group. If found
 * take a reference to that mark and return it, else return NULL.
 */
struct fsnotify_mark *fsnotify_find_mark(struct hlist_head *head,
					 struct fsnotify_group *group)
{
	struct fsnotify_mark *mark;

	hlist_for_each_entry(mark, head, obj_list) {
		if (mark->group == group) {
			fsnotify_get_mark(mark);
			return mark;
		}
	}
	return NULL;
}

/*
 * clear any marks in a group in which mark->flags & flags is true
 */
void fsnotify_clear_marks_by_group_flags(struct fsnotify_group *group,
					 unsigned int flags)
{
	struct fsnotify_mark *lmark, *mark;
	LIST_HEAD(to_free);

	/*
	 * We have to be really careful here. Anytime we drop mark_mutex, e.g.
	 * fsnotify_clear_marks_by_inode() can come and free marks. Even in our
	 * to_free list so we have to use mark_mutex even when accessing that
	 * list. And freeing mark requires us to drop mark_mutex. So we can
	 * reliably free only the first mark in the list. That's why we first
	 * move marks to free to to_free list in one go and then free marks in
	 * to_free list one by one.
	 */
	mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
	list_for_each_entry_safe(mark, lmark, &group->marks_list, g_list) {
		if (mark->flags & flags)
			list_move(&mark->g_list, &to_free);
	}
	mutex_unlock(&group->mark_mutex);

	while (1) {
		mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
		if (list_empty(&to_free)) {
			mutex_unlock(&group->mark_mutex);
			break;
		}
		mark = list_first_entry(&to_free, struct fsnotify_mark, g_list);
		fsnotify_get_mark(mark);
		fsnotify_detach_mark(mark);
		mutex_unlock(&group->mark_mutex);
		fsnotify_free_mark(mark);
		fsnotify_put_mark(mark);
	}
}

/*
 * Given a group, prepare for freeing all the marks associated with that group.
 * The marks are attached to the list of marks prepared for destruction, the
 * caller is responsible for freeing marks in that list after SRCU period has
 * ended.
 */
void fsnotify_detach_group_marks(struct fsnotify_group *group)
{
	struct fsnotify_mark *mark;

	while (1) {
		mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
		if (list_empty(&group->marks_list)) {
			mutex_unlock(&group->mark_mutex);
			break;
		}
		mark = list_first_entry(&group->marks_list,
					struct fsnotify_mark, g_list);
		fsnotify_get_mark(mark);
		fsnotify_detach_mark(mark);
		mutex_unlock(&group->mark_mutex);
		__fsnotify_free_mark(mark);
		fsnotify_put_mark(mark);
	}
}

/*
 * Nothing fancy, just initialize lists and locks and counters.
 */
void fsnotify_init_mark(struct fsnotify_mark *mark,
			void (*free_mark)(struct fsnotify_mark *mark))
{
	memset(mark, 0, sizeof(*mark));
	spin_lock_init(&mark->lock);
	atomic_set(&mark->refcnt, 1);
	mark->free_mark = free_mark;
}

/*
 * Destroy all marks in destroy_list, waits for SRCU period to finish before
 * actually freeing marks.
 */
void fsnotify_mark_destroy_list(void)
{
	struct fsnotify_mark *mark, *next;
	struct list_head private_destroy_list;

	spin_lock(&destroy_lock);
	/* exchange the list head */
	list_replace_init(&destroy_list, &private_destroy_list);
	spin_unlock(&destroy_lock);

	synchronize_srcu(&fsnotify_mark_srcu);

	list_for_each_entry_safe(mark, next, &private_destroy_list, g_list) {
		list_del_init(&mark->g_list);
		fsnotify_put_mark(mark);
	}
}

static void fsnotify_mark_destroy_workfn(struct work_struct *work)
{
	fsnotify_mark_destroy_list();
}

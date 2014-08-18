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

struct srcu_struct fsnotify_mark_srcu;
static DEFINE_SPINLOCK(destroy_lock);
static LIST_HEAD(destroy_list);
static DECLARE_WAIT_QUEUE_HEAD(destroy_waitq);

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

/*
 * Any time a mark is getting freed we end up here.
 * The caller had better be holding a reference to this mark so we don't actually
 * do the final put under the mark->lock
 */
void fsnotify_destroy_mark_locked(struct fsnotify_mark *mark,
				  struct fsnotify_group *group)
{
	struct inode *inode = NULL;

	BUG_ON(!mutex_is_locked(&group->mark_mutex));

	spin_lock(&mark->lock);

	/* something else already called this function on this mark */
	if (!(mark->flags & FSNOTIFY_MARK_FLAG_ALIVE)) {
		spin_unlock(&mark->lock);
		return;
	}

	mark->flags &= ~FSNOTIFY_MARK_FLAG_ALIVE;

	if (mark->flags & FSNOTIFY_MARK_FLAG_INODE) {
		inode = mark->i.inode;
		fsnotify_destroy_inode_mark(mark);
	} else if (mark->flags & FSNOTIFY_MARK_FLAG_VFSMOUNT)
		fsnotify_destroy_vfsmount_mark(mark);
	else
		BUG();

	list_del_init(&mark->g_list);

	spin_unlock(&mark->lock);

	if (inode && (mark->flags & FSNOTIFY_MARK_FLAG_OBJECT_PINNED))
		iput(inode);
	/* release lock temporarily */
	mutex_unlock(&group->mark_mutex);

	spin_lock(&destroy_lock);
	list_add(&mark->destroy_list, &destroy_list);
	spin_unlock(&destroy_lock);
	wake_up(&destroy_waitq);
	/*
	 * We don't necessarily have a ref on mark from caller so the above destroy
	 * may have actually freed it, unless this group provides a 'freeing_mark'
	 * function which must be holding a reference.
	 */

	/*
	 * Some groups like to know that marks are being freed.  This is a
	 * callback to the group function to let it know that this mark
	 * is being freed.
	 */
	if (group->ops->freeing_mark)
		group->ops->freeing_mark(mark, group);

	/*
	 * __fsnotify_update_child_dentry_flags(inode);
	 *
	 * I really want to call that, but we can't, we have no idea if the inode
	 * still exists the second we drop the mark->lock.
	 *
	 * The next time an event arrive to this inode from one of it's children
	 * __fsnotify_parent will see that the inode doesn't care about it's
	 * children and will update all of these flags then.  So really this
	 * is just a lazy update (and could be a perf win...)
	 */

	atomic_dec(&group->num_marks);

	mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
}

void fsnotify_destroy_mark(struct fsnotify_mark *mark,
			   struct fsnotify_group *group)
{
	mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
	fsnotify_destroy_mark_locked(mark, group);
	mutex_unlock(&group->mark_mutex);
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
	mark->flags |= FSNOTIFY_MARK_FLAG_ALIVE;

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
	list_add(&mark->destroy_list, &destroy_list);
	spin_unlock(&destroy_lock);
	wake_up(&destroy_waitq);

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
 * clear any marks in a group in which mark->flags & flags is true
 */
void fsnotify_clear_marks_by_group_flags(struct fsnotify_group *group,
					 unsigned int flags)
{
	struct fsnotify_mark *lmark, *mark;

	mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
	list_for_each_entry_safe(mark, lmark, &group->marks_list, g_list) {
		if (mark->flags & flags) {
			fsnotify_get_mark(mark);
			fsnotify_destroy_mark_locked(mark, group);
			fsnotify_put_mark(mark);
		}
	}
	mutex_unlock(&group->mark_mutex);
}

/*
 * Given a group, destroy all of the marks associated with that group.
 */
void fsnotify_clear_marks_by_group(struct fsnotify_group *group)
{
	fsnotify_clear_marks_by_group_flags(group, (unsigned int)-1);
}

void fsnotify_duplicate_mark(struct fsnotify_mark *new, struct fsnotify_mark *old)
{
	assert_spin_locked(&old->lock);
	new->i.inode = old->i.inode;
	new->m.mnt = old->m.mnt;
	if (old->group)
		fsnotify_get_group(old->group);
	new->group = old->group;
	new->mask = old->mask;
	new->free_mark = old->free_mark;
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

static int fsnotify_mark_destroy(void *ignored)
{
	struct fsnotify_mark *mark, *next;
	struct list_head private_destroy_list;

	for (;;) {
		spin_lock(&destroy_lock);
		/* exchange the list head */
		list_replace_init(&destroy_list, &private_destroy_list);
		spin_unlock(&destroy_lock);

		synchronize_srcu(&fsnotify_mark_srcu);

		list_for_each_entry_safe(mark, next, &private_destroy_list, destroy_list) {
			list_del_init(&mark->destroy_list);
			fsnotify_put_mark(mark);
		}

		wait_event_interruptible(destroy_waitq, !list_empty(&destroy_list));
	}

	return 0;
}

static int __init fsnotify_mark_init(void)
{
	struct task_struct *thread;

	thread = kthread_run(fsnotify_mark_destroy, NULL,
			     "fsnotify_mark");
	if (IS_ERR(thread))
		panic("unable to start fsnotify mark destruction thread.");

	return 0;
}
device_initcall(fsnotify_mark_init);

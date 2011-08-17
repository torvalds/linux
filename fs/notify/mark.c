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
 * The mark->refcnt tells how many "things" in the kernel currently are
 * referencing this object.  The object typically will live inside the kernel
 * with a refcnt of 2, one for each list it is on (i_list, g_list).  Any task
 * which can find this object holding the appropriete locks, can take a reference
 * and the object itself is guaranteed to survive until the reference is dropped.
 *
 * LOCKING:
 * There are 3 spinlocks involved with fsnotify inode marks and they MUST
 * be taken in order as follows:
 *
 * mark->lock
 * group->mark_lock
 * inode->i_lock
 *
 * mark->lock protects 2 things, mark->group and mark->inode.  You must hold
 * that lock to dereference either of these things (they could be NULL even with
 * the lock)
 *
 * group->mark_lock protects the marks_list anchored inside a given group
 * and each mark is hooked via the g_list.  It also sorta protects the
 * free_g_list, which when used is anchored by a private list on the stack of the
 * task which held the group->mark_lock.
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
 * private list anchored on the stack using i_free_list;  At this point we no
 * longer fear anything finding the mark using the inode's list of marks.
 *
 * We can safely and locklessly run the private list on the stack of everything
 * we just unattached from the original inode.  For each mark on the private list
 * we grab the mark-> and can thus dereference mark->group and mark->inode.  If
 * we see the group and inode are not NULL we take those locks.  Now holding all
 * 3 locks we can completely remove the mark from other tasks finding it in the
 * future.  Remember, 10 things might already be referencing this mark, but they
 * better be holding a ref.  We drop our reference we took before we unhooked it
 * from the inode.  When the ref hits 0 we can free the mark.
 *
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
	if (atomic_dec_and_test(&mark->refcnt))
		mark->free_mark(mark);
}

/*
 * Any time a mark is getting freed we end up here.
 * The caller had better be holding a reference to this mark so we don't actually
 * do the final put under the mark->lock
 */
void fsnotify_destroy_mark(struct fsnotify_mark *mark)
{
	struct fsnotify_group *group;
	struct inode *inode = NULL;

	spin_lock(&mark->lock);

	group = mark->group;

	/* something else already called this function on this mark */
	if (!(mark->flags & FSNOTIFY_MARK_FLAG_ALIVE)) {
		spin_unlock(&mark->lock);
		return;
	}

	mark->flags &= ~FSNOTIFY_MARK_FLAG_ALIVE;

	/* 1 from caller and 1 for being on i_list/g_list */
	BUG_ON(atomic_read(&mark->refcnt) < 2);

	spin_lock(&group->mark_lock);

	if (mark->flags & FSNOTIFY_MARK_FLAG_INODE) {
		inode = mark->i.inode;
		fsnotify_destroy_inode_mark(mark);
	} else if (mark->flags & FSNOTIFY_MARK_FLAG_VFSMOUNT)
		fsnotify_destroy_vfsmount_mark(mark);
	else
		BUG();

	list_del_init(&mark->g_list);

	spin_unlock(&group->mark_lock);
	spin_unlock(&mark->lock);

	spin_lock(&destroy_lock);
	list_add(&mark->destroy_list, &destroy_list);
	spin_unlock(&destroy_lock);
	wake_up(&destroy_waitq);

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

	if (inode && (mark->flags & FSNOTIFY_MARK_FLAG_OBJECT_PINNED))
		iput(inode);

	/*
	 * it's possible that this group tried to destroy itself, but this
	 * this mark was simultaneously being freed by inode.  If that's the
	 * case, we finish freeing the group here.
	 */
	if (unlikely(atomic_dec_and_test(&group->num_marks)))
		fsnotify_final_destroy_group(group);
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
int fsnotify_add_mark(struct fsnotify_mark *mark,
		      struct fsnotify_group *group, struct inode *inode,
		      struct vfsmount *mnt, int allow_dups)
{
	int ret = 0;

	BUG_ON(inode && mnt);
	BUG_ON(!inode && !mnt);

	/*
	 * LOCKING ORDER!!!!
	 * mark->lock
	 * group->mark_lock
	 * inode->i_lock
	 */
	spin_lock(&mark->lock);
	spin_lock(&group->mark_lock);

	mark->flags |= FSNOTIFY_MARK_FLAG_ALIVE;

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

	spin_unlock(&group->mark_lock);

	/* this will pin the object if appropriate */
	fsnotify_set_mark_mask_locked(mark, mark->mask);

	spin_unlock(&mark->lock);

	if (inode)
		__fsnotify_update_child_dentry_flags(inode);

	return ret;
err:
	mark->flags &= ~FSNOTIFY_MARK_FLAG_ALIVE;
	list_del_init(&mark->g_list);
	mark->group = NULL;
	atomic_dec(&group->num_marks);

	spin_unlock(&group->mark_lock);
	spin_unlock(&mark->lock);

	spin_lock(&destroy_lock);
	list_add(&mark->destroy_list, &destroy_list);
	spin_unlock(&destroy_lock);
	wake_up(&destroy_waitq);

	return ret;
}

/*
 * clear any marks in a group in which mark->flags & flags is true
 */
void fsnotify_clear_marks_by_group_flags(struct fsnotify_group *group,
					 unsigned int flags)
{
	struct fsnotify_mark *lmark, *mark;
	LIST_HEAD(free_list);

	spin_lock(&group->mark_lock);
	list_for_each_entry_safe(mark, lmark, &group->marks_list, g_list) {
		if (mark->flags & flags) {
			list_add(&mark->free_g_list, &free_list);
			list_del_init(&mark->g_list);
			fsnotify_get_mark(mark);
		}
	}
	spin_unlock(&group->mark_lock);

	list_for_each_entry_safe(mark, lmark, &free_list, free_g_list) {
		fsnotify_destroy_mark(mark);
		fsnotify_put_mark(mark);
	}
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
	LIST_HEAD(private_destroy_list);

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

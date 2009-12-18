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
 * and the object itself is guarenteed to survive until the reference is dropped.
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
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/writeback.h> /* for inode_lock */

#include <asm/atomic.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"

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

	/* if !group something else already marked this to die */
	if (!group) {
		spin_unlock(&mark->lock);
		return;
	}

	/* 1 from caller and 1 for being on i_list/g_list */
	BUG_ON(atomic_read(&mark->refcnt) < 2);

	spin_lock(&group->mark_lock);

	if (mark->flags & FSNOTIFY_MARK_FLAG_INODE) {
		fsnotify_destroy_inode_mark(mark);
		inode = mark->i.inode;
	} else if (mark->flags & FSNOTIFY_MARK_FLAG_VFSMOUNT)
		fsnotify_destroy_vfsmount_mark(mark);
	else
		BUG();

	list_del_init(&mark->g_list);
	mark->group = NULL;

	fsnotify_put_mark(mark); /* for i_list and g_list */

	spin_unlock(&group->mark_lock);
	spin_unlock(&mark->lock);

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

	if (inode)
		iput(inode);

	/*
	 * it's possible that this group tried to destroy itself, but this
	 * this mark was simultaneously being freed by inode.  If that's the
	 * case, we finish freeing the group here.
	 */
	if (unlikely(atomic_dec_and_test(&group->num_marks)))
		fsnotify_final_destroy_group(group);
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
	 * if this group isn't being testing for inode type events we need
	 * to start testing
	 */
	if (inode && unlikely(list_empty(&group->inode_group_list)))
		fsnotify_add_inode_group(group);
	else if (mnt && unlikely(list_empty(&group->vfsmount_group_list)))
		fsnotify_add_vfsmount_group(group);

	/*
	 * LOCKING ORDER!!!!
	 * mark->lock
	 * group->mark_lock
	 * inode->i_lock
	 */
	spin_lock(&mark->lock);
	spin_lock(&group->mark_lock);

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
	spin_unlock(&mark->lock);

	if (inode)
		__fsnotify_update_child_dentry_flags(inode);

	return ret;
err:
	mark->group = NULL;
	list_del_init(&mark->g_list);
	atomic_dec(&group->num_marks);
	fsnotify_put_mark(mark);

	spin_unlock(&group->mark_lock);
	spin_unlock(&mark->lock);

	return ret;
}

/*
 * Given a group, destroy all of the marks associated with that group.
 */
void fsnotify_clear_marks_by_group(struct fsnotify_group *group)
{
	struct fsnotify_mark *lmark, *mark;
	LIST_HEAD(free_list);

	spin_lock(&group->mark_lock);
	list_for_each_entry_safe(mark, lmark, &group->marks_list, g_list) {
		list_add(&mark->free_g_list, &free_list);
		list_del_init(&mark->g_list);
		fsnotify_get_mark(mark);
	}
	spin_unlock(&group->mark_lock);

	list_for_each_entry_safe(mark, lmark, &free_list, free_g_list) {
		fsnotify_destroy_mark(mark);
		fsnotify_put_mark(mark);
	}
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

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

#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/srcu.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"
#include "../mount.h"

/*
 * Clear all of the marks on an inode when it is being evicted from core
 */
void __fsnotify_inode_delete(struct inode *inode)
{
	fsnotify_clear_marks_by_inode(inode);
}
EXPORT_SYMBOL_GPL(__fsnotify_inode_delete);

void __fsnotify_vfsmount_delete(struct vfsmount *mnt)
{
	fsnotify_clear_marks_by_mount(mnt);
}

/*
 * Given an inode, first check if we care what happens to our children.  Inotify
 * and dnotify both tell their parents about events.  If we care about any event
 * on a child we run all of our children and set a dentry flag saying that the
 * parent cares.  Thus when an event happens on a child it can quickly tell if
 * if there is a need to find a parent and send the event to the parent.
 */
void __fsnotify_update_child_dentry_flags(struct inode *inode)
{
	struct dentry *alias;
	int watched;

	if (!S_ISDIR(inode->i_mode))
		return;

	/* determine if the children should tell inode about their events */
	watched = fsnotify_inode_watches_children(inode);

	spin_lock(&inode->i_lock);
	/* run all of the dentries associated with this inode.  Since this is a
	 * directory, there damn well better only be one item on this list */
	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		struct dentry *child;

		/* run all of the children of the original inode and fix their
		 * d_flags to indicate parental interest (their parent is the
		 * original inode) */
		spin_lock(&alias->d_lock);
		list_for_each_entry(child, &alias->d_subdirs, d_child) {
			if (!child->d_inode)
				continue;

			spin_lock_nested(&child->d_lock, DENTRY_D_LOCK_NESTED);
			if (watched)
				child->d_flags |= DCACHE_FSNOTIFY_PARENT_WATCHED;
			else
				child->d_flags &= ~DCACHE_FSNOTIFY_PARENT_WATCHED;
			spin_unlock(&child->d_lock);
		}
		spin_unlock(&alias->d_lock);
	}
	spin_unlock(&inode->i_lock);
}

/* Notify this dentry's parent about a child's events. */
int __fsnotify_parent(struct path *path, struct dentry *dentry, __u32 mask)
{
	struct dentry *parent;
	struct inode *p_inode;
	int ret = 0;

	if (!dentry)
		dentry = path->dentry;

	if (!(dentry->d_flags & DCACHE_FSNOTIFY_PARENT_WATCHED))
		return 0;

	parent = dget_parent(dentry);
	p_inode = parent->d_inode;

	if (unlikely(!fsnotify_inode_watches_children(p_inode)))
		__fsnotify_update_child_dentry_flags(p_inode);
	else if (p_inode->i_fsnotify_mask & mask) {
		/* we are notifying a parent so come up with the new mask which
		 * specifies these are events which came from a child. */
		mask |= FS_EVENT_ON_CHILD;

		if (path)
			ret = fsnotify(p_inode, mask, path, FSNOTIFY_EVENT_PATH,
				       dentry->d_name.name, 0);
		else
			ret = fsnotify(p_inode, mask, dentry->d_inode, FSNOTIFY_EVENT_INODE,
				       dentry->d_name.name, 0);
	}

	dput(parent);

	return ret;
}
EXPORT_SYMBOL_GPL(__fsnotify_parent);

static int send_to_group(struct inode *to_tell,
			 struct fsnotify_mark *inode_mark,
			 struct fsnotify_mark *vfsmount_mark,
			 __u32 mask, void *data,
			 int data_is, u32 cookie,
			 const unsigned char *file_name)
{
	struct fsnotify_group *group = NULL;
	__u32 inode_test_mask = 0;
	__u32 vfsmount_test_mask = 0;

	if (unlikely(!inode_mark && !vfsmount_mark)) {
		BUG();
		return 0;
	}

	/* clear ignored on inode modification */
	if (mask & FS_MODIFY) {
		if (inode_mark &&
		    !(inode_mark->flags & FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY))
			inode_mark->ignored_mask = 0;
		if (vfsmount_mark &&
		    !(vfsmount_mark->flags & FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY))
			vfsmount_mark->ignored_mask = 0;
	}

	/* does the inode mark tell us to do something? */
	if (inode_mark) {
		group = inode_mark->group;
		inode_test_mask = (mask & ~FS_EVENT_ON_CHILD);
		inode_test_mask &= inode_mark->mask;
		inode_test_mask &= ~inode_mark->ignored_mask;
	}

	/* does the vfsmount_mark tell us to do something? */
	if (vfsmount_mark) {
		vfsmount_test_mask = (mask & ~FS_EVENT_ON_CHILD);
		group = vfsmount_mark->group;
		vfsmount_test_mask &= vfsmount_mark->mask;
		vfsmount_test_mask &= ~vfsmount_mark->ignored_mask;
		if (inode_mark)
			vfsmount_test_mask &= ~inode_mark->ignored_mask;
	}

	pr_debug("%s: group=%p to_tell=%p mask=%x inode_mark=%p"
		 " inode_test_mask=%x vfsmount_mark=%p vfsmount_test_mask=%x"
		 " data=%p data_is=%d cookie=%d\n",
		 __func__, group, to_tell, mask, inode_mark,
		 inode_test_mask, vfsmount_mark, vfsmount_test_mask, data,
		 data_is, cookie);

	if (!inode_test_mask && !vfsmount_test_mask)
		return 0;

	return group->ops->handle_event(group, to_tell, inode_mark,
					vfsmount_mark, mask, data, data_is,
					file_name, cookie);
}

/*
 * This is the main call to fsnotify.  The VFS calls into hook specific functions
 * in linux/fsnotify.h.  Those functions then in turn call here.  Here will call
 * out to all of the registered fsnotify_group.  Those groups can then use the
 * notification event in whatever means they feel necessary.
 */
int fsnotify(struct inode *to_tell, __u32 mask, void *data, int data_is,
	     const unsigned char *file_name, u32 cookie)
{
	struct hlist_node *inode_node = NULL, *vfsmount_node = NULL;
	struct fsnotify_mark *inode_mark = NULL, *vfsmount_mark = NULL;
	struct fsnotify_group *inode_group, *vfsmount_group;
	struct mount *mnt;
	int idx, ret = 0;
	/* global tests shouldn't care about events on child only the specific event */
	__u32 test_mask = (mask & ~FS_EVENT_ON_CHILD);

	if (data_is == FSNOTIFY_EVENT_PATH)
		mnt = real_mount(((struct path *)data)->mnt);
	else
		mnt = NULL;

	/*
	 * if this is a modify event we may need to clear the ignored masks
	 * otherwise return if neither the inode nor the vfsmount care about
	 * this type of event.
	 */
	if (!(mask & FS_MODIFY) &&
	    !(test_mask & to_tell->i_fsnotify_mask) &&
	    !(mnt && test_mask & mnt->mnt_fsnotify_mask))
		return 0;

	idx = srcu_read_lock(&fsnotify_mark_srcu);

	if ((mask & FS_MODIFY) ||
	    (test_mask & to_tell->i_fsnotify_mask))
		inode_node = srcu_dereference(to_tell->i_fsnotify_marks.first,
					      &fsnotify_mark_srcu);

	if (mnt && ((mask & FS_MODIFY) ||
		    (test_mask & mnt->mnt_fsnotify_mask))) {
		vfsmount_node = srcu_dereference(mnt->mnt_fsnotify_marks.first,
						 &fsnotify_mark_srcu);
		inode_node = srcu_dereference(to_tell->i_fsnotify_marks.first,
					      &fsnotify_mark_srcu);
	}

	while (inode_node || vfsmount_node) {
		inode_group = vfsmount_group = NULL;

		if (inode_node) {
			inode_mark = hlist_entry(srcu_dereference(inode_node, &fsnotify_mark_srcu),
						 struct fsnotify_mark, i.i_list);
			inode_group = inode_mark->group;
		}

		if (vfsmount_node) {
			vfsmount_mark = hlist_entry(srcu_dereference(vfsmount_node, &fsnotify_mark_srcu),
							struct fsnotify_mark, m.m_list);
			vfsmount_group = vfsmount_mark->group;
		}

		if (inode_group > vfsmount_group) {
			/* handle inode */
			ret = send_to_group(to_tell, inode_mark, NULL, mask,
					    data, data_is, cookie, file_name);
			/* we didn't use the vfsmount_mark */
			vfsmount_group = NULL;
		} else if (vfsmount_group > inode_group) {
			ret = send_to_group(to_tell, NULL, vfsmount_mark, mask,
					    data, data_is, cookie, file_name);
			inode_group = NULL;
		} else {
			ret = send_to_group(to_tell, inode_mark, vfsmount_mark,
					    mask, data, data_is, cookie,
					    file_name);
		}

		if (ret && (mask & ALL_FSNOTIFY_PERM_EVENTS))
			goto out;

		if (inode_group)
			inode_node = srcu_dereference(inode_node->next,
						      &fsnotify_mark_srcu);
		if (vfsmount_group)
			vfsmount_node = srcu_dereference(vfsmount_node->next,
							 &fsnotify_mark_srcu);
	}
	ret = 0;
out:
	srcu_read_unlock(&fsnotify_mark_srcu, idx);

	return ret;
}
EXPORT_SYMBOL_GPL(fsnotify);

static __init int fsnotify_init(void)
{
	int ret;

	BUG_ON(hweight32(ALL_FSNOTIFY_EVENTS) != 23);

	ret = init_srcu_struct(&fsnotify_mark_srcu);
	if (ret)
		panic("initializing fsnotify_mark_srcu");

	return 0;
}
core_initcall(fsnotify_init);

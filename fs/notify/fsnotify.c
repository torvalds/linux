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

/*
 * Clear all of the marks on an inode when it is being evicted from core
 */
void __fsnotify_inode_delete(struct inode *inode)
{
	fsnotify_clear_marks_by_inode(inode);
}
EXPORT_SYMBOL_GPL(__fsnotify_inode_delete);

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

	spin_lock(&dcache_lock);
	/* run all of the dentries associated with this inode.  Since this is a
	 * directory, there damn well better only be one item on this list */
	list_for_each_entry(alias, &inode->i_dentry, d_alias) {
		struct dentry *child;

		/* run all of the children of the original inode and fix their
		 * d_flags to indicate parental interest (their parent is the
		 * original inode) */
		list_for_each_entry(child, &alias->d_subdirs, d_u.d_child) {
			if (!child->d_inode)
				continue;

			spin_lock(&child->d_lock);
			if (watched)
				child->d_flags |= DCACHE_FSNOTIFY_PARENT_WATCHED;
			else
				child->d_flags &= ~DCACHE_FSNOTIFY_PARENT_WATCHED;
			spin_unlock(&child->d_lock);
		}
	}
	spin_unlock(&dcache_lock);
}

/* Notify this dentry's parent about a child's events. */
void __fsnotify_parent(struct path *path, struct dentry *dentry, __u32 mask)
{
	struct dentry *parent;
	struct inode *p_inode;
	bool send = false;
	bool should_update_children = false;

	if (!dentry)
		dentry = path->dentry;

	if (!(dentry->d_flags & DCACHE_FSNOTIFY_PARENT_WATCHED))
		return;

	spin_lock(&dentry->d_lock);
	parent = dentry->d_parent;
	p_inode = parent->d_inode;

	if (fsnotify_inode_watches_children(p_inode)) {
		if (p_inode->i_fsnotify_mask & mask) {
			dget(parent);
			send = true;
		}
	} else {
		/*
		 * The parent doesn't care about events on it's children but
		 * at least one child thought it did.  We need to run all the
		 * children and update their d_flags to let them know p_inode
		 * doesn't care about them any more.
		 */
		dget(parent);
		should_update_children = true;
	}

	spin_unlock(&dentry->d_lock);

	if (send) {
		/* we are notifying a parent so come up with the new mask which
		 * specifies these are events which came from a child. */
		mask |= FS_EVENT_ON_CHILD;

		if (path)
			fsnotify(p_inode, mask, path, FSNOTIFY_EVENT_PATH,
				 dentry->d_name.name, 0);
		else
			fsnotify(p_inode, mask, dentry->d_inode, FSNOTIFY_EVENT_INODE,
				 dentry->d_name.name, 0);
		dput(parent);
	}

	if (unlikely(should_update_children)) {
		__fsnotify_update_child_dentry_flags(p_inode);
		dput(parent);
	}
}
EXPORT_SYMBOL_GPL(__fsnotify_parent);

static void send_to_group(struct fsnotify_group *group, struct inode *to_tell,
			  struct vfsmount *mnt, __u32 mask, void *data,
			  int data_is, u32 cookie, const char *file_name,
			  struct fsnotify_event **event)
{
	if (!group->ops->should_send_event(group, to_tell, mnt, mask,
					   data, data_is))
		return;
	if (!*event) {
		*event = fsnotify_create_event(to_tell, mask, data,
						data_is, file_name,
						cookie, GFP_KERNEL);
		/*
		 * shit, we OOM'd and now we can't tell, maybe
		 * someday someone else will want to do something
		 * here
		 */
		if (!*event)
			return;
	}
	group->ops->handle_event(group, *event);
}

static bool needed_by_vfsmount(__u32 test_mask, struct vfsmount *mnt)
{
	if (!mnt)
		return false;

	/* hook in this when mnt->mnt_fsnotify_mask is defined */
	/* return (test_mask & path->mnt->mnt_fsnotify_mask); */
	return false;
}
/*
 * This is the main call to fsnotify.  The VFS calls into hook specific functions
 * in linux/fsnotify.h.  Those functions then in turn call here.  Here will call
 * out to all of the registered fsnotify_group.  Those groups can then use the
 * notification event in whatever means they feel necessary.
 */
void fsnotify(struct inode *to_tell, __u32 mask, void *data, int data_is, const char *file_name, u32 cookie)
{
	struct fsnotify_group *group;
	struct fsnotify_event *event = NULL;
	struct vfsmount *mnt = NULL;
	int idx;
	/* global tests shouldn't care about events on child only the specific event */
	__u32 test_mask = (mask & ~FS_EVENT_ON_CHILD);

	/* if no fsnotify listeners, nothing to do */
	if (list_empty(&fsnotify_inode_groups) &&
	    list_empty(&fsnotify_vfsmount_groups))
                return;
 
	/* if none of the directed listeners or vfsmount listeners care */
	if (!(test_mask & fsnotify_inode_mask) &&
	    !(test_mask & fsnotify_vfsmount_mask))
                return;
 
	if (data_is == FSNOTIFY_EVENT_PATH)
		mnt = ((struct path *)data)->mnt;

	/* if this inode's directed listeners don't care and nothing on the vfsmount
	 * listeners list cares, nothing to do */
	if (!(test_mask & to_tell->i_fsnotify_mask) &&
	    !needed_by_vfsmount(test_mask, mnt))
                return;

	/*
	 * SRCU!!  the groups list is very very much read only and the path is
	 * very hot.  The VAST majority of events are not going to need to do
	 * anything other than walk the list so it's crazy to pre-allocate.
	 */
	idx = srcu_read_lock(&fsnotify_grp_srcu);

	if (test_mask & to_tell->i_fsnotify_mask) {
		list_for_each_entry_rcu(group, &fsnotify_inode_groups, inode_group_list) {
			if (test_mask & group->mask) {
				send_to_group(group, to_tell, NULL, mask, data, data_is,
					      cookie, file_name, &event);
			}
		}
	}
	if (needed_by_vfsmount(test_mask, mnt)) {
		list_for_each_entry_rcu(group, &fsnotify_vfsmount_groups, vfsmount_group_list) {
			if (test_mask & group->mask) {
				send_to_group(group, to_tell, mnt, mask, data, data_is,
					      cookie, file_name, &event);
			}
		}
	}

	srcu_read_unlock(&fsnotify_grp_srcu, idx);
	/*
	 * fsnotify_create_event() took a reference so the event can't be cleaned
	 * up while we are still trying to add it to lists, drop that one.
	 */
	if (event)
		fsnotify_put_event(event);
}
EXPORT_SYMBOL_GPL(fsnotify);

static __init int fsnotify_init(void)
{
	return init_srcu_struct(&fsnotify_grp_srcu);
}
subsys_initcall(fsnotify_init);

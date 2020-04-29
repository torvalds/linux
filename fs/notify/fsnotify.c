// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
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

void __fsnotify_vfsmount_delete(struct vfsmount *mnt)
{
	fsnotify_clear_marks_by_mount(mnt);
}

/**
 * fsnotify_unmount_inodes - an sb is unmounting.  handle any watched inodes.
 * @sb: superblock being unmounted.
 *
 * Called during unmount with no locks held, so needs to be safe against
 * concurrent modifiers. We temporarily drop sb->s_inode_list_lock and CAN block.
 */
static void fsnotify_unmount_inodes(struct super_block *sb)
{
	struct inode *inode, *iput_inode = NULL;

	spin_lock(&sb->s_inode_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		/*
		 * We cannot __iget() an inode in state I_FREEING,
		 * I_WILL_FREE, or I_NEW which is fine because by that point
		 * the inode cannot have any associated watches.
		 */
		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_FREEING|I_WILL_FREE|I_NEW)) {
			spin_unlock(&inode->i_lock);
			continue;
		}

		/*
		 * If i_count is zero, the inode cannot have any watches and
		 * doing an __iget/iput with SB_ACTIVE clear would actually
		 * evict all inodes with zero i_count from icache which is
		 * unnecessarily violent and may in fact be illegal to do.
		 * However, we should have been called /after/ evict_inodes
		 * removed all zero refcount inodes, in any case.  Test to
		 * be sure.
		 */
		if (!atomic_read(&inode->i_count)) {
			spin_unlock(&inode->i_lock);
			continue;
		}

		__iget(inode);
		spin_unlock(&inode->i_lock);
		spin_unlock(&sb->s_inode_list_lock);

		if (iput_inode)
			iput(iput_inode);

		/* for each watch, send FS_UNMOUNT and then remove it */
		fsnotify(inode, FS_UNMOUNT, inode, FSNOTIFY_EVENT_INODE, NULL, 0);

		fsnotify_inode_delete(inode);

		iput_inode = inode;

		cond_resched();
		spin_lock(&sb->s_inode_list_lock);
	}
	spin_unlock(&sb->s_inode_list_lock);

	if (iput_inode)
		iput(iput_inode);
	/* Wait for outstanding inode references from connectors */
	wait_var_event(&sb->s_fsnotify_inode_refs,
		       !atomic_long_read(&sb->s_fsnotify_inode_refs));
}

void fsnotify_sb_delete(struct super_block *sb)
{
	fsnotify_unmount_inodes(sb);
	fsnotify_clear_marks_by_sb(sb);
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
int fsnotify_parent(struct dentry *dentry, __u32 mask, const void *data,
		    int data_type)
{
	struct dentry *parent;
	struct inode *p_inode;
	int ret = 0;

	if (!(dentry->d_flags & DCACHE_FSNOTIFY_PARENT_WATCHED))
		return 0;

	parent = dget_parent(dentry);
	p_inode = parent->d_inode;

	if (unlikely(!fsnotify_inode_watches_children(p_inode))) {
		__fsnotify_update_child_dentry_flags(p_inode);
	} else if (p_inode->i_fsnotify_mask & mask & ALL_FSNOTIFY_EVENTS) {
		struct name_snapshot name;

		/* we are notifying a parent so come up with the new mask which
		 * specifies these are events which came from a child. */
		mask |= FS_EVENT_ON_CHILD;

		take_dentry_name_snapshot(&name, dentry);
		ret = fsnotify(p_inode, mask, data, data_type, &name.name, 0);
		release_dentry_name_snapshot(&name);
	}

	dput(parent);

	return ret;
}
EXPORT_SYMBOL_GPL(fsnotify_parent);

static int send_to_group(struct inode *to_tell,
			 __u32 mask, const void *data,
			 int data_is, u32 cookie,
			 const struct qstr *file_name,
			 struct fsnotify_iter_info *iter_info)
{
	struct fsnotify_group *group = NULL;
	__u32 test_mask = (mask & ALL_FSNOTIFY_EVENTS);
	__u32 marks_mask = 0;
	__u32 marks_ignored_mask = 0;
	struct fsnotify_mark *mark;
	int type;

	if (WARN_ON(!iter_info->report_mask))
		return 0;

	/* clear ignored on inode modification */
	if (mask & FS_MODIFY) {
		fsnotify_foreach_obj_type(type) {
			if (!fsnotify_iter_should_report_type(iter_info, type))
				continue;
			mark = iter_info->marks[type];
			if (mark &&
			    !(mark->flags & FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY))
				mark->ignored_mask = 0;
		}
	}

	fsnotify_foreach_obj_type(type) {
		if (!fsnotify_iter_should_report_type(iter_info, type))
			continue;
		mark = iter_info->marks[type];
		/* does the object mark tell us to do something? */
		if (mark) {
			group = mark->group;
			marks_mask |= mark->mask;
			marks_ignored_mask |= mark->ignored_mask;
		}
	}

	pr_debug("%s: group=%p to_tell=%p mask=%x marks_mask=%x marks_ignored_mask=%x"
		 " data=%p data_is=%d cookie=%d\n",
		 __func__, group, to_tell, mask, marks_mask, marks_ignored_mask,
		 data, data_is, cookie);

	if (!(test_mask & marks_mask & ~marks_ignored_mask))
		return 0;

	return group->ops->handle_event(group, to_tell, mask, data, data_is,
					file_name, cookie, iter_info);
}

static struct fsnotify_mark *fsnotify_first_mark(struct fsnotify_mark_connector **connp)
{
	struct fsnotify_mark_connector *conn;
	struct hlist_node *node = NULL;

	conn = srcu_dereference(*connp, &fsnotify_mark_srcu);
	if (conn)
		node = srcu_dereference(conn->list.first, &fsnotify_mark_srcu);

	return hlist_entry_safe(node, struct fsnotify_mark, obj_list);
}

static struct fsnotify_mark *fsnotify_next_mark(struct fsnotify_mark *mark)
{
	struct hlist_node *node = NULL;

	if (mark)
		node = srcu_dereference(mark->obj_list.next,
					&fsnotify_mark_srcu);

	return hlist_entry_safe(node, struct fsnotify_mark, obj_list);
}

/*
 * iter_info is a multi head priority queue of marks.
 * Pick a subset of marks from queue heads, all with the
 * same group and set the report_mask for selected subset.
 * Returns the report_mask of the selected subset.
 */
static unsigned int fsnotify_iter_select_report_types(
		struct fsnotify_iter_info *iter_info)
{
	struct fsnotify_group *max_prio_group = NULL;
	struct fsnotify_mark *mark;
	int type;

	/* Choose max prio group among groups of all queue heads */
	fsnotify_foreach_obj_type(type) {
		mark = iter_info->marks[type];
		if (mark &&
		    fsnotify_compare_groups(max_prio_group, mark->group) > 0)
			max_prio_group = mark->group;
	}

	if (!max_prio_group)
		return 0;

	/* Set the report mask for marks from same group as max prio group */
	iter_info->report_mask = 0;
	fsnotify_foreach_obj_type(type) {
		mark = iter_info->marks[type];
		if (mark &&
		    fsnotify_compare_groups(max_prio_group, mark->group) == 0)
			fsnotify_iter_set_report_type(iter_info, type);
	}

	return iter_info->report_mask;
}

/*
 * Pop from iter_info multi head queue, the marks that were iterated in the
 * current iteration step.
 */
static void fsnotify_iter_next(struct fsnotify_iter_info *iter_info)
{
	int type;

	fsnotify_foreach_obj_type(type) {
		if (fsnotify_iter_should_report_type(iter_info, type))
			iter_info->marks[type] =
				fsnotify_next_mark(iter_info->marks[type]);
	}
}

/*
 * This is the main call to fsnotify.  The VFS calls into hook specific functions
 * in linux/fsnotify.h.  Those functions then in turn call here.  Here will call
 * out to all of the registered fsnotify_group.  Those groups can then use the
 * notification event in whatever means they feel necessary.
 */
int fsnotify(struct inode *to_tell, __u32 mask, const void *data, int data_is,
	     const struct qstr *file_name, u32 cookie)
{
	const struct path *path = fsnotify_data_path(data, data_is);
	struct fsnotify_iter_info iter_info = {};
	struct super_block *sb = to_tell->i_sb;
	struct mount *mnt = NULL;
	__u32 mnt_or_sb_mask = sb->s_fsnotify_mask;
	int ret = 0;
	__u32 test_mask = (mask & ALL_FSNOTIFY_EVENTS);

	if (path) {
		mnt = real_mount(path->mnt);
		mnt_or_sb_mask |= mnt->mnt_fsnotify_mask;
	}
	/* An event "on child" is not intended for a mount/sb mark */
	if (mask & FS_EVENT_ON_CHILD)
		mnt_or_sb_mask = 0;

	/*
	 * Optimization: srcu_read_lock() has a memory barrier which can
	 * be expensive.  It protects walking the *_fsnotify_marks lists.
	 * However, if we do not walk the lists, we do not have to do
	 * SRCU because we have no references to any objects and do not
	 * need SRCU to keep them "alive".
	 */
	if (!to_tell->i_fsnotify_marks && !sb->s_fsnotify_marks &&
	    (!mnt || !mnt->mnt_fsnotify_marks))
		return 0;
	/*
	 * if this is a modify event we may need to clear the ignored masks
	 * otherwise return if neither the inode nor the vfsmount/sb care about
	 * this type of event.
	 */
	if (!(mask & FS_MODIFY) &&
	    !(test_mask & (to_tell->i_fsnotify_mask | mnt_or_sb_mask)))
		return 0;

	iter_info.srcu_idx = srcu_read_lock(&fsnotify_mark_srcu);

	iter_info.marks[FSNOTIFY_OBJ_TYPE_INODE] =
		fsnotify_first_mark(&to_tell->i_fsnotify_marks);
	iter_info.marks[FSNOTIFY_OBJ_TYPE_SB] =
		fsnotify_first_mark(&sb->s_fsnotify_marks);
	if (mnt) {
		iter_info.marks[FSNOTIFY_OBJ_TYPE_VFSMOUNT] =
			fsnotify_first_mark(&mnt->mnt_fsnotify_marks);
	}

	/*
	 * We need to merge inode/vfsmount/sb mark lists so that e.g. inode mark
	 * ignore masks are properly reflected for mount/sb mark notifications.
	 * That's why this traversal is so complicated...
	 */
	while (fsnotify_iter_select_report_types(&iter_info)) {
		ret = send_to_group(to_tell, mask, data, data_is, cookie,
				    file_name, &iter_info);

		if (ret && (mask & ALL_FSNOTIFY_PERM_EVENTS))
			goto out;

		fsnotify_iter_next(&iter_info);
	}
	ret = 0;
out:
	srcu_read_unlock(&fsnotify_mark_srcu, iter_info.srcu_idx);

	return ret;
}
EXPORT_SYMBOL_GPL(fsnotify);

static __init int fsnotify_init(void)
{
	int ret;

	BUILD_BUG_ON(HWEIGHT32(ALL_FSNOTIFY_BITS) != 26);

	ret = init_srcu_struct(&fsnotify_mark_srcu);
	if (ret)
		panic("initializing fsnotify_mark_srcu");

	fsnotify_mark_connector_cachep = KMEM_CACHE(fsnotify_mark_connector,
						    SLAB_PANIC);

	return 0;
}
core_initcall(fsnotify_init);

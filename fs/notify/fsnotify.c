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

void __fsnotify_mntns_delete(struct mnt_namespace *mntns)
{
	fsnotify_clear_marks_by_mntns(mntns);
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
		if (!icount_read(inode)) {
			spin_unlock(&inode->i_lock);
			continue;
		}

		__iget(inode);
		spin_unlock(&inode->i_lock);
		spin_unlock(&sb->s_inode_list_lock);

		iput(iput_inode);

		/* for each watch, send FS_UNMOUNT and then remove it */
		fsnotify_inode(inode, FS_UNMOUNT);

		fsnotify_inode_delete(inode);

		iput_inode = inode;

		cond_resched();
		spin_lock(&sb->s_inode_list_lock);
	}
	spin_unlock(&sb->s_inode_list_lock);

	iput(iput_inode);
}

void fsnotify_sb_delete(struct super_block *sb)
{
	struct fsnotify_sb_info *sbinfo = fsnotify_sb_info(sb);

	/* Were any marks ever added to any object on this sb? */
	if (!sbinfo)
		return;

	fsnotify_unmount_inodes(sb);
	fsnotify_clear_marks_by_sb(sb);
	/* Wait for outstanding object references from connectors */
	wait_var_event(fsnotify_sb_watched_objects(sb),
		       !atomic_long_read(fsnotify_sb_watched_objects(sb)));
	WARN_ON(fsnotify_sb_has_priority_watchers(sb, FSNOTIFY_PRIO_CONTENT));
	WARN_ON(fsnotify_sb_has_priority_watchers(sb,
						  FSNOTIFY_PRIO_PRE_CONTENT));
}

void fsnotify_sb_free(struct super_block *sb)
{
	kfree(sb->s_fsnotify_info);
}

/*
 * Given an inode, first check if we care what happens to our children.  Inotify
 * and dnotify both tell their parents about events.  If we care about any event
 * on a child we run all of our children and set a dentry flag saying that the
 * parent cares.  Thus when an event happens on a child it can quickly tell
 * if there is a need to find a parent and send the event to the parent.
 */
void fsnotify_set_children_dentry_flags(struct inode *inode)
{
	struct dentry *alias;

	if (!S_ISDIR(inode->i_mode))
		return;

	spin_lock(&inode->i_lock);
	/* run all of the dentries associated with this inode.  Since this is a
	 * directory, there damn well better only be one item on this list */
	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		struct dentry *child;

		/* run all of the children of the original inode and fix their
		 * d_flags to indicate parental interest (their parent is the
		 * original inode) */
		spin_lock(&alias->d_lock);
		hlist_for_each_entry(child, &alias->d_children, d_sib) {
			if (!child->d_inode)
				continue;

			spin_lock_nested(&child->d_lock, DENTRY_D_LOCK_NESTED);
			child->d_flags |= DCACHE_FSNOTIFY_PARENT_WATCHED;
			spin_unlock(&child->d_lock);
		}
		spin_unlock(&alias->d_lock);
	}
	spin_unlock(&inode->i_lock);
}

/*
 * Lazily clear false positive PARENT_WATCHED flag for child whose parent had
 * stopped watching children.
 */
static void fsnotify_clear_child_dentry_flag(struct inode *pinode,
					     struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	/*
	 * d_lock is a sufficient barrier to prevent observing a non-watched
	 * parent state from before the fsnotify_set_children_dentry_flags()
	 * or fsnotify_update_flags() call that had set PARENT_WATCHED.
	 */
	if (!fsnotify_inode_watches_children(pinode))
		dentry->d_flags &= ~DCACHE_FSNOTIFY_PARENT_WATCHED;
	spin_unlock(&dentry->d_lock);
}

/* Are inode/sb/mount interested in parent and name info with this event? */
static bool fsnotify_event_needs_parent(struct inode *inode, __u32 mnt_mask,
					__u32 mask)
{
	__u32 marks_mask = 0;

	/* We only send parent/name to inode/sb/mount for events on non-dir */
	if (mask & FS_ISDIR)
		return false;

	/*
	 * All events that are possible on child can also may be reported with
	 * parent/name info to inode/sb/mount.  Otherwise, a watching parent
	 * could result in events reported with unexpected name info to sb/mount.
	 */
	BUILD_BUG_ON(FS_EVENTS_POSS_ON_CHILD & ~FS_EVENTS_POSS_TO_PARENT);

	/* Did either inode/sb/mount subscribe for events with parent/name? */
	marks_mask |= fsnotify_parent_needed_mask(
				READ_ONCE(inode->i_fsnotify_mask));
	marks_mask |= fsnotify_parent_needed_mask(
				READ_ONCE(inode->i_sb->s_fsnotify_mask));
	marks_mask |= fsnotify_parent_needed_mask(mnt_mask);

	/* Did they subscribe for this event with parent/name info? */
	return mask & marks_mask;
}

/* Are there any inode/mount/sb objects that watch for these events? */
static inline __u32 fsnotify_object_watched(struct inode *inode, __u32 mnt_mask,
					    __u32 mask)
{
	__u32 marks_mask = READ_ONCE(inode->i_fsnotify_mask) | mnt_mask |
			   READ_ONCE(inode->i_sb->s_fsnotify_mask);

	return mask & marks_mask & ALL_FSNOTIFY_EVENTS;
}

/* Report pre-content event with optional range info */
int fsnotify_pre_content(const struct path *path, const loff_t *ppos,
			 size_t count)
{
	struct file_range range;

	/* Report page aligned range only when pos is known */
	if (!ppos)
		return fsnotify_path(path, FS_PRE_ACCESS);

	range.path = path;
	range.pos = PAGE_ALIGN_DOWN(*ppos);
	range.count = PAGE_ALIGN(*ppos + count) - range.pos;

	return fsnotify_parent(path->dentry, FS_PRE_ACCESS, &range,
			       FSNOTIFY_EVENT_FILE_RANGE);
}

/*
 * Notify this dentry's parent about a child's events with child name info
 * if parent is watching or if inode/sb/mount are interested in events with
 * parent and name info.
 *
 * Notify only the child without name info if parent is not watching and
 * inode/sb/mount are not interested in events with parent and name info.
 */
int __fsnotify_parent(struct dentry *dentry, __u32 mask, const void *data,
		      int data_type)
{
	const struct path *path = fsnotify_data_path(data, data_type);
	__u32 mnt_mask = path ?
		READ_ONCE(real_mount(path->mnt)->mnt_fsnotify_mask) : 0;
	struct inode *inode = d_inode(dentry);
	struct dentry *parent;
	bool parent_watched = dentry->d_flags & DCACHE_FSNOTIFY_PARENT_WATCHED;
	bool parent_needed, parent_interested;
	__u32 p_mask;
	struct inode *p_inode = NULL;
	struct name_snapshot name;
	struct qstr *file_name = NULL;
	int ret = 0;

	/* Optimize the likely case of nobody watching this path */
	if (likely(!parent_watched &&
		   !fsnotify_object_watched(inode, mnt_mask, mask)))
		return 0;

	parent = NULL;
	parent_needed = fsnotify_event_needs_parent(inode, mnt_mask, mask);
	if (!parent_watched && !parent_needed)
		goto notify;

	/* Does parent inode care about events on children? */
	parent = dget_parent(dentry);
	p_inode = parent->d_inode;
	p_mask = fsnotify_inode_watches_children(p_inode);
	if (unlikely(parent_watched && !p_mask))
		fsnotify_clear_child_dentry_flag(p_inode, dentry);

	/*
	 * Include parent/name in notification either if some notification
	 * groups require parent info or the parent is interested in this event.
	 */
	parent_interested = mask & p_mask & ALL_FSNOTIFY_EVENTS;
	if (parent_needed || parent_interested) {
		/* When notifying parent, child should be passed as data */
		WARN_ON_ONCE(inode != fsnotify_data_inode(data, data_type));

		/* Notify both parent and child with child name info */
		take_dentry_name_snapshot(&name, dentry);
		file_name = &name.name;
		if (parent_interested)
			mask |= FS_EVENT_ON_CHILD;
	}

notify:
	ret = fsnotify(mask, data, data_type, p_inode, file_name, inode, 0);

	if (file_name)
		release_dentry_name_snapshot(&name);
	dput(parent);

	return ret;
}
EXPORT_SYMBOL_GPL(__fsnotify_parent);

static int fsnotify_handle_inode_event(struct fsnotify_group *group,
				       struct fsnotify_mark *inode_mark,
				       u32 mask, const void *data, int data_type,
				       struct inode *dir, const struct qstr *name,
				       u32 cookie)
{
	const struct path *path = fsnotify_data_path(data, data_type);
	struct inode *inode = fsnotify_data_inode(data, data_type);
	const struct fsnotify_ops *ops = group->ops;

	if (WARN_ON_ONCE(!ops->handle_inode_event))
		return 0;

	if (WARN_ON_ONCE(!inode && !dir))
		return 0;

	if ((inode_mark->flags & FSNOTIFY_MARK_FLAG_EXCL_UNLINK) &&
	    path && d_unlinked(path->dentry))
		return 0;

	/* Check interest of this mark in case event was sent with two marks */
	if (!(mask & inode_mark->mask & ALL_FSNOTIFY_EVENTS))
		return 0;

	return ops->handle_inode_event(inode_mark, mask, inode, dir, name, cookie);
}

static int fsnotify_handle_event(struct fsnotify_group *group, __u32 mask,
				 const void *data, int data_type,
				 struct inode *dir, const struct qstr *name,
				 u32 cookie, struct fsnotify_iter_info *iter_info)
{
	struct fsnotify_mark *inode_mark = fsnotify_iter_inode_mark(iter_info);
	struct fsnotify_mark *parent_mark = fsnotify_iter_parent_mark(iter_info);
	int ret;

	if (WARN_ON_ONCE(fsnotify_iter_sb_mark(iter_info)) ||
	    WARN_ON_ONCE(fsnotify_iter_vfsmount_mark(iter_info)))
		return 0;

	/*
	 * For FS_RENAME, 'dir' is old dir and 'data' is new dentry.
	 * The only ->handle_inode_event() backend that supports FS_RENAME is
	 * dnotify, where it means file was renamed within same parent.
	 */
	if (mask & FS_RENAME) {
		struct dentry *moved = fsnotify_data_dentry(data, data_type);

		if (dir != moved->d_parent->d_inode)
			return 0;
	}

	if (parent_mark) {
		ret = fsnotify_handle_inode_event(group, parent_mark, mask,
						  data, data_type, dir, name, 0);
		if (ret)
			return ret;
	}

	if (!inode_mark)
		return 0;

	/*
	 * Some events can be sent on both parent dir and child marks (e.g.
	 * FS_ATTRIB).  If both parent dir and child are watching, report the
	 * event once to parent dir with name (if interested) and once to child
	 * without name (if interested).
	 *
	 * In any case regardless whether the parent is watching or not, the
	 * child watcher is expecting an event without the FS_EVENT_ON_CHILD
	 * flag. The file name is expected if and only if this is a directory
	 * event.
	 */
	mask &= ~FS_EVENT_ON_CHILD;
	if (!(mask & ALL_FSNOTIFY_DIRENT_EVENTS)) {
		dir = NULL;
		name = NULL;
	}

	return fsnotify_handle_inode_event(group, inode_mark, mask, data, data_type,
					   dir, name, cookie);
}

static int send_to_group(__u32 mask, const void *data, int data_type,
			 struct inode *dir, const struct qstr *file_name,
			 u32 cookie, struct fsnotify_iter_info *iter_info)
{
	struct fsnotify_group *group = NULL;
	__u32 test_mask = (mask & ALL_FSNOTIFY_EVENTS);
	__u32 marks_mask = 0;
	__u32 marks_ignore_mask = 0;
	bool is_dir = mask & FS_ISDIR;
	struct fsnotify_mark *mark;
	int type;

	if (!iter_info->report_mask)
		return 0;

	/* clear ignored on inode modification */
	if (mask & FS_MODIFY) {
		fsnotify_foreach_iter_mark_type(iter_info, mark, type) {
			if (!(mark->flags &
			      FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY))
				mark->ignore_mask = 0;
		}
	}

	/* Are any of the group marks interested in this event? */
	fsnotify_foreach_iter_mark_type(iter_info, mark, type) {
		group = mark->group;
		marks_mask |= mark->mask;
		marks_ignore_mask |=
			fsnotify_effective_ignore_mask(mark, is_dir, type);
	}

	pr_debug("%s: group=%p mask=%x marks_mask=%x marks_ignore_mask=%x data=%p data_type=%d dir=%p cookie=%d\n",
		 __func__, group, mask, marks_mask, marks_ignore_mask,
		 data, data_type, dir, cookie);

	if (!(test_mask & marks_mask & ~marks_ignore_mask))
		return 0;

	if (group->ops->handle_event) {
		return group->ops->handle_event(group, mask, data, data_type, dir,
						file_name, cookie, iter_info);
	}

	return fsnotify_handle_event(group, mask, data, data_type, dir,
				     file_name, cookie, iter_info);
}

static struct fsnotify_mark *fsnotify_first_mark(struct fsnotify_mark_connector *const *connp)
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
 * Pick a subset of marks from queue heads, all with the same group
 * and set the report_mask to a subset of the selected marks.
 * Returns false if there are no more groups to iterate.
 */
static bool fsnotify_iter_select_report_types(
		struct fsnotify_iter_info *iter_info)
{
	struct fsnotify_group *max_prio_group = NULL;
	struct fsnotify_mark *mark;
	int type;

	/* Choose max prio group among groups of all queue heads */
	fsnotify_foreach_iter_type(type) {
		mark = iter_info->marks[type];
		if (mark &&
		    fsnotify_compare_groups(max_prio_group, mark->group) > 0)
			max_prio_group = mark->group;
	}

	if (!max_prio_group)
		return false;

	/* Set the report mask for marks from same group as max prio group */
	iter_info->current_group = max_prio_group;
	iter_info->report_mask = 0;
	fsnotify_foreach_iter_type(type) {
		mark = iter_info->marks[type];
		if (mark && mark->group == iter_info->current_group) {
			/*
			 * FSNOTIFY_ITER_TYPE_PARENT indicates that this inode
			 * is watching children and interested in this event,
			 * which is an event possible on child.
			 * But is *this mark* watching children?
			 */
			if (type == FSNOTIFY_ITER_TYPE_PARENT &&
			    !(mark->mask & FS_EVENT_ON_CHILD) &&
			    !(fsnotify_ignore_mask(mark) & FS_EVENT_ON_CHILD))
				continue;

			fsnotify_iter_set_report_type(iter_info, type);
		}
	}

	return true;
}

/*
 * Pop from iter_info multi head queue, the marks that belong to the group of
 * current iteration step.
 */
static void fsnotify_iter_next(struct fsnotify_iter_info *iter_info)
{
	struct fsnotify_mark *mark;
	int type;

	/*
	 * We cannot use fsnotify_foreach_iter_mark_type() here because we
	 * may need to advance a mark of type X that belongs to current_group
	 * but was not selected for reporting.
	 */
	fsnotify_foreach_iter_type(type) {
		mark = iter_info->marks[type];
		if (mark && mark->group == iter_info->current_group)
			iter_info->marks[type] =
				fsnotify_next_mark(iter_info->marks[type]);
	}
}

/*
 * fsnotify - This is the main call to fsnotify.
 *
 * The VFS calls into hook specific functions in linux/fsnotify.h.
 * Those functions then in turn call here.  Here will call out to all of the
 * registered fsnotify_group.  Those groups can then use the notification event
 * in whatever means they feel necessary.
 *
 * @mask:	event type and flags
 * @data:	object that event happened on
 * @data_type:	type of object for fanotify_data_XXX() accessors
 * @dir:	optional directory associated with event -
 *		if @file_name is not NULL, this is the directory that
 *		@file_name is relative to
 * @file_name:	optional file name associated with event
 * @inode:	optional inode associated with event -
 *		If @dir and @inode are both non-NULL, event may be
 *		reported to both.
 * @cookie:	inotify rename cookie
 */
int fsnotify(__u32 mask, const void *data, int data_type, struct inode *dir,
	     const struct qstr *file_name, struct inode *inode, u32 cookie)
{
	const struct path *path = fsnotify_data_path(data, data_type);
	struct super_block *sb = fsnotify_data_sb(data, data_type);
	const struct fsnotify_mnt *mnt_data = fsnotify_data_mnt(data, data_type);
	struct fsnotify_sb_info *sbinfo = sb ? fsnotify_sb_info(sb) : NULL;
	struct fsnotify_iter_info iter_info = {};
	struct mount *mnt = NULL;
	struct inode *inode2 = NULL;
	struct dentry *moved;
	int inode2_type;
	int ret = 0;
	__u32 test_mask, marks_mask = 0;

	if (path)
		mnt = real_mount(path->mnt);

	if (!inode) {
		/* Dirent event - report on TYPE_INODE to dir */
		inode = dir;
		/* For FS_RENAME, inode is old_dir and inode2 is new_dir */
		if (mask & FS_RENAME) {
			moved = fsnotify_data_dentry(data, data_type);
			inode2 = moved->d_parent->d_inode;
			inode2_type = FSNOTIFY_ITER_TYPE_INODE2;
		}
	} else if (mask & FS_EVENT_ON_CHILD) {
		/*
		 * Event on child - report on TYPE_PARENT to dir if it is
		 * watching children and on TYPE_INODE to child.
		 */
		inode2 = dir;
		inode2_type = FSNOTIFY_ITER_TYPE_PARENT;
	}

	/*
	 * Optimization: srcu_read_lock() has a memory barrier which can
	 * be expensive.  It protects walking the *_fsnotify_marks lists.
	 * However, if we do not walk the lists, we do not have to do
	 * SRCU because we have no references to any objects and do not
	 * need SRCU to keep them "alive".
	 */
	if ((!sbinfo || !sbinfo->sb_marks) &&
	    (!mnt || !mnt->mnt_fsnotify_marks) &&
	    (!inode || !inode->i_fsnotify_marks) &&
	    (!inode2 || !inode2->i_fsnotify_marks) &&
	    (!mnt_data || !mnt_data->ns->n_fsnotify_marks))
		return 0;

	if (sb)
		marks_mask |= READ_ONCE(sb->s_fsnotify_mask);
	if (mnt)
		marks_mask |= READ_ONCE(mnt->mnt_fsnotify_mask);
	if (inode)
		marks_mask |= READ_ONCE(inode->i_fsnotify_mask);
	if (inode2)
		marks_mask |= READ_ONCE(inode2->i_fsnotify_mask);
	if (mnt_data)
		marks_mask |= READ_ONCE(mnt_data->ns->n_fsnotify_mask);

	/*
	 * If this is a modify event we may need to clear some ignore masks.
	 * In that case, the object with ignore masks will have the FS_MODIFY
	 * event in its mask.
	 * Otherwise, return if none of the marks care about this type of event.
	 */
	test_mask = (mask & ALL_FSNOTIFY_EVENTS);
	if (!(test_mask & marks_mask))
		return 0;

	iter_info.srcu_idx = srcu_read_lock(&fsnotify_mark_srcu);

	if (sbinfo) {
		iter_info.marks[FSNOTIFY_ITER_TYPE_SB] =
			fsnotify_first_mark(&sbinfo->sb_marks);
	}
	if (mnt) {
		iter_info.marks[FSNOTIFY_ITER_TYPE_VFSMOUNT] =
			fsnotify_first_mark(&mnt->mnt_fsnotify_marks);
	}
	if (inode) {
		iter_info.marks[FSNOTIFY_ITER_TYPE_INODE] =
			fsnotify_first_mark(&inode->i_fsnotify_marks);
	}
	if (inode2) {
		iter_info.marks[inode2_type] =
			fsnotify_first_mark(&inode2->i_fsnotify_marks);
	}
	if (mnt_data) {
		iter_info.marks[FSNOTIFY_ITER_TYPE_MNTNS] =
			fsnotify_first_mark(&mnt_data->ns->n_fsnotify_marks);
	}

	/*
	 * We need to merge inode/vfsmount/sb mark lists so that e.g. inode mark
	 * ignore masks are properly reflected for mount/sb mark notifications.
	 * That's why this traversal is so complicated...
	 */
	while (fsnotify_iter_select_report_types(&iter_info)) {
		ret = send_to_group(mask, data, data_type, dir, file_name,
				    cookie, &iter_info);

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

#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
/*
 * At open time we check fsnotify_sb_has_priority_watchers(), call the open perm
 * hook and set the FMODE_NONOTIFY_ mode bits accordignly.
 * Later, fsnotify permission hooks do not check if there are permission event
 * watches, but that there were permission event watches at open time.
 */
int fsnotify_open_perm_and_set_mode(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry, *parent;
	struct super_block *sb = dentry->d_sb;
	__u32 mnt_mask, p_mask = 0;

	/* Is it a file opened by fanotify? */
	if (FMODE_FSNOTIFY_NONE(file->f_mode))
		return 0;

	/*
	 * Permission events is a super set of pre-content events, so if there
	 * are no permission event watchers, there are also no pre-content event
	 * watchers and this is implied from the single FMODE_NONOTIFY_PERM bit.
	 */
	if (likely(!fsnotify_sb_has_priority_watchers(sb,
						FSNOTIFY_PRIO_CONTENT))) {
		file_set_fsnotify_mode(file, FMODE_NONOTIFY_PERM);
		return 0;
	}

	/*
	 * OK, there are some permission event watchers. Check if anybody is
	 * watching for permission events on *this* file.
	 */
	mnt_mask = READ_ONCE(real_mount(file->f_path.mnt)->mnt_fsnotify_mask);
	p_mask = fsnotify_object_watched(d_inode(dentry), mnt_mask,
					 ALL_FSNOTIFY_PERM_EVENTS);
	if (dentry->d_flags & DCACHE_FSNOTIFY_PARENT_WATCHED) {
		parent = dget_parent(dentry);
		p_mask |= fsnotify_inode_watches_children(d_inode(parent));
		dput(parent);
	}

	/*
	 * Legacy FAN_ACCESS_PERM events have very high performance overhead,
	 * so unlikely to be used in the wild. If they are used there will be
	 * no optimizations at all.
	 */
	if (unlikely(p_mask & FS_ACCESS_PERM)) {
		/* Enable all permission and pre-content events */
		file_set_fsnotify_mode(file, 0);
		goto open_perm;
	}

	/*
	 * Pre-content events are only supported on regular files.
	 * If there are pre-content event watchers and no permission access
	 * watchers, set FMODE_NONOTIFY | FMODE_NONOTIFY_PERM to indicate that.
	 * That is the common case with HSM service.
	 */
	if (d_is_reg(dentry) && (p_mask & FSNOTIFY_PRE_CONTENT_EVENTS)) {
		file_set_fsnotify_mode(file, FMODE_NONOTIFY |
					     FMODE_NONOTIFY_PERM);
		goto open_perm;
	}

	/* Nobody watching permission and pre-content events on this file */
	file_set_fsnotify_mode(file, FMODE_NONOTIFY_PERM);

open_perm:
	/*
	 * Send open perm events depending on object masks and regardless of
	 * FMODE_NONOTIFY_PERM.
	 */
	if (file->f_flags & __FMODE_EXEC && p_mask & FS_OPEN_EXEC_PERM) {
		int ret = fsnotify_path(&file->f_path, FS_OPEN_EXEC_PERM);

		if (ret)
			return ret;
	}

	if (p_mask & FS_OPEN_PERM)
		return fsnotify_path(&file->f_path, FS_OPEN_PERM);

	return 0;
}
#endif

void fsnotify_mnt(__u32 mask, struct mnt_namespace *ns, struct vfsmount *mnt)
{
	struct fsnotify_mnt data = {
		.ns = ns,
		.mnt_id = real_mount(mnt)->mnt_id_unique,
	};

	if (WARN_ON_ONCE(!ns))
		return;

	/*
	 * This is an optimization as well as making sure fsnotify_init() has
	 * been called.
	 */
	if (!ns->n_fsnotify_marks)
		return;

	fsnotify(mask, &data, FSNOTIFY_EVENT_MNT, NULL, NULL, NULL, 0);
}

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

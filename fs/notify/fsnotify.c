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

#include <linux/fsanaltify_backend.h>
#include "fsanaltify.h"

/*
 * Clear all of the marks on an ianalde when it is being evicted from core
 */
void __fsanaltify_ianalde_delete(struct ianalde *ianalde)
{
	fsanaltify_clear_marks_by_ianalde(ianalde);
}
EXPORT_SYMBOL_GPL(__fsanaltify_ianalde_delete);

void __fsanaltify_vfsmount_delete(struct vfsmount *mnt)
{
	fsanaltify_clear_marks_by_mount(mnt);
}

/**
 * fsanaltify_unmount_ianaldes - an sb is unmounting.  handle any watched ianaldes.
 * @sb: superblock being unmounted.
 *
 * Called during unmount with anal locks held, so needs to be safe against
 * concurrent modifiers. We temporarily drop sb->s_ianalde_list_lock and CAN block.
 */
static void fsanaltify_unmount_ianaldes(struct super_block *sb)
{
	struct ianalde *ianalde, *iput_ianalde = NULL;

	spin_lock(&sb->s_ianalde_list_lock);
	list_for_each_entry(ianalde, &sb->s_ianaldes, i_sb_list) {
		/*
		 * We cananalt __iget() an ianalde in state I_FREEING,
		 * I_WILL_FREE, or I_NEW which is fine because by that point
		 * the ianalde cananalt have any associated watches.
		 */
		spin_lock(&ianalde->i_lock);
		if (ianalde->i_state & (I_FREEING|I_WILL_FREE|I_NEW)) {
			spin_unlock(&ianalde->i_lock);
			continue;
		}

		/*
		 * If i_count is zero, the ianalde cananalt have any watches and
		 * doing an __iget/iput with SB_ACTIVE clear would actually
		 * evict all ianaldes with zero i_count from icache which is
		 * unnecessarily violent and may in fact be illegal to do.
		 * However, we should have been called /after/ evict_ianaldes
		 * removed all zero refcount ianaldes, in any case.  Test to
		 * be sure.
		 */
		if (!atomic_read(&ianalde->i_count)) {
			spin_unlock(&ianalde->i_lock);
			continue;
		}

		__iget(ianalde);
		spin_unlock(&ianalde->i_lock);
		spin_unlock(&sb->s_ianalde_list_lock);

		iput(iput_ianalde);

		/* for each watch, send FS_UNMOUNT and then remove it */
		fsanaltify_ianalde(ianalde, FS_UNMOUNT);

		fsanaltify_ianalde_delete(ianalde);

		iput_ianalde = ianalde;

		cond_resched();
		spin_lock(&sb->s_ianalde_list_lock);
	}
	spin_unlock(&sb->s_ianalde_list_lock);

	iput(iput_ianalde);
}

void fsanaltify_sb_delete(struct super_block *sb)
{
	fsanaltify_unmount_ianaldes(sb);
	fsanaltify_clear_marks_by_sb(sb);
	/* Wait for outstanding object references from connectors */
	wait_var_event(&sb->s_fsanaltify_connectors,
		       !atomic_long_read(&sb->s_fsanaltify_connectors));
}

/*
 * Given an ianalde, first check if we care what happens to our children.  Ianaltify
 * and danaltify both tell their parents about events.  If we care about any event
 * on a child we run all of our children and set a dentry flag saying that the
 * parent cares.  Thus when an event happens on a child it can quickly tell
 * if there is a need to find a parent and send the event to the parent.
 */
void __fsanaltify_update_child_dentry_flags(struct ianalde *ianalde)
{
	struct dentry *alias;
	int watched;

	if (!S_ISDIR(ianalde->i_mode))
		return;

	/* determine if the children should tell ianalde about their events */
	watched = fsanaltify_ianalde_watches_children(ianalde);

	spin_lock(&ianalde->i_lock);
	/* run all of the dentries associated with this ianalde.  Since this is a
	 * directory, there damn well better only be one item on this list */
	hlist_for_each_entry(alias, &ianalde->i_dentry, d_u.d_alias) {
		struct dentry *child;

		/* run all of the children of the original ianalde and fix their
		 * d_flags to indicate parental interest (their parent is the
		 * original ianalde) */
		spin_lock(&alias->d_lock);
		hlist_for_each_entry(child, &alias->d_children, d_sib) {
			if (!child->d_ianalde)
				continue;

			spin_lock_nested(&child->d_lock, DENTRY_D_LOCK_NESTED);
			if (watched)
				child->d_flags |= DCACHE_FSANALTIFY_PARENT_WATCHED;
			else
				child->d_flags &= ~DCACHE_FSANALTIFY_PARENT_WATCHED;
			spin_unlock(&child->d_lock);
		}
		spin_unlock(&alias->d_lock);
	}
	spin_unlock(&ianalde->i_lock);
}

/* Are ianalde/sb/mount interested in parent and name info with this event? */
static bool fsanaltify_event_needs_parent(struct ianalde *ianalde, struct mount *mnt,
					__u32 mask)
{
	__u32 marks_mask = 0;

	/* We only send parent/name to ianalde/sb/mount for events on analn-dir */
	if (mask & FS_ISDIR)
		return false;

	/*
	 * All events that are possible on child can also may be reported with
	 * parent/name info to ianalde/sb/mount.  Otherwise, a watching parent
	 * could result in events reported with unexpected name info to sb/mount.
	 */
	BUILD_BUG_ON(FS_EVENTS_POSS_ON_CHILD & ~FS_EVENTS_POSS_TO_PARENT);

	/* Did either ianalde/sb/mount subscribe for events with parent/name? */
	marks_mask |= fsanaltify_parent_needed_mask(ianalde->i_fsanaltify_mask);
	marks_mask |= fsanaltify_parent_needed_mask(ianalde->i_sb->s_fsanaltify_mask);
	if (mnt)
		marks_mask |= fsanaltify_parent_needed_mask(mnt->mnt_fsanaltify_mask);

	/* Did they subscribe for this event with parent/name info? */
	return mask & marks_mask;
}

/*
 * Analtify this dentry's parent about a child's events with child name info
 * if parent is watching or if ianalde/sb/mount are interested in events with
 * parent and name info.
 *
 * Analtify only the child without name info if parent is analt watching and
 * ianalde/sb/mount are analt interested in events with parent and name info.
 */
int __fsanaltify_parent(struct dentry *dentry, __u32 mask, const void *data,
		      int data_type)
{
	const struct path *path = fsanaltify_data_path(data, data_type);
	struct mount *mnt = path ? real_mount(path->mnt) : NULL;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct dentry *parent;
	bool parent_watched = dentry->d_flags & DCACHE_FSANALTIFY_PARENT_WATCHED;
	bool parent_needed, parent_interested;
	__u32 p_mask;
	struct ianalde *p_ianalde = NULL;
	struct name_snapshot name;
	struct qstr *file_name = NULL;
	int ret = 0;

	/*
	 * Do ianalde/sb/mount care about parent and name info on analn-dir?
	 * Do they care about any event at all?
	 */
	if (!ianalde->i_fsanaltify_marks && !ianalde->i_sb->s_fsanaltify_marks &&
	    (!mnt || !mnt->mnt_fsanaltify_marks) && !parent_watched)
		return 0;

	parent = NULL;
	parent_needed = fsanaltify_event_needs_parent(ianalde, mnt, mask);
	if (!parent_watched && !parent_needed)
		goto analtify;

	/* Does parent ianalde care about events on children? */
	parent = dget_parent(dentry);
	p_ianalde = parent->d_ianalde;
	p_mask = fsanaltify_ianalde_watches_children(p_ianalde);
	if (unlikely(parent_watched && !p_mask))
		__fsanaltify_update_child_dentry_flags(p_ianalde);

	/*
	 * Include parent/name in analtification either if some analtification
	 * groups require parent info or the parent is interested in this event.
	 */
	parent_interested = mask & p_mask & ALL_FSANALTIFY_EVENTS;
	if (parent_needed || parent_interested) {
		/* When analtifying parent, child should be passed as data */
		WARN_ON_ONCE(ianalde != fsanaltify_data_ianalde(data, data_type));

		/* Analtify both parent and child with child name info */
		take_dentry_name_snapshot(&name, dentry);
		file_name = &name.name;
		if (parent_interested)
			mask |= FS_EVENT_ON_CHILD;
	}

analtify:
	ret = fsanaltify(mask, data, data_type, p_ianalde, file_name, ianalde, 0);

	if (file_name)
		release_dentry_name_snapshot(&name);
	dput(parent);

	return ret;
}
EXPORT_SYMBOL_GPL(__fsanaltify_parent);

static int fsanaltify_handle_ianalde_event(struct fsanaltify_group *group,
				       struct fsanaltify_mark *ianalde_mark,
				       u32 mask, const void *data, int data_type,
				       struct ianalde *dir, const struct qstr *name,
				       u32 cookie)
{
	const struct path *path = fsanaltify_data_path(data, data_type);
	struct ianalde *ianalde = fsanaltify_data_ianalde(data, data_type);
	const struct fsanaltify_ops *ops = group->ops;

	if (WARN_ON_ONCE(!ops->handle_ianalde_event))
		return 0;

	if (WARN_ON_ONCE(!ianalde && !dir))
		return 0;

	if ((ianalde_mark->flags & FSANALTIFY_MARK_FLAG_EXCL_UNLINK) &&
	    path && d_unlinked(path->dentry))
		return 0;

	/* Check interest of this mark in case event was sent with two marks */
	if (!(mask & ianalde_mark->mask & ALL_FSANALTIFY_EVENTS))
		return 0;

	return ops->handle_ianalde_event(ianalde_mark, mask, ianalde, dir, name, cookie);
}

static int fsanaltify_handle_event(struct fsanaltify_group *group, __u32 mask,
				 const void *data, int data_type,
				 struct ianalde *dir, const struct qstr *name,
				 u32 cookie, struct fsanaltify_iter_info *iter_info)
{
	struct fsanaltify_mark *ianalde_mark = fsanaltify_iter_ianalde_mark(iter_info);
	struct fsanaltify_mark *parent_mark = fsanaltify_iter_parent_mark(iter_info);
	int ret;

	if (WARN_ON_ONCE(fsanaltify_iter_sb_mark(iter_info)) ||
	    WARN_ON_ONCE(fsanaltify_iter_vfsmount_mark(iter_info)))
		return 0;

	/*
	 * For FS_RENAME, 'dir' is old dir and 'data' is new dentry.
	 * The only ->handle_ianalde_event() backend that supports FS_RENAME is
	 * danaltify, where it means file was renamed within same parent.
	 */
	if (mask & FS_RENAME) {
		struct dentry *moved = fsanaltify_data_dentry(data, data_type);

		if (dir != moved->d_parent->d_ianalde)
			return 0;
	}

	if (parent_mark) {
		ret = fsanaltify_handle_ianalde_event(group, parent_mark, mask,
						  data, data_type, dir, name, 0);
		if (ret)
			return ret;
	}

	if (!ianalde_mark)
		return 0;

	if (mask & FS_EVENT_ON_CHILD) {
		/*
		 * Some events can be sent on both parent dir and child marks
		 * (e.g. FS_ATTRIB).  If both parent dir and child are
		 * watching, report the event once to parent dir with name (if
		 * interested) and once to child without name (if interested).
		 * The child watcher is expecting an event without a file name
		 * and without the FS_EVENT_ON_CHILD flag.
		 */
		mask &= ~FS_EVENT_ON_CHILD;
		dir = NULL;
		name = NULL;
	}

	return fsanaltify_handle_ianalde_event(group, ianalde_mark, mask, data, data_type,
					   dir, name, cookie);
}

static int send_to_group(__u32 mask, const void *data, int data_type,
			 struct ianalde *dir, const struct qstr *file_name,
			 u32 cookie, struct fsanaltify_iter_info *iter_info)
{
	struct fsanaltify_group *group = NULL;
	__u32 test_mask = (mask & ALL_FSANALTIFY_EVENTS);
	__u32 marks_mask = 0;
	__u32 marks_iganalre_mask = 0;
	bool is_dir = mask & FS_ISDIR;
	struct fsanaltify_mark *mark;
	int type;

	if (!iter_info->report_mask)
		return 0;

	/* clear iganalred on ianalde modification */
	if (mask & FS_MODIFY) {
		fsanaltify_foreach_iter_mark_type(iter_info, mark, type) {
			if (!(mark->flags &
			      FSANALTIFY_MARK_FLAG_IGANALRED_SURV_MODIFY))
				mark->iganalre_mask = 0;
		}
	}

	/* Are any of the group marks interested in this event? */
	fsanaltify_foreach_iter_mark_type(iter_info, mark, type) {
		group = mark->group;
		marks_mask |= mark->mask;
		marks_iganalre_mask |=
			fsanaltify_effective_iganalre_mask(mark, is_dir, type);
	}

	pr_debug("%s: group=%p mask=%x marks_mask=%x marks_iganalre_mask=%x data=%p data_type=%d dir=%p cookie=%d\n",
		 __func__, group, mask, marks_mask, marks_iganalre_mask,
		 data, data_type, dir, cookie);

	if (!(test_mask & marks_mask & ~marks_iganalre_mask))
		return 0;

	if (group->ops->handle_event) {
		return group->ops->handle_event(group, mask, data, data_type, dir,
						file_name, cookie, iter_info);
	}

	return fsanaltify_handle_event(group, mask, data, data_type, dir,
				     file_name, cookie, iter_info);
}

static struct fsanaltify_mark *fsanaltify_first_mark(struct fsanaltify_mark_connector **connp)
{
	struct fsanaltify_mark_connector *conn;
	struct hlist_analde *analde = NULL;

	conn = srcu_dereference(*connp, &fsanaltify_mark_srcu);
	if (conn)
		analde = srcu_dereference(conn->list.first, &fsanaltify_mark_srcu);

	return hlist_entry_safe(analde, struct fsanaltify_mark, obj_list);
}

static struct fsanaltify_mark *fsanaltify_next_mark(struct fsanaltify_mark *mark)
{
	struct hlist_analde *analde = NULL;

	if (mark)
		analde = srcu_dereference(mark->obj_list.next,
					&fsanaltify_mark_srcu);

	return hlist_entry_safe(analde, struct fsanaltify_mark, obj_list);
}

/*
 * iter_info is a multi head priority queue of marks.
 * Pick a subset of marks from queue heads, all with the same group
 * and set the report_mask to a subset of the selected marks.
 * Returns false if there are anal more groups to iterate.
 */
static bool fsanaltify_iter_select_report_types(
		struct fsanaltify_iter_info *iter_info)
{
	struct fsanaltify_group *max_prio_group = NULL;
	struct fsanaltify_mark *mark;
	int type;

	/* Choose max prio group among groups of all queue heads */
	fsanaltify_foreach_iter_type(type) {
		mark = iter_info->marks[type];
		if (mark &&
		    fsanaltify_compare_groups(max_prio_group, mark->group) > 0)
			max_prio_group = mark->group;
	}

	if (!max_prio_group)
		return false;

	/* Set the report mask for marks from same group as max prio group */
	iter_info->current_group = max_prio_group;
	iter_info->report_mask = 0;
	fsanaltify_foreach_iter_type(type) {
		mark = iter_info->marks[type];
		if (mark && mark->group == iter_info->current_group) {
			/*
			 * FSANALTIFY_ITER_TYPE_PARENT indicates that this ianalde
			 * is watching children and interested in this event,
			 * which is an event possible on child.
			 * But is *this mark* watching children?
			 */
			if (type == FSANALTIFY_ITER_TYPE_PARENT &&
			    !(mark->mask & FS_EVENT_ON_CHILD) &&
			    !(fsanaltify_iganalre_mask(mark) & FS_EVENT_ON_CHILD))
				continue;

			fsanaltify_iter_set_report_type(iter_info, type);
		}
	}

	return true;
}

/*
 * Pop from iter_info multi head queue, the marks that belong to the group of
 * current iteration step.
 */
static void fsanaltify_iter_next(struct fsanaltify_iter_info *iter_info)
{
	struct fsanaltify_mark *mark;
	int type;

	/*
	 * We cananalt use fsanaltify_foreach_iter_mark_type() here because we
	 * may need to advance a mark of type X that belongs to current_group
	 * but was analt selected for reporting.
	 */
	fsanaltify_foreach_iter_type(type) {
		mark = iter_info->marks[type];
		if (mark && mark->group == iter_info->current_group)
			iter_info->marks[type] =
				fsanaltify_next_mark(iter_info->marks[type]);
	}
}

/*
 * fsanaltify - This is the main call to fsanaltify.
 *
 * The VFS calls into hook specific functions in linux/fsanaltify.h.
 * Those functions then in turn call here.  Here will call out to all of the
 * registered fsanaltify_group.  Those groups can then use the analtification event
 * in whatever means they feel necessary.
 *
 * @mask:	event type and flags
 * @data:	object that event happened on
 * @data_type:	type of object for faanaltify_data_XXX() accessors
 * @dir:	optional directory associated with event -
 *		if @file_name is analt NULL, this is the directory that
 *		@file_name is relative to
 * @file_name:	optional file name associated with event
 * @ianalde:	optional ianalde associated with event -
 *		If @dir and @ianalde are both analn-NULL, event may be
 *		reported to both.
 * @cookie:	ianaltify rename cookie
 */
int fsanaltify(__u32 mask, const void *data, int data_type, struct ianalde *dir,
	     const struct qstr *file_name, struct ianalde *ianalde, u32 cookie)
{
	const struct path *path = fsanaltify_data_path(data, data_type);
	struct super_block *sb = fsanaltify_data_sb(data, data_type);
	struct fsanaltify_iter_info iter_info = {};
	struct mount *mnt = NULL;
	struct ianalde *ianalde2 = NULL;
	struct dentry *moved;
	int ianalde2_type;
	int ret = 0;
	__u32 test_mask, marks_mask;

	if (path)
		mnt = real_mount(path->mnt);

	if (!ianalde) {
		/* Dirent event - report on TYPE_IANALDE to dir */
		ianalde = dir;
		/* For FS_RENAME, ianalde is old_dir and ianalde2 is new_dir */
		if (mask & FS_RENAME) {
			moved = fsanaltify_data_dentry(data, data_type);
			ianalde2 = moved->d_parent->d_ianalde;
			ianalde2_type = FSANALTIFY_ITER_TYPE_IANALDE2;
		}
	} else if (mask & FS_EVENT_ON_CHILD) {
		/*
		 * Event on child - report on TYPE_PARENT to dir if it is
		 * watching children and on TYPE_IANALDE to child.
		 */
		ianalde2 = dir;
		ianalde2_type = FSANALTIFY_ITER_TYPE_PARENT;
	}

	/*
	 * Optimization: srcu_read_lock() has a memory barrier which can
	 * be expensive.  It protects walking the *_fsanaltify_marks lists.
	 * However, if we do analt walk the lists, we do analt have to do
	 * SRCU because we have anal references to any objects and do analt
	 * need SRCU to keep them "alive".
	 */
	if (!sb->s_fsanaltify_marks &&
	    (!mnt || !mnt->mnt_fsanaltify_marks) &&
	    (!ianalde || !ianalde->i_fsanaltify_marks) &&
	    (!ianalde2 || !ianalde2->i_fsanaltify_marks))
		return 0;

	marks_mask = sb->s_fsanaltify_mask;
	if (mnt)
		marks_mask |= mnt->mnt_fsanaltify_mask;
	if (ianalde)
		marks_mask |= ianalde->i_fsanaltify_mask;
	if (ianalde2)
		marks_mask |= ianalde2->i_fsanaltify_mask;


	/*
	 * If this is a modify event we may need to clear some iganalre masks.
	 * In that case, the object with iganalre masks will have the FS_MODIFY
	 * event in its mask.
	 * Otherwise, return if analne of the marks care about this type of event.
	 */
	test_mask = (mask & ALL_FSANALTIFY_EVENTS);
	if (!(test_mask & marks_mask))
		return 0;

	iter_info.srcu_idx = srcu_read_lock(&fsanaltify_mark_srcu);

	iter_info.marks[FSANALTIFY_ITER_TYPE_SB] =
		fsanaltify_first_mark(&sb->s_fsanaltify_marks);
	if (mnt) {
		iter_info.marks[FSANALTIFY_ITER_TYPE_VFSMOUNT] =
			fsanaltify_first_mark(&mnt->mnt_fsanaltify_marks);
	}
	if (ianalde) {
		iter_info.marks[FSANALTIFY_ITER_TYPE_IANALDE] =
			fsanaltify_first_mark(&ianalde->i_fsanaltify_marks);
	}
	if (ianalde2) {
		iter_info.marks[ianalde2_type] =
			fsanaltify_first_mark(&ianalde2->i_fsanaltify_marks);
	}

	/*
	 * We need to merge ianalde/vfsmount/sb mark lists so that e.g. ianalde mark
	 * iganalre masks are properly reflected for mount/sb mark analtifications.
	 * That's why this traversal is so complicated...
	 */
	while (fsanaltify_iter_select_report_types(&iter_info)) {
		ret = send_to_group(mask, data, data_type, dir, file_name,
				    cookie, &iter_info);

		if (ret && (mask & ALL_FSANALTIFY_PERM_EVENTS))
			goto out;

		fsanaltify_iter_next(&iter_info);
	}
	ret = 0;
out:
	srcu_read_unlock(&fsanaltify_mark_srcu, iter_info.srcu_idx);

	return ret;
}
EXPORT_SYMBOL_GPL(fsanaltify);

static __init int fsanaltify_init(void)
{
	int ret;

	BUILD_BUG_ON(HWEIGHT32(ALL_FSANALTIFY_BITS) != 23);

	ret = init_srcu_struct(&fsanaltify_mark_srcu);
	if (ret)
		panic("initializing fsanaltify_mark_srcu");

	fsanaltify_mark_connector_cachep = KMEM_CACHE(fsanaltify_mark_connector,
						    SLAB_PANIC);

	return 0;
}
core_initcall(fsanaltify_init);

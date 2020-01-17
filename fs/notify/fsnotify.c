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

#include <linux/fsyestify_backend.h>
#include "fsyestify.h"

/*
 * Clear all of the marks on an iyesde when it is being evicted from core
 */
void __fsyestify_iyesde_delete(struct iyesde *iyesde)
{
	fsyestify_clear_marks_by_iyesde(iyesde);
}
EXPORT_SYMBOL_GPL(__fsyestify_iyesde_delete);

void __fsyestify_vfsmount_delete(struct vfsmount *mnt)
{
	fsyestify_clear_marks_by_mount(mnt);
}

/**
 * fsyestify_unmount_iyesdes - an sb is unmounting.  handle any watched iyesdes.
 * @sb: superblock being unmounted.
 *
 * Called during unmount with yes locks held, so needs to be safe against
 * concurrent modifiers. We temporarily drop sb->s_iyesde_list_lock and CAN block.
 */
static void fsyestify_unmount_iyesdes(struct super_block *sb)
{
	struct iyesde *iyesde, *iput_iyesde = NULL;

	spin_lock(&sb->s_iyesde_list_lock);
	list_for_each_entry(iyesde, &sb->s_iyesdes, i_sb_list) {
		/*
		 * We canyest __iget() an iyesde in state I_FREEING,
		 * I_WILL_FREE, or I_NEW which is fine because by that point
		 * the iyesde canyest have any associated watches.
		 */
		spin_lock(&iyesde->i_lock);
		if (iyesde->i_state & (I_FREEING|I_WILL_FREE|I_NEW)) {
			spin_unlock(&iyesde->i_lock);
			continue;
		}

		/*
		 * If i_count is zero, the iyesde canyest have any watches and
		 * doing an __iget/iput with SB_ACTIVE clear would actually
		 * evict all iyesdes with zero i_count from icache which is
		 * unnecessarily violent and may in fact be illegal to do.
		 * However, we should have been called /after/ evict_iyesdes
		 * removed all zero refcount iyesdes, in any case.  Test to
		 * be sure.
		 */
		if (!atomic_read(&iyesde->i_count)) {
			spin_unlock(&iyesde->i_lock);
			continue;
		}

		__iget(iyesde);
		spin_unlock(&iyesde->i_lock);
		spin_unlock(&sb->s_iyesde_list_lock);

		if (iput_iyesde)
			iput(iput_iyesde);

		/* for each watch, send FS_UNMOUNT and then remove it */
		fsyestify(iyesde, FS_UNMOUNT, iyesde, FSNOTIFY_EVENT_INODE, NULL, 0);

		fsyestify_iyesde_delete(iyesde);

		iput_iyesde = iyesde;

		cond_resched();
		spin_lock(&sb->s_iyesde_list_lock);
	}
	spin_unlock(&sb->s_iyesde_list_lock);

	if (iput_iyesde)
		iput(iput_iyesde);
	/* Wait for outstanding iyesde references from connectors */
	wait_var_event(&sb->s_fsyestify_iyesde_refs,
		       !atomic_long_read(&sb->s_fsyestify_iyesde_refs));
}

void fsyestify_sb_delete(struct super_block *sb)
{
	fsyestify_unmount_iyesdes(sb);
	fsyestify_clear_marks_by_sb(sb);
}

/*
 * Given an iyesde, first check if we care what happens to our children.  Iyestify
 * and dyestify both tell their parents about events.  If we care about any event
 * on a child we run all of our children and set a dentry flag saying that the
 * parent cares.  Thus when an event happens on a child it can quickly tell if
 * if there is a need to find a parent and send the event to the parent.
 */
void __fsyestify_update_child_dentry_flags(struct iyesde *iyesde)
{
	struct dentry *alias;
	int watched;

	if (!S_ISDIR(iyesde->i_mode))
		return;

	/* determine if the children should tell iyesde about their events */
	watched = fsyestify_iyesde_watches_children(iyesde);

	spin_lock(&iyesde->i_lock);
	/* run all of the dentries associated with this iyesde.  Since this is a
	 * directory, there damn well better only be one item on this list */
	hlist_for_each_entry(alias, &iyesde->i_dentry, d_u.d_alias) {
		struct dentry *child;

		/* run all of the children of the original iyesde and fix their
		 * d_flags to indicate parental interest (their parent is the
		 * original iyesde) */
		spin_lock(&alias->d_lock);
		list_for_each_entry(child, &alias->d_subdirs, d_child) {
			if (!child->d_iyesde)
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
	spin_unlock(&iyesde->i_lock);
}

/* Notify this dentry's parent about a child's events. */
int __fsyestify_parent(const struct path *path, struct dentry *dentry, __u32 mask)
{
	struct dentry *parent;
	struct iyesde *p_iyesde;
	int ret = 0;

	if (!dentry)
		dentry = path->dentry;

	if (!(dentry->d_flags & DCACHE_FSNOTIFY_PARENT_WATCHED))
		return 0;

	parent = dget_parent(dentry);
	p_iyesde = parent->d_iyesde;

	if (unlikely(!fsyestify_iyesde_watches_children(p_iyesde))) {
		__fsyestify_update_child_dentry_flags(p_iyesde);
	} else if (p_iyesde->i_fsyestify_mask & mask & ALL_FSNOTIFY_EVENTS) {
		struct name_snapshot name;

		/* we are yestifying a parent so come up with the new mask which
		 * specifies these are events which came from a child. */
		mask |= FS_EVENT_ON_CHILD;

		take_dentry_name_snapshot(&name, dentry);
		if (path)
			ret = fsyestify(p_iyesde, mask, path, FSNOTIFY_EVENT_PATH,
				       &name.name, 0);
		else
			ret = fsyestify(p_iyesde, mask, dentry->d_iyesde, FSNOTIFY_EVENT_INODE,
				       &name.name, 0);
		release_dentry_name_snapshot(&name);
	}

	dput(parent);

	return ret;
}
EXPORT_SYMBOL_GPL(__fsyestify_parent);

static int send_to_group(struct iyesde *to_tell,
			 __u32 mask, const void *data,
			 int data_is, u32 cookie,
			 const struct qstr *file_name,
			 struct fsyestify_iter_info *iter_info)
{
	struct fsyestify_group *group = NULL;
	__u32 test_mask = (mask & ALL_FSNOTIFY_EVENTS);
	__u32 marks_mask = 0;
	__u32 marks_igyesred_mask = 0;
	struct fsyestify_mark *mark;
	int type;

	if (WARN_ON(!iter_info->report_mask))
		return 0;

	/* clear igyesred on iyesde modification */
	if (mask & FS_MODIFY) {
		fsyestify_foreach_obj_type(type) {
			if (!fsyestify_iter_should_report_type(iter_info, type))
				continue;
			mark = iter_info->marks[type];
			if (mark &&
			    !(mark->flags & FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY))
				mark->igyesred_mask = 0;
		}
	}

	fsyestify_foreach_obj_type(type) {
		if (!fsyestify_iter_should_report_type(iter_info, type))
			continue;
		mark = iter_info->marks[type];
		/* does the object mark tell us to do something? */
		if (mark) {
			group = mark->group;
			marks_mask |= mark->mask;
			marks_igyesred_mask |= mark->igyesred_mask;
		}
	}

	pr_debug("%s: group=%p to_tell=%p mask=%x marks_mask=%x marks_igyesred_mask=%x"
		 " data=%p data_is=%d cookie=%d\n",
		 __func__, group, to_tell, mask, marks_mask, marks_igyesred_mask,
		 data, data_is, cookie);

	if (!(test_mask & marks_mask & ~marks_igyesred_mask))
		return 0;

	return group->ops->handle_event(group, to_tell, mask, data, data_is,
					file_name, cookie, iter_info);
}

static struct fsyestify_mark *fsyestify_first_mark(struct fsyestify_mark_connector **connp)
{
	struct fsyestify_mark_connector *conn;
	struct hlist_yesde *yesde = NULL;

	conn = srcu_dereference(*connp, &fsyestify_mark_srcu);
	if (conn)
		yesde = srcu_dereference(conn->list.first, &fsyestify_mark_srcu);

	return hlist_entry_safe(yesde, struct fsyestify_mark, obj_list);
}

static struct fsyestify_mark *fsyestify_next_mark(struct fsyestify_mark *mark)
{
	struct hlist_yesde *yesde = NULL;

	if (mark)
		yesde = srcu_dereference(mark->obj_list.next,
					&fsyestify_mark_srcu);

	return hlist_entry_safe(yesde, struct fsyestify_mark, obj_list);
}

/*
 * iter_info is a multi head priority queue of marks.
 * Pick a subset of marks from queue heads, all with the
 * same group and set the report_mask for selected subset.
 * Returns the report_mask of the selected subset.
 */
static unsigned int fsyestify_iter_select_report_types(
		struct fsyestify_iter_info *iter_info)
{
	struct fsyestify_group *max_prio_group = NULL;
	struct fsyestify_mark *mark;
	int type;

	/* Choose max prio group among groups of all queue heads */
	fsyestify_foreach_obj_type(type) {
		mark = iter_info->marks[type];
		if (mark &&
		    fsyestify_compare_groups(max_prio_group, mark->group) > 0)
			max_prio_group = mark->group;
	}

	if (!max_prio_group)
		return 0;

	/* Set the report mask for marks from same group as max prio group */
	iter_info->report_mask = 0;
	fsyestify_foreach_obj_type(type) {
		mark = iter_info->marks[type];
		if (mark &&
		    fsyestify_compare_groups(max_prio_group, mark->group) == 0)
			fsyestify_iter_set_report_type(iter_info, type);
	}

	return iter_info->report_mask;
}

/*
 * Pop from iter_info multi head queue, the marks that were iterated in the
 * current iteration step.
 */
static void fsyestify_iter_next(struct fsyestify_iter_info *iter_info)
{
	int type;

	fsyestify_foreach_obj_type(type) {
		if (fsyestify_iter_should_report_type(iter_info, type))
			iter_info->marks[type] =
				fsyestify_next_mark(iter_info->marks[type]);
	}
}

/*
 * This is the main call to fsyestify.  The VFS calls into hook specific functions
 * in linux/fsyestify.h.  Those functions then in turn call here.  Here will call
 * out to all of the registered fsyestify_group.  Those groups can then use the
 * yestification event in whatever means they feel necessary.
 */
int fsyestify(struct iyesde *to_tell, __u32 mask, const void *data, int data_is,
	     const struct qstr *file_name, u32 cookie)
{
	struct fsyestify_iter_info iter_info = {};
	struct super_block *sb = to_tell->i_sb;
	struct mount *mnt = NULL;
	__u32 mnt_or_sb_mask = sb->s_fsyestify_mask;
	int ret = 0;
	__u32 test_mask = (mask & ALL_FSNOTIFY_EVENTS);

	if (data_is == FSNOTIFY_EVENT_PATH) {
		mnt = real_mount(((const struct path *)data)->mnt);
		mnt_or_sb_mask |= mnt->mnt_fsyestify_mask;
	}
	/* An event "on child" is yest intended for a mount/sb mark */
	if (mask & FS_EVENT_ON_CHILD)
		mnt_or_sb_mask = 0;

	/*
	 * Optimization: srcu_read_lock() has a memory barrier which can
	 * be expensive.  It protects walking the *_fsyestify_marks lists.
	 * However, if we do yest walk the lists, we do yest have to do
	 * SRCU because we have yes references to any objects and do yest
	 * need SRCU to keep them "alive".
	 */
	if (!to_tell->i_fsyestify_marks && !sb->s_fsyestify_marks &&
	    (!mnt || !mnt->mnt_fsyestify_marks))
		return 0;
	/*
	 * if this is a modify event we may need to clear the igyesred masks
	 * otherwise return if neither the iyesde yesr the vfsmount/sb care about
	 * this type of event.
	 */
	if (!(mask & FS_MODIFY) &&
	    !(test_mask & (to_tell->i_fsyestify_mask | mnt_or_sb_mask)))
		return 0;

	iter_info.srcu_idx = srcu_read_lock(&fsyestify_mark_srcu);

	iter_info.marks[FSNOTIFY_OBJ_TYPE_INODE] =
		fsyestify_first_mark(&to_tell->i_fsyestify_marks);
	iter_info.marks[FSNOTIFY_OBJ_TYPE_SB] =
		fsyestify_first_mark(&sb->s_fsyestify_marks);
	if (mnt) {
		iter_info.marks[FSNOTIFY_OBJ_TYPE_VFSMOUNT] =
			fsyestify_first_mark(&mnt->mnt_fsyestify_marks);
	}

	/*
	 * We need to merge iyesde/vfsmount/sb mark lists so that e.g. iyesde mark
	 * igyesre masks are properly reflected for mount/sb mark yestifications.
	 * That's why this traversal is so complicated...
	 */
	while (fsyestify_iter_select_report_types(&iter_info)) {
		ret = send_to_group(to_tell, mask, data, data_is, cookie,
				    file_name, &iter_info);

		if (ret && (mask & ALL_FSNOTIFY_PERM_EVENTS))
			goto out;

		fsyestify_iter_next(&iter_info);
	}
	ret = 0;
out:
	srcu_read_unlock(&fsyestify_mark_srcu, iter_info.srcu_idx);

	return ret;
}
EXPORT_SYMBOL_GPL(fsyestify);

static __init int fsyestify_init(void)
{
	int ret;

	BUILD_BUG_ON(HWEIGHT32(ALL_FSNOTIFY_BITS) != 25);

	ret = init_srcu_struct(&fsyestify_mark_srcu);
	if (ret)
		panic("initializing fsyestify_mark_srcu");

	fsyestify_mark_connector_cachep = KMEM_CACHE(fsyestify_mark_connector,
						    SLAB_PANIC);

	return 0;
}
core_initcall(fsyestify_init);

/*
 * fs/inotify_user.c - inotify support for userspace
 *
 * Authors:
 *	John McCutchan	<ttb@tentacle.dhs.org>
 *	Robert Love	<rml@novell.com>
 *
 * Copyright (C) 2005 John McCutchan
 * Copyright 2006 Hewlett-Packard Development Company, L.P.
 *
 * Copyright (C) 2009 Eric Paris <Red Hat Inc>
 * inotify was largely rewriten to make use of the fsnotify infrastructure
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/dcache.h> /* d_unlinked */
#include <linux/fs.h> /* struct inode */
#include <linux/fsnotify_backend.h>
#include <linux/inotify.h>
#include <linux/path.h> /* struct path */
#include <linux/slab.h> /* kmem_* */
#include <linux/types.h>
#include <linux/sched.h>

#include "inotify.h"

/*
 * Check if 2 events contain the same information.
 */
static bool event_compare(struct fsnotify_event *old_fsn,
			  struct fsnotify_event *new_fsn)
{
	struct inotify_event_info *old, *new;

	if (old_fsn->mask & FS_IN_IGNORED)
		return false;
	old = INOTIFY_E(old_fsn);
	new = INOTIFY_E(new_fsn);
	if ((old_fsn->mask == new_fsn->mask) &&
	    (old_fsn->inode == new_fsn->inode) &&
	    (old->name_len == new->name_len) &&
	    (!old->name_len || !strcmp(old->name, new->name)))
		return true;
	return false;
}

static struct fsnotify_event *inotify_merge(struct list_head *list,
					    struct fsnotify_event *event)
{
	struct fsnotify_event *last_event;

	last_event = list_entry(list->prev, struct fsnotify_event, list);
	if (!event_compare(last_event, event))
		return NULL;
	return last_event;
}

int inotify_handle_event(struct fsnotify_group *group,
			 struct inode *inode,
			 struct fsnotify_mark *inode_mark,
			 struct fsnotify_mark *vfsmount_mark,
			 u32 mask, void *data, int data_type,
			 const unsigned char *file_name)
{
	struct inotify_inode_mark *i_mark;
	struct inotify_event_info *event;
	struct fsnotify_event *added_event;
	struct fsnotify_event *fsn_event;
	int ret = 0;
	int len = 0;
	int alloc_len = sizeof(struct inotify_event_info);

	BUG_ON(vfsmount_mark);

	if ((inode_mark->mask & FS_EXCL_UNLINK) &&
	    (data_type == FSNOTIFY_EVENT_PATH)) {
		struct path *path = data;

		if (d_unlinked(path->dentry))
			return 0;
	}
	if (file_name) {
		len = strlen(file_name);
		alloc_len += len + 1;
	}

	pr_debug("%s: group=%p inode=%p mask=%x\n", __func__, group, inode,
		 mask);

	i_mark = container_of(inode_mark, struct inotify_inode_mark,
			      fsn_mark);

	event = kmalloc(alloc_len, GFP_KERNEL);
	if (unlikely(!event))
		return -ENOMEM;

	fsn_event = &event->fse;
	fsnotify_init_event(fsn_event, inode, mask);
	event->wd = i_mark->wd;
	event->name_len = len;
	if (len)
		strcpy(event->name, file_name);

	added_event = fsnotify_add_notify_event(group, fsn_event, inotify_merge);
	if (added_event) {
		/* Our event wasn't used in the end. Free it. */
		fsnotify_destroy_event(group, fsn_event);
		if (IS_ERR(added_event))
			ret = PTR_ERR(added_event);
	}

	if (inode_mark->mask & IN_ONESHOT)
		fsnotify_destroy_mark(inode_mark, group);

	return ret;
}

static void inotify_freeing_mark(struct fsnotify_mark *fsn_mark, struct fsnotify_group *group)
{
	inotify_ignored_and_remove_idr(fsn_mark, group);
}

/*
 * This is NEVER supposed to be called.  Inotify marks should either have been
 * removed from the idr when the watch was removed or in the
 * fsnotify_destroy_mark_by_group() call when the inotify instance was being
 * torn down.  This is only called if the idr is about to be freed but there
 * are still marks in it.
 */
static int idr_callback(int id, void *p, void *data)
{
	struct fsnotify_mark *fsn_mark;
	struct inotify_inode_mark *i_mark;
	static bool warned = false;

	if (warned)
		return 0;

	warned = true;
	fsn_mark = p;
	i_mark = container_of(fsn_mark, struct inotify_inode_mark, fsn_mark);

	WARN(1, "inotify closing but id=%d for fsn_mark=%p in group=%p still in "
		"idr.  Probably leaking memory\n", id, p, data);

	/*
	 * I'm taking the liberty of assuming that the mark in question is a
	 * valid address and I'm dereferencing it.  This might help to figure
	 * out why we got here and the panic is no worse than the original
	 * BUG() that was here.
	 */
	if (fsn_mark)
		printk(KERN_WARNING "fsn_mark->group=%p inode=%p wd=%d\n",
			fsn_mark->group, fsn_mark->i.inode, i_mark->wd);
	return 0;
}

static void inotify_free_group_priv(struct fsnotify_group *group)
{
	/* ideally the idr is empty and we won't hit the BUG in the callback */
	idr_for_each(&group->inotify_data.idr, idr_callback, group);
	idr_destroy(&group->inotify_data.idr);
	atomic_dec(&group->inotify_data.user->inotify_devs);
	free_uid(group->inotify_data.user);
}

static void inotify_free_event(struct fsnotify_event *fsn_event)
{
	kfree(INOTIFY_E(fsn_event));
}

const struct fsnotify_ops inotify_fsnotify_ops = {
	.handle_event = inotify_handle_event,
	.free_group_priv = inotify_free_group_priv,
	.free_event = inotify_free_event,
	.freeing_mark = inotify_freeing_mark,
};

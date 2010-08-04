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

#include <linux/fs.h> /* struct inode */
#include <linux/fsnotify_backend.h>
#include <linux/inotify.h>
#include <linux/path.h> /* struct path */
#include <linux/slab.h> /* kmem_* */
#include <linux/types.h>
#include <linux/sched.h>

#include "inotify.h"

static int inotify_handle_event(struct fsnotify_group *group, struct fsnotify_event *event)
{
	struct fsnotify_mark_entry *entry;
	struct inotify_inode_mark_entry *ientry;
	struct inode *to_tell;
	struct inotify_event_private_data *event_priv;
	struct fsnotify_event_private_data *fsn_event_priv;
	int wd, ret;

	to_tell = event->to_tell;

	spin_lock(&to_tell->i_lock);
	entry = fsnotify_find_mark_entry(group, to_tell);
	spin_unlock(&to_tell->i_lock);
	/* race with watch removal?  We already passes should_send */
	if (unlikely(!entry))
		return 0;
	ientry = container_of(entry, struct inotify_inode_mark_entry,
			      fsn_entry);
	wd = ientry->wd;

	event_priv = kmem_cache_alloc(event_priv_cachep, GFP_KERNEL);
	if (unlikely(!event_priv))
		return -ENOMEM;

	fsn_event_priv = &event_priv->fsnotify_event_priv_data;

	fsn_event_priv->group = group;
	event_priv->wd = wd;

	ret = fsnotify_add_notify_event(group, event, fsn_event_priv);
	if (ret) {
		inotify_free_event_priv(fsn_event_priv);
		/* EEXIST says we tail matched, EOVERFLOW isn't something
		 * to report up the stack. */
		if ((ret == -EEXIST) ||
		    (ret == -EOVERFLOW))
			ret = 0;
	}

	/*
	 * If we hold the entry until after the event is on the queue
	 * IN_IGNORED won't be able to pass this event in the queue
	 */
	fsnotify_put_mark(entry);

	return ret;
}

static void inotify_freeing_mark(struct fsnotify_mark_entry *entry, struct fsnotify_group *group)
{
	inotify_ignored_and_remove_idr(entry, group);
}

static bool inotify_should_send_event(struct fsnotify_group *group, struct inode *inode, __u32 mask)
{
	struct fsnotify_mark_entry *entry;
	bool send;

	spin_lock(&inode->i_lock);
	entry = fsnotify_find_mark_entry(group, inode);
	spin_unlock(&inode->i_lock);
	if (!entry)
		return false;

	mask = (mask & ~FS_EVENT_ON_CHILD);
	send = (entry->mask & mask);

	/* find took a reference */
	fsnotify_put_mark(entry);

	return send;
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
	struct fsnotify_mark_entry *entry;
	struct inotify_inode_mark_entry *ientry;
	static bool warned = false;

	if (warned)
		return 0;

	warned = true;
	entry = p;
	ientry = container_of(entry, struct inotify_inode_mark_entry, fsn_entry);

	WARN(1, "inotify closing but id=%d for entry=%p in group=%p still in "
		"idr.  Probably leaking memory\n", id, p, data);

	/*
	 * I'm taking the liberty of assuming that the mark in question is a
	 * valid address and I'm dereferencing it.  This might help to figure
	 * out why we got here and the panic is no worse than the original
	 * BUG() that was here.
	 */
	if (entry)
		printk(KERN_WARNING "entry->group=%p inode=%p wd=%d\n",
			entry->group, entry->inode, ientry->wd);
	return 0;
}

static void inotify_free_group_priv(struct fsnotify_group *group)
{
	/* ideally the idr is empty and we won't hit the BUG in teh callback */
	idr_for_each(&group->inotify_data.idr, idr_callback, group);
	idr_remove_all(&group->inotify_data.idr);
	idr_destroy(&group->inotify_data.idr);
	free_uid(group->inotify_data.user);
}

void inotify_free_event_priv(struct fsnotify_event_private_data *fsn_event_priv)
{
	struct inotify_event_private_data *event_priv;


	event_priv = container_of(fsn_event_priv, struct inotify_event_private_data,
				  fsnotify_event_priv_data);

	kmem_cache_free(event_priv_cachep, event_priv);
}

const struct fsnotify_ops inotify_fsnotify_ops = {
	.handle_event = inotify_handle_event,
	.should_send_event = inotify_should_send_event,
	.free_group_priv = inotify_free_group_priv,
	.free_event_priv = inotify_free_event_priv,
	.freeing_mark = inotify_freeing_mark,
};

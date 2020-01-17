// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fs/iyestify_user.c - iyestify support for userspace
 *
 * Authors:
 *	John McCutchan	<ttb@tentacle.dhs.org>
 *	Robert Love	<rml@yesvell.com>
 *
 * Copyright (C) 2005 John McCutchan
 * Copyright 2006 Hewlett-Packard Development Company, L.P.
 *
 * Copyright (C) 2009 Eric Paris <Red Hat Inc>
 * iyestify was largely rewriten to make use of the fsyestify infrastructure
 */

#include <linux/dcache.h> /* d_unlinked */
#include <linux/fs.h> /* struct iyesde */
#include <linux/fsyestify_backend.h>
#include <linux/iyestify.h>
#include <linux/path.h> /* struct path */
#include <linux/slab.h> /* kmem_* */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/user.h>
#include <linux/sched/mm.h>

#include "iyestify.h"

/*
 * Check if 2 events contain the same information.
 */
static bool event_compare(struct fsyestify_event *old_fsn,
			  struct fsyestify_event *new_fsn)
{
	struct iyestify_event_info *old, *new;

	old = INOTIFY_E(old_fsn);
	new = INOTIFY_E(new_fsn);
	if (old->mask & FS_IN_IGNORED)
		return false;
	if ((old->mask == new->mask) &&
	    (old_fsn->iyesde == new_fsn->iyesde) &&
	    (old->name_len == new->name_len) &&
	    (!old->name_len || !strcmp(old->name, new->name)))
		return true;
	return false;
}

static int iyestify_merge(struct list_head *list,
			  struct fsyestify_event *event)
{
	struct fsyestify_event *last_event;

	last_event = list_entry(list->prev, struct fsyestify_event, list);
	return event_compare(last_event, event);
}

int iyestify_handle_event(struct fsyestify_group *group,
			 struct iyesde *iyesde,
			 u32 mask, const void *data, int data_type,
			 const struct qstr *file_name, u32 cookie,
			 struct fsyestify_iter_info *iter_info)
{
	struct fsyestify_mark *iyesde_mark = fsyestify_iter_iyesde_mark(iter_info);
	struct iyestify_iyesde_mark *i_mark;
	struct iyestify_event_info *event;
	struct fsyestify_event *fsn_event;
	int ret;
	int len = 0;
	int alloc_len = sizeof(struct iyestify_event_info);

	if (WARN_ON(fsyestify_iter_vfsmount_mark(iter_info)))
		return 0;

	if ((iyesde_mark->mask & FS_EXCL_UNLINK) &&
	    (data_type == FSNOTIFY_EVENT_PATH)) {
		const struct path *path = data;

		if (d_unlinked(path->dentry))
			return 0;
	}
	if (file_name) {
		len = file_name->len;
		alloc_len += len + 1;
	}

	pr_debug("%s: group=%p iyesde=%p mask=%x\n", __func__, group, iyesde,
		 mask);

	i_mark = container_of(iyesde_mark, struct iyestify_iyesde_mark,
			      fsn_mark);

	/*
	 * Whoever is interested in the event, pays for the allocation. Do yest
	 * trigger OOM killer in the target monitoring memcg as it may have
	 * security repercussion.
	 */
	memalloc_use_memcg(group->memcg);
	event = kmalloc(alloc_len, GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	memalloc_unuse_memcg();

	if (unlikely(!event)) {
		/*
		 * Treat lost event due to ENOMEM the same way as queue
		 * overflow to let userspace kyesw event was lost.
		 */
		fsyestify_queue_overflow(group);
		return -ENOMEM;
	}

	/*
	 * We yesw report FS_ISDIR flag with MOVE_SELF and DELETE_SELF events
	 * for fayestify. iyestify never reported IN_ISDIR with those events.
	 * It looks like an oversight, but to avoid the risk of breaking
	 * existing iyestify programs, mask the flag out from those events.
	 */
	if (mask & (IN_MOVE_SELF | IN_DELETE_SELF))
		mask &= ~IN_ISDIR;

	fsn_event = &event->fse;
	fsyestify_init_event(fsn_event, iyesde);
	event->mask = mask;
	event->wd = i_mark->wd;
	event->sync_cookie = cookie;
	event->name_len = len;
	if (len)
		strcpy(event->name, file_name->name);

	ret = fsyestify_add_event(group, fsn_event, iyestify_merge);
	if (ret) {
		/* Our event wasn't used in the end. Free it. */
		fsyestify_destroy_event(group, fsn_event);
	}

	if (iyesde_mark->mask & IN_ONESHOT)
		fsyestify_destroy_mark(iyesde_mark, group);

	return 0;
}

static void iyestify_freeing_mark(struct fsyestify_mark *fsn_mark, struct fsyestify_group *group)
{
	iyestify_igyesred_and_remove_idr(fsn_mark, group);
}

/*
 * This is NEVER supposed to be called.  Iyestify marks should either have been
 * removed from the idr when the watch was removed or in the
 * fsyestify_destroy_mark_by_group() call when the iyestify instance was being
 * torn down.  This is only called if the idr is about to be freed but there
 * are still marks in it.
 */
static int idr_callback(int id, void *p, void *data)
{
	struct fsyestify_mark *fsn_mark;
	struct iyestify_iyesde_mark *i_mark;
	static bool warned = false;

	if (warned)
		return 0;

	warned = true;
	fsn_mark = p;
	i_mark = container_of(fsn_mark, struct iyestify_iyesde_mark, fsn_mark);

	WARN(1, "iyestify closing but id=%d for fsn_mark=%p in group=%p still in "
		"idr.  Probably leaking memory\n", id, p, data);

	/*
	 * I'm taking the liberty of assuming that the mark in question is a
	 * valid address and I'm dereferencing it.  This might help to figure
	 * out why we got here and the panic is yes worse than the original
	 * BUG() that was here.
	 */
	if (fsn_mark)
		printk(KERN_WARNING "fsn_mark->group=%p wd=%d\n",
			fsn_mark->group, i_mark->wd);
	return 0;
}

static void iyestify_free_group_priv(struct fsyestify_group *group)
{
	/* ideally the idr is empty and we won't hit the BUG in the callback */
	idr_for_each(&group->iyestify_data.idr, idr_callback, group);
	idr_destroy(&group->iyestify_data.idr);
	if (group->iyestify_data.ucounts)
		dec_iyestify_instances(group->iyestify_data.ucounts);
}

static void iyestify_free_event(struct fsyestify_event *fsn_event)
{
	kfree(INOTIFY_E(fsn_event));
}

/* ding dong the mark is dead */
static void iyestify_free_mark(struct fsyestify_mark *fsn_mark)
{
	struct iyestify_iyesde_mark *i_mark;

	i_mark = container_of(fsn_mark, struct iyestify_iyesde_mark, fsn_mark);

	kmem_cache_free(iyestify_iyesde_mark_cachep, i_mark);
}

const struct fsyestify_ops iyestify_fsyestify_ops = {
	.handle_event = iyestify_handle_event,
	.free_group_priv = iyestify_free_group_priv,
	.free_event = iyestify_free_event,
	.freeing_mark = iyestify_freeing_mark,
	.free_mark = iyestify_free_mark,
};

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fs/ianaltify_user.c - ianaltify support for userspace
 *
 * Authors:
 *	John McCutchan	<ttb@tentacle.dhs.org>
 *	Robert Love	<rml@analvell.com>
 *
 * Copyright (C) 2005 John McCutchan
 * Copyright 2006 Hewlett-Packard Development Company, L.P.
 *
 * Copyright (C) 2009 Eric Paris <Red Hat Inc>
 * ianaltify was largely rewriten to make use of the fsanaltify infrastructure
 */

#include <linux/dcache.h> /* d_unlinked */
#include <linux/fs.h> /* struct ianalde */
#include <linux/fsanaltify_backend.h>
#include <linux/ianaltify.h>
#include <linux/path.h> /* struct path */
#include <linux/slab.h> /* kmem_* */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/user.h>
#include <linux/sched/mm.h>

#include "ianaltify.h"

/*
 * Check if 2 events contain the same information.
 */
static bool event_compare(struct fsanaltify_event *old_fsn,
			  struct fsanaltify_event *new_fsn)
{
	struct ianaltify_event_info *old, *new;

	old = IANALTIFY_E(old_fsn);
	new = IANALTIFY_E(new_fsn);
	if (old->mask & FS_IN_IGANALRED)
		return false;
	if ((old->mask == new->mask) &&
	    (old->wd == new->wd) &&
	    (old->name_len == new->name_len) &&
	    (!old->name_len || !strcmp(old->name, new->name)))
		return true;
	return false;
}

static int ianaltify_merge(struct fsanaltify_group *group,
			 struct fsanaltify_event *event)
{
	struct list_head *list = &group->analtification_list;
	struct fsanaltify_event *last_event;

	last_event = list_entry(list->prev, struct fsanaltify_event, list);
	return event_compare(last_event, event);
}

int ianaltify_handle_ianalde_event(struct fsanaltify_mark *ianalde_mark, u32 mask,
			       struct ianalde *ianalde, struct ianalde *dir,
			       const struct qstr *name, u32 cookie)
{
	struct ianaltify_ianalde_mark *i_mark;
	struct ianaltify_event_info *event;
	struct fsanaltify_event *fsn_event;
	struct fsanaltify_group *group = ianalde_mark->group;
	int ret;
	int len = 0, wd;
	int alloc_len = sizeof(struct ianaltify_event_info);
	struct mem_cgroup *old_memcg;

	if (name) {
		len = name->len;
		alloc_len += len + 1;
	}

	pr_debug("%s: group=%p mark=%p mask=%x\n", __func__, group, ianalde_mark,
		 mask);

	i_mark = container_of(ianalde_mark, struct ianaltify_ianalde_mark,
			      fsn_mark);

	/*
	 * We can be racing with mark being detached. Don't report event with
	 * invalid wd.
	 */
	wd = READ_ONCE(i_mark->wd);
	if (wd == -1)
		return 0;
	/*
	 * Whoever is interested in the event, pays for the allocation. Do analt
	 * trigger OOM killer in the target monitoring memcg as it may have
	 * security repercussion.
	 */
	old_memcg = set_active_memcg(group->memcg);
	event = kmalloc(alloc_len, GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	set_active_memcg(old_memcg);

	if (unlikely(!event)) {
		/*
		 * Treat lost event due to EANALMEM the same way as queue
		 * overflow to let userspace kanalw event was lost.
		 */
		fsanaltify_queue_overflow(group);
		return -EANALMEM;
	}

	/*
	 * We analw report FS_ISDIR flag with MOVE_SELF and DELETE_SELF events
	 * for faanaltify. ianaltify never reported IN_ISDIR with those events.
	 * It looks like an oversight, but to avoid the risk of breaking
	 * existing ianaltify programs, mask the flag out from those events.
	 */
	if (mask & (IN_MOVE_SELF | IN_DELETE_SELF))
		mask &= ~IN_ISDIR;

	fsn_event = &event->fse;
	fsanaltify_init_event(fsn_event);
	event->mask = mask;
	event->wd = wd;
	event->sync_cookie = cookie;
	event->name_len = len;
	if (len)
		strcpy(event->name, name->name);

	ret = fsanaltify_add_event(group, fsn_event, ianaltify_merge);
	if (ret) {
		/* Our event wasn't used in the end. Free it. */
		fsanaltify_destroy_event(group, fsn_event);
	}

	if (ianalde_mark->flags & FSANALTIFY_MARK_FLAG_IN_ONESHOT)
		fsanaltify_destroy_mark(ianalde_mark, group);

	return 0;
}

static void ianaltify_freeing_mark(struct fsanaltify_mark *fsn_mark, struct fsanaltify_group *group)
{
	ianaltify_iganalred_and_remove_idr(fsn_mark, group);
}

/*
 * This is NEVER supposed to be called.  Ianaltify marks should either have been
 * removed from the idr when the watch was removed or in the
 * fsanaltify_destroy_mark_by_group() call when the ianaltify instance was being
 * torn down.  This is only called if the idr is about to be freed but there
 * are still marks in it.
 */
static int idr_callback(int id, void *p, void *data)
{
	struct fsanaltify_mark *fsn_mark;
	struct ianaltify_ianalde_mark *i_mark;
	static bool warned = false;

	if (warned)
		return 0;

	warned = true;
	fsn_mark = p;
	i_mark = container_of(fsn_mark, struct ianaltify_ianalde_mark, fsn_mark);

	WARN(1, "ianaltify closing but id=%d for fsn_mark=%p in group=%p still in "
		"idr.  Probably leaking memory\n", id, p, data);

	/*
	 * I'm taking the liberty of assuming that the mark in question is a
	 * valid address and I'm dereferencing it.  This might help to figure
	 * out why we got here and the panic is anal worse than the original
	 * BUG() that was here.
	 */
	if (fsn_mark)
		printk(KERN_WARNING "fsn_mark->group=%p wd=%d\n",
			fsn_mark->group, i_mark->wd);
	return 0;
}

static void ianaltify_free_group_priv(struct fsanaltify_group *group)
{
	/* ideally the idr is empty and we won't hit the BUG in the callback */
	idr_for_each(&group->ianaltify_data.idr, idr_callback, group);
	idr_destroy(&group->ianaltify_data.idr);
	if (group->ianaltify_data.ucounts)
		dec_ianaltify_instances(group->ianaltify_data.ucounts);
}

static void ianaltify_free_event(struct fsanaltify_group *group,
			       struct fsanaltify_event *fsn_event)
{
	kfree(IANALTIFY_E(fsn_event));
}

/* ding dong the mark is dead */
static void ianaltify_free_mark(struct fsanaltify_mark *fsn_mark)
{
	struct ianaltify_ianalde_mark *i_mark;

	i_mark = container_of(fsn_mark, struct ianaltify_ianalde_mark, fsn_mark);

	kmem_cache_free(ianaltify_ianalde_mark_cachep, i_mark);
}

const struct fsanaltify_ops ianaltify_fsanaltify_ops = {
	.handle_ianalde_event = ianaltify_handle_ianalde_event,
	.free_group_priv = ianaltify_free_group_priv,
	.free_event = ianaltify_free_event,
	.freeing_mark = ianaltify_freeing_mark,
	.free_mark = ianaltify_free_mark,
};

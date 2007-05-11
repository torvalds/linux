/*
 * fs/inotify.c - inode-based file event notifications
 *
 * Authors:
 *	John McCutchan	<ttb@tentacle.dhs.org>
 *	Robert Love	<rml@novell.com>
 *
 * Kernel API added by: Amy Griffis <amy.griffis@hp.com>
 *
 * Copyright (C) 2005 John McCutchan
 * Copyright 2006 Hewlett-Packard Development Company, L.P.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/writeback.h>
#include <linux/inotify.h>

static atomic_t inotify_cookie;

/*
 * Lock ordering:
 *
 * dentry->d_lock (used to keep d_move() away from dentry->d_parent)
 * iprune_mutex (synchronize shrink_icache_memory())
 * 	inode_lock (protects the super_block->s_inodes list)
 * 	inode->inotify_mutex (protects inode->inotify_watches and watches->i_list)
 * 		inotify_handle->mutex (protects inotify_handle and watches->h_list)
 *
 * The inode->inotify_mutex and inotify_handle->mutex and held during execution
 * of a caller's event handler.  Thus, the caller must not hold any locks
 * taken in their event handler while calling any of the published inotify
 * interfaces.
 */

/*
 * Lifetimes of the three main data structures--inotify_handle, inode, and
 * inotify_watch--are managed by reference count.
 *
 * inotify_handle: Lifetime is from inotify_init() to inotify_destroy().
 * Additional references can bump the count via get_inotify_handle() and drop
 * the count via put_inotify_handle().
 *
 * inotify_watch: for inotify's purposes, lifetime is from inotify_add_watch()
 * to remove_watch_no_event().  Additional references can bump the count via
 * get_inotify_watch() and drop the count via put_inotify_watch().  The caller
 * is reponsible for the final put after receiving IN_IGNORED, or when using
 * IN_ONESHOT after receiving the first event.  Inotify does the final put if
 * inotify_destroy() is called.
 *
 * inode: Pinned so long as the inode is associated with a watch, from
 * inotify_add_watch() to the final put_inotify_watch().
 */

/*
 * struct inotify_handle - represents an inotify instance
 *
 * This structure is protected by the mutex 'mutex'.
 */
struct inotify_handle {
	struct idr		idr;		/* idr mapping wd -> watch */
	struct mutex		mutex;		/* protects this bad boy */
	struct list_head	watches;	/* list of watches */
	atomic_t		count;		/* reference count */
	u32			last_wd;	/* the last wd allocated */
	const struct inotify_operations *in_ops; /* inotify caller operations */
};

static inline void get_inotify_handle(struct inotify_handle *ih)
{
	atomic_inc(&ih->count);
}

static inline void put_inotify_handle(struct inotify_handle *ih)
{
	if (atomic_dec_and_test(&ih->count)) {
		idr_destroy(&ih->idr);
		kfree(ih);
	}
}

/**
 * get_inotify_watch - grab a reference to an inotify_watch
 * @watch: watch to grab
 */
void get_inotify_watch(struct inotify_watch *watch)
{
	atomic_inc(&watch->count);
}
EXPORT_SYMBOL_GPL(get_inotify_watch);

/**
 * put_inotify_watch - decrements the ref count on a given watch.  cleans up
 * watch references if the count reaches zero.  inotify_watch is freed by
 * inotify callers via the destroy_watch() op.
 * @watch: watch to release
 */
void put_inotify_watch(struct inotify_watch *watch)
{
	if (atomic_dec_and_test(&watch->count)) {
		struct inotify_handle *ih = watch->ih;

		iput(watch->inode);
		ih->in_ops->destroy_watch(watch);
		put_inotify_handle(ih);
	}
}
EXPORT_SYMBOL_GPL(put_inotify_watch);

/*
 * inotify_handle_get_wd - returns the next WD for use by the given handle
 *
 * Callers must hold ih->mutex.  This function can sleep.
 */
static int inotify_handle_get_wd(struct inotify_handle *ih,
				 struct inotify_watch *watch)
{
	int ret;

	do {
		if (unlikely(!idr_pre_get(&ih->idr, GFP_KERNEL)))
			return -ENOSPC;
		ret = idr_get_new_above(&ih->idr, watch, ih->last_wd+1, &watch->wd);
	} while (ret == -EAGAIN);

	if (likely(!ret))
		ih->last_wd = watch->wd;

	return ret;
}

/*
 * inotify_inode_watched - returns nonzero if there are watches on this inode
 * and zero otherwise.  We call this lockless, we do not care if we race.
 */
static inline int inotify_inode_watched(struct inode *inode)
{
	return !list_empty(&inode->inotify_watches);
}

/*
 * Get child dentry flag into synch with parent inode.
 * Flag should always be clear for negative dentrys.
 */
static void set_dentry_child_flags(struct inode *inode, int watched)
{
	struct dentry *alias;

	spin_lock(&dcache_lock);
	list_for_each_entry(alias, &inode->i_dentry, d_alias) {
		struct dentry *child;

		list_for_each_entry(child, &alias->d_subdirs, d_u.d_child) {
			if (!child->d_inode) {
				WARN_ON(child->d_flags & DCACHE_INOTIFY_PARENT_WATCHED);
				continue;
			}
			spin_lock(&child->d_lock);
			if (watched) {
				WARN_ON(child->d_flags &
						DCACHE_INOTIFY_PARENT_WATCHED);
				child->d_flags |= DCACHE_INOTIFY_PARENT_WATCHED;
			} else {
				WARN_ON(!(child->d_flags &
					DCACHE_INOTIFY_PARENT_WATCHED));
				child->d_flags&=~DCACHE_INOTIFY_PARENT_WATCHED;
			}
			spin_unlock(&child->d_lock);
		}
	}
	spin_unlock(&dcache_lock);
}

/*
 * inotify_find_handle - find the watch associated with the given inode and
 * handle
 *
 * Callers must hold inode->inotify_mutex.
 */
static struct inotify_watch *inode_find_handle(struct inode *inode,
					       struct inotify_handle *ih)
{
	struct inotify_watch *watch;

	list_for_each_entry(watch, &inode->inotify_watches, i_list) {
		if (watch->ih == ih)
			return watch;
	}

	return NULL;
}

/*
 * remove_watch_no_event - remove watch without the IN_IGNORED event.
 *
 * Callers must hold both inode->inotify_mutex and ih->mutex.
 */
static void remove_watch_no_event(struct inotify_watch *watch,
				  struct inotify_handle *ih)
{
	list_del(&watch->i_list);
	list_del(&watch->h_list);

	if (!inotify_inode_watched(watch->inode))
		set_dentry_child_flags(watch->inode, 0);

	idr_remove(&ih->idr, watch->wd);
}

/**
 * inotify_remove_watch_locked - Remove a watch from both the handle and the
 * inode.  Sends the IN_IGNORED event signifying that the inode is no longer
 * watched.  May be invoked from a caller's event handler.
 * @ih: inotify handle associated with watch
 * @watch: watch to remove
 *
 * Callers must hold both inode->inotify_mutex and ih->mutex.
 */
void inotify_remove_watch_locked(struct inotify_handle *ih,
				 struct inotify_watch *watch)
{
	remove_watch_no_event(watch, ih);
	ih->in_ops->handle_event(watch, watch->wd, IN_IGNORED, 0, NULL, NULL);
}
EXPORT_SYMBOL_GPL(inotify_remove_watch_locked);

/* Kernel API for producing events */

/*
 * inotify_d_instantiate - instantiate dcache entry for inode
 */
void inotify_d_instantiate(struct dentry *entry, struct inode *inode)
{
	struct dentry *parent;

	if (!inode)
		return;

	WARN_ON(entry->d_flags & DCACHE_INOTIFY_PARENT_WATCHED);
	spin_lock(&entry->d_lock);
	parent = entry->d_parent;
	if (parent->d_inode && inotify_inode_watched(parent->d_inode))
		entry->d_flags |= DCACHE_INOTIFY_PARENT_WATCHED;
	spin_unlock(&entry->d_lock);
}

/*
 * inotify_d_move - dcache entry has been moved
 */
void inotify_d_move(struct dentry *entry)
{
	struct dentry *parent;

	parent = entry->d_parent;
	if (inotify_inode_watched(parent->d_inode))
		entry->d_flags |= DCACHE_INOTIFY_PARENT_WATCHED;
	else
		entry->d_flags &= ~DCACHE_INOTIFY_PARENT_WATCHED;
}

/**
 * inotify_inode_queue_event - queue an event to all watches on this inode
 * @inode: inode event is originating from
 * @mask: event mask describing this event
 * @cookie: cookie for synchronization, or zero
 * @name: filename, if any
 * @n_inode: inode associated with name
 */
void inotify_inode_queue_event(struct inode *inode, u32 mask, u32 cookie,
			       const char *name, struct inode *n_inode)
{
	struct inotify_watch *watch, *next;

	if (!inotify_inode_watched(inode))
		return;

	mutex_lock(&inode->inotify_mutex);
	list_for_each_entry_safe(watch, next, &inode->inotify_watches, i_list) {
		u32 watch_mask = watch->mask;
		if (watch_mask & mask) {
			struct inotify_handle *ih= watch->ih;
			mutex_lock(&ih->mutex);
			if (watch_mask & IN_ONESHOT)
				remove_watch_no_event(watch, ih);
			ih->in_ops->handle_event(watch, watch->wd, mask, cookie,
						 name, n_inode);
			mutex_unlock(&ih->mutex);
		}
	}
	mutex_unlock(&inode->inotify_mutex);
}
EXPORT_SYMBOL_GPL(inotify_inode_queue_event);

/**
 * inotify_dentry_parent_queue_event - queue an event to a dentry's parent
 * @dentry: the dentry in question, we queue against this dentry's parent
 * @mask: event mask describing this event
 * @cookie: cookie for synchronization, or zero
 * @name: filename, if any
 */
void inotify_dentry_parent_queue_event(struct dentry *dentry, u32 mask,
				       u32 cookie, const char *name)
{
	struct dentry *parent;
	struct inode *inode;

	if (!(dentry->d_flags & DCACHE_INOTIFY_PARENT_WATCHED))
		return;

	spin_lock(&dentry->d_lock);
	parent = dentry->d_parent;
	inode = parent->d_inode;

	if (inotify_inode_watched(inode)) {
		dget(parent);
		spin_unlock(&dentry->d_lock);
		inotify_inode_queue_event(inode, mask, cookie, name,
					  dentry->d_inode);
		dput(parent);
	} else
		spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL_GPL(inotify_dentry_parent_queue_event);

/**
 * inotify_get_cookie - return a unique cookie for use in synchronizing events.
 */
u32 inotify_get_cookie(void)
{
	return atomic_inc_return(&inotify_cookie);
}
EXPORT_SYMBOL_GPL(inotify_get_cookie);

/**
 * inotify_unmount_inodes - an sb is unmounting.  handle any watched inodes.
 * @list: list of inodes being unmounted (sb->s_inodes)
 *
 * Called with inode_lock held, protecting the unmounting super block's list
 * of inodes, and with iprune_mutex held, keeping shrink_icache_memory() at bay.
 * We temporarily drop inode_lock, however, and CAN block.
 */
void inotify_unmount_inodes(struct list_head *list)
{
	struct inode *inode, *next_i, *need_iput = NULL;

	list_for_each_entry_safe(inode, next_i, list, i_sb_list) {
		struct inotify_watch *watch, *next_w;
		struct inode *need_iput_tmp;
		struct list_head *watches;

		/*
		 * If i_count is zero, the inode cannot have any watches and
		 * doing an __iget/iput with MS_ACTIVE clear would actually
		 * evict all inodes with zero i_count from icache which is
		 * unnecessarily violent and may in fact be illegal to do.
		 */
		if (!atomic_read(&inode->i_count))
			continue;

		/*
		 * We cannot __iget() an inode in state I_CLEAR, I_FREEING, or
		 * I_WILL_FREE which is fine because by that point the inode
		 * cannot have any associated watches.
		 */
		if (inode->i_state & (I_CLEAR | I_FREEING | I_WILL_FREE))
			continue;

		need_iput_tmp = need_iput;
		need_iput = NULL;
		/* In case inotify_remove_watch_locked() drops a reference. */
		if (inode != need_iput_tmp)
			__iget(inode);
		else
			need_iput_tmp = NULL;
		/* In case the dropping of a reference would nuke next_i. */
		if ((&next_i->i_sb_list != list) &&
				atomic_read(&next_i->i_count) &&
				!(next_i->i_state & (I_CLEAR | I_FREEING |
					I_WILL_FREE))) {
			__iget(next_i);
			need_iput = next_i;
		}

		/*
		 * We can safely drop inode_lock here because we hold
		 * references on both inode and next_i.  Also no new inodes
		 * will be added since the umount has begun.  Finally,
		 * iprune_mutex keeps shrink_icache_memory() away.
		 */
		spin_unlock(&inode_lock);

		if (need_iput_tmp)
			iput(need_iput_tmp);

		/* for each watch, send IN_UNMOUNT and then remove it */
		mutex_lock(&inode->inotify_mutex);
		watches = &inode->inotify_watches;
		list_for_each_entry_safe(watch, next_w, watches, i_list) {
			struct inotify_handle *ih= watch->ih;
			mutex_lock(&ih->mutex);
			ih->in_ops->handle_event(watch, watch->wd, IN_UNMOUNT, 0,
						 NULL, NULL);
			inotify_remove_watch_locked(ih, watch);
			mutex_unlock(&ih->mutex);
		}
		mutex_unlock(&inode->inotify_mutex);
		iput(inode);		

		spin_lock(&inode_lock);
	}
}
EXPORT_SYMBOL_GPL(inotify_unmount_inodes);

/**
 * inotify_inode_is_dead - an inode has been deleted, cleanup any watches
 * @inode: inode that is about to be removed
 */
void inotify_inode_is_dead(struct inode *inode)
{
	struct inotify_watch *watch, *next;

	mutex_lock(&inode->inotify_mutex);
	list_for_each_entry_safe(watch, next, &inode->inotify_watches, i_list) {
		struct inotify_handle *ih = watch->ih;
		mutex_lock(&ih->mutex);
		inotify_remove_watch_locked(ih, watch);
		mutex_unlock(&ih->mutex);
	}
	mutex_unlock(&inode->inotify_mutex);
}
EXPORT_SYMBOL_GPL(inotify_inode_is_dead);

/* Kernel Consumer API */

/**
 * inotify_init - allocate and initialize an inotify instance
 * @ops: caller's inotify operations
 */
struct inotify_handle *inotify_init(const struct inotify_operations *ops)
{
	struct inotify_handle *ih;

	ih = kmalloc(sizeof(struct inotify_handle), GFP_KERNEL);
	if (unlikely(!ih))
		return ERR_PTR(-ENOMEM);

	idr_init(&ih->idr);
	INIT_LIST_HEAD(&ih->watches);
	mutex_init(&ih->mutex);
	ih->last_wd = 0;
	ih->in_ops = ops;
	atomic_set(&ih->count, 0);
	get_inotify_handle(ih);

	return ih;
}
EXPORT_SYMBOL_GPL(inotify_init);

/**
 * inotify_init_watch - initialize an inotify watch
 * @watch: watch to initialize
 */
void inotify_init_watch(struct inotify_watch *watch)
{
	INIT_LIST_HEAD(&watch->h_list);
	INIT_LIST_HEAD(&watch->i_list);
	atomic_set(&watch->count, 0);
	get_inotify_watch(watch); /* initial get */
}
EXPORT_SYMBOL_GPL(inotify_init_watch);

/**
 * inotify_destroy - clean up and destroy an inotify instance
 * @ih: inotify handle
 */
void inotify_destroy(struct inotify_handle *ih)
{
	/*
	 * Destroy all of the watches for this handle. Unfortunately, not very
	 * pretty.  We cannot do a simple iteration over the list, because we
	 * do not know the inode until we iterate to the watch.  But we need to
	 * hold inode->inotify_mutex before ih->mutex.  The following works.
	 */
	while (1) {
		struct inotify_watch *watch;
		struct list_head *watches;
		struct inode *inode;

		mutex_lock(&ih->mutex);
		watches = &ih->watches;
		if (list_empty(watches)) {
			mutex_unlock(&ih->mutex);
			break;
		}
		watch = list_first_entry(watches, struct inotify_watch, h_list);
		get_inotify_watch(watch);
		mutex_unlock(&ih->mutex);

		inode = watch->inode;
		mutex_lock(&inode->inotify_mutex);
		mutex_lock(&ih->mutex);

		/* make sure we didn't race with another list removal */
		if (likely(idr_find(&ih->idr, watch->wd))) {
			remove_watch_no_event(watch, ih);
			put_inotify_watch(watch);
		}

		mutex_unlock(&ih->mutex);
		mutex_unlock(&inode->inotify_mutex);
		put_inotify_watch(watch);
	}

	/* free this handle: the put matching the get in inotify_init() */
	put_inotify_handle(ih);
}
EXPORT_SYMBOL_GPL(inotify_destroy);

/**
 * inotify_find_watch - find an existing watch for an (ih,inode) pair
 * @ih: inotify handle
 * @inode: inode to watch
 * @watchp: pointer to existing inotify_watch
 *
 * Caller must pin given inode (via nameidata).
 */
s32 inotify_find_watch(struct inotify_handle *ih, struct inode *inode,
		       struct inotify_watch **watchp)
{
	struct inotify_watch *old;
	int ret = -ENOENT;

	mutex_lock(&inode->inotify_mutex);
	mutex_lock(&ih->mutex);

	old = inode_find_handle(inode, ih);
	if (unlikely(old)) {
		get_inotify_watch(old); /* caller must put watch */
		*watchp = old;
		ret = old->wd;
	}

	mutex_unlock(&ih->mutex);
	mutex_unlock(&inode->inotify_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(inotify_find_watch);

/**
 * inotify_find_update_watch - find and update the mask of an existing watch
 * @ih: inotify handle
 * @inode: inode's watch to update
 * @mask: mask of events to watch
 *
 * Caller must pin given inode (via nameidata).
 */
s32 inotify_find_update_watch(struct inotify_handle *ih, struct inode *inode,
			      u32 mask)
{
	struct inotify_watch *old;
	int mask_add = 0;
	int ret;

	if (mask & IN_MASK_ADD)
		mask_add = 1;

	/* don't allow invalid bits: we don't want flags set */
	mask &= IN_ALL_EVENTS | IN_ONESHOT;
	if (unlikely(!mask))
		return -EINVAL;

	mutex_lock(&inode->inotify_mutex);
	mutex_lock(&ih->mutex);

	/*
	 * Handle the case of re-adding a watch on an (inode,ih) pair that we
	 * are already watching.  We just update the mask and return its wd.
	 */
	old = inode_find_handle(inode, ih);
	if (unlikely(!old)) {
		ret = -ENOENT;
		goto out;
	}

	if (mask_add)
		old->mask |= mask;
	else
		old->mask = mask;
	ret = old->wd;
out:
	mutex_unlock(&ih->mutex);
	mutex_unlock(&inode->inotify_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(inotify_find_update_watch);

/**
 * inotify_add_watch - add a watch to an inotify instance
 * @ih: inotify handle
 * @watch: caller allocated watch structure
 * @inode: inode to watch
 * @mask: mask of events to watch
 *
 * Caller must pin given inode (via nameidata).
 * Caller must ensure it only calls inotify_add_watch() once per watch.
 * Calls inotify_handle_get_wd() so may sleep.
 */
s32 inotify_add_watch(struct inotify_handle *ih, struct inotify_watch *watch,
		      struct inode *inode, u32 mask)
{
	int ret = 0;

	/* don't allow invalid bits: we don't want flags set */
	mask &= IN_ALL_EVENTS | IN_ONESHOT;
	if (unlikely(!mask))
		return -EINVAL;
	watch->mask = mask;

	mutex_lock(&inode->inotify_mutex);
	mutex_lock(&ih->mutex);

	/* Initialize a new watch */
	ret = inotify_handle_get_wd(ih, watch);
	if (unlikely(ret))
		goto out;
	ret = watch->wd;

	/* save a reference to handle and bump the count to make it official */
	get_inotify_handle(ih);
	watch->ih = ih;

	/*
	 * Save a reference to the inode and bump the ref count to make it
	 * official.  We hold a reference to nameidata, which makes this safe.
	 */
	watch->inode = igrab(inode);

	if (!inotify_inode_watched(inode))
		set_dentry_child_flags(inode, 1);

	/* Add the watch to the handle's and the inode's list */
	list_add(&watch->h_list, &ih->watches);
	list_add(&watch->i_list, &inode->inotify_watches);
out:
	mutex_unlock(&ih->mutex);
	mutex_unlock(&inode->inotify_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(inotify_add_watch);

/**
 * inotify_rm_wd - remove a watch from an inotify instance
 * @ih: inotify handle
 * @wd: watch descriptor to remove
 *
 * Can sleep.
 */
int inotify_rm_wd(struct inotify_handle *ih, u32 wd)
{
	struct inotify_watch *watch;
	struct inode *inode;

	mutex_lock(&ih->mutex);
	watch = idr_find(&ih->idr, wd);
	if (unlikely(!watch)) {
		mutex_unlock(&ih->mutex);
		return -EINVAL;
	}
	get_inotify_watch(watch);
	inode = watch->inode;
	mutex_unlock(&ih->mutex);

	mutex_lock(&inode->inotify_mutex);
	mutex_lock(&ih->mutex);

	/* make sure that we did not race */
	if (likely(idr_find(&ih->idr, wd) == watch))
		inotify_remove_watch_locked(ih, watch);

	mutex_unlock(&ih->mutex);
	mutex_unlock(&inode->inotify_mutex);
	put_inotify_watch(watch);

	return 0;
}
EXPORT_SYMBOL_GPL(inotify_rm_wd);

/**
 * inotify_rm_watch - remove a watch from an inotify instance
 * @ih: inotify handle
 * @watch: watch to remove
 *
 * Can sleep.
 */
int inotify_rm_watch(struct inotify_handle *ih,
		     struct inotify_watch *watch)
{
	return inotify_rm_wd(ih, watch->wd);
}
EXPORT_SYMBOL_GPL(inotify_rm_watch);

/*
 * inotify_setup - core initialization function
 */
static int __init inotify_setup(void)
{
	atomic_set(&inotify_cookie, 0);

	return 0;
}

module_init(inotify_setup);

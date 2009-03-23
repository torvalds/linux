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

int pin_inotify_watch(struct inotify_watch *watch)
{
	struct super_block *sb = watch->inode->i_sb;
	spin_lock(&sb_lock);
	if (sb->s_count >= S_BIAS) {
		atomic_inc(&sb->s_active);
		spin_unlock(&sb_lock);
		atomic_inc(&watch->count);
		return 1;
	}
	spin_unlock(&sb_lock);
	return 0;
}

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

void unpin_inotify_watch(struct inotify_watch *watch)
{
	struct super_block *sb = watch->inode->i_sb;
	put_inotify_watch(watch);
	deactivate_super(sb);
}

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
		if (unlikely(!idr_pre_get(&ih->idr, GFP_NOFS)))
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
			if (!child->d_inode)
				continue;

			spin_lock(&child->d_lock);
			if (watched)
				child->d_flags |= DCACHE_INOTIFY_PARENT_WATCHED;
			else
				child->d_flags &=~DCACHE_INOTIFY_PARENT_WATCHED;
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
			get_inotify_watch(watch);
			mutex_lock(&ih->mutex);
			ih->in_ops->handle_event(watch, watch->wd, IN_UNMOUNT, 0,
						 NULL, NULL);
			inotify_remove_watch_locked(ih, watch);
			mutex_unlock(&ih->mutex);
			put_inotify_watch(watch);
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

/*
 * Watch removals suck violently.  To kick the watch out we need (in this
 * order) inode->inotify_mutex and ih->mutex.  That's fine if we have
 * a hold on inode; however, for all other cases we need to make damn sure
 * we don't race with umount.  We can *NOT* just grab a reference to a
 * watch - inotify_unmount_inodes() will happily sail past it and we'll end
 * with reference to inode potentially outliving its superblock.  Ideally
 * we just want to grab an active reference to superblock if we can; that
 * will make sure we won't go into inotify_umount_inodes() until we are
 * done.  Cleanup is just deactivate_super().  However, that leaves a messy
 * case - what if we *are* racing with umount() and active references to
 * superblock can't be acquired anymore?  We can bump ->s_count, grab
 * ->s_umount, which will almost certainly wait until the superblock is shut
 * down and the watch in question is pining for fjords.  That's fine, but
 * there is a problem - we might have hit the window between ->s_active
 * getting to 0 / ->s_count - below S_BIAS (i.e. the moment when superblock
 * is past the point of no return and is heading for shutdown) and the
 * moment when deactivate_super() acquires ->s_umount.  We could just do
 * drop_super() yield() and retry, but that's rather antisocial and this
 * stuff is luser-triggerable.  OTOH, having grabbed ->s_umount and having
 * found that we'd got there first (i.e. that ->s_root is non-NULL) we know
 * that we won't race with inotify_umount_inodes().  So we could grab a
 * reference to watch and do the rest as above, just with drop_super() instead
 * of deactivate_super(), right?  Wrong.  We had to drop ih->mutex before we
 * could grab ->s_umount.  So the watch could've been gone already.
 *
 * That still can be dealt with - we need to save watch->wd, do idr_find()
 * and compare its result with our pointer.  If they match, we either have
 * the damn thing still alive or we'd lost not one but two races at once,
 * the watch had been killed and a new one got created with the same ->wd
 * at the same address.  That couldn't have happened in inotify_destroy(),
 * but inotify_rm_wd() could run into that.  Still, "new one got created"
 * is not a problem - we have every right to kill it or leave it alone,
 * whatever's more convenient.
 *
 * So we can use idr_find(...) == watch && watch->inode->i_sb == sb as
 * "grab it and kill it" check.  If it's been our original watch, we are
 * fine, if it's a newcomer - nevermind, just pretend that we'd won the
 * race and kill the fscker anyway; we are safe since we know that its
 * superblock won't be going away.
 *
 * And yes, this is far beyond mere "not very pretty"; so's the entire
 * concept of inotify to start with.
 */

/**
 * pin_to_kill - pin the watch down for removal
 * @ih: inotify handle
 * @watch: watch to kill
 *
 * Called with ih->mutex held, drops it.  Possible return values:
 * 0 - nothing to do, it has died
 * 1 - remove it, drop the reference and deactivate_super()
 * 2 - remove it, drop the reference and drop_super(); we tried hard to avoid
 * that variant, since it involved a lot of PITA, but that's the best that
 * could've been done.
 */
static int pin_to_kill(struct inotify_handle *ih, struct inotify_watch *watch)
{
	struct super_block *sb = watch->inode->i_sb;
	s32 wd = watch->wd;

	spin_lock(&sb_lock);
	if (sb->s_count >= S_BIAS) {
		atomic_inc(&sb->s_active);
		spin_unlock(&sb_lock);
		get_inotify_watch(watch);
		mutex_unlock(&ih->mutex);
		return 1;	/* the best outcome */
	}
	sb->s_count++;
	spin_unlock(&sb_lock);
	mutex_unlock(&ih->mutex); /* can't grab ->s_umount under it */
	down_read(&sb->s_umount);
	if (likely(!sb->s_root)) {
		/* fs is already shut down; the watch is dead */
		drop_super(sb);
		return 0;
	}
	/* raced with the final deactivate_super() */
	mutex_lock(&ih->mutex);
	if (idr_find(&ih->idr, wd) != watch || watch->inode->i_sb != sb) {
		/* the watch is dead */
		mutex_unlock(&ih->mutex);
		drop_super(sb);
		return 0;
	}
	/* still alive or freed and reused with the same sb and wd; kill */
	get_inotify_watch(watch);
	mutex_unlock(&ih->mutex);
	return 2;
}

static void unpin_and_kill(struct inotify_watch *watch, int how)
{
	struct super_block *sb = watch->inode->i_sb;
	put_inotify_watch(watch);
	switch (how) {
	case 1:
		deactivate_super(sb);
		break;
	case 2:
		drop_super(sb);
	}
}

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
	 *
	 * AV: it had to become even uglier to start working ;-/
	 */
	while (1) {
		struct inotify_watch *watch;
		struct list_head *watches;
		struct super_block *sb;
		struct inode *inode;
		int how;

		mutex_lock(&ih->mutex);
		watches = &ih->watches;
		if (list_empty(watches)) {
			mutex_unlock(&ih->mutex);
			break;
		}
		watch = list_first_entry(watches, struct inotify_watch, h_list);
		sb = watch->inode->i_sb;
		how = pin_to_kill(ih, watch);
		if (!how)
			continue;

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
		unpin_and_kill(watch, how);
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
	int newly_watched;

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

	/* Add the watch to the handle's and the inode's list */
	newly_watched = !inotify_inode_watched(inode);
	list_add(&watch->h_list, &ih->watches);
	list_add(&watch->i_list, &inode->inotify_watches);
	/*
	 * Set child flags _after_ adding the watch, so there is no race
	 * windows where newly instantiated children could miss their parent's
	 * watched flag.
	 */
	if (newly_watched)
		set_dentry_child_flags(inode, 1);

out:
	mutex_unlock(&ih->mutex);
	mutex_unlock(&inode->inotify_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(inotify_add_watch);

/**
 * inotify_clone_watch - put the watch next to existing one
 * @old: already installed watch
 * @new: new watch
 *
 * Caller must hold the inotify_mutex of inode we are dealing with;
 * it is expected to remove the old watch before unlocking the inode.
 */
s32 inotify_clone_watch(struct inotify_watch *old, struct inotify_watch *new)
{
	struct inotify_handle *ih = old->ih;
	int ret = 0;

	new->mask = old->mask;
	new->ih = ih;

	mutex_lock(&ih->mutex);

	/* Initialize a new watch */
	ret = inotify_handle_get_wd(ih, new);
	if (unlikely(ret))
		goto out;
	ret = new->wd;

	get_inotify_handle(ih);

	new->inode = igrab(old->inode);

	list_add(&new->h_list, &ih->watches);
	list_add(&new->i_list, &old->inode->inotify_watches);
out:
	mutex_unlock(&ih->mutex);
	return ret;
}

void inotify_evict_watch(struct inotify_watch *watch)
{
	get_inotify_watch(watch);
	mutex_lock(&watch->ih->mutex);
	inotify_remove_watch_locked(watch->ih, watch);
	mutex_unlock(&watch->ih->mutex);
}

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
	struct super_block *sb;
	struct inode *inode;
	int how;

	mutex_lock(&ih->mutex);
	watch = idr_find(&ih->idr, wd);
	if (unlikely(!watch)) {
		mutex_unlock(&ih->mutex);
		return -EINVAL;
	}
	sb = watch->inode->i_sb;
	how = pin_to_kill(ih, watch);
	if (!how)
		return 0;

	inode = watch->inode;

	mutex_lock(&inode->inotify_mutex);
	mutex_lock(&ih->mutex);

	/* make sure that we did not race */
	if (likely(idr_find(&ih->idr, wd) == watch))
		inotify_remove_watch_locked(ih, watch);

	mutex_unlock(&ih->mutex);
	mutex_unlock(&inode->inotify_mutex);
	unpin_and_kill(watch, how);

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

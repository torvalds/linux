// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/mmu_notifier.c
 *
 *  Copyright (C) 2008  Qumranet, Inc.
 *  Copyright (C) 2008  SGI
 *             Christoph Lameter <cl@linux.com>
 */

#include <linux/rculist.h>
#include <linux/mmu_notifier.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/interval_tree.h>
#include <linux/srcu.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>

/* global SRCU for all MMs */
DEFINE_STATIC_SRCU(srcu);

#ifdef CONFIG_LOCKDEP
struct lockdep_map __mmu_notifier_invalidate_range_start_map = {
	.name = "mmu_notifier_invalidate_range_start"
};
#endif

/*
 * The mmu notifier_mm structure is allocated and installed in
 * mm->mmu_notifier_mm inside the mm_take_all_locks() protected
 * critical section and it's released only when mm_count reaches zero
 * in mmdrop().
 */
struct mmu_notifier_mm {
	/* all mmu notifiers registered in this mm are queued in this list */
	struct hlist_head list;
	bool has_itree;
	/* to serialize the list modifications and hlist_unhashed */
	spinlock_t lock;
	unsigned long invalidate_seq;
	unsigned long active_invalidate_ranges;
	struct rb_root_cached itree;
	wait_queue_head_t wq;
	struct hlist_head deferred_list;
};

/*
 * This is a collision-retry read-side/write-side 'lock', a lot like a
 * seqcount, however this allows multiple write-sides to hold it at
 * once. Conceptually the write side is protecting the values of the PTEs in
 * this mm, such that PTES cannot be read into SPTEs (shadow PTEs) while any
 * writer exists.
 *
 * Note that the core mm creates nested invalidate_range_start()/end() regions
 * within the same thread, and runs invalidate_range_start()/end() in parallel
 * on multiple CPUs. This is designed to not reduce concurrency or block
 * progress on the mm side.
 *
 * As a secondary function, holding the full write side also serves to prevent
 * writers for the itree, this is an optimization to avoid extra locking
 * during invalidate_range_start/end notifiers.
 *
 * The write side has two states, fully excluded:
 *  - mm->active_invalidate_ranges != 0
 *  - mnn->invalidate_seq & 1 == True (odd)
 *  - some range on the mm_struct is being invalidated
 *  - the itree is not allowed to change
 *
 * And partially excluded:
 *  - mm->active_invalidate_ranges != 0
 *  - mnn->invalidate_seq & 1 == False (even)
 *  - some range on the mm_struct is being invalidated
 *  - the itree is allowed to change
 *
 * Operations on mmu_notifier_mm->invalidate_seq (under spinlock):
 *    seq |= 1  # Begin writing
 *    seq++     # Release the writing state
 *    seq & 1   # True if a writer exists
 *
 * The later state avoids some expensive work on inv_end in the common case of
 * no mni monitoring the VA.
 */
static bool mn_itree_is_invalidating(struct mmu_notifier_mm *mmn_mm)
{
	lockdep_assert_held(&mmn_mm->lock);
	return mmn_mm->invalidate_seq & 1;
}

static struct mmu_interval_notifier *
mn_itree_inv_start_range(struct mmu_notifier_mm *mmn_mm,
			 const struct mmu_notifier_range *range,
			 unsigned long *seq)
{
	struct interval_tree_node *node;
	struct mmu_interval_notifier *res = NULL;

	spin_lock(&mmn_mm->lock);
	mmn_mm->active_invalidate_ranges++;
	node = interval_tree_iter_first(&mmn_mm->itree, range->start,
					range->end - 1);
	if (node) {
		mmn_mm->invalidate_seq |= 1;
		res = container_of(node, struct mmu_interval_notifier,
				   interval_tree);
	}

	*seq = mmn_mm->invalidate_seq;
	spin_unlock(&mmn_mm->lock);
	return res;
}

static struct mmu_interval_notifier *
mn_itree_inv_next(struct mmu_interval_notifier *mni,
		  const struct mmu_notifier_range *range)
{
	struct interval_tree_node *node;

	node = interval_tree_iter_next(&mni->interval_tree, range->start,
				       range->end - 1);
	if (!node)
		return NULL;
	return container_of(node, struct mmu_interval_notifier, interval_tree);
}

static void mn_itree_inv_end(struct mmu_notifier_mm *mmn_mm)
{
	struct mmu_interval_notifier *mni;
	struct hlist_node *next;

	spin_lock(&mmn_mm->lock);
	if (--mmn_mm->active_invalidate_ranges ||
	    !mn_itree_is_invalidating(mmn_mm)) {
		spin_unlock(&mmn_mm->lock);
		return;
	}

	/* Make invalidate_seq even */
	mmn_mm->invalidate_seq++;

	/*
	 * The inv_end incorporates a deferred mechanism like rtnl_unlock().
	 * Adds and removes are queued until the final inv_end happens then
	 * they are progressed. This arrangement for tree updates is used to
	 * avoid using a blocking lock during invalidate_range_start.
	 */
	hlist_for_each_entry_safe(mni, next, &mmn_mm->deferred_list,
				  deferred_item) {
		if (RB_EMPTY_NODE(&mni->interval_tree.rb))
			interval_tree_insert(&mni->interval_tree,
					     &mmn_mm->itree);
		else
			interval_tree_remove(&mni->interval_tree,
					     &mmn_mm->itree);
		hlist_del(&mni->deferred_item);
	}
	spin_unlock(&mmn_mm->lock);

	wake_up_all(&mmn_mm->wq);
}

/**
 * mmu_interval_read_begin - Begin a read side critical section against a VA
 *                           range
 * mni: The range to use
 *
 * mmu_iterval_read_begin()/mmu_iterval_read_retry() implement a
 * collision-retry scheme similar to seqcount for the VA range under mni. If
 * the mm invokes invalidation during the critical section then
 * mmu_interval_read_retry() will return true.
 *
 * This is useful to obtain shadow PTEs where teardown or setup of the SPTEs
 * require a blocking context.  The critical region formed by this can sleep,
 * and the required 'user_lock' can also be a sleeping lock.
 *
 * The caller is required to provide a 'user_lock' to serialize both teardown
 * and setup.
 *
 * The return value should be passed to mmu_interval_read_retry().
 */
unsigned long mmu_interval_read_begin(struct mmu_interval_notifier *mni)
{
	struct mmu_notifier_mm *mmn_mm = mni->mm->mmu_notifier_mm;
	unsigned long seq;
	bool is_invalidating;

	/*
	 * If the mni has a different seq value under the user_lock than we
	 * started with then it has collided.
	 *
	 * If the mni currently has the same seq value as the mmn_mm seq, then
	 * it is currently between invalidate_start/end and is colliding.
	 *
	 * The locking looks broadly like this:
	 *   mn_tree_invalidate_start():          mmu_interval_read_begin():
	 *                                         spin_lock
	 *                                          seq = READ_ONCE(mni->invalidate_seq);
	 *                                          seq == mmn_mm->invalidate_seq
	 *                                         spin_unlock
	 *    spin_lock
	 *     seq = ++mmn_mm->invalidate_seq
	 *    spin_unlock
	 *     op->invalidate_range():
	 *       user_lock
	 *        mmu_interval_set_seq()
	 *         mni->invalidate_seq = seq
	 *       user_unlock
	 *
	 *                          [Required: mmu_interval_read_retry() == true]
	 *
	 *   mn_itree_inv_end():
	 *    spin_lock
	 *     seq = ++mmn_mm->invalidate_seq
	 *    spin_unlock
	 *
	 *                                        user_lock
	 *                                         mmu_interval_read_retry():
	 *                                          mni->invalidate_seq != seq
	 *                                        user_unlock
	 *
	 * Barriers are not needed here as any races here are closed by an
	 * eventual mmu_interval_read_retry(), which provides a barrier via the
	 * user_lock.
	 */
	spin_lock(&mmn_mm->lock);
	/* Pairs with the WRITE_ONCE in mmu_interval_set_seq() */
	seq = READ_ONCE(mni->invalidate_seq);
	is_invalidating = seq == mmn_mm->invalidate_seq;
	spin_unlock(&mmn_mm->lock);

	/*
	 * mni->invalidate_seq must always be set to an odd value via
	 * mmu_interval_set_seq() using the provided cur_seq from
	 * mn_itree_inv_start_range(). This ensures that if seq does wrap we
	 * will always clear the below sleep in some reasonable time as
	 * mmn_mm->invalidate_seq is even in the idle state.
	 */
	lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
	lock_map_release(&__mmu_notifier_invalidate_range_start_map);
	if (is_invalidating)
		wait_event(mmn_mm->wq,
			   READ_ONCE(mmn_mm->invalidate_seq) != seq);

	/*
	 * Notice that mmu_interval_read_retry() can already be true at this
	 * point, avoiding loops here allows the caller to provide a global
	 * time bound.
	 */

	return seq;
}
EXPORT_SYMBOL_GPL(mmu_interval_read_begin);

static void mn_itree_release(struct mmu_notifier_mm *mmn_mm,
			     struct mm_struct *mm)
{
	struct mmu_notifier_range range = {
		.flags = MMU_NOTIFIER_RANGE_BLOCKABLE,
		.event = MMU_NOTIFY_RELEASE,
		.mm = mm,
		.start = 0,
		.end = ULONG_MAX,
	};
	struct mmu_interval_notifier *mni;
	unsigned long cur_seq;
	bool ret;

	for (mni = mn_itree_inv_start_range(mmn_mm, &range, &cur_seq); mni;
	     mni = mn_itree_inv_next(mni, &range)) {
		ret = mni->ops->invalidate(mni, &range, cur_seq);
		WARN_ON(!ret);
	}

	mn_itree_inv_end(mmn_mm);
}

/*
 * This function can't run concurrently against mmu_notifier_register
 * because mm->mm_users > 0 during mmu_notifier_register and exit_mmap
 * runs with mm_users == 0. Other tasks may still invoke mmu notifiers
 * in parallel despite there being no task using this mm any more,
 * through the vmas outside of the exit_mmap context, such as with
 * vmtruncate. This serializes against mmu_notifier_unregister with
 * the mmu_notifier_mm->lock in addition to SRCU and it serializes
 * against the other mmu notifiers with SRCU. struct mmu_notifier_mm
 * can't go away from under us as exit_mmap holds an mm_count pin
 * itself.
 */
static void mn_hlist_release(struct mmu_notifier_mm *mmn_mm,
			     struct mm_struct *mm)
{
	struct mmu_notifier *mn;
	int id;

	/*
	 * SRCU here will block mmu_notifier_unregister until
	 * ->release returns.
	 */
	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &mmn_mm->list, hlist)
		/*
		 * If ->release runs before mmu_notifier_unregister it must be
		 * handled, as it's the only way for the driver to flush all
		 * existing sptes and stop the driver from establishing any more
		 * sptes before all the pages in the mm are freed.
		 */
		if (mn->ops->release)
			mn->ops->release(mn, mm);

	spin_lock(&mmn_mm->lock);
	while (unlikely(!hlist_empty(&mmn_mm->list))) {
		mn = hlist_entry(mmn_mm->list.first, struct mmu_notifier,
				 hlist);
		/*
		 * We arrived before mmu_notifier_unregister so
		 * mmu_notifier_unregister will do nothing other than to wait
		 * for ->release to finish and for mmu_notifier_unregister to
		 * return.
		 */
		hlist_del_init_rcu(&mn->hlist);
	}
	spin_unlock(&mmn_mm->lock);
	srcu_read_unlock(&srcu, id);

	/*
	 * synchronize_srcu here prevents mmu_notifier_release from returning to
	 * exit_mmap (which would proceed with freeing all pages in the mm)
	 * until the ->release method returns, if it was invoked by
	 * mmu_notifier_unregister.
	 *
	 * The mmu_notifier_mm can't go away from under us because one mm_count
	 * is held by exit_mmap.
	 */
	synchronize_srcu(&srcu);
}

void __mmu_notifier_release(struct mm_struct *mm)
{
	struct mmu_notifier_mm *mmn_mm = mm->mmu_notifier_mm;

	if (mmn_mm->has_itree)
		mn_itree_release(mmn_mm, mm);

	if (!hlist_empty(&mmn_mm->list))
		mn_hlist_release(mmn_mm, mm);
}

/*
 * If no young bitflag is supported by the hardware, ->clear_flush_young can
 * unmap the address and return 1 or 0 depending if the mapping previously
 * existed or not.
 */
int __mmu_notifier_clear_flush_young(struct mm_struct *mm,
					unsigned long start,
					unsigned long end)
{
	struct mmu_notifier *mn;
	int young = 0, id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &mm->mmu_notifier_mm->list, hlist) {
		if (mn->ops->clear_flush_young)
			young |= mn->ops->clear_flush_young(mn, mm, start, end);
	}
	srcu_read_unlock(&srcu, id);

	return young;
}

int __mmu_notifier_clear_young(struct mm_struct *mm,
			       unsigned long start,
			       unsigned long end)
{
	struct mmu_notifier *mn;
	int young = 0, id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &mm->mmu_notifier_mm->list, hlist) {
		if (mn->ops->clear_young)
			young |= mn->ops->clear_young(mn, mm, start, end);
	}
	srcu_read_unlock(&srcu, id);

	return young;
}

int __mmu_notifier_test_young(struct mm_struct *mm,
			      unsigned long address)
{
	struct mmu_notifier *mn;
	int young = 0, id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &mm->mmu_notifier_mm->list, hlist) {
		if (mn->ops->test_young) {
			young = mn->ops->test_young(mn, mm, address);
			if (young)
				break;
		}
	}
	srcu_read_unlock(&srcu, id);

	return young;
}

void __mmu_notifier_change_pte(struct mm_struct *mm, unsigned long address,
			       pte_t pte)
{
	struct mmu_notifier *mn;
	int id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &mm->mmu_notifier_mm->list, hlist) {
		if (mn->ops->change_pte)
			mn->ops->change_pte(mn, mm, address, pte);
	}
	srcu_read_unlock(&srcu, id);
}

static int mn_itree_invalidate(struct mmu_notifier_mm *mmn_mm,
			       const struct mmu_notifier_range *range)
{
	struct mmu_interval_notifier *mni;
	unsigned long cur_seq;

	for (mni = mn_itree_inv_start_range(mmn_mm, range, &cur_seq); mni;
	     mni = mn_itree_inv_next(mni, range)) {
		bool ret;

		ret = mni->ops->invalidate(mni, range, cur_seq);
		if (!ret) {
			if (WARN_ON(mmu_notifier_range_blockable(range)))
				continue;
			goto out_would_block;
		}
	}
	return 0;

out_would_block:
	/*
	 * On -EAGAIN the non-blocking caller is not allowed to call
	 * invalidate_range_end()
	 */
	mn_itree_inv_end(mmn_mm);
	return -EAGAIN;
}

static int mn_hlist_invalidate_range_start(struct mmu_notifier_mm *mmn_mm,
					   struct mmu_notifier_range *range)
{
	struct mmu_notifier *mn;
	int ret = 0;
	int id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &mmn_mm->list, hlist) {
		if (mn->ops->invalidate_range_start) {
			int _ret;

			if (!mmu_notifier_range_blockable(range))
				non_block_start();
			_ret = mn->ops->invalidate_range_start(mn, range);
			if (!mmu_notifier_range_blockable(range))
				non_block_end();
			if (_ret) {
				pr_info("%pS callback failed with %d in %sblockable context.\n",
					mn->ops->invalidate_range_start, _ret,
					!mmu_notifier_range_blockable(range) ? "non-" : "");
				WARN_ON(mmu_notifier_range_blockable(range) ||
					_ret != -EAGAIN);
				ret = _ret;
			}
		}
	}
	srcu_read_unlock(&srcu, id);

	return ret;
}

int __mmu_notifier_invalidate_range_start(struct mmu_notifier_range *range)
{
	struct mmu_notifier_mm *mmn_mm = range->mm->mmu_notifier_mm;
	int ret;

	if (mmn_mm->has_itree) {
		ret = mn_itree_invalidate(mmn_mm, range);
		if (ret)
			return ret;
	}
	if (!hlist_empty(&mmn_mm->list))
		return mn_hlist_invalidate_range_start(mmn_mm, range);
	return 0;
}

static void mn_hlist_invalidate_end(struct mmu_notifier_mm *mmn_mm,
				    struct mmu_notifier_range *range,
				    bool only_end)
{
	struct mmu_notifier *mn;
	int id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &mmn_mm->list, hlist) {
		/*
		 * Call invalidate_range here too to avoid the need for the
		 * subsystem of having to register an invalidate_range_end
		 * call-back when there is invalidate_range already. Usually a
		 * subsystem registers either invalidate_range_start()/end() or
		 * invalidate_range(), so this will be no additional overhead
		 * (besides the pointer check).
		 *
		 * We skip call to invalidate_range() if we know it is safe ie
		 * call site use mmu_notifier_invalidate_range_only_end() which
		 * is safe to do when we know that a call to invalidate_range()
		 * already happen under page table lock.
		 */
		if (!only_end && mn->ops->invalidate_range)
			mn->ops->invalidate_range(mn, range->mm,
						  range->start,
						  range->end);
		if (mn->ops->invalidate_range_end) {
			if (!mmu_notifier_range_blockable(range))
				non_block_start();
			mn->ops->invalidate_range_end(mn, range);
			if (!mmu_notifier_range_blockable(range))
				non_block_end();
		}
	}
	srcu_read_unlock(&srcu, id);
}

void __mmu_notifier_invalidate_range_end(struct mmu_notifier_range *range,
					 bool only_end)
{
	struct mmu_notifier_mm *mmn_mm = range->mm->mmu_notifier_mm;

	lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
	if (mmn_mm->has_itree)
		mn_itree_inv_end(mmn_mm);

	if (!hlist_empty(&mmn_mm->list))
		mn_hlist_invalidate_end(mmn_mm, range, only_end);
	lock_map_release(&__mmu_notifier_invalidate_range_start_map);
}

void __mmu_notifier_invalidate_range(struct mm_struct *mm,
				  unsigned long start, unsigned long end)
{
	struct mmu_notifier *mn;
	int id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &mm->mmu_notifier_mm->list, hlist) {
		if (mn->ops->invalidate_range)
			mn->ops->invalidate_range(mn, mm, start, end);
	}
	srcu_read_unlock(&srcu, id);
}

/*
 * Same as mmu_notifier_register but here the caller must hold the mmap_sem in
 * write mode. A NULL mn signals the notifier is being registered for itree
 * mode.
 */
int __mmu_notifier_register(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct mmu_notifier_mm *mmu_notifier_mm = NULL;
	int ret;

	lockdep_assert_held_write(&mm->mmap_sem);
	BUG_ON(atomic_read(&mm->mm_users) <= 0);

	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		fs_reclaim_acquire(GFP_KERNEL);
		lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
		lock_map_release(&__mmu_notifier_invalidate_range_start_map);
		fs_reclaim_release(GFP_KERNEL);
	}

	if (!mm->mmu_notifier_mm) {
		/*
		 * kmalloc cannot be called under mm_take_all_locks(), but we
		 * know that mm->mmu_notifier_mm can't change while we hold
		 * the write side of the mmap_sem.
		 */
		mmu_notifier_mm =
			kzalloc(sizeof(struct mmu_notifier_mm), GFP_KERNEL);
		if (!mmu_notifier_mm)
			return -ENOMEM;

		INIT_HLIST_HEAD(&mmu_notifier_mm->list);
		spin_lock_init(&mmu_notifier_mm->lock);
		mmu_notifier_mm->invalidate_seq = 2;
		mmu_notifier_mm->itree = RB_ROOT_CACHED;
		init_waitqueue_head(&mmu_notifier_mm->wq);
		INIT_HLIST_HEAD(&mmu_notifier_mm->deferred_list);
	}

	ret = mm_take_all_locks(mm);
	if (unlikely(ret))
		goto out_clean;

	/*
	 * Serialize the update against mmu_notifier_unregister. A
	 * side note: mmu_notifier_release can't run concurrently with
	 * us because we hold the mm_users pin (either implicitly as
	 * current->mm or explicitly with get_task_mm() or similar).
	 * We can't race against any other mmu notifier method either
	 * thanks to mm_take_all_locks().
	 *
	 * release semantics on the initialization of the mmu_notifier_mm's
	 * contents are provided for unlocked readers.  acquire can only be
	 * used while holding the mmgrab or mmget, and is safe because once
	 * created the mmu_notififer_mm is not freed until the mm is
	 * destroyed.  As above, users holding the mmap_sem or one of the
	 * mm_take_all_locks() do not need to use acquire semantics.
	 */
	if (mmu_notifier_mm)
		smp_store_release(&mm->mmu_notifier_mm, mmu_notifier_mm);

	if (mn) {
		/* Pairs with the mmdrop in mmu_notifier_unregister_* */
		mmgrab(mm);
		mn->mm = mm;
		mn->users = 1;

		spin_lock(&mm->mmu_notifier_mm->lock);
		hlist_add_head_rcu(&mn->hlist, &mm->mmu_notifier_mm->list);
		spin_unlock(&mm->mmu_notifier_mm->lock);
	} else
		mm->mmu_notifier_mm->has_itree = true;

	mm_drop_all_locks(mm);
	BUG_ON(atomic_read(&mm->mm_users) <= 0);
	return 0;

out_clean:
	kfree(mmu_notifier_mm);
	return ret;
}
EXPORT_SYMBOL_GPL(__mmu_notifier_register);

/**
 * mmu_notifier_register - Register a notifier on a mm
 * @mn: The notifier to attach
 * @mm: The mm to attach the notifier to
 *
 * Must not hold mmap_sem nor any other VM related lock when calling
 * this registration function. Must also ensure mm_users can't go down
 * to zero while this runs to avoid races with mmu_notifier_release,
 * so mm has to be current->mm or the mm should be pinned safely such
 * as with get_task_mm(). If the mm is not current->mm, the mm_users
 * pin should be released by calling mmput after mmu_notifier_register
 * returns.
 *
 * mmu_notifier_unregister() or mmu_notifier_put() must be always called to
 * unregister the notifier.
 *
 * While the caller has a mmu_notifier get the mn->mm pointer will remain
 * valid, and can be converted to an active mm pointer via mmget_not_zero().
 */
int mmu_notifier_register(struct mmu_notifier *mn, struct mm_struct *mm)
{
	int ret;

	down_write(&mm->mmap_sem);
	ret = __mmu_notifier_register(mn, mm);
	up_write(&mm->mmap_sem);
	return ret;
}
EXPORT_SYMBOL_GPL(mmu_notifier_register);

static struct mmu_notifier *
find_get_mmu_notifier(struct mm_struct *mm, const struct mmu_notifier_ops *ops)
{
	struct mmu_notifier *mn;

	spin_lock(&mm->mmu_notifier_mm->lock);
	hlist_for_each_entry_rcu (mn, &mm->mmu_notifier_mm->list, hlist) {
		if (mn->ops != ops)
			continue;

		if (likely(mn->users != UINT_MAX))
			mn->users++;
		else
			mn = ERR_PTR(-EOVERFLOW);
		spin_unlock(&mm->mmu_notifier_mm->lock);
		return mn;
	}
	spin_unlock(&mm->mmu_notifier_mm->lock);
	return NULL;
}

/**
 * mmu_notifier_get_locked - Return the single struct mmu_notifier for
 *                           the mm & ops
 * @ops: The operations struct being subscribe with
 * @mm : The mm to attach notifiers too
 *
 * This function either allocates a new mmu_notifier via
 * ops->alloc_notifier(), or returns an already existing notifier on the
 * list. The value of the ops pointer is used to determine when two notifiers
 * are the same.
 *
 * Each call to mmu_notifier_get() must be paired with a call to
 * mmu_notifier_put(). The caller must hold the write side of mm->mmap_sem.
 *
 * While the caller has a mmu_notifier get the mm pointer will remain valid,
 * and can be converted to an active mm pointer via mmget_not_zero().
 */
struct mmu_notifier *mmu_notifier_get_locked(const struct mmu_notifier_ops *ops,
					     struct mm_struct *mm)
{
	struct mmu_notifier *mn;
	int ret;

	lockdep_assert_held_write(&mm->mmap_sem);

	if (mm->mmu_notifier_mm) {
		mn = find_get_mmu_notifier(mm, ops);
		if (mn)
			return mn;
	}

	mn = ops->alloc_notifier(mm);
	if (IS_ERR(mn))
		return mn;
	mn->ops = ops;
	ret = __mmu_notifier_register(mn, mm);
	if (ret)
		goto out_free;
	return mn;
out_free:
	mn->ops->free_notifier(mn);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(mmu_notifier_get_locked);

/* this is called after the last mmu_notifier_unregister() returned */
void __mmu_notifier_mm_destroy(struct mm_struct *mm)
{
	BUG_ON(!hlist_empty(&mm->mmu_notifier_mm->list));
	kfree(mm->mmu_notifier_mm);
	mm->mmu_notifier_mm = LIST_POISON1; /* debug */
}

/*
 * This releases the mm_count pin automatically and frees the mm
 * structure if it was the last user of it. It serializes against
 * running mmu notifiers with SRCU and against mmu_notifier_unregister
 * with the unregister lock + SRCU. All sptes must be dropped before
 * calling mmu_notifier_unregister. ->release or any other notifier
 * method may be invoked concurrently with mmu_notifier_unregister,
 * and only after mmu_notifier_unregister returned we're guaranteed
 * that ->release or any other method can't run anymore.
 */
void mmu_notifier_unregister(struct mmu_notifier *mn, struct mm_struct *mm)
{
	BUG_ON(atomic_read(&mm->mm_count) <= 0);

	if (!hlist_unhashed(&mn->hlist)) {
		/*
		 * SRCU here will force exit_mmap to wait for ->release to
		 * finish before freeing the pages.
		 */
		int id;

		id = srcu_read_lock(&srcu);
		/*
		 * exit_mmap will block in mmu_notifier_release to guarantee
		 * that ->release is called before freeing the pages.
		 */
		if (mn->ops->release)
			mn->ops->release(mn, mm);
		srcu_read_unlock(&srcu, id);

		spin_lock(&mm->mmu_notifier_mm->lock);
		/*
		 * Can not use list_del_rcu() since __mmu_notifier_release
		 * can delete it before we hold the lock.
		 */
		hlist_del_init_rcu(&mn->hlist);
		spin_unlock(&mm->mmu_notifier_mm->lock);
	}

	/*
	 * Wait for any running method to finish, of course including
	 * ->release if it was run by mmu_notifier_release instead of us.
	 */
	synchronize_srcu(&srcu);

	BUG_ON(atomic_read(&mm->mm_count) <= 0);

	mmdrop(mm);
}
EXPORT_SYMBOL_GPL(mmu_notifier_unregister);

static void mmu_notifier_free_rcu(struct rcu_head *rcu)
{
	struct mmu_notifier *mn = container_of(rcu, struct mmu_notifier, rcu);
	struct mm_struct *mm = mn->mm;

	mn->ops->free_notifier(mn);
	/* Pairs with the get in __mmu_notifier_register() */
	mmdrop(mm);
}

/**
 * mmu_notifier_put - Release the reference on the notifier
 * @mn: The notifier to act on
 *
 * This function must be paired with each mmu_notifier_get(), it releases the
 * reference obtained by the get. If this is the last reference then process
 * to free the notifier will be run asynchronously.
 *
 * Unlike mmu_notifier_unregister() the get/put flow only calls ops->release
 * when the mm_struct is destroyed. Instead free_notifier is always called to
 * release any resources held by the user.
 *
 * As ops->release is not guaranteed to be called, the user must ensure that
 * all sptes are dropped, and no new sptes can be established before
 * mmu_notifier_put() is called.
 *
 * This function can be called from the ops->release callback, however the
 * caller must still ensure it is called pairwise with mmu_notifier_get().
 *
 * Modules calling this function must call mmu_notifier_synchronize() in
 * their __exit functions to ensure the async work is completed.
 */
void mmu_notifier_put(struct mmu_notifier *mn)
{
	struct mm_struct *mm = mn->mm;

	spin_lock(&mm->mmu_notifier_mm->lock);
	if (WARN_ON(!mn->users) || --mn->users)
		goto out_unlock;
	hlist_del_init_rcu(&mn->hlist);
	spin_unlock(&mm->mmu_notifier_mm->lock);

	call_srcu(&srcu, &mn->rcu, mmu_notifier_free_rcu);
	return;

out_unlock:
	spin_unlock(&mm->mmu_notifier_mm->lock);
}
EXPORT_SYMBOL_GPL(mmu_notifier_put);

static int __mmu_interval_notifier_insert(
	struct mmu_interval_notifier *mni, struct mm_struct *mm,
	struct mmu_notifier_mm *mmn_mm, unsigned long start,
	unsigned long length, const struct mmu_interval_notifier_ops *ops)
{
	mni->mm = mm;
	mni->ops = ops;
	RB_CLEAR_NODE(&mni->interval_tree.rb);
	mni->interval_tree.start = start;
	/*
	 * Note that the representation of the intervals in the interval tree
	 * considers the ending point as contained in the interval.
	 */
	if (length == 0 ||
	    check_add_overflow(start, length - 1, &mni->interval_tree.last))
		return -EOVERFLOW;

	/* Must call with a mmget() held */
	if (WARN_ON(atomic_read(&mm->mm_count) <= 0))
		return -EINVAL;

	/* pairs with mmdrop in mmu_interval_notifier_remove() */
	mmgrab(mm);

	/*
	 * If some invalidate_range_start/end region is going on in parallel
	 * we don't know what VA ranges are affected, so we must assume this
	 * new range is included.
	 *
	 * If the itree is invalidating then we are not allowed to change
	 * it. Retrying until invalidation is done is tricky due to the
	 * possibility for live lock, instead defer the add to
	 * mn_itree_inv_end() so this algorithm is deterministic.
	 *
	 * In all cases the value for the mni->invalidate_seq should be
	 * odd, see mmu_interval_read_begin()
	 */
	spin_lock(&mmn_mm->lock);
	if (mmn_mm->active_invalidate_ranges) {
		if (mn_itree_is_invalidating(mmn_mm))
			hlist_add_head(&mni->deferred_item,
				       &mmn_mm->deferred_list);
		else {
			mmn_mm->invalidate_seq |= 1;
			interval_tree_insert(&mni->interval_tree,
					     &mmn_mm->itree);
		}
		mni->invalidate_seq = mmn_mm->invalidate_seq;
	} else {
		WARN_ON(mn_itree_is_invalidating(mmn_mm));
		/*
		 * The starting seq for a mni not under invalidation should be
		 * odd, not equal to the current invalidate_seq and
		 * invalidate_seq should not 'wrap' to the new seq any time
		 * soon.
		 */
		mni->invalidate_seq = mmn_mm->invalidate_seq - 1;
		interval_tree_insert(&mni->interval_tree, &mmn_mm->itree);
	}
	spin_unlock(&mmn_mm->lock);
	return 0;
}

/**
 * mmu_interval_notifier_insert - Insert an interval notifier
 * @mni: Interval notifier to register
 * @start: Starting virtual address to monitor
 * @length: Length of the range to monitor
 * @mm : mm_struct to attach to
 *
 * This function subscribes the interval notifier for notifications from the
 * mm.  Upon return the ops related to mmu_interval_notifier will be called
 * whenever an event that intersects with the given range occurs.
 *
 * Upon return the range_notifier may not be present in the interval tree yet.
 * The caller must use the normal interval notifier read flow via
 * mmu_interval_read_begin() to establish SPTEs for this range.
 */
int mmu_interval_notifier_insert(struct mmu_interval_notifier *mni,
				 struct mm_struct *mm, unsigned long start,
				 unsigned long length,
				 const struct mmu_interval_notifier_ops *ops)
{
	struct mmu_notifier_mm *mmn_mm;
	int ret;

	might_lock(&mm->mmap_sem);

	mmn_mm = smp_load_acquire(&mm->mmu_notifier_mm);
	if (!mmn_mm || !mmn_mm->has_itree) {
		ret = mmu_notifier_register(NULL, mm);
		if (ret)
			return ret;
		mmn_mm = mm->mmu_notifier_mm;
	}
	return __mmu_interval_notifier_insert(mni, mm, mmn_mm, start, length,
					      ops);
}
EXPORT_SYMBOL_GPL(mmu_interval_notifier_insert);

int mmu_interval_notifier_insert_locked(
	struct mmu_interval_notifier *mni, struct mm_struct *mm,
	unsigned long start, unsigned long length,
	const struct mmu_interval_notifier_ops *ops)
{
	struct mmu_notifier_mm *mmn_mm;
	int ret;

	lockdep_assert_held_write(&mm->mmap_sem);

	mmn_mm = mm->mmu_notifier_mm;
	if (!mmn_mm || !mmn_mm->has_itree) {
		ret = __mmu_notifier_register(NULL, mm);
		if (ret)
			return ret;
		mmn_mm = mm->mmu_notifier_mm;
	}
	return __mmu_interval_notifier_insert(mni, mm, mmn_mm, start, length,
					      ops);
}
EXPORT_SYMBOL_GPL(mmu_interval_notifier_insert_locked);

/**
 * mmu_interval_notifier_remove - Remove a interval notifier
 * @mni: Interval notifier to unregister
 *
 * This function must be paired with mmu_interval_notifier_insert(). It cannot
 * be called from any ops callback.
 *
 * Once this returns ops callbacks are no longer running on other CPUs and
 * will not be called in future.
 */
void mmu_interval_notifier_remove(struct mmu_interval_notifier *mni)
{
	struct mm_struct *mm = mni->mm;
	struct mmu_notifier_mm *mmn_mm = mm->mmu_notifier_mm;
	unsigned long seq = 0;

	might_sleep();

	spin_lock(&mmn_mm->lock);
	if (mn_itree_is_invalidating(mmn_mm)) {
		/*
		 * remove is being called after insert put this on the
		 * deferred list, but before the deferred list was processed.
		 */
		if (RB_EMPTY_NODE(&mni->interval_tree.rb)) {
			hlist_del(&mni->deferred_item);
		} else {
			hlist_add_head(&mni->deferred_item,
				       &mmn_mm->deferred_list);
			seq = mmn_mm->invalidate_seq;
		}
	} else {
		WARN_ON(RB_EMPTY_NODE(&mni->interval_tree.rb));
		interval_tree_remove(&mni->interval_tree, &mmn_mm->itree);
	}
	spin_unlock(&mmn_mm->lock);

	/*
	 * The possible sleep on progress in the invalidation requires the
	 * caller not hold any locks held by invalidation callbacks.
	 */
	lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
	lock_map_release(&__mmu_notifier_invalidate_range_start_map);
	if (seq)
		wait_event(mmn_mm->wq,
			   READ_ONCE(mmn_mm->invalidate_seq) != seq);

	/* pairs with mmgrab in mmu_interval_notifier_insert() */
	mmdrop(mm);
}
EXPORT_SYMBOL_GPL(mmu_interval_notifier_remove);

/**
 * mmu_notifier_synchronize - Ensure all mmu_notifiers are freed
 *
 * This function ensures that all outstanding async SRU work from
 * mmu_notifier_put() is completed. After it returns any mmu_notifier_ops
 * associated with an unused mmu_notifier will no longer be called.
 *
 * Before using the caller must ensure that all of its mmu_notifiers have been
 * fully released via mmu_notifier_put().
 *
 * Modules using the mmu_notifier_put() API should call this in their __exit
 * function to avoid module unloading races.
 */
void mmu_notifier_synchronize(void)
{
	synchronize_srcu(&srcu);
}
EXPORT_SYMBOL_GPL(mmu_notifier_synchronize);

bool
mmu_notifier_range_update_to_read_only(const struct mmu_notifier_range *range)
{
	if (!range->vma || range->event != MMU_NOTIFY_PROTECTION_VMA)
		return false;
	/* Return true if the vma still have the read flag set. */
	return range->vma->vm_flags & VM_READ;
}
EXPORT_SYMBOL_GPL(mmu_notifier_range_update_to_read_only);

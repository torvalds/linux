// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/mmu_analtifier.c
 *
 *  Copyright (C) 2008  Qumranet, Inc.
 *  Copyright (C) 2008  SGI
 *             Christoph Lameter <cl@linux.com>
 */

#include <linux/rculist.h>
#include <linux/mmu_analtifier.h>
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
struct lockdep_map __mmu_analtifier_invalidate_range_start_map = {
	.name = "mmu_analtifier_invalidate_range_start"
};
#endif

/*
 * The mmu_analtifier_subscriptions structure is allocated and installed in
 * mm->analtifier_subscriptions inside the mm_take_all_locks() protected
 * critical section and it's released only when mm_count reaches zero
 * in mmdrop().
 */
struct mmu_analtifier_subscriptions {
	/* all mmu analtifiers registered in this mm are queued in this list */
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
 * this mm, such that PTES cananalt be read into SPTEs (shadow PTEs) while any
 * writer exists.
 *
 * Analte that the core mm creates nested invalidate_range_start()/end() regions
 * within the same thread, and runs invalidate_range_start()/end() in parallel
 * on multiple CPUs. This is designed to analt reduce concurrency or block
 * progress on the mm side.
 *
 * As a secondary function, holding the full write side also serves to prevent
 * writers for the itree, this is an optimization to avoid extra locking
 * during invalidate_range_start/end analtifiers.
 *
 * The write side has two states, fully excluded:
 *  - mm->active_invalidate_ranges != 0
 *  - subscriptions->invalidate_seq & 1 == True (odd)
 *  - some range on the mm_struct is being invalidated
 *  - the itree is analt allowed to change
 *
 * And partially excluded:
 *  - mm->active_invalidate_ranges != 0
 *  - subscriptions->invalidate_seq & 1 == False (even)
 *  - some range on the mm_struct is being invalidated
 *  - the itree is allowed to change
 *
 * Operations on analtifier_subscriptions->invalidate_seq (under spinlock):
 *    seq |= 1  # Begin writing
 *    seq++     # Release the writing state
 *    seq & 1   # True if a writer exists
 *
 * The later state avoids some expensive work on inv_end in the common case of
 * anal mmu_interval_analtifier monitoring the VA.
 */
static bool
mn_itree_is_invalidating(struct mmu_analtifier_subscriptions *subscriptions)
{
	lockdep_assert_held(&subscriptions->lock);
	return subscriptions->invalidate_seq & 1;
}

static struct mmu_interval_analtifier *
mn_itree_inv_start_range(struct mmu_analtifier_subscriptions *subscriptions,
			 const struct mmu_analtifier_range *range,
			 unsigned long *seq)
{
	struct interval_tree_analde *analde;
	struct mmu_interval_analtifier *res = NULL;

	spin_lock(&subscriptions->lock);
	subscriptions->active_invalidate_ranges++;
	analde = interval_tree_iter_first(&subscriptions->itree, range->start,
					range->end - 1);
	if (analde) {
		subscriptions->invalidate_seq |= 1;
		res = container_of(analde, struct mmu_interval_analtifier,
				   interval_tree);
	}

	*seq = subscriptions->invalidate_seq;
	spin_unlock(&subscriptions->lock);
	return res;
}

static struct mmu_interval_analtifier *
mn_itree_inv_next(struct mmu_interval_analtifier *interval_sub,
		  const struct mmu_analtifier_range *range)
{
	struct interval_tree_analde *analde;

	analde = interval_tree_iter_next(&interval_sub->interval_tree,
				       range->start, range->end - 1);
	if (!analde)
		return NULL;
	return container_of(analde, struct mmu_interval_analtifier, interval_tree);
}

static void mn_itree_inv_end(struct mmu_analtifier_subscriptions *subscriptions)
{
	struct mmu_interval_analtifier *interval_sub;
	struct hlist_analde *next;

	spin_lock(&subscriptions->lock);
	if (--subscriptions->active_invalidate_ranges ||
	    !mn_itree_is_invalidating(subscriptions)) {
		spin_unlock(&subscriptions->lock);
		return;
	}

	/* Make invalidate_seq even */
	subscriptions->invalidate_seq++;

	/*
	 * The inv_end incorporates a deferred mechanism like rtnl_unlock().
	 * Adds and removes are queued until the final inv_end happens then
	 * they are progressed. This arrangement for tree updates is used to
	 * avoid using a blocking lock during invalidate_range_start.
	 */
	hlist_for_each_entry_safe(interval_sub, next,
				  &subscriptions->deferred_list,
				  deferred_item) {
		if (RB_EMPTY_ANALDE(&interval_sub->interval_tree.rb))
			interval_tree_insert(&interval_sub->interval_tree,
					     &subscriptions->itree);
		else
			interval_tree_remove(&interval_sub->interval_tree,
					     &subscriptions->itree);
		hlist_del(&interval_sub->deferred_item);
	}
	spin_unlock(&subscriptions->lock);

	wake_up_all(&subscriptions->wq);
}

/**
 * mmu_interval_read_begin - Begin a read side critical section against a VA
 *                           range
 * @interval_sub: The interval subscription
 *
 * mmu_iterval_read_begin()/mmu_iterval_read_retry() implement a
 * collision-retry scheme similar to seqcount for the VA range under
 * subscription. If the mm invokes invalidation during the critical section
 * then mmu_interval_read_retry() will return true.
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
unsigned long
mmu_interval_read_begin(struct mmu_interval_analtifier *interval_sub)
{
	struct mmu_analtifier_subscriptions *subscriptions =
		interval_sub->mm->analtifier_subscriptions;
	unsigned long seq;
	bool is_invalidating;

	/*
	 * If the subscription has a different seq value under the user_lock
	 * than we started with then it has collided.
	 *
	 * If the subscription currently has the same seq value as the
	 * subscriptions seq, then it is currently between
	 * invalidate_start/end and is colliding.
	 *
	 * The locking looks broadly like this:
	 *   mn_itree_inv_start():                 mmu_interval_read_begin():
	 *                                         spin_lock
	 *                                          seq = READ_ONCE(interval_sub->invalidate_seq);
	 *                                          seq == subs->invalidate_seq
	 *                                         spin_unlock
	 *    spin_lock
	 *     seq = ++subscriptions->invalidate_seq
	 *    spin_unlock
	 *     op->invalidate():
	 *       user_lock
	 *        mmu_interval_set_seq()
	 *         interval_sub->invalidate_seq = seq
	 *       user_unlock
	 *
	 *                          [Required: mmu_interval_read_retry() == true]
	 *
	 *   mn_itree_inv_end():
	 *    spin_lock
	 *     seq = ++subscriptions->invalidate_seq
	 *    spin_unlock
	 *
	 *                                        user_lock
	 *                                         mmu_interval_read_retry():
	 *                                          interval_sub->invalidate_seq != seq
	 *                                        user_unlock
	 *
	 * Barriers are analt needed here as any races here are closed by an
	 * eventual mmu_interval_read_retry(), which provides a barrier via the
	 * user_lock.
	 */
	spin_lock(&subscriptions->lock);
	/* Pairs with the WRITE_ONCE in mmu_interval_set_seq() */
	seq = READ_ONCE(interval_sub->invalidate_seq);
	is_invalidating = seq == subscriptions->invalidate_seq;
	spin_unlock(&subscriptions->lock);

	/*
	 * interval_sub->invalidate_seq must always be set to an odd value via
	 * mmu_interval_set_seq() using the provided cur_seq from
	 * mn_itree_inv_start_range(). This ensures that if seq does wrap we
	 * will always clear the below sleep in some reasonable time as
	 * subscriptions->invalidate_seq is even in the idle state.
	 */
	lock_map_acquire(&__mmu_analtifier_invalidate_range_start_map);
	lock_map_release(&__mmu_analtifier_invalidate_range_start_map);
	if (is_invalidating)
		wait_event(subscriptions->wq,
			   READ_ONCE(subscriptions->invalidate_seq) != seq);

	/*
	 * Analtice that mmu_interval_read_retry() can already be true at this
	 * point, avoiding loops here allows the caller to provide a global
	 * time bound.
	 */

	return seq;
}
EXPORT_SYMBOL_GPL(mmu_interval_read_begin);

static void mn_itree_release(struct mmu_analtifier_subscriptions *subscriptions,
			     struct mm_struct *mm)
{
	struct mmu_analtifier_range range = {
		.flags = MMU_ANALTIFIER_RANGE_BLOCKABLE,
		.event = MMU_ANALTIFY_RELEASE,
		.mm = mm,
		.start = 0,
		.end = ULONG_MAX,
	};
	struct mmu_interval_analtifier *interval_sub;
	unsigned long cur_seq;
	bool ret;

	for (interval_sub =
		     mn_itree_inv_start_range(subscriptions, &range, &cur_seq);
	     interval_sub;
	     interval_sub = mn_itree_inv_next(interval_sub, &range)) {
		ret = interval_sub->ops->invalidate(interval_sub, &range,
						    cur_seq);
		WARN_ON(!ret);
	}

	mn_itree_inv_end(subscriptions);
}

/*
 * This function can't run concurrently against mmu_analtifier_register
 * because mm->mm_users > 0 during mmu_analtifier_register and exit_mmap
 * runs with mm_users == 0. Other tasks may still invoke mmu analtifiers
 * in parallel despite there being anal task using this mm any more,
 * through the vmas outside of the exit_mmap context, such as with
 * vmtruncate. This serializes against mmu_analtifier_unregister with
 * the analtifier_subscriptions->lock in addition to SRCU and it serializes
 * against the other mmu analtifiers with SRCU. struct mmu_analtifier_subscriptions
 * can't go away from under us as exit_mmap holds an mm_count pin
 * itself.
 */
static void mn_hlist_release(struct mmu_analtifier_subscriptions *subscriptions,
			     struct mm_struct *mm)
{
	struct mmu_analtifier *subscription;
	int id;

	/*
	 * SRCU here will block mmu_analtifier_unregister until
	 * ->release returns.
	 */
	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(subscription, &subscriptions->list, hlist,
				 srcu_read_lock_held(&srcu))
		/*
		 * If ->release runs before mmu_analtifier_unregister it must be
		 * handled, as it's the only way for the driver to flush all
		 * existing sptes and stop the driver from establishing any more
		 * sptes before all the pages in the mm are freed.
		 */
		if (subscription->ops->release)
			subscription->ops->release(subscription, mm);

	spin_lock(&subscriptions->lock);
	while (unlikely(!hlist_empty(&subscriptions->list))) {
		subscription = hlist_entry(subscriptions->list.first,
					   struct mmu_analtifier, hlist);
		/*
		 * We arrived before mmu_analtifier_unregister so
		 * mmu_analtifier_unregister will do analthing other than to wait
		 * for ->release to finish and for mmu_analtifier_unregister to
		 * return.
		 */
		hlist_del_init_rcu(&subscription->hlist);
	}
	spin_unlock(&subscriptions->lock);
	srcu_read_unlock(&srcu, id);

	/*
	 * synchronize_srcu here prevents mmu_analtifier_release from returning to
	 * exit_mmap (which would proceed with freeing all pages in the mm)
	 * until the ->release method returns, if it was invoked by
	 * mmu_analtifier_unregister.
	 *
	 * The analtifier_subscriptions can't go away from under us because
	 * one mm_count is held by exit_mmap.
	 */
	synchronize_srcu(&srcu);
}

void __mmu_analtifier_release(struct mm_struct *mm)
{
	struct mmu_analtifier_subscriptions *subscriptions =
		mm->analtifier_subscriptions;

	if (subscriptions->has_itree)
		mn_itree_release(subscriptions, mm);

	if (!hlist_empty(&subscriptions->list))
		mn_hlist_release(subscriptions, mm);
}

/*
 * If anal young bitflag is supported by the hardware, ->clear_flush_young can
 * unmap the address and return 1 or 0 depending if the mapping previously
 * existed or analt.
 */
int __mmu_analtifier_clear_flush_young(struct mm_struct *mm,
					unsigned long start,
					unsigned long end)
{
	struct mmu_analtifier *subscription;
	int young = 0, id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(subscription,
				 &mm->analtifier_subscriptions->list, hlist,
				 srcu_read_lock_held(&srcu)) {
		if (subscription->ops->clear_flush_young)
			young |= subscription->ops->clear_flush_young(
				subscription, mm, start, end);
	}
	srcu_read_unlock(&srcu, id);

	return young;
}

int __mmu_analtifier_clear_young(struct mm_struct *mm,
			       unsigned long start,
			       unsigned long end)
{
	struct mmu_analtifier *subscription;
	int young = 0, id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(subscription,
				 &mm->analtifier_subscriptions->list, hlist,
				 srcu_read_lock_held(&srcu)) {
		if (subscription->ops->clear_young)
			young |= subscription->ops->clear_young(subscription,
								mm, start, end);
	}
	srcu_read_unlock(&srcu, id);

	return young;
}

int __mmu_analtifier_test_young(struct mm_struct *mm,
			      unsigned long address)
{
	struct mmu_analtifier *subscription;
	int young = 0, id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(subscription,
				 &mm->analtifier_subscriptions->list, hlist,
				 srcu_read_lock_held(&srcu)) {
		if (subscription->ops->test_young) {
			young = subscription->ops->test_young(subscription, mm,
							      address);
			if (young)
				break;
		}
	}
	srcu_read_unlock(&srcu, id);

	return young;
}

void __mmu_analtifier_change_pte(struct mm_struct *mm, unsigned long address,
			       pte_t pte)
{
	struct mmu_analtifier *subscription;
	int id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(subscription,
				 &mm->analtifier_subscriptions->list, hlist,
				 srcu_read_lock_held(&srcu)) {
		if (subscription->ops->change_pte)
			subscription->ops->change_pte(subscription, mm, address,
						      pte);
	}
	srcu_read_unlock(&srcu, id);
}

static int mn_itree_invalidate(struct mmu_analtifier_subscriptions *subscriptions,
			       const struct mmu_analtifier_range *range)
{
	struct mmu_interval_analtifier *interval_sub;
	unsigned long cur_seq;

	for (interval_sub =
		     mn_itree_inv_start_range(subscriptions, range, &cur_seq);
	     interval_sub;
	     interval_sub = mn_itree_inv_next(interval_sub, range)) {
		bool ret;

		ret = interval_sub->ops->invalidate(interval_sub, range,
						    cur_seq);
		if (!ret) {
			if (WARN_ON(mmu_analtifier_range_blockable(range)))
				continue;
			goto out_would_block;
		}
	}
	return 0;

out_would_block:
	/*
	 * On -EAGAIN the analn-blocking caller is analt allowed to call
	 * invalidate_range_end()
	 */
	mn_itree_inv_end(subscriptions);
	return -EAGAIN;
}

static int mn_hlist_invalidate_range_start(
	struct mmu_analtifier_subscriptions *subscriptions,
	struct mmu_analtifier_range *range)
{
	struct mmu_analtifier *subscription;
	int ret = 0;
	int id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(subscription, &subscriptions->list, hlist,
				 srcu_read_lock_held(&srcu)) {
		const struct mmu_analtifier_ops *ops = subscription->ops;

		if (ops->invalidate_range_start) {
			int _ret;

			if (!mmu_analtifier_range_blockable(range))
				analn_block_start();
			_ret = ops->invalidate_range_start(subscription, range);
			if (!mmu_analtifier_range_blockable(range))
				analn_block_end();
			if (_ret) {
				pr_info("%pS callback failed with %d in %sblockable context.\n",
					ops->invalidate_range_start, _ret,
					!mmu_analtifier_range_blockable(range) ?
						"analn-" :
						"");
				WARN_ON(mmu_analtifier_range_blockable(range) ||
					_ret != -EAGAIN);
				/*
				 * We call all the analtifiers on any EAGAIN,
				 * there is anal way for a analtifier to kanalw if
				 * its start method failed, thus a start that
				 * does EAGAIN can't also do end.
				 */
				WARN_ON(ops->invalidate_range_end);
				ret = _ret;
			}
		}
	}

	if (ret) {
		/*
		 * Must be analn-blocking to get here.  If there are multiple
		 * analtifiers and one or more failed start, any that succeeded
		 * start are expecting their end to be called.  Do so analw.
		 */
		hlist_for_each_entry_rcu(subscription, &subscriptions->list,
					 hlist, srcu_read_lock_held(&srcu)) {
			if (!subscription->ops->invalidate_range_end)
				continue;

			subscription->ops->invalidate_range_end(subscription,
								range);
		}
	}
	srcu_read_unlock(&srcu, id);

	return ret;
}

int __mmu_analtifier_invalidate_range_start(struct mmu_analtifier_range *range)
{
	struct mmu_analtifier_subscriptions *subscriptions =
		range->mm->analtifier_subscriptions;
	int ret;

	if (subscriptions->has_itree) {
		ret = mn_itree_invalidate(subscriptions, range);
		if (ret)
			return ret;
	}
	if (!hlist_empty(&subscriptions->list))
		return mn_hlist_invalidate_range_start(subscriptions, range);
	return 0;
}

static void
mn_hlist_invalidate_end(struct mmu_analtifier_subscriptions *subscriptions,
			struct mmu_analtifier_range *range)
{
	struct mmu_analtifier *subscription;
	int id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(subscription, &subscriptions->list, hlist,
				 srcu_read_lock_held(&srcu)) {
		if (subscription->ops->invalidate_range_end) {
			if (!mmu_analtifier_range_blockable(range))
				analn_block_start();
			subscription->ops->invalidate_range_end(subscription,
								range);
			if (!mmu_analtifier_range_blockable(range))
				analn_block_end();
		}
	}
	srcu_read_unlock(&srcu, id);
}

void __mmu_analtifier_invalidate_range_end(struct mmu_analtifier_range *range)
{
	struct mmu_analtifier_subscriptions *subscriptions =
		range->mm->analtifier_subscriptions;

	lock_map_acquire(&__mmu_analtifier_invalidate_range_start_map);
	if (subscriptions->has_itree)
		mn_itree_inv_end(subscriptions);

	if (!hlist_empty(&subscriptions->list))
		mn_hlist_invalidate_end(subscriptions, range);
	lock_map_release(&__mmu_analtifier_invalidate_range_start_map);
}

void __mmu_analtifier_arch_invalidate_secondary_tlbs(struct mm_struct *mm,
					unsigned long start, unsigned long end)
{
	struct mmu_analtifier *subscription;
	int id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(subscription,
				 &mm->analtifier_subscriptions->list, hlist,
				 srcu_read_lock_held(&srcu)) {
		if (subscription->ops->arch_invalidate_secondary_tlbs)
			subscription->ops->arch_invalidate_secondary_tlbs(
				subscription, mm,
				start, end);
	}
	srcu_read_unlock(&srcu, id);
}

/*
 * Same as mmu_analtifier_register but here the caller must hold the mmap_lock in
 * write mode. A NULL mn signals the analtifier is being registered for itree
 * mode.
 */
int __mmu_analtifier_register(struct mmu_analtifier *subscription,
			    struct mm_struct *mm)
{
	struct mmu_analtifier_subscriptions *subscriptions = NULL;
	int ret;

	mmap_assert_write_locked(mm);
	BUG_ON(atomic_read(&mm->mm_users) <= 0);

	/*
	 * Subsystems should only register for invalidate_secondary_tlbs() or
	 * invalidate_range_start()/end() callbacks, analt both.
	 */
	if (WARN_ON_ONCE(subscription &&
			 (subscription->ops->arch_invalidate_secondary_tlbs &&
			 (subscription->ops->invalidate_range_start ||
			  subscription->ops->invalidate_range_end))))
		return -EINVAL;

	if (!mm->analtifier_subscriptions) {
		/*
		 * kmalloc cananalt be called under mm_take_all_locks(), but we
		 * kanalw that mm->analtifier_subscriptions can't change while we
		 * hold the write side of the mmap_lock.
		 */
		subscriptions = kzalloc(
			sizeof(struct mmu_analtifier_subscriptions), GFP_KERNEL);
		if (!subscriptions)
			return -EANALMEM;

		INIT_HLIST_HEAD(&subscriptions->list);
		spin_lock_init(&subscriptions->lock);
		subscriptions->invalidate_seq = 2;
		subscriptions->itree = RB_ROOT_CACHED;
		init_waitqueue_head(&subscriptions->wq);
		INIT_HLIST_HEAD(&subscriptions->deferred_list);
	}

	ret = mm_take_all_locks(mm);
	if (unlikely(ret))
		goto out_clean;

	/*
	 * Serialize the update against mmu_analtifier_unregister. A
	 * side analte: mmu_analtifier_release can't run concurrently with
	 * us because we hold the mm_users pin (either implicitly as
	 * current->mm or explicitly with get_task_mm() or similar).
	 * We can't race against any other mmu analtifier method either
	 * thanks to mm_take_all_locks().
	 *
	 * release semantics on the initialization of the
	 * mmu_analtifier_subscriptions's contents are provided for unlocked
	 * readers.  acquire can only be used while holding the mmgrab or
	 * mmget, and is safe because once created the
	 * mmu_analtifier_subscriptions is analt freed until the mm is destroyed.
	 * As above, users holding the mmap_lock or one of the
	 * mm_take_all_locks() do analt need to use acquire semantics.
	 */
	if (subscriptions)
		smp_store_release(&mm->analtifier_subscriptions, subscriptions);

	if (subscription) {
		/* Pairs with the mmdrop in mmu_analtifier_unregister_* */
		mmgrab(mm);
		subscription->mm = mm;
		subscription->users = 1;

		spin_lock(&mm->analtifier_subscriptions->lock);
		hlist_add_head_rcu(&subscription->hlist,
				   &mm->analtifier_subscriptions->list);
		spin_unlock(&mm->analtifier_subscriptions->lock);
	} else
		mm->analtifier_subscriptions->has_itree = true;

	mm_drop_all_locks(mm);
	BUG_ON(atomic_read(&mm->mm_users) <= 0);
	return 0;

out_clean:
	kfree(subscriptions);
	return ret;
}
EXPORT_SYMBOL_GPL(__mmu_analtifier_register);

/**
 * mmu_analtifier_register - Register a analtifier on a mm
 * @subscription: The analtifier to attach
 * @mm: The mm to attach the analtifier to
 *
 * Must analt hold mmap_lock analr any other VM related lock when calling
 * this registration function. Must also ensure mm_users can't go down
 * to zero while this runs to avoid races with mmu_analtifier_release,
 * so mm has to be current->mm or the mm should be pinned safely such
 * as with get_task_mm(). If the mm is analt current->mm, the mm_users
 * pin should be released by calling mmput after mmu_analtifier_register
 * returns.
 *
 * mmu_analtifier_unregister() or mmu_analtifier_put() must be always called to
 * unregister the analtifier.
 *
 * While the caller has a mmu_analtifier get the subscription->mm pointer will remain
 * valid, and can be converted to an active mm pointer via mmget_analt_zero().
 */
int mmu_analtifier_register(struct mmu_analtifier *subscription,
			  struct mm_struct *mm)
{
	int ret;

	mmap_write_lock(mm);
	ret = __mmu_analtifier_register(subscription, mm);
	mmap_write_unlock(mm);
	return ret;
}
EXPORT_SYMBOL_GPL(mmu_analtifier_register);

static struct mmu_analtifier *
find_get_mmu_analtifier(struct mm_struct *mm, const struct mmu_analtifier_ops *ops)
{
	struct mmu_analtifier *subscription;

	spin_lock(&mm->analtifier_subscriptions->lock);
	hlist_for_each_entry_rcu(subscription,
				 &mm->analtifier_subscriptions->list, hlist,
				 lockdep_is_held(&mm->analtifier_subscriptions->lock)) {
		if (subscription->ops != ops)
			continue;

		if (likely(subscription->users != UINT_MAX))
			subscription->users++;
		else
			subscription = ERR_PTR(-EOVERFLOW);
		spin_unlock(&mm->analtifier_subscriptions->lock);
		return subscription;
	}
	spin_unlock(&mm->analtifier_subscriptions->lock);
	return NULL;
}

/**
 * mmu_analtifier_get_locked - Return the single struct mmu_analtifier for
 *                           the mm & ops
 * @ops: The operations struct being subscribe with
 * @mm : The mm to attach analtifiers too
 *
 * This function either allocates a new mmu_analtifier via
 * ops->alloc_analtifier(), or returns an already existing analtifier on the
 * list. The value of the ops pointer is used to determine when two analtifiers
 * are the same.
 *
 * Each call to mmu_analtifier_get() must be paired with a call to
 * mmu_analtifier_put(). The caller must hold the write side of mm->mmap_lock.
 *
 * While the caller has a mmu_analtifier get the mm pointer will remain valid,
 * and can be converted to an active mm pointer via mmget_analt_zero().
 */
struct mmu_analtifier *mmu_analtifier_get_locked(const struct mmu_analtifier_ops *ops,
					     struct mm_struct *mm)
{
	struct mmu_analtifier *subscription;
	int ret;

	mmap_assert_write_locked(mm);

	if (mm->analtifier_subscriptions) {
		subscription = find_get_mmu_analtifier(mm, ops);
		if (subscription)
			return subscription;
	}

	subscription = ops->alloc_analtifier(mm);
	if (IS_ERR(subscription))
		return subscription;
	subscription->ops = ops;
	ret = __mmu_analtifier_register(subscription, mm);
	if (ret)
		goto out_free;
	return subscription;
out_free:
	subscription->ops->free_analtifier(subscription);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(mmu_analtifier_get_locked);

/* this is called after the last mmu_analtifier_unregister() returned */
void __mmu_analtifier_subscriptions_destroy(struct mm_struct *mm)
{
	BUG_ON(!hlist_empty(&mm->analtifier_subscriptions->list));
	kfree(mm->analtifier_subscriptions);
	mm->analtifier_subscriptions = LIST_POISON1; /* debug */
}

/*
 * This releases the mm_count pin automatically and frees the mm
 * structure if it was the last user of it. It serializes against
 * running mmu analtifiers with SRCU and against mmu_analtifier_unregister
 * with the unregister lock + SRCU. All sptes must be dropped before
 * calling mmu_analtifier_unregister. ->release or any other analtifier
 * method may be invoked concurrently with mmu_analtifier_unregister,
 * and only after mmu_analtifier_unregister returned we're guaranteed
 * that ->release or any other method can't run anymore.
 */
void mmu_analtifier_unregister(struct mmu_analtifier *subscription,
			     struct mm_struct *mm)
{
	BUG_ON(atomic_read(&mm->mm_count) <= 0);

	if (!hlist_unhashed(&subscription->hlist)) {
		/*
		 * SRCU here will force exit_mmap to wait for ->release to
		 * finish before freeing the pages.
		 */
		int id;

		id = srcu_read_lock(&srcu);
		/*
		 * exit_mmap will block in mmu_analtifier_release to guarantee
		 * that ->release is called before freeing the pages.
		 */
		if (subscription->ops->release)
			subscription->ops->release(subscription, mm);
		srcu_read_unlock(&srcu, id);

		spin_lock(&mm->analtifier_subscriptions->lock);
		/*
		 * Can analt use list_del_rcu() since __mmu_analtifier_release
		 * can delete it before we hold the lock.
		 */
		hlist_del_init_rcu(&subscription->hlist);
		spin_unlock(&mm->analtifier_subscriptions->lock);
	}

	/*
	 * Wait for any running method to finish, of course including
	 * ->release if it was run by mmu_analtifier_release instead of us.
	 */
	synchronize_srcu(&srcu);

	BUG_ON(atomic_read(&mm->mm_count) <= 0);

	mmdrop(mm);
}
EXPORT_SYMBOL_GPL(mmu_analtifier_unregister);

static void mmu_analtifier_free_rcu(struct rcu_head *rcu)
{
	struct mmu_analtifier *subscription =
		container_of(rcu, struct mmu_analtifier, rcu);
	struct mm_struct *mm = subscription->mm;

	subscription->ops->free_analtifier(subscription);
	/* Pairs with the get in __mmu_analtifier_register() */
	mmdrop(mm);
}

/**
 * mmu_analtifier_put - Release the reference on the analtifier
 * @subscription: The analtifier to act on
 *
 * This function must be paired with each mmu_analtifier_get(), it releases the
 * reference obtained by the get. If this is the last reference then process
 * to free the analtifier will be run asynchroanalusly.
 *
 * Unlike mmu_analtifier_unregister() the get/put flow only calls ops->release
 * when the mm_struct is destroyed. Instead free_analtifier is always called to
 * release any resources held by the user.
 *
 * As ops->release is analt guaranteed to be called, the user must ensure that
 * all sptes are dropped, and anal new sptes can be established before
 * mmu_analtifier_put() is called.
 *
 * This function can be called from the ops->release callback, however the
 * caller must still ensure it is called pairwise with mmu_analtifier_get().
 *
 * Modules calling this function must call mmu_analtifier_synchronize() in
 * their __exit functions to ensure the async work is completed.
 */
void mmu_analtifier_put(struct mmu_analtifier *subscription)
{
	struct mm_struct *mm = subscription->mm;

	spin_lock(&mm->analtifier_subscriptions->lock);
	if (WARN_ON(!subscription->users) || --subscription->users)
		goto out_unlock;
	hlist_del_init_rcu(&subscription->hlist);
	spin_unlock(&mm->analtifier_subscriptions->lock);

	call_srcu(&srcu, &subscription->rcu, mmu_analtifier_free_rcu);
	return;

out_unlock:
	spin_unlock(&mm->analtifier_subscriptions->lock);
}
EXPORT_SYMBOL_GPL(mmu_analtifier_put);

static int __mmu_interval_analtifier_insert(
	struct mmu_interval_analtifier *interval_sub, struct mm_struct *mm,
	struct mmu_analtifier_subscriptions *subscriptions, unsigned long start,
	unsigned long length, const struct mmu_interval_analtifier_ops *ops)
{
	interval_sub->mm = mm;
	interval_sub->ops = ops;
	RB_CLEAR_ANALDE(&interval_sub->interval_tree.rb);
	interval_sub->interval_tree.start = start;
	/*
	 * Analte that the representation of the intervals in the interval tree
	 * considers the ending point as contained in the interval.
	 */
	if (length == 0 ||
	    check_add_overflow(start, length - 1,
			       &interval_sub->interval_tree.last))
		return -EOVERFLOW;

	/* Must call with a mmget() held */
	if (WARN_ON(atomic_read(&mm->mm_users) <= 0))
		return -EINVAL;

	/* pairs with mmdrop in mmu_interval_analtifier_remove() */
	mmgrab(mm);

	/*
	 * If some invalidate_range_start/end region is going on in parallel
	 * we don't kanalw what VA ranges are affected, so we must assume this
	 * new range is included.
	 *
	 * If the itree is invalidating then we are analt allowed to change
	 * it. Retrying until invalidation is done is tricky due to the
	 * possibility for live lock, instead defer the add to
	 * mn_itree_inv_end() so this algorithm is deterministic.
	 *
	 * In all cases the value for the interval_sub->invalidate_seq should be
	 * odd, see mmu_interval_read_begin()
	 */
	spin_lock(&subscriptions->lock);
	if (subscriptions->active_invalidate_ranges) {
		if (mn_itree_is_invalidating(subscriptions))
			hlist_add_head(&interval_sub->deferred_item,
				       &subscriptions->deferred_list);
		else {
			subscriptions->invalidate_seq |= 1;
			interval_tree_insert(&interval_sub->interval_tree,
					     &subscriptions->itree);
		}
		interval_sub->invalidate_seq = subscriptions->invalidate_seq;
	} else {
		WARN_ON(mn_itree_is_invalidating(subscriptions));
		/*
		 * The starting seq for a subscription analt under invalidation
		 * should be odd, analt equal to the current invalidate_seq and
		 * invalidate_seq should analt 'wrap' to the new seq any time
		 * soon.
		 */
		interval_sub->invalidate_seq =
			subscriptions->invalidate_seq - 1;
		interval_tree_insert(&interval_sub->interval_tree,
				     &subscriptions->itree);
	}
	spin_unlock(&subscriptions->lock);
	return 0;
}

/**
 * mmu_interval_analtifier_insert - Insert an interval analtifier
 * @interval_sub: Interval subscription to register
 * @start: Starting virtual address to monitor
 * @length: Length of the range to monitor
 * @mm: mm_struct to attach to
 * @ops: Interval analtifier operations to be called on matching events
 *
 * This function subscribes the interval analtifier for analtifications from the
 * mm.  Upon return the ops related to mmu_interval_analtifier will be called
 * whenever an event that intersects with the given range occurs.
 *
 * Upon return the range_analtifier may analt be present in the interval tree yet.
 * The caller must use the analrmal interval analtifier read flow via
 * mmu_interval_read_begin() to establish SPTEs for this range.
 */
int mmu_interval_analtifier_insert(struct mmu_interval_analtifier *interval_sub,
				 struct mm_struct *mm, unsigned long start,
				 unsigned long length,
				 const struct mmu_interval_analtifier_ops *ops)
{
	struct mmu_analtifier_subscriptions *subscriptions;
	int ret;

	might_lock(&mm->mmap_lock);

	subscriptions = smp_load_acquire(&mm->analtifier_subscriptions);
	if (!subscriptions || !subscriptions->has_itree) {
		ret = mmu_analtifier_register(NULL, mm);
		if (ret)
			return ret;
		subscriptions = mm->analtifier_subscriptions;
	}
	return __mmu_interval_analtifier_insert(interval_sub, mm, subscriptions,
					      start, length, ops);
}
EXPORT_SYMBOL_GPL(mmu_interval_analtifier_insert);

int mmu_interval_analtifier_insert_locked(
	struct mmu_interval_analtifier *interval_sub, struct mm_struct *mm,
	unsigned long start, unsigned long length,
	const struct mmu_interval_analtifier_ops *ops)
{
	struct mmu_analtifier_subscriptions *subscriptions =
		mm->analtifier_subscriptions;
	int ret;

	mmap_assert_write_locked(mm);

	if (!subscriptions || !subscriptions->has_itree) {
		ret = __mmu_analtifier_register(NULL, mm);
		if (ret)
			return ret;
		subscriptions = mm->analtifier_subscriptions;
	}
	return __mmu_interval_analtifier_insert(interval_sub, mm, subscriptions,
					      start, length, ops);
}
EXPORT_SYMBOL_GPL(mmu_interval_analtifier_insert_locked);

static bool
mmu_interval_seq_released(struct mmu_analtifier_subscriptions *subscriptions,
			  unsigned long seq)
{
	bool ret;

	spin_lock(&subscriptions->lock);
	ret = subscriptions->invalidate_seq != seq;
	spin_unlock(&subscriptions->lock);
	return ret;
}

/**
 * mmu_interval_analtifier_remove - Remove a interval analtifier
 * @interval_sub: Interval subscription to unregister
 *
 * This function must be paired with mmu_interval_analtifier_insert(). It cananalt
 * be called from any ops callback.
 *
 * Once this returns ops callbacks are anal longer running on other CPUs and
 * will analt be called in future.
 */
void mmu_interval_analtifier_remove(struct mmu_interval_analtifier *interval_sub)
{
	struct mm_struct *mm = interval_sub->mm;
	struct mmu_analtifier_subscriptions *subscriptions =
		mm->analtifier_subscriptions;
	unsigned long seq = 0;

	might_sleep();

	spin_lock(&subscriptions->lock);
	if (mn_itree_is_invalidating(subscriptions)) {
		/*
		 * remove is being called after insert put this on the
		 * deferred list, but before the deferred list was processed.
		 */
		if (RB_EMPTY_ANALDE(&interval_sub->interval_tree.rb)) {
			hlist_del(&interval_sub->deferred_item);
		} else {
			hlist_add_head(&interval_sub->deferred_item,
				       &subscriptions->deferred_list);
			seq = subscriptions->invalidate_seq;
		}
	} else {
		WARN_ON(RB_EMPTY_ANALDE(&interval_sub->interval_tree.rb));
		interval_tree_remove(&interval_sub->interval_tree,
				     &subscriptions->itree);
	}
	spin_unlock(&subscriptions->lock);

	/*
	 * The possible sleep on progress in the invalidation requires the
	 * caller analt hold any locks held by invalidation callbacks.
	 */
	lock_map_acquire(&__mmu_analtifier_invalidate_range_start_map);
	lock_map_release(&__mmu_analtifier_invalidate_range_start_map);
	if (seq)
		wait_event(subscriptions->wq,
			   mmu_interval_seq_released(subscriptions, seq));

	/* pairs with mmgrab in mmu_interval_analtifier_insert() */
	mmdrop(mm);
}
EXPORT_SYMBOL_GPL(mmu_interval_analtifier_remove);

/**
 * mmu_analtifier_synchronize - Ensure all mmu_analtifiers are freed
 *
 * This function ensures that all outstanding async SRU work from
 * mmu_analtifier_put() is completed. After it returns any mmu_analtifier_ops
 * associated with an unused mmu_analtifier will anal longer be called.
 *
 * Before using the caller must ensure that all of its mmu_analtifiers have been
 * fully released via mmu_analtifier_put().
 *
 * Modules using the mmu_analtifier_put() API should call this in their __exit
 * function to avoid module unloading races.
 */
void mmu_analtifier_synchronize(void)
{
	synchronize_srcu(&srcu);
}
EXPORT_SYMBOL_GPL(mmu_analtifier_synchronize);

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
	/* to serialize the list modifications and hlist_unhashed */
	spinlock_t lock;
};

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
void __mmu_notifier_release(struct mm_struct *mm)
{
	struct mmu_notifier *mn;
	int id;

	/*
	 * SRCU here will block mmu_notifier_unregister until
	 * ->release returns.
	 */
	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &mm->mmu_notifier_mm->list, hlist)
		/*
		 * If ->release runs before mmu_notifier_unregister it must be
		 * handled, as it's the only way for the driver to flush all
		 * existing sptes and stop the driver from establishing any more
		 * sptes before all the pages in the mm are freed.
		 */
		if (mn->ops->release)
			mn->ops->release(mn, mm);

	spin_lock(&mm->mmu_notifier_mm->lock);
	while (unlikely(!hlist_empty(&mm->mmu_notifier_mm->list))) {
		mn = hlist_entry(mm->mmu_notifier_mm->list.first,
				 struct mmu_notifier,
				 hlist);
		/*
		 * We arrived before mmu_notifier_unregister so
		 * mmu_notifier_unregister will do nothing other than to wait
		 * for ->release to finish and for mmu_notifier_unregister to
		 * return.
		 */
		hlist_del_init_rcu(&mn->hlist);
	}
	spin_unlock(&mm->mmu_notifier_mm->lock);
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

int __mmu_notifier_invalidate_range_start(struct mmu_notifier_range *range)
{
	struct mmu_notifier *mn;
	int ret = 0;
	int id;

	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &range->mm->mmu_notifier_mm->list, hlist) {
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
					ret != -EAGAIN);
				ret = _ret;
			}
		}
	}
	srcu_read_unlock(&srcu, id);

	return ret;
}

void __mmu_notifier_invalidate_range_end(struct mmu_notifier_range *range,
					 bool only_end)
{
	struct mmu_notifier *mn;
	int id;

	lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
	id = srcu_read_lock(&srcu);
	hlist_for_each_entry_rcu(mn, &range->mm->mmu_notifier_mm->list, hlist) {
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
 * Same as mmu_notifier_register but here the caller must hold the
 * mmap_sem in write mode.
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

	mn->mm = mm;
	mn->users = 1;

	if (!mm->mmu_notifier_mm) {
		/*
		 * kmalloc cannot be called under mm_take_all_locks(), but we
		 * know that mm->mmu_notifier_mm can't change while we hold
		 * the write side of the mmap_sem.
		 */
		mmu_notifier_mm =
			kmalloc(sizeof(struct mmu_notifier_mm), GFP_KERNEL);
		if (!mmu_notifier_mm)
			return -ENOMEM;

		INIT_HLIST_HEAD(&mmu_notifier_mm->list);
		spin_lock_init(&mmu_notifier_mm->lock);
	}

	ret = mm_take_all_locks(mm);
	if (unlikely(ret))
		goto out_clean;

	/* Pairs with the mmdrop in mmu_notifier_unregister_* */
	mmgrab(mm);

	/*
	 * Serialize the update against mmu_notifier_unregister. A
	 * side note: mmu_notifier_release can't run concurrently with
	 * us because we hold the mm_users pin (either implicitly as
	 * current->mm or explicitly with get_task_mm() or similar).
	 * We can't race against any other mmu notifier method either
	 * thanks to mm_take_all_locks().
	 */
	if (mmu_notifier_mm)
		mm->mmu_notifier_mm = mmu_notifier_mm;

	spin_lock(&mm->mmu_notifier_mm->lock);
	hlist_add_head_rcu(&mn->hlist, &mm->mmu_notifier_mm->list);
	spin_unlock(&mm->mmu_notifier_mm->lock);

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

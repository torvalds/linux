// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables kernel and guest-mode vCPU access to guest physical
 * memory with suitable invalidation mechanisms.
 *
 * Copyright © 2021 Amazon.com, Inc. or its affiliates.
 *
 * Authors:
 *   David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/errno.h>

#include "kvm_mm.h"

/*
 * MMU notifier 'invalidate_range_start' hook.
 */
void gfn_to_pfn_cache_invalidate_start(struct kvm *kvm, unsigned long start,
				       unsigned long end, bool may_block)
{
	struct gfn_to_pfn_cache *gpc;

	spin_lock(&kvm->gpc_lock);
	list_for_each_entry(gpc, &kvm->gpc_list, list) {
		read_lock_irq(&gpc->lock);

		/* Only a single page so no need to care about length */
		if (gpc->valid && !is_error_noslot_pfn(gpc->pfn) &&
		    gpc->uhva >= start && gpc->uhva < end) {
			read_unlock_irq(&gpc->lock);

			/*
			 * There is a small window here where the cache could
			 * be modified, and invalidation would no longer be
			 * necessary. Hence check again whether invalidation
			 * is still necessary once the write lock has been
			 * acquired.
			 */

			write_lock_irq(&gpc->lock);
			if (gpc->valid && !is_error_noslot_pfn(gpc->pfn) &&
			    gpc->uhva >= start && gpc->uhva < end)
				gpc->valid = false;
			write_unlock_irq(&gpc->lock);
			continue;
		}

		read_unlock_irq(&gpc->lock);
	}
	spin_unlock(&kvm->gpc_lock);
}

bool kvm_gpc_check(struct gfn_to_pfn_cache *gpc, unsigned long len)
{
	struct kvm_memslots *slots = kvm_memslots(gpc->kvm);

	if (!gpc->active)
		return false;

	/*
	 * If the page was cached from a memslot, make sure the memslots have
	 * not been re-configured.
	 */
	if (!kvm_is_error_gpa(gpc->gpa) && gpc->generation != slots->generation)
		return false;

	if (kvm_is_error_hva(gpc->uhva))
		return false;

	if (offset_in_page(gpc->uhva) + len > PAGE_SIZE)
		return false;

	if (!gpc->valid)
		return false;

	return true;
}

static void *gpc_map(kvm_pfn_t pfn)
{
	if (pfn_valid(pfn))
		return kmap(pfn_to_page(pfn));

#ifdef CONFIG_HAS_IOMEM
	return memremap(pfn_to_hpa(pfn), PAGE_SIZE, MEMREMAP_WB);
#else
	return NULL;
#endif
}

static void gpc_unmap(kvm_pfn_t pfn, void *khva)
{
	/* Unmap the old pfn/page if it was mapped before. */
	if (is_error_noslot_pfn(pfn) || !khva)
		return;

	if (pfn_valid(pfn)) {
		kunmap(pfn_to_page(pfn));
		return;
	}

#ifdef CONFIG_HAS_IOMEM
	memunmap(khva);
#endif
}

static inline bool mmu_notifier_retry_cache(struct kvm *kvm, unsigned long mmu_seq)
{
	/*
	 * mn_active_invalidate_count acts for all intents and purposes
	 * like mmu_invalidate_in_progress here; but the latter cannot
	 * be used here because the invalidation of caches in the
	 * mmu_notifier event occurs _before_ mmu_invalidate_in_progress
	 * is elevated.
	 *
	 * Note, it does not matter that mn_active_invalidate_count
	 * is not protected by gpc->lock.  It is guaranteed to
	 * be elevated before the mmu_notifier acquires gpc->lock, and
	 * isn't dropped until after mmu_invalidate_seq is updated.
	 */
	if (kvm->mn_active_invalidate_count)
		return true;

	/*
	 * Ensure mn_active_invalidate_count is read before
	 * mmu_invalidate_seq.  This pairs with the smp_wmb() in
	 * mmu_notifier_invalidate_range_end() to guarantee either the
	 * old (non-zero) value of mn_active_invalidate_count or the
	 * new (incremented) value of mmu_invalidate_seq is observed.
	 */
	smp_rmb();
	return kvm->mmu_invalidate_seq != mmu_seq;
}

static kvm_pfn_t hva_to_pfn_retry(struct gfn_to_pfn_cache *gpc)
{
	/* Note, the new page offset may be different than the old! */
	void *old_khva = (void *)PAGE_ALIGN_DOWN((uintptr_t)gpc->khva);
	kvm_pfn_t new_pfn = KVM_PFN_ERR_FAULT;
	void *new_khva = NULL;
	unsigned long mmu_seq;

	lockdep_assert_held(&gpc->refresh_lock);

	lockdep_assert_held_write(&gpc->lock);

	/*
	 * Invalidate the cache prior to dropping gpc->lock, the gpa=>uhva
	 * assets have already been updated and so a concurrent check() from a
	 * different task may not fail the gpa/uhva/generation checks.
	 */
	gpc->valid = false;

	do {
		mmu_seq = gpc->kvm->mmu_invalidate_seq;
		smp_rmb();

		write_unlock_irq(&gpc->lock);

		/*
		 * If the previous iteration "failed" due to an mmu_notifier
		 * event, release the pfn and unmap the kernel virtual address
		 * from the previous attempt.  Unmapping might sleep, so this
		 * needs to be done after dropping the lock.  Opportunistically
		 * check for resched while the lock isn't held.
		 */
		if (new_pfn != KVM_PFN_ERR_FAULT) {
			/*
			 * Keep the mapping if the previous iteration reused
			 * the existing mapping and didn't create a new one.
			 */
			if (new_khva != old_khva)
				gpc_unmap(new_pfn, new_khva);

			kvm_release_pfn_clean(new_pfn);

			cond_resched();
		}

		/* We always request a writeable mapping */
		new_pfn = hva_to_pfn(gpc->uhva, false, false, NULL, true, NULL);
		if (is_error_noslot_pfn(new_pfn))
			goto out_error;

		/*
		 * Obtain a new kernel mapping if KVM itself will access the
		 * pfn.  Note, kmap() and memremap() can both sleep, so this
		 * too must be done outside of gpc->lock!
		 */
		if (new_pfn == gpc->pfn)
			new_khva = old_khva;
		else
			new_khva = gpc_map(new_pfn);

		if (!new_khva) {
			kvm_release_pfn_clean(new_pfn);
			goto out_error;
		}

		write_lock_irq(&gpc->lock);

		/*
		 * Other tasks must wait for _this_ refresh to complete before
		 * attempting to refresh.
		 */
		WARN_ON_ONCE(gpc->valid);
	} while (mmu_notifier_retry_cache(gpc->kvm, mmu_seq));

	gpc->valid = true;
	gpc->pfn = new_pfn;
	gpc->khva = new_khva + offset_in_page(gpc->uhva);

	/*
	 * Put the reference to the _new_ pfn.  The pfn is now tracked by the
	 * cache and can be safely migrated, swapped, etc... as the cache will
	 * invalidate any mappings in response to relevant mmu_notifier events.
	 */
	kvm_release_pfn_clean(new_pfn);

	return 0;

out_error:
	write_lock_irq(&gpc->lock);

	return -EFAULT;
}

static int __kvm_gpc_refresh(struct gfn_to_pfn_cache *gpc, gpa_t gpa, unsigned long uhva,
			     unsigned long len)
{
	unsigned long page_offset;
	bool unmap_old = false;
	unsigned long old_uhva;
	kvm_pfn_t old_pfn;
	bool hva_change = false;
	void *old_khva;
	int ret;

	/* Either gpa or uhva must be valid, but not both */
	if (WARN_ON_ONCE(kvm_is_error_gpa(gpa) == kvm_is_error_hva(uhva)))
		return -EINVAL;

	/*
	 * The cached acces must fit within a single page. The 'len' argument
	 * exists only to enforce that.
	 */
	page_offset = kvm_is_error_gpa(gpa) ? offset_in_page(uhva) :
					      offset_in_page(gpa);
	if (page_offset + len > PAGE_SIZE)
		return -EINVAL;

	lockdep_assert_held(&gpc->refresh_lock);

	write_lock_irq(&gpc->lock);

	if (!gpc->active) {
		ret = -EINVAL;
		goto out_unlock;
	}

	old_pfn = gpc->pfn;
	old_khva = (void *)PAGE_ALIGN_DOWN((uintptr_t)gpc->khva);
	old_uhva = PAGE_ALIGN_DOWN(gpc->uhva);

	if (kvm_is_error_gpa(gpa)) {
		gpc->gpa = INVALID_GPA;
		gpc->memslot = NULL;
		gpc->uhva = PAGE_ALIGN_DOWN(uhva);

		if (gpc->uhva != old_uhva)
			hva_change = true;
	} else {
		struct kvm_memslots *slots = kvm_memslots(gpc->kvm);

		if (gpc->gpa != gpa || gpc->generation != slots->generation ||
		    kvm_is_error_hva(gpc->uhva)) {
			gfn_t gfn = gpa_to_gfn(gpa);

			gpc->gpa = gpa;
			gpc->generation = slots->generation;
			gpc->memslot = __gfn_to_memslot(slots, gfn);
			gpc->uhva = gfn_to_hva_memslot(gpc->memslot, gfn);

			if (kvm_is_error_hva(gpc->uhva)) {
				ret = -EFAULT;
				goto out;
			}

			/*
			 * Even if the GPA and/or the memslot generation changed, the
			 * HVA may still be the same.
			 */
			if (gpc->uhva != old_uhva)
				hva_change = true;
		} else {
			gpc->uhva = old_uhva;
		}
	}

	/* Note: the offset must be correct before calling hva_to_pfn_retry() */
	gpc->uhva += page_offset;

	/*
	 * If the userspace HVA changed or the PFN was already invalid,
	 * drop the lock and do the HVA to PFN lookup again.
	 */
	if (!gpc->valid || hva_change) {
		ret = hva_to_pfn_retry(gpc);
	} else {
		/*
		 * If the HVA→PFN mapping was already valid, don't unmap it.
		 * But do update gpc->khva because the offset within the page
		 * may have changed.
		 */
		gpc->khva = old_khva + page_offset;
		ret = 0;
		goto out_unlock;
	}

 out:
	/*
	 * Invalidate the cache and purge the pfn/khva if the refresh failed.
	 * Some/all of the uhva, gpa, and memslot generation info may still be
	 * valid, leave it as is.
	 */
	if (ret) {
		gpc->valid = false;
		gpc->pfn = KVM_PFN_ERR_FAULT;
		gpc->khva = NULL;
	}

	/* Detect a pfn change before dropping the lock! */
	unmap_old = (old_pfn != gpc->pfn);

out_unlock:
	write_unlock_irq(&gpc->lock);

	if (unmap_old)
		gpc_unmap(old_pfn, old_khva);

	return ret;
}

int kvm_gpc_refresh(struct gfn_to_pfn_cache *gpc, unsigned long len)
{
	unsigned long uhva;

	guard(mutex)(&gpc->refresh_lock);

	/*
	 * If the GPA is valid then ignore the HVA, as a cache can be GPA-based
	 * or HVA-based, not both.  For GPA-based caches, the HVA will be
	 * recomputed during refresh if necessary.
	 */
	uhva = kvm_is_error_gpa(gpc->gpa) ? gpc->uhva : KVM_HVA_ERR_BAD;

	return __kvm_gpc_refresh(gpc, gpc->gpa, uhva, len);
}

void kvm_gpc_init(struct gfn_to_pfn_cache *gpc, struct kvm *kvm)
{
	rwlock_init(&gpc->lock);
	mutex_init(&gpc->refresh_lock);

	gpc->kvm = kvm;
	gpc->pfn = KVM_PFN_ERR_FAULT;
	gpc->gpa = INVALID_GPA;
	gpc->uhva = KVM_HVA_ERR_BAD;
	gpc->active = gpc->valid = false;
}

static int __kvm_gpc_activate(struct gfn_to_pfn_cache *gpc, gpa_t gpa, unsigned long uhva,
			      unsigned long len)
{
	struct kvm *kvm = gpc->kvm;

	guard(mutex)(&gpc->refresh_lock);

	if (!gpc->active) {
		if (KVM_BUG_ON(gpc->valid, kvm))
			return -EIO;

		spin_lock(&kvm->gpc_lock);
		list_add(&gpc->list, &kvm->gpc_list);
		spin_unlock(&kvm->gpc_lock);

		/*
		 * Activate the cache after adding it to the list, a concurrent
		 * refresh must not establish a mapping until the cache is
		 * reachable by mmu_notifier events.
		 */
		write_lock_irq(&gpc->lock);
		gpc->active = true;
		write_unlock_irq(&gpc->lock);
	}
	return __kvm_gpc_refresh(gpc, gpa, uhva, len);
}

int kvm_gpc_activate(struct gfn_to_pfn_cache *gpc, gpa_t gpa, unsigned long len)
{
	return __kvm_gpc_activate(gpc, gpa, KVM_HVA_ERR_BAD, len);
}

int kvm_gpc_activate_hva(struct gfn_to_pfn_cache *gpc, unsigned long uhva, unsigned long len)
{
	return __kvm_gpc_activate(gpc, INVALID_GPA, uhva, len);
}

void kvm_gpc_deactivate(struct gfn_to_pfn_cache *gpc)
{
	struct kvm *kvm = gpc->kvm;
	kvm_pfn_t old_pfn;
	void *old_khva;

	guard(mutex)(&gpc->refresh_lock);

	if (gpc->active) {
		/*
		 * Deactivate the cache before removing it from the list, KVM
		 * must stall mmu_notifier events until all users go away, i.e.
		 * until gpc->lock is dropped and refresh is guaranteed to fail.
		 */
		write_lock_irq(&gpc->lock);
		gpc->active = false;
		gpc->valid = false;

		/*
		 * Leave the GPA => uHVA cache intact, it's protected by the
		 * memslot generation.  The PFN lookup needs to be redone every
		 * time as mmu_notifier protection is lost when the cache is
		 * removed from the VM's gpc_list.
		 */
		old_khva = gpc->khva - offset_in_page(gpc->khva);
		gpc->khva = NULL;

		old_pfn = gpc->pfn;
		gpc->pfn = KVM_PFN_ERR_FAULT;
		write_unlock_irq(&gpc->lock);

		spin_lock(&kvm->gpc_lock);
		list_del(&gpc->list);
		spin_unlock(&kvm->gpc_lock);

		gpc_unmap(old_pfn, old_khva);
	}
}

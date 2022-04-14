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
	DECLARE_BITMAP(vcpu_bitmap, KVM_MAX_VCPUS);
	struct gfn_to_pfn_cache *gpc;
	bool evict_vcpus = false;

	spin_lock(&kvm->gpc_lock);
	list_for_each_entry(gpc, &kvm->gpc_list, list) {
		write_lock_irq(&gpc->lock);

		/* Only a single page so no need to care about length */
		if (gpc->valid && !is_error_noslot_pfn(gpc->pfn) &&
		    gpc->uhva >= start && gpc->uhva < end) {
			gpc->valid = false;

			/*
			 * If a guest vCPU could be using the physical address,
			 * it needs to be forced out of guest mode.
			 */
			if (gpc->usage & KVM_GUEST_USES_PFN) {
				if (!evict_vcpus) {
					evict_vcpus = true;
					bitmap_zero(vcpu_bitmap, KVM_MAX_VCPUS);
				}
				__set_bit(gpc->vcpu->vcpu_idx, vcpu_bitmap);
			}
		}
		write_unlock_irq(&gpc->lock);
	}
	spin_unlock(&kvm->gpc_lock);

	if (evict_vcpus) {
		/*
		 * KVM needs to ensure the vCPU is fully out of guest context
		 * before allowing the invalidation to continue.
		 */
		unsigned int req = KVM_REQ_OUTSIDE_GUEST_MODE;
		bool called;

		/*
		 * If the OOM reaper is active, then all vCPUs should have
		 * been stopped already, so perform the request without
		 * KVM_REQUEST_WAIT and be sad if any needed to be IPI'd.
		 */
		if (!may_block)
			req &= ~KVM_REQUEST_WAIT;

		called = kvm_make_vcpus_request_mask(kvm, req, vcpu_bitmap);

		WARN_ON_ONCE(called && !may_block);
	}
}

bool kvm_gfn_to_pfn_cache_check(struct kvm *kvm, struct gfn_to_pfn_cache *gpc,
				gpa_t gpa, unsigned long len)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);

	if ((gpa & ~PAGE_MASK) + len > PAGE_SIZE)
		return false;

	if (gpc->gpa != gpa || gpc->generation != slots->generation ||
	    kvm_is_error_hva(gpc->uhva))
		return false;

	if (!gpc->valid)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(kvm_gfn_to_pfn_cache_check);

static void __release_gpc(struct kvm *kvm, kvm_pfn_t pfn, void *khva, gpa_t gpa)
{
	/* Unmap the old page if it was mapped before, and release it */
	if (!is_error_noslot_pfn(pfn)) {
		if (khva) {
			if (pfn_valid(pfn))
				kunmap(pfn_to_page(pfn));
#ifdef CONFIG_HAS_IOMEM
			else
				memunmap(khva);
#endif
		}

		kvm_release_pfn(pfn, false);
	}
}

static kvm_pfn_t hva_to_pfn_retry(struct kvm *kvm, unsigned long uhva)
{
	unsigned long mmu_seq;
	kvm_pfn_t new_pfn;
	int retry;

	do {
		mmu_seq = kvm->mmu_notifier_seq;
		smp_rmb();

		/* We always request a writeable mapping */
		new_pfn = hva_to_pfn(uhva, false, NULL, true, NULL);
		if (is_error_noslot_pfn(new_pfn))
			break;

		KVM_MMU_READ_LOCK(kvm);
		retry = mmu_notifier_retry_hva(kvm, mmu_seq, uhva);
		KVM_MMU_READ_UNLOCK(kvm);
		if (!retry)
			break;

		cond_resched();
	} while (1);

	return new_pfn;
}

int kvm_gfn_to_pfn_cache_refresh(struct kvm *kvm, struct gfn_to_pfn_cache *gpc,
				 gpa_t gpa, unsigned long len)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);
	unsigned long page_offset = gpa & ~PAGE_MASK;
	kvm_pfn_t old_pfn, new_pfn;
	unsigned long old_uhva;
	gpa_t old_gpa;
	void *old_khva;
	bool old_valid;
	int ret = 0;

	/*
	 * If must fit within a single page. The 'len' argument is
	 * only to enforce that.
	 */
	if (page_offset + len > PAGE_SIZE)
		return -EINVAL;

	write_lock_irq(&gpc->lock);

	old_gpa = gpc->gpa;
	old_pfn = gpc->pfn;
	old_khva = gpc->khva - offset_in_page(gpc->khva);
	old_uhva = gpc->uhva;
	old_valid = gpc->valid;

	/* If the userspace HVA is invalid, refresh that first */
	if (gpc->gpa != gpa || gpc->generation != slots->generation ||
	    kvm_is_error_hva(gpc->uhva)) {
		gfn_t gfn = gpa_to_gfn(gpa);

		gpc->gpa = gpa;
		gpc->generation = slots->generation;
		gpc->memslot = __gfn_to_memslot(slots, gfn);
		gpc->uhva = gfn_to_hva_memslot(gpc->memslot, gfn);

		if (kvm_is_error_hva(gpc->uhva)) {
			gpc->pfn = KVM_PFN_ERR_FAULT;
			ret = -EFAULT;
			goto out;
		}

		gpc->uhva += page_offset;
	}

	/*
	 * If the userspace HVA changed or the PFN was already invalid,
	 * drop the lock and do the HVA to PFN lookup again.
	 */
	if (!old_valid || old_uhva != gpc->uhva) {
		unsigned long uhva = gpc->uhva;
		void *new_khva = NULL;

		/* Placeholders for "hva is valid but not yet mapped" */
		gpc->pfn = KVM_PFN_ERR_FAULT;
		gpc->khva = NULL;
		gpc->valid = true;

		write_unlock_irq(&gpc->lock);

		new_pfn = hva_to_pfn_retry(kvm, uhva);
		if (is_error_noslot_pfn(new_pfn)) {
			ret = -EFAULT;
			goto map_done;
		}

		if (gpc->usage & KVM_HOST_USES_PFN) {
			if (new_pfn == old_pfn) {
				new_khva = old_khva;
				old_pfn = KVM_PFN_ERR_FAULT;
				old_khva = NULL;
			} else if (pfn_valid(new_pfn)) {
				new_khva = kmap(pfn_to_page(new_pfn));
#ifdef CONFIG_HAS_IOMEM
			} else {
				new_khva = memremap(pfn_to_hpa(new_pfn), PAGE_SIZE, MEMREMAP_WB);
#endif
			}
			if (new_khva)
				new_khva += page_offset;
			else
				ret = -EFAULT;
		}

	map_done:
		write_lock_irq(&gpc->lock);
		if (ret) {
			gpc->valid = false;
			gpc->pfn = KVM_PFN_ERR_FAULT;
			gpc->khva = NULL;
		} else {
			/* At this point, gpc->valid may already have been cleared */
			gpc->pfn = new_pfn;
			gpc->khva = new_khva;
		}
	} else {
		/* If the HVA→PFN mapping was already valid, don't unmap it. */
		old_pfn = KVM_PFN_ERR_FAULT;
		old_khva = NULL;
	}

 out:
	write_unlock_irq(&gpc->lock);

	__release_gpc(kvm, old_pfn, old_khva, old_gpa);

	return ret;
}
EXPORT_SYMBOL_GPL(kvm_gfn_to_pfn_cache_refresh);

void kvm_gfn_to_pfn_cache_unmap(struct kvm *kvm, struct gfn_to_pfn_cache *gpc)
{
	void *old_khva;
	kvm_pfn_t old_pfn;
	gpa_t old_gpa;

	write_lock_irq(&gpc->lock);

	gpc->valid = false;

	old_khva = gpc->khva - offset_in_page(gpc->khva);
	old_gpa = gpc->gpa;
	old_pfn = gpc->pfn;

	/*
	 * We can leave the GPA → uHVA map cache intact but the PFN
	 * lookup will need to be redone even for the same page.
	 */
	gpc->khva = NULL;
	gpc->pfn = KVM_PFN_ERR_FAULT;

	write_unlock_irq(&gpc->lock);

	__release_gpc(kvm, old_pfn, old_khva, old_gpa);
}
EXPORT_SYMBOL_GPL(kvm_gfn_to_pfn_cache_unmap);


int kvm_gfn_to_pfn_cache_init(struct kvm *kvm, struct gfn_to_pfn_cache *gpc,
			      struct kvm_vcpu *vcpu, enum pfn_cache_usage usage,
			      gpa_t gpa, unsigned long len)
{
	WARN_ON_ONCE(!usage || (usage & KVM_GUEST_AND_HOST_USE_PFN) != usage);

	if (!gpc->active) {
		rwlock_init(&gpc->lock);

		gpc->khva = NULL;
		gpc->pfn = KVM_PFN_ERR_FAULT;
		gpc->uhva = KVM_HVA_ERR_BAD;
		gpc->vcpu = vcpu;
		gpc->usage = usage;
		gpc->valid = false;
		gpc->active = true;

		spin_lock(&kvm->gpc_lock);
		list_add(&gpc->list, &kvm->gpc_list);
		spin_unlock(&kvm->gpc_lock);
	}
	return kvm_gfn_to_pfn_cache_refresh(kvm, gpc, gpa, len);
}
EXPORT_SYMBOL_GPL(kvm_gfn_to_pfn_cache_init);

void kvm_gfn_to_pfn_cache_destroy(struct kvm *kvm, struct gfn_to_pfn_cache *gpc)
{
	if (gpc->active) {
		spin_lock(&kvm->gpc_lock);
		list_del(&gpc->list);
		spin_unlock(&kvm->gpc_lock);

		kvm_gfn_to_pfn_cache_unmap(kvm, gpc);
		gpc->active = false;
	}
}
EXPORT_SYMBOL_GPL(kvm_gfn_to_pfn_cache_destroy);

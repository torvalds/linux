// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM dirty ring implementation
 *
 * Copyright 2019 Red Hat, Inc.
 */
#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/vmalloc.h>
#include <linux/kvm_dirty_ring.h>
#include <trace/events/kvm.h>
#include "kvm_mm.h"

int __weak kvm_cpu_dirty_log_size(struct kvm *kvm)
{
	return 0;
}

u32 kvm_dirty_ring_get_rsvd_entries(struct kvm *kvm)
{
	return KVM_DIRTY_RING_RSVD_ENTRIES + kvm_cpu_dirty_log_size(kvm);
}

bool kvm_use_dirty_bitmap(struct kvm *kvm)
{
	lockdep_assert_held(&kvm->slots_lock);

	return !kvm->dirty_ring_size || kvm->dirty_ring_with_bitmap;
}

#ifndef CONFIG_NEED_KVM_DIRTY_RING_WITH_BITMAP
bool kvm_arch_allow_write_without_running_vcpu(struct kvm *kvm)
{
	return false;
}
#endif

static u32 kvm_dirty_ring_used(struct kvm_dirty_ring *ring)
{
	return READ_ONCE(ring->dirty_index) - READ_ONCE(ring->reset_index);
}

static bool kvm_dirty_ring_soft_full(struct kvm_dirty_ring *ring)
{
	return kvm_dirty_ring_used(ring) >= ring->soft_limit;
}

static bool kvm_dirty_ring_full(struct kvm_dirty_ring *ring)
{
	return kvm_dirty_ring_used(ring) >= ring->size;
}

static void kvm_reset_dirty_gfn(struct kvm *kvm, u32 slot, u64 offset, u64 mask)
{
	struct kvm_memory_slot *memslot;
	int as_id, id;

	as_id = slot >> 16;
	id = (u16)slot;

	if (as_id >= kvm_arch_nr_memslot_as_ids(kvm) || id >= KVM_USER_MEM_SLOTS)
		return;

	memslot = id_to_memslot(__kvm_memslots(kvm, as_id), id);

	if (!memslot || (offset + __fls(mask)) >= memslot->npages)
		return;

	KVM_MMU_LOCK(kvm);
	kvm_arch_mmu_enable_log_dirty_pt_masked(kvm, memslot, offset, mask);
	KVM_MMU_UNLOCK(kvm);
}

int kvm_dirty_ring_alloc(struct kvm *kvm, struct kvm_dirty_ring *ring,
			 int index, u32 size)
{
	ring->dirty_gfns = vzalloc(size);
	if (!ring->dirty_gfns)
		return -ENOMEM;

	ring->size = size / sizeof(struct kvm_dirty_gfn);
	ring->soft_limit = ring->size - kvm_dirty_ring_get_rsvd_entries(kvm);
	ring->dirty_index = 0;
	ring->reset_index = 0;
	ring->index = index;

	return 0;
}

static inline void kvm_dirty_gfn_set_invalid(struct kvm_dirty_gfn *gfn)
{
	smp_store_release(&gfn->flags, 0);
}

static inline void kvm_dirty_gfn_set_dirtied(struct kvm_dirty_gfn *gfn)
{
	gfn->flags = KVM_DIRTY_GFN_F_DIRTY;
}

static inline bool kvm_dirty_gfn_harvested(struct kvm_dirty_gfn *gfn)
{
	return smp_load_acquire(&gfn->flags) & KVM_DIRTY_GFN_F_RESET;
}

int kvm_dirty_ring_reset(struct kvm *kvm, struct kvm_dirty_ring *ring,
			 int *nr_entries_reset)
{
	/*
	 * To minimize mmu_lock contention, batch resets for harvested entries
	 * whose gfns are in the same slot, and are within N frame numbers of
	 * each other, where N is the number of bits in an unsigned long.  For
	 * simplicity, process the current set of entries when the next entry
	 * can't be included in the batch.
	 *
	 * Track the current batch slot, the gfn offset into the slot for the
	 * batch, and the bitmask of gfns that need to be reset (relative to
	 * offset).  Note, the offset may be adjusted backwards, e.g. so that
	 * a sequence of gfns X, X-1, ... X-N-1 can be batched.
	 */
	u32 cur_slot, next_slot;
	u64 cur_offset, next_offset;
	unsigned long mask = 0;
	struct kvm_dirty_gfn *entry;

	/*
	 * Ensure concurrent calls to KVM_RESET_DIRTY_RINGS are serialized,
	 * e.g. so that KVM fully resets all entries processed by a given call
	 * before returning to userspace.  Holding slots_lock also protects
	 * the various memslot accesses.
	 */
	lockdep_assert_held(&kvm->slots_lock);

	while (likely((*nr_entries_reset) < INT_MAX)) {
		if (signal_pending(current))
			return -EINTR;

		entry = &ring->dirty_gfns[ring->reset_index & (ring->size - 1)];

		if (!kvm_dirty_gfn_harvested(entry))
			break;

		next_slot = READ_ONCE(entry->slot);
		next_offset = READ_ONCE(entry->offset);

		/* Update the flags to reflect that this GFN is reset */
		kvm_dirty_gfn_set_invalid(entry);

		ring->reset_index++;
		(*nr_entries_reset)++;

		if (mask) {
			/*
			 * While the size of each ring is fixed, it's possible
			 * for the ring to be constantly re-dirtied/harvested
			 * while the reset is in-progress (the hard limit exists
			 * only to guard against the count becoming negative).
			 */
			cond_resched();

			/*
			 * Try to coalesce the reset operations when the guest
			 * is scanning pages in the same slot.
			 */
			if (next_slot == cur_slot) {
				s64 delta = next_offset - cur_offset;

				if (delta >= 0 && delta < BITS_PER_LONG) {
					mask |= 1ull << delta;
					continue;
				}

				/* Backwards visit, careful about overflows! */
				if (delta > -BITS_PER_LONG && delta < 0 &&
				(mask << -delta >> -delta) == mask) {
					cur_offset = next_offset;
					mask = (mask << -delta) | 1;
					continue;
				}
			}

			/*
			 * Reset the slot for all the harvested entries that
			 * have been gathered, but not yet fully processed.
			 */
			kvm_reset_dirty_gfn(kvm, cur_slot, cur_offset, mask);
		}

		/*
		 * The current slot was reset or this is the first harvested
		 * entry, (re)initialize the metadata.
		 */
		cur_slot = next_slot;
		cur_offset = next_offset;
		mask = 1;
	}

	/*
	 * Perform a final reset if there are harvested entries that haven't
	 * been processed, which is guaranteed if at least one harvested was
	 * found.  The loop only performs a reset when the "next" entry can't
	 * be batched with the "current" entry(s), and that reset processes the
	 * _current_ entry(s); i.e. the last harvested entry, a.k.a. next, will
	 * always be left pending.
	 */
	if (mask)
		kvm_reset_dirty_gfn(kvm, cur_slot, cur_offset, mask);

	/*
	 * The request KVM_REQ_DIRTY_RING_SOFT_FULL will be cleared
	 * by the VCPU thread next time when it enters the guest.
	 */

	trace_kvm_dirty_ring_reset(ring);

	return 0;
}

void kvm_dirty_ring_push(struct kvm_vcpu *vcpu, u32 slot, u64 offset)
{
	struct kvm_dirty_ring *ring = &vcpu->dirty_ring;
	struct kvm_dirty_gfn *entry;

	/* It should never get full */
	WARN_ON_ONCE(kvm_dirty_ring_full(ring));

	entry = &ring->dirty_gfns[ring->dirty_index & (ring->size - 1)];

	entry->slot = slot;
	entry->offset = offset;
	/*
	 * Make sure the data is filled in before we publish this to
	 * the userspace program.  There's no paired kernel-side reader.
	 */
	smp_wmb();
	kvm_dirty_gfn_set_dirtied(entry);
	ring->dirty_index++;
	trace_kvm_dirty_ring_push(ring, slot, offset);

	if (kvm_dirty_ring_soft_full(ring))
		kvm_make_request(KVM_REQ_DIRTY_RING_SOFT_FULL, vcpu);
}

bool kvm_dirty_ring_check_request(struct kvm_vcpu *vcpu)
{
	/*
	 * The VCPU isn't runnable when the dirty ring becomes soft full.
	 * The KVM_REQ_DIRTY_RING_SOFT_FULL event is always set to prevent
	 * the VCPU from running until the dirty pages are harvested and
	 * the dirty ring is reset by userspace.
	 */
	if (kvm_check_request(KVM_REQ_DIRTY_RING_SOFT_FULL, vcpu) &&
	    kvm_dirty_ring_soft_full(&vcpu->dirty_ring)) {
		kvm_make_request(KVM_REQ_DIRTY_RING_SOFT_FULL, vcpu);
		vcpu->run->exit_reason = KVM_EXIT_DIRTY_RING_FULL;
		trace_kvm_dirty_ring_exit(vcpu);
		return true;
	}

	return false;
}

struct page *kvm_dirty_ring_get_page(struct kvm_dirty_ring *ring, u32 offset)
{
	return vmalloc_to_page((void *)ring->dirty_gfns + offset * PAGE_SIZE);
}

void kvm_dirty_ring_free(struct kvm_dirty_ring *ring)
{
	vfree(ring->dirty_gfns);
	ring->dirty_gfns = NULL;
}

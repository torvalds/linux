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

int __weak kvm_cpu_dirty_log_size(void)
{
	return 0;
}

u32 kvm_dirty_ring_get_rsvd_entries(void)
{
	return KVM_DIRTY_RING_RSVD_ENTRIES + kvm_cpu_dirty_log_size();
}

static u32 kvm_dirty_ring_used(struct kvm_dirty_ring *ring)
{
	return READ_ONCE(ring->dirty_index) - READ_ONCE(ring->reset_index);
}

bool kvm_dirty_ring_soft_full(struct kvm_dirty_ring *ring)
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

	if (as_id >= KVM_ADDRESS_SPACE_NUM || id >= KVM_USER_MEM_SLOTS)
		return;

	memslot = id_to_memslot(__kvm_memslots(kvm, as_id), id);

	if (!memslot || (offset + __fls(mask)) >= memslot->npages)
		return;

	KVM_MMU_LOCK(kvm);
	kvm_arch_mmu_enable_log_dirty_pt_masked(kvm, memslot, offset, mask);
	KVM_MMU_UNLOCK(kvm);
}

int kvm_dirty_ring_alloc(struct kvm_dirty_ring *ring, int index, u32 size)
{
	ring->dirty_gfns = vzalloc(size);
	if (!ring->dirty_gfns)
		return -ENOMEM;

	ring->size = size / sizeof(struct kvm_dirty_gfn);
	ring->soft_limit = ring->size - kvm_dirty_ring_get_rsvd_entries();
	ring->dirty_index = 0;
	ring->reset_index = 0;
	ring->index = index;

	return 0;
}

static inline void kvm_dirty_gfn_set_invalid(struct kvm_dirty_gfn *gfn)
{
	gfn->flags = 0;
}

static inline void kvm_dirty_gfn_set_dirtied(struct kvm_dirty_gfn *gfn)
{
	gfn->flags = KVM_DIRTY_GFN_F_DIRTY;
}

static inline bool kvm_dirty_gfn_harvested(struct kvm_dirty_gfn *gfn)
{
	return gfn->flags & KVM_DIRTY_GFN_F_RESET;
}

int kvm_dirty_ring_reset(struct kvm *kvm, struct kvm_dirty_ring *ring)
{
	u32 cur_slot, next_slot;
	u64 cur_offset, next_offset;
	unsigned long mask;
	int count = 0;
	struct kvm_dirty_gfn *entry;
	bool first_round = true;

	/* This is only needed to make compilers happy */
	cur_slot = cur_offset = mask = 0;

	while (true) {
		entry = &ring->dirty_gfns[ring->reset_index & (ring->size - 1)];

		if (!kvm_dirty_gfn_harvested(entry))
			break;

		next_slot = READ_ONCE(entry->slot);
		next_offset = READ_ONCE(entry->offset);

		/* Update the flags to reflect that this GFN is reset */
		kvm_dirty_gfn_set_invalid(entry);

		ring->reset_index++;
		count++;
		/*
		 * Try to coalesce the reset operations when the guest is
		 * scanning pages in the same slot.
		 */
		if (!first_round && next_slot == cur_slot) {
			s64 delta = next_offset - cur_offset;

			if (delta >= 0 && delta < BITS_PER_LONG) {
				mask |= 1ull << delta;
				continue;
			}

			/* Backwards visit, careful about overflows!  */
			if (delta > -BITS_PER_LONG && delta < 0 &&
			    (mask << -delta >> -delta) == mask) {
				cur_offset = next_offset;
				mask = (mask << -delta) | 1;
				continue;
			}
		}
		kvm_reset_dirty_gfn(kvm, cur_slot, cur_offset, mask);
		cur_slot = next_slot;
		cur_offset = next_offset;
		mask = 1;
		first_round = false;
	}

	kvm_reset_dirty_gfn(kvm, cur_slot, cur_offset, mask);

	trace_kvm_dirty_ring_reset(ring);

	return count;
}

void kvm_dirty_ring_push(struct kvm_dirty_ring *ring, u32 slot, u64 offset)
{
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

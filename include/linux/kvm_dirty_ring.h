#ifndef KVM_DIRTY_RING_H
#define KVM_DIRTY_RING_H

#include <linux/kvm.h>

/**
 * kvm_dirty_ring: KVM internal dirty ring structure
 *
 * @dirty_index: free running counter that points to the next slot in
 *               dirty_ring->dirty_gfns, where a new dirty page should go
 * @reset_index: free running counter that points to the next dirty page
 *               in dirty_ring->dirty_gfns for which dirty trap needs to
 *               be reenabled
 * @size:        size of the compact list, dirty_ring->dirty_gfns
 * @soft_limit:  when the number of dirty pages in the list reaches this
 *               limit, vcpu that owns this ring should exit to userspace
 *               to allow userspace to harvest all the dirty pages
 * @dirty_gfns:  the array to keep the dirty gfns
 * @index:       index of this dirty ring
 */
struct kvm_dirty_ring {
	u32 dirty_index;
	u32 reset_index;
	u32 size;
	u32 soft_limit;
	struct kvm_dirty_gfn *dirty_gfns;
	int index;
};

#ifndef CONFIG_HAVE_KVM_DIRTY_RING
/*
 * If CONFIG_HAVE_HVM_DIRTY_RING not defined, kvm_dirty_ring.o should
 * not be included as well, so define these nop functions for the arch.
 */
static inline u32 kvm_dirty_ring_get_rsvd_entries(void)
{
	return 0;
}

static inline bool kvm_use_dirty_bitmap(struct kvm *kvm)
{
	return true;
}

static inline int kvm_dirty_ring_alloc(struct kvm_dirty_ring *ring,
				       int index, u32 size)
{
	return 0;
}

static inline int kvm_dirty_ring_reset(struct kvm *kvm,
				       struct kvm_dirty_ring *ring)
{
	return 0;
}

static inline void kvm_dirty_ring_push(struct kvm_vcpu *vcpu,
				       u32 slot, u64 offset)
{
}

static inline struct page *kvm_dirty_ring_get_page(struct kvm_dirty_ring *ring,
						   u32 offset)
{
	return NULL;
}

static inline void kvm_dirty_ring_free(struct kvm_dirty_ring *ring)
{
}

#else /* CONFIG_HAVE_KVM_DIRTY_RING */

int kvm_cpu_dirty_log_size(void);
bool kvm_use_dirty_bitmap(struct kvm *kvm);
bool kvm_arch_allow_write_without_running_vcpu(struct kvm *kvm);
u32 kvm_dirty_ring_get_rsvd_entries(void);
int kvm_dirty_ring_alloc(struct kvm_dirty_ring *ring, int index, u32 size);

/*
 * called with kvm->slots_lock held, returns the number of
 * processed pages.
 */
int kvm_dirty_ring_reset(struct kvm *kvm, struct kvm_dirty_ring *ring);

/*
 * returns =0: successfully pushed
 *         <0: unable to push, need to wait
 */
void kvm_dirty_ring_push(struct kvm_vcpu *vcpu, u32 slot, u64 offset);

bool kvm_dirty_ring_check_request(struct kvm_vcpu *vcpu);

/* for use in vm_operations_struct */
struct page *kvm_dirty_ring_get_page(struct kvm_dirty_ring *ring, u32 offset);

void kvm_dirty_ring_free(struct kvm_dirty_ring *ring);

#endif /* CONFIG_HAVE_KVM_DIRTY_RING */

#endif	/* KVM_DIRTY_RING_H */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine (KVM) Hypervisor
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 */

#include <kvm/iodev.h>

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/percpu.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/reboot.h>
#include <linux/debugfs.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/syscore_ops.h>
#include <linux/cpu.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/sched/stat.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/anon_inodes.h>
#include <linux/profile.h>
#include <linux/kvm_para.h>
#include <linux/pagemap.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/compat.h>
#include <linux/srcu.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/bsearch.h>
#include <linux/io.h>
#include <linux/lockdep.h>
#include <linux/kthread.h>
#include <linux/suspend.h>

#include <asm/processor.h>
#include <asm/ioctl.h>
#include <linux/uaccess.h>

#include "coalesced_mmio.h"
#include "async_pf.h"
#include "kvm_mm.h"
#include "vfio.h"

#include <trace/events/ipi.h>

#define CREATE_TRACE_POINTS
#include <trace/events/kvm.h>

#include <linux/kvm_dirty_ring.h>


/* Worst case buffer size needed for holding an integer. */
#define ITOA_MAX_LEN 12

MODULE_AUTHOR("Qumranet");
MODULE_DESCRIPTION("Kernel-based Virtual Machine (KVM) Hypervisor");
MODULE_LICENSE("GPL");

/* Architectures should define their poll value according to the halt latency */
unsigned int halt_poll_ns = KVM_HALT_POLL_NS_DEFAULT;
module_param(halt_poll_ns, uint, 0644);
EXPORT_SYMBOL_GPL(halt_poll_ns);

/* Default doubles per-vcpu halt_poll_ns. */
unsigned int halt_poll_ns_grow = 2;
module_param(halt_poll_ns_grow, uint, 0644);
EXPORT_SYMBOL_GPL(halt_poll_ns_grow);

/* The start value to grow halt_poll_ns from */
unsigned int halt_poll_ns_grow_start = 10000; /* 10us */
module_param(halt_poll_ns_grow_start, uint, 0644);
EXPORT_SYMBOL_GPL(halt_poll_ns_grow_start);

/* Default halves per-vcpu halt_poll_ns. */
unsigned int halt_poll_ns_shrink = 2;
module_param(halt_poll_ns_shrink, uint, 0644);
EXPORT_SYMBOL_GPL(halt_poll_ns_shrink);

/*
 * Allow direct access (from KVM or the CPU) without MMU notifier protection
 * to unpinned pages.
 */
static bool allow_unsafe_mappings;
module_param(allow_unsafe_mappings, bool, 0444);

/*
 * Ordering of locks:
 *
 *	kvm->lock --> kvm->slots_lock --> kvm->irq_lock
 */

DEFINE_MUTEX(kvm_lock);
LIST_HEAD(vm_list);

static struct kmem_cache *kvm_vcpu_cache;

static __read_mostly struct preempt_ops kvm_preempt_ops;
static DEFINE_PER_CPU(struct kvm_vcpu *, kvm_running_vcpu);

static struct dentry *kvm_debugfs_dir;

static const struct file_operations stat_fops_per_vm;

static long kvm_vcpu_ioctl(struct file *file, unsigned int ioctl,
			   unsigned long arg);
#ifdef CONFIG_KVM_COMPAT
static long kvm_vcpu_compat_ioctl(struct file *file, unsigned int ioctl,
				  unsigned long arg);
#define KVM_COMPAT(c)	.compat_ioctl	= (c)
#else
/*
 * For architectures that don't implement a compat infrastructure,
 * adopt a double line of defense:
 * - Prevent a compat task from opening /dev/kvm
 * - If the open has been done by a 64bit task, and the KVM fd
 *   passed to a compat task, let the ioctls fail.
 */
static long kvm_no_compat_ioctl(struct file *file, unsigned int ioctl,
				unsigned long arg) { return -EINVAL; }

static int kvm_no_compat_open(struct inode *inode, struct file *file)
{
	return is_compat_task() ? -ENODEV : 0;
}
#define KVM_COMPAT(c)	.compat_ioctl	= kvm_no_compat_ioctl,	\
			.open		= kvm_no_compat_open
#endif
static int kvm_enable_virtualization(void);
static void kvm_disable_virtualization(void);

static void kvm_io_bus_destroy(struct kvm_io_bus *bus);

#define KVM_EVENT_CREATE_VM 0
#define KVM_EVENT_DESTROY_VM 1
static void kvm_uevent_notify_change(unsigned int type, struct kvm *kvm);
static unsigned long long kvm_createvm_count;
static unsigned long long kvm_active_vms;

static DEFINE_PER_CPU(cpumask_var_t, cpu_kick_mask);

__weak void kvm_arch_guest_memory_reclaimed(struct kvm *kvm)
{
}

/*
 * Switches to specified vcpu, until a matching vcpu_put()
 */
void vcpu_load(struct kvm_vcpu *vcpu)
{
	int cpu = get_cpu();

	__this_cpu_write(kvm_running_vcpu, vcpu);
	preempt_notifier_register(&vcpu->preempt_notifier);
	kvm_arch_vcpu_load(vcpu, cpu);
	put_cpu();
}
EXPORT_SYMBOL_GPL(vcpu_load);

void vcpu_put(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	kvm_arch_vcpu_put(vcpu);
	preempt_notifier_unregister(&vcpu->preempt_notifier);
	__this_cpu_write(kvm_running_vcpu, NULL);
	preempt_enable();
}
EXPORT_SYMBOL_GPL(vcpu_put);

/* TODO: merge with kvm_arch_vcpu_should_kick */
static bool kvm_request_needs_ipi(struct kvm_vcpu *vcpu, unsigned req)
{
	int mode = kvm_vcpu_exiting_guest_mode(vcpu);

	/*
	 * We need to wait for the VCPU to reenable interrupts and get out of
	 * READING_SHADOW_PAGE_TABLES mode.
	 */
	if (req & KVM_REQUEST_WAIT)
		return mode != OUTSIDE_GUEST_MODE;

	/*
	 * Need to kick a running VCPU, but otherwise there is nothing to do.
	 */
	return mode == IN_GUEST_MODE;
}

static void ack_kick(void *_completed)
{
}

static inline bool kvm_kick_many_cpus(struct cpumask *cpus, bool wait)
{
	if (cpumask_empty(cpus))
		return false;

	smp_call_function_many(cpus, ack_kick, NULL, wait);
	return true;
}

static void kvm_make_vcpu_request(struct kvm_vcpu *vcpu, unsigned int req,
				  struct cpumask *tmp, int current_cpu)
{
	int cpu;

	if (likely(!(req & KVM_REQUEST_NO_ACTION)))
		__kvm_make_request(req, vcpu);

	if (!(req & KVM_REQUEST_NO_WAKEUP) && kvm_vcpu_wake_up(vcpu))
		return;

	/*
	 * Note, the vCPU could get migrated to a different pCPU at any point
	 * after kvm_request_needs_ipi(), which could result in sending an IPI
	 * to the previous pCPU.  But, that's OK because the purpose of the IPI
	 * is to ensure the vCPU returns to OUTSIDE_GUEST_MODE, which is
	 * satisfied if the vCPU migrates. Entering READING_SHADOW_PAGE_TABLES
	 * after this point is also OK, as the requirement is only that KVM wait
	 * for vCPUs that were reading SPTEs _before_ any changes were
	 * finalized. See kvm_vcpu_kick() for more details on handling requests.
	 */
	if (kvm_request_needs_ipi(vcpu, req)) {
		cpu = READ_ONCE(vcpu->cpu);
		if (cpu != -1 && cpu != current_cpu)
			__cpumask_set_cpu(cpu, tmp);
	}
}

bool kvm_make_vcpus_request_mask(struct kvm *kvm, unsigned int req,
				 unsigned long *vcpu_bitmap)
{
	struct kvm_vcpu *vcpu;
	struct cpumask *cpus;
	int i, me;
	bool called;

	me = get_cpu();

	cpus = this_cpu_cpumask_var_ptr(cpu_kick_mask);
	cpumask_clear(cpus);

	for_each_set_bit(i, vcpu_bitmap, KVM_MAX_VCPUS) {
		vcpu = kvm_get_vcpu(kvm, i);
		if (!vcpu)
			continue;
		kvm_make_vcpu_request(vcpu, req, cpus, me);
	}

	called = kvm_kick_many_cpus(cpus, !!(req & KVM_REQUEST_WAIT));
	put_cpu();

	return called;
}

bool kvm_make_all_cpus_request(struct kvm *kvm, unsigned int req)
{
	struct kvm_vcpu *vcpu;
	struct cpumask *cpus;
	unsigned long i;
	bool called;
	int me;

	me = get_cpu();

	cpus = this_cpu_cpumask_var_ptr(cpu_kick_mask);
	cpumask_clear(cpus);

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_make_vcpu_request(vcpu, req, cpus, me);

	called = kvm_kick_many_cpus(cpus, !!(req & KVM_REQUEST_WAIT));
	put_cpu();

	return called;
}
EXPORT_SYMBOL_GPL(kvm_make_all_cpus_request);

void kvm_flush_remote_tlbs(struct kvm *kvm)
{
	++kvm->stat.generic.remote_tlb_flush_requests;

	/*
	 * We want to publish modifications to the page tables before reading
	 * mode. Pairs with a memory barrier in arch-specific code.
	 * - x86: smp_mb__after_srcu_read_unlock in vcpu_enter_guest
	 * and smp_mb in walk_shadow_page_lockless_begin/end.
	 * - powerpc: smp_mb in kvmppc_prepare_to_enter.
	 *
	 * There is already an smp_mb__after_atomic() before
	 * kvm_make_all_cpus_request() reads vcpu->mode. We reuse that
	 * barrier here.
	 */
	if (!kvm_arch_flush_remote_tlbs(kvm)
	    || kvm_make_all_cpus_request(kvm, KVM_REQ_TLB_FLUSH))
		++kvm->stat.generic.remote_tlb_flush;
}
EXPORT_SYMBOL_GPL(kvm_flush_remote_tlbs);

void kvm_flush_remote_tlbs_range(struct kvm *kvm, gfn_t gfn, u64 nr_pages)
{
	if (!kvm_arch_flush_remote_tlbs_range(kvm, gfn, nr_pages))
		return;

	/*
	 * Fall back to a flushing entire TLBs if the architecture range-based
	 * TLB invalidation is unsupported or can't be performed for whatever
	 * reason.
	 */
	kvm_flush_remote_tlbs(kvm);
}

void kvm_flush_remote_tlbs_memslot(struct kvm *kvm,
				   const struct kvm_memory_slot *memslot)
{
	/*
	 * All current use cases for flushing the TLBs for a specific memslot
	 * are related to dirty logging, and many do the TLB flush out of
	 * mmu_lock. The interaction between the various operations on memslot
	 * must be serialized by slots_locks to ensure the TLB flush from one
	 * operation is observed by any other operation on the same memslot.
	 */
	lockdep_assert_held(&kvm->slots_lock);
	kvm_flush_remote_tlbs_range(kvm, memslot->base_gfn, memslot->npages);
}

static void kvm_flush_shadow_all(struct kvm *kvm)
{
	kvm_arch_flush_shadow_all(kvm);
	kvm_arch_guest_memory_reclaimed(kvm);
}

#ifdef KVM_ARCH_NR_OBJS_PER_MEMORY_CACHE
static inline void *mmu_memory_cache_alloc_obj(struct kvm_mmu_memory_cache *mc,
					       gfp_t gfp_flags)
{
	void *page;

	gfp_flags |= mc->gfp_zero;

	if (mc->kmem_cache)
		return kmem_cache_alloc(mc->kmem_cache, gfp_flags);

	page = (void *)__get_free_page(gfp_flags);
	if (page && mc->init_value)
		memset64(page, mc->init_value, PAGE_SIZE / sizeof(u64));
	return page;
}

int __kvm_mmu_topup_memory_cache(struct kvm_mmu_memory_cache *mc, int capacity, int min)
{
	gfp_t gfp = mc->gfp_custom ? mc->gfp_custom : GFP_KERNEL_ACCOUNT;
	void *obj;

	if (mc->nobjs >= min)
		return 0;

	if (unlikely(!mc->objects)) {
		if (WARN_ON_ONCE(!capacity))
			return -EIO;

		/*
		 * Custom init values can be used only for page allocations,
		 * and obviously conflict with __GFP_ZERO.
		 */
		if (WARN_ON_ONCE(mc->init_value && (mc->kmem_cache || mc->gfp_zero)))
			return -EIO;

		mc->objects = kvmalloc_array(capacity, sizeof(void *), gfp);
		if (!mc->objects)
			return -ENOMEM;

		mc->capacity = capacity;
	}

	/* It is illegal to request a different capacity across topups. */
	if (WARN_ON_ONCE(mc->capacity != capacity))
		return -EIO;

	while (mc->nobjs < mc->capacity) {
		obj = mmu_memory_cache_alloc_obj(mc, gfp);
		if (!obj)
			return mc->nobjs >= min ? 0 : -ENOMEM;
		mc->objects[mc->nobjs++] = obj;
	}
	return 0;
}

int kvm_mmu_topup_memory_cache(struct kvm_mmu_memory_cache *mc, int min)
{
	return __kvm_mmu_topup_memory_cache(mc, KVM_ARCH_NR_OBJS_PER_MEMORY_CACHE, min);
}

int kvm_mmu_memory_cache_nr_free_objects(struct kvm_mmu_memory_cache *mc)
{
	return mc->nobjs;
}

void kvm_mmu_free_memory_cache(struct kvm_mmu_memory_cache *mc)
{
	while (mc->nobjs) {
		if (mc->kmem_cache)
			kmem_cache_free(mc->kmem_cache, mc->objects[--mc->nobjs]);
		else
			free_page((unsigned long)mc->objects[--mc->nobjs]);
	}

	kvfree(mc->objects);

	mc->objects = NULL;
	mc->capacity = 0;
}

void *kvm_mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc)
{
	void *p;

	if (WARN_ON(!mc->nobjs))
		p = mmu_memory_cache_alloc_obj(mc, GFP_ATOMIC | __GFP_ACCOUNT);
	else
		p = mc->objects[--mc->nobjs];
	BUG_ON(!p);
	return p;
}
#endif

static void kvm_vcpu_init(struct kvm_vcpu *vcpu, struct kvm *kvm, unsigned id)
{
	mutex_init(&vcpu->mutex);
	vcpu->cpu = -1;
	vcpu->kvm = kvm;
	vcpu->vcpu_id = id;
	vcpu->pid = NULL;
	rwlock_init(&vcpu->pid_lock);
#ifndef __KVM_HAVE_ARCH_WQP
	rcuwait_init(&vcpu->wait);
#endif
	kvm_async_pf_vcpu_init(vcpu);

	kvm_vcpu_set_in_spin_loop(vcpu, false);
	kvm_vcpu_set_dy_eligible(vcpu, false);
	vcpu->preempted = false;
	vcpu->ready = false;
	preempt_notifier_init(&vcpu->preempt_notifier, &kvm_preempt_ops);
	vcpu->last_used_slot = NULL;

	/* Fill the stats id string for the vcpu */
	snprintf(vcpu->stats_id, sizeof(vcpu->stats_id), "kvm-%d/vcpu-%d",
		 task_pid_nr(current), id);
}

static void kvm_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_arch_vcpu_destroy(vcpu);
	kvm_dirty_ring_free(&vcpu->dirty_ring);

	/*
	 * No need for rcu_read_lock as VCPU_RUN is the only place that changes
	 * the vcpu->pid pointer, and at destruction time all file descriptors
	 * are already gone.
	 */
	put_pid(vcpu->pid);

	free_page((unsigned long)vcpu->run);
	kmem_cache_free(kvm_vcpu_cache, vcpu);
}

void kvm_destroy_vcpus(struct kvm *kvm)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		kvm_vcpu_destroy(vcpu);
		xa_erase(&kvm->vcpu_array, i);
	}

	atomic_set(&kvm->online_vcpus, 0);
}
EXPORT_SYMBOL_GPL(kvm_destroy_vcpus);

#ifdef CONFIG_KVM_GENERIC_MMU_NOTIFIER
static inline struct kvm *mmu_notifier_to_kvm(struct mmu_notifier *mn)
{
	return container_of(mn, struct kvm, mmu_notifier);
}

typedef bool (*gfn_handler_t)(struct kvm *kvm, struct kvm_gfn_range *range);

typedef void (*on_lock_fn_t)(struct kvm *kvm);

struct kvm_mmu_notifier_range {
	/*
	 * 64-bit addresses, as KVM notifiers can operate on host virtual
	 * addresses (unsigned long) and guest physical addresses (64-bit).
	 */
	u64 start;
	u64 end;
	union kvm_mmu_notifier_arg arg;
	gfn_handler_t handler;
	on_lock_fn_t on_lock;
	bool flush_on_ret;
	bool may_block;
};

/*
 * The inner-most helper returns a tuple containing the return value from the
 * arch- and action-specific handler, plus a flag indicating whether or not at
 * least one memslot was found, i.e. if the handler found guest memory.
 *
 * Note, most notifiers are averse to booleans, so even though KVM tracks the
 * return from arch code as a bool, outer helpers will cast it to an int. :-(
 */
typedef struct kvm_mmu_notifier_return {
	bool ret;
	bool found_memslot;
} kvm_mn_ret_t;

/*
 * Use a dedicated stub instead of NULL to indicate that there is no callback
 * function/handler.  The compiler technically can't guarantee that a real
 * function will have a non-zero address, and so it will generate code to
 * check for !NULL, whereas comparing against a stub will be elided at compile
 * time (unless the compiler is getting long in the tooth, e.g. gcc 4.9).
 */
static void kvm_null_fn(void)
{

}
#define IS_KVM_NULL_FN(fn) ((fn) == (void *)kvm_null_fn)

/* Iterate over each memslot intersecting [start, last] (inclusive) range */
#define kvm_for_each_memslot_in_hva_range(node, slots, start, last)	     \
	for (node = interval_tree_iter_first(&slots->hva_tree, start, last); \
	     node;							     \
	     node = interval_tree_iter_next(node, start, last))	     \

static __always_inline kvm_mn_ret_t __kvm_handle_hva_range(struct kvm *kvm,
							   const struct kvm_mmu_notifier_range *range)
{
	struct kvm_mmu_notifier_return r = {
		.ret = false,
		.found_memslot = false,
	};
	struct kvm_gfn_range gfn_range;
	struct kvm_memory_slot *slot;
	struct kvm_memslots *slots;
	int i, idx;

	if (WARN_ON_ONCE(range->end <= range->start))
		return r;

	/* A null handler is allowed if and only if on_lock() is provided. */
	if (WARN_ON_ONCE(IS_KVM_NULL_FN(range->on_lock) &&
			 IS_KVM_NULL_FN(range->handler)))
		return r;

	idx = srcu_read_lock(&kvm->srcu);

	for (i = 0; i < kvm_arch_nr_memslot_as_ids(kvm); i++) {
		struct interval_tree_node *node;

		slots = __kvm_memslots(kvm, i);
		kvm_for_each_memslot_in_hva_range(node, slots,
						  range->start, range->end - 1) {
			unsigned long hva_start, hva_end;

			slot = container_of(node, struct kvm_memory_slot, hva_node[slots->node_idx]);
			hva_start = max_t(unsigned long, range->start, slot->userspace_addr);
			hva_end = min_t(unsigned long, range->end,
					slot->userspace_addr + (slot->npages << PAGE_SHIFT));

			/*
			 * To optimize for the likely case where the address
			 * range is covered by zero or one memslots, don't
			 * bother making these conditional (to avoid writes on
			 * the second or later invocation of the handler).
			 */
			gfn_range.arg = range->arg;
			gfn_range.may_block = range->may_block;
			/*
			 * HVA-based notifications aren't relevant to private
			 * mappings as they don't have a userspace mapping.
			 */
			gfn_range.attr_filter = KVM_FILTER_SHARED;

			/*
			 * {gfn(page) | page intersects with [hva_start, hva_end)} =
			 * {gfn_start, gfn_start+1, ..., gfn_end-1}.
			 */
			gfn_range.start = hva_to_gfn_memslot(hva_start, slot);
			gfn_range.end = hva_to_gfn_memslot(hva_end + PAGE_SIZE - 1, slot);
			gfn_range.slot = slot;

			if (!r.found_memslot) {
				r.found_memslot = true;
				KVM_MMU_LOCK(kvm);
				if (!IS_KVM_NULL_FN(range->on_lock))
					range->on_lock(kvm);

				if (IS_KVM_NULL_FN(range->handler))
					goto mmu_unlock;
			}
			r.ret |= range->handler(kvm, &gfn_range);
		}
	}

	if (range->flush_on_ret && r.ret)
		kvm_flush_remote_tlbs(kvm);

mmu_unlock:
	if (r.found_memslot)
		KVM_MMU_UNLOCK(kvm);

	srcu_read_unlock(&kvm->srcu, idx);

	return r;
}

static __always_inline int kvm_handle_hva_range(struct mmu_notifier *mn,
						unsigned long start,
						unsigned long end,
						gfn_handler_t handler,
						bool flush_on_ret)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	const struct kvm_mmu_notifier_range range = {
		.start		= start,
		.end		= end,
		.handler	= handler,
		.on_lock	= (void *)kvm_null_fn,
		.flush_on_ret	= flush_on_ret,
		.may_block	= false,
	};

	return __kvm_handle_hva_range(kvm, &range).ret;
}

static __always_inline int kvm_handle_hva_range_no_flush(struct mmu_notifier *mn,
							 unsigned long start,
							 unsigned long end,
							 gfn_handler_t handler)
{
	return kvm_handle_hva_range(mn, start, end, handler, false);
}

void kvm_mmu_invalidate_begin(struct kvm *kvm)
{
	lockdep_assert_held_write(&kvm->mmu_lock);
	/*
	 * The count increase must become visible at unlock time as no
	 * spte can be established without taking the mmu_lock and
	 * count is also read inside the mmu_lock critical section.
	 */
	kvm->mmu_invalidate_in_progress++;

	if (likely(kvm->mmu_invalidate_in_progress == 1)) {
		kvm->mmu_invalidate_range_start = INVALID_GPA;
		kvm->mmu_invalidate_range_end = INVALID_GPA;
	}
}

void kvm_mmu_invalidate_range_add(struct kvm *kvm, gfn_t start, gfn_t end)
{
	lockdep_assert_held_write(&kvm->mmu_lock);

	WARN_ON_ONCE(!kvm->mmu_invalidate_in_progress);

	if (likely(kvm->mmu_invalidate_range_start == INVALID_GPA)) {
		kvm->mmu_invalidate_range_start = start;
		kvm->mmu_invalidate_range_end = end;
	} else {
		/*
		 * Fully tracking multiple concurrent ranges has diminishing
		 * returns. Keep things simple and just find the minimal range
		 * which includes the current and new ranges. As there won't be
		 * enough information to subtract a range after its invalidate
		 * completes, any ranges invalidated concurrently will
		 * accumulate and persist until all outstanding invalidates
		 * complete.
		 */
		kvm->mmu_invalidate_range_start =
			min(kvm->mmu_invalidate_range_start, start);
		kvm->mmu_invalidate_range_end =
			max(kvm->mmu_invalidate_range_end, end);
	}
}

bool kvm_mmu_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range)
{
	kvm_mmu_invalidate_range_add(kvm, range->start, range->end);
	return kvm_unmap_gfn_range(kvm, range);
}

static int kvm_mmu_notifier_invalidate_range_start(struct mmu_notifier *mn,
					const struct mmu_notifier_range *range)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	const struct kvm_mmu_notifier_range hva_range = {
		.start		= range->start,
		.end		= range->end,
		.handler	= kvm_mmu_unmap_gfn_range,
		.on_lock	= kvm_mmu_invalidate_begin,
		.flush_on_ret	= true,
		.may_block	= mmu_notifier_range_blockable(range),
	};

	trace_kvm_unmap_hva_range(range->start, range->end);

	/*
	 * Prevent memslot modification between range_start() and range_end()
	 * so that conditionally locking provides the same result in both
	 * functions.  Without that guarantee, the mmu_invalidate_in_progress
	 * adjustments will be imbalanced.
	 *
	 * Pairs with the decrement in range_end().
	 */
	spin_lock(&kvm->mn_invalidate_lock);
	kvm->mn_active_invalidate_count++;
	spin_unlock(&kvm->mn_invalidate_lock);

	/*
	 * Invalidate pfn caches _before_ invalidating the secondary MMUs, i.e.
	 * before acquiring mmu_lock, to avoid holding mmu_lock while acquiring
	 * each cache's lock.  There are relatively few caches in existence at
	 * any given time, and the caches themselves can check for hva overlap,
	 * i.e. don't need to rely on memslot overlap checks for performance.
	 * Because this runs without holding mmu_lock, the pfn caches must use
	 * mn_active_invalidate_count (see above) instead of
	 * mmu_invalidate_in_progress.
	 */
	gfn_to_pfn_cache_invalidate_start(kvm, range->start, range->end);

	/*
	 * If one or more memslots were found and thus zapped, notify arch code
	 * that guest memory has been reclaimed.  This needs to be done *after*
	 * dropping mmu_lock, as x86's reclaim path is slooooow.
	 */
	if (__kvm_handle_hva_range(kvm, &hva_range).found_memslot)
		kvm_arch_guest_memory_reclaimed(kvm);

	return 0;
}

void kvm_mmu_invalidate_end(struct kvm *kvm)
{
	lockdep_assert_held_write(&kvm->mmu_lock);

	/*
	 * This sequence increase will notify the kvm page fault that
	 * the page that is going to be mapped in the spte could have
	 * been freed.
	 */
	kvm->mmu_invalidate_seq++;
	smp_wmb();
	/*
	 * The above sequence increase must be visible before the
	 * below count decrease, which is ensured by the smp_wmb above
	 * in conjunction with the smp_rmb in mmu_invalidate_retry().
	 */
	kvm->mmu_invalidate_in_progress--;
	KVM_BUG_ON(kvm->mmu_invalidate_in_progress < 0, kvm);

	/*
	 * Assert that at least one range was added between start() and end().
	 * Not adding a range isn't fatal, but it is a KVM bug.
	 */
	WARN_ON_ONCE(kvm->mmu_invalidate_range_start == INVALID_GPA);
}

static void kvm_mmu_notifier_invalidate_range_end(struct mmu_notifier *mn,
					const struct mmu_notifier_range *range)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	const struct kvm_mmu_notifier_range hva_range = {
		.start		= range->start,
		.end		= range->end,
		.handler	= (void *)kvm_null_fn,
		.on_lock	= kvm_mmu_invalidate_end,
		.flush_on_ret	= false,
		.may_block	= mmu_notifier_range_blockable(range),
	};
	bool wake;

	__kvm_handle_hva_range(kvm, &hva_range);

	/* Pairs with the increment in range_start(). */
	spin_lock(&kvm->mn_invalidate_lock);
	if (!WARN_ON_ONCE(!kvm->mn_active_invalidate_count))
		--kvm->mn_active_invalidate_count;
	wake = !kvm->mn_active_invalidate_count;
	spin_unlock(&kvm->mn_invalidate_lock);

	/*
	 * There can only be one waiter, since the wait happens under
	 * slots_lock.
	 */
	if (wake)
		rcuwait_wake_up(&kvm->mn_memslots_update_rcuwait);
}

static int kvm_mmu_notifier_clear_flush_young(struct mmu_notifier *mn,
					      struct mm_struct *mm,
					      unsigned long start,
					      unsigned long end)
{
	trace_kvm_age_hva(start, end);

	return kvm_handle_hva_range(mn, start, end, kvm_age_gfn,
				    !IS_ENABLED(CONFIG_KVM_ELIDE_TLB_FLUSH_IF_YOUNG));
}

static int kvm_mmu_notifier_clear_young(struct mmu_notifier *mn,
					struct mm_struct *mm,
					unsigned long start,
					unsigned long end)
{
	trace_kvm_age_hva(start, end);

	/*
	 * Even though we do not flush TLB, this will still adversely
	 * affect performance on pre-Haswell Intel EPT, where there is
	 * no EPT Access Bit to clear so that we have to tear down EPT
	 * tables instead. If we find this unacceptable, we can always
	 * add a parameter to kvm_age_hva so that it effectively doesn't
	 * do anything on clear_young.
	 *
	 * Also note that currently we never issue secondary TLB flushes
	 * from clear_young, leaving this job up to the regular system
	 * cadence. If we find this inaccurate, we might come up with a
	 * more sophisticated heuristic later.
	 */
	return kvm_handle_hva_range_no_flush(mn, start, end, kvm_age_gfn);
}

static int kvm_mmu_notifier_test_young(struct mmu_notifier *mn,
				       struct mm_struct *mm,
				       unsigned long address)
{
	trace_kvm_test_age_hva(address);

	return kvm_handle_hva_range_no_flush(mn, address, address + 1,
					     kvm_test_age_gfn);
}

static void kvm_mmu_notifier_release(struct mmu_notifier *mn,
				     struct mm_struct *mm)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	kvm_flush_shadow_all(kvm);
	srcu_read_unlock(&kvm->srcu, idx);
}

static const struct mmu_notifier_ops kvm_mmu_notifier_ops = {
	.invalidate_range_start	= kvm_mmu_notifier_invalidate_range_start,
	.invalidate_range_end	= kvm_mmu_notifier_invalidate_range_end,
	.clear_flush_young	= kvm_mmu_notifier_clear_flush_young,
	.clear_young		= kvm_mmu_notifier_clear_young,
	.test_young		= kvm_mmu_notifier_test_young,
	.release		= kvm_mmu_notifier_release,
};

static int kvm_init_mmu_notifier(struct kvm *kvm)
{
	kvm->mmu_notifier.ops = &kvm_mmu_notifier_ops;
	return mmu_notifier_register(&kvm->mmu_notifier, current->mm);
}

#else  /* !CONFIG_KVM_GENERIC_MMU_NOTIFIER */

static int kvm_init_mmu_notifier(struct kvm *kvm)
{
	return 0;
}

#endif /* CONFIG_KVM_GENERIC_MMU_NOTIFIER */

#ifdef CONFIG_HAVE_KVM_PM_NOTIFIER
static int kvm_pm_notifier_call(struct notifier_block *bl,
				unsigned long state,
				void *unused)
{
	struct kvm *kvm = container_of(bl, struct kvm, pm_notifier);

	return kvm_arch_pm_notifier(kvm, state);
}

static void kvm_init_pm_notifier(struct kvm *kvm)
{
	kvm->pm_notifier.notifier_call = kvm_pm_notifier_call;
	/* Suspend KVM before we suspend ftrace, RCU, etc. */
	kvm->pm_notifier.priority = INT_MAX;
	register_pm_notifier(&kvm->pm_notifier);
}

static void kvm_destroy_pm_notifier(struct kvm *kvm)
{
	unregister_pm_notifier(&kvm->pm_notifier);
}
#else /* !CONFIG_HAVE_KVM_PM_NOTIFIER */
static void kvm_init_pm_notifier(struct kvm *kvm)
{
}

static void kvm_destroy_pm_notifier(struct kvm *kvm)
{
}
#endif /* CONFIG_HAVE_KVM_PM_NOTIFIER */

static void kvm_destroy_dirty_bitmap(struct kvm_memory_slot *memslot)
{
	if (!memslot->dirty_bitmap)
		return;

	vfree(memslot->dirty_bitmap);
	memslot->dirty_bitmap = NULL;
}

/* This does not remove the slot from struct kvm_memslots data structures */
static void kvm_free_memslot(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	if (slot->flags & KVM_MEM_GUEST_MEMFD)
		kvm_gmem_unbind(slot);

	kvm_destroy_dirty_bitmap(slot);

	kvm_arch_free_memslot(kvm, slot);

	kfree(slot);
}

static void kvm_free_memslots(struct kvm *kvm, struct kvm_memslots *slots)
{
	struct hlist_node *idnode;
	struct kvm_memory_slot *memslot;
	int bkt;

	/*
	 * The same memslot objects live in both active and inactive sets,
	 * arbitrarily free using index '1' so the second invocation of this
	 * function isn't operating over a structure with dangling pointers
	 * (even though this function isn't actually touching them).
	 */
	if (!slots->node_idx)
		return;

	hash_for_each_safe(slots->id_hash, bkt, idnode, memslot, id_node[1])
		kvm_free_memslot(kvm, memslot);
}

static umode_t kvm_stats_debugfs_mode(const struct _kvm_stats_desc *pdesc)
{
	switch (pdesc->desc.flags & KVM_STATS_TYPE_MASK) {
	case KVM_STATS_TYPE_INSTANT:
		return 0444;
	case KVM_STATS_TYPE_CUMULATIVE:
	case KVM_STATS_TYPE_PEAK:
	default:
		return 0644;
	}
}


static void kvm_destroy_vm_debugfs(struct kvm *kvm)
{
	int i;
	int kvm_debugfs_num_entries = kvm_vm_stats_header.num_desc +
				      kvm_vcpu_stats_header.num_desc;

	if (IS_ERR(kvm->debugfs_dentry))
		return;

	debugfs_remove_recursive(kvm->debugfs_dentry);

	if (kvm->debugfs_stat_data) {
		for (i = 0; i < kvm_debugfs_num_entries; i++)
			kfree(kvm->debugfs_stat_data[i]);
		kfree(kvm->debugfs_stat_data);
	}
}

static int kvm_create_vm_debugfs(struct kvm *kvm, const char *fdname)
{
	static DEFINE_MUTEX(kvm_debugfs_lock);
	struct dentry *dent;
	char dir_name[ITOA_MAX_LEN * 2];
	struct kvm_stat_data *stat_data;
	const struct _kvm_stats_desc *pdesc;
	int i, ret = -ENOMEM;
	int kvm_debugfs_num_entries = kvm_vm_stats_header.num_desc +
				      kvm_vcpu_stats_header.num_desc;

	if (!debugfs_initialized())
		return 0;

	snprintf(dir_name, sizeof(dir_name), "%d-%s", task_pid_nr(current), fdname);
	mutex_lock(&kvm_debugfs_lock);
	dent = debugfs_lookup(dir_name, kvm_debugfs_dir);
	if (dent) {
		pr_warn_ratelimited("KVM: debugfs: duplicate directory %s\n", dir_name);
		dput(dent);
		mutex_unlock(&kvm_debugfs_lock);
		return 0;
	}
	dent = debugfs_create_dir(dir_name, kvm_debugfs_dir);
	mutex_unlock(&kvm_debugfs_lock);
	if (IS_ERR(dent))
		return 0;

	kvm->debugfs_dentry = dent;
	kvm->debugfs_stat_data = kcalloc(kvm_debugfs_num_entries,
					 sizeof(*kvm->debugfs_stat_data),
					 GFP_KERNEL_ACCOUNT);
	if (!kvm->debugfs_stat_data)
		goto out_err;

	for (i = 0; i < kvm_vm_stats_header.num_desc; ++i) {
		pdesc = &kvm_vm_stats_desc[i];
		stat_data = kzalloc(sizeof(*stat_data), GFP_KERNEL_ACCOUNT);
		if (!stat_data)
			goto out_err;

		stat_data->kvm = kvm;
		stat_data->desc = pdesc;
		stat_data->kind = KVM_STAT_VM;
		kvm->debugfs_stat_data[i] = stat_data;
		debugfs_create_file(pdesc->name, kvm_stats_debugfs_mode(pdesc),
				    kvm->debugfs_dentry, stat_data,
				    &stat_fops_per_vm);
	}

	for (i = 0; i < kvm_vcpu_stats_header.num_desc; ++i) {
		pdesc = &kvm_vcpu_stats_desc[i];
		stat_data = kzalloc(sizeof(*stat_data), GFP_KERNEL_ACCOUNT);
		if (!stat_data)
			goto out_err;

		stat_data->kvm = kvm;
		stat_data->desc = pdesc;
		stat_data->kind = KVM_STAT_VCPU;
		kvm->debugfs_stat_data[i + kvm_vm_stats_header.num_desc] = stat_data;
		debugfs_create_file(pdesc->name, kvm_stats_debugfs_mode(pdesc),
				    kvm->debugfs_dentry, stat_data,
				    &stat_fops_per_vm);
	}

	kvm_arch_create_vm_debugfs(kvm);
	return 0;
out_err:
	kvm_destroy_vm_debugfs(kvm);
	return ret;
}

/*
 * Called just after removing the VM from the vm_list, but before doing any
 * other destruction.
 */
void __weak kvm_arch_pre_destroy_vm(struct kvm *kvm)
{
}

/*
 * Called after per-vm debugfs created.  When called kvm->debugfs_dentry should
 * be setup already, so we can create arch-specific debugfs entries under it.
 * Cleanup should be automatic done in kvm_destroy_vm_debugfs() recursively, so
 * a per-arch destroy interface is not needed.
 */
void __weak kvm_arch_create_vm_debugfs(struct kvm *kvm)
{
}

static struct kvm *kvm_create_vm(unsigned long type, const char *fdname)
{
	struct kvm *kvm = kvm_arch_alloc_vm();
	struct kvm_memslots *slots;
	int r, i, j;

	if (!kvm)
		return ERR_PTR(-ENOMEM);

	KVM_MMU_LOCK_INIT(kvm);
	mmgrab(current->mm);
	kvm->mm = current->mm;
	kvm_eventfd_init(kvm);
	mutex_init(&kvm->lock);
	mutex_init(&kvm->irq_lock);
	mutex_init(&kvm->slots_lock);
	mutex_init(&kvm->slots_arch_lock);
	spin_lock_init(&kvm->mn_invalidate_lock);
	rcuwait_init(&kvm->mn_memslots_update_rcuwait);
	xa_init(&kvm->vcpu_array);
#ifdef CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES
	xa_init(&kvm->mem_attr_array);
#endif

	INIT_LIST_HEAD(&kvm->gpc_list);
	spin_lock_init(&kvm->gpc_lock);

	INIT_LIST_HEAD(&kvm->devices);
	kvm->max_vcpus = KVM_MAX_VCPUS;

	BUILD_BUG_ON(KVM_MEM_SLOTS_NUM > SHRT_MAX);

	/*
	 * Force subsequent debugfs file creations to fail if the VM directory
	 * is not created (by kvm_create_vm_debugfs()).
	 */
	kvm->debugfs_dentry = ERR_PTR(-ENOENT);

	snprintf(kvm->stats_id, sizeof(kvm->stats_id), "kvm-%d",
		 task_pid_nr(current));

	r = -ENOMEM;
	if (init_srcu_struct(&kvm->srcu))
		goto out_err_no_srcu;
	if (init_srcu_struct(&kvm->irq_srcu))
		goto out_err_no_irq_srcu;

	r = kvm_init_irq_routing(kvm);
	if (r)
		goto out_err_no_irq_routing;

	refcount_set(&kvm->users_count, 1);

	for (i = 0; i < kvm_arch_nr_memslot_as_ids(kvm); i++) {
		for (j = 0; j < 2; j++) {
			slots = &kvm->__memslots[i][j];

			atomic_long_set(&slots->last_used_slot, (unsigned long)NULL);
			slots->hva_tree = RB_ROOT_CACHED;
			slots->gfn_tree = RB_ROOT;
			hash_init(slots->id_hash);
			slots->node_idx = j;

			/* Generations must be different for each address space. */
			slots->generation = i;
		}

		rcu_assign_pointer(kvm->memslots[i], &kvm->__memslots[i][0]);
	}

	r = -ENOMEM;
	for (i = 0; i < KVM_NR_BUSES; i++) {
		rcu_assign_pointer(kvm->buses[i],
			kzalloc(sizeof(struct kvm_io_bus), GFP_KERNEL_ACCOUNT));
		if (!kvm->buses[i])
			goto out_err_no_arch_destroy_vm;
	}

	r = kvm_arch_init_vm(kvm, type);
	if (r)
		goto out_err_no_arch_destroy_vm;

	r = kvm_enable_virtualization();
	if (r)
		goto out_err_no_disable;

#ifdef CONFIG_HAVE_KVM_IRQCHIP
	INIT_HLIST_HEAD(&kvm->irq_ack_notifier_list);
#endif

	r = kvm_init_mmu_notifier(kvm);
	if (r)
		goto out_err_no_mmu_notifier;

	r = kvm_coalesced_mmio_init(kvm);
	if (r < 0)
		goto out_no_coalesced_mmio;

	r = kvm_create_vm_debugfs(kvm, fdname);
	if (r)
		goto out_err_no_debugfs;

	mutex_lock(&kvm_lock);
	list_add(&kvm->vm_list, &vm_list);
	mutex_unlock(&kvm_lock);

	preempt_notifier_inc();
	kvm_init_pm_notifier(kvm);

	return kvm;

out_err_no_debugfs:
	kvm_coalesced_mmio_free(kvm);
out_no_coalesced_mmio:
#ifdef CONFIG_KVM_GENERIC_MMU_NOTIFIER
	if (kvm->mmu_notifier.ops)
		mmu_notifier_unregister(&kvm->mmu_notifier, current->mm);
#endif
out_err_no_mmu_notifier:
	kvm_disable_virtualization();
out_err_no_disable:
	kvm_arch_destroy_vm(kvm);
out_err_no_arch_destroy_vm:
	WARN_ON_ONCE(!refcount_dec_and_test(&kvm->users_count));
	for (i = 0; i < KVM_NR_BUSES; i++)
		kfree(kvm_get_bus(kvm, i));
	kvm_free_irq_routing(kvm);
out_err_no_irq_routing:
	cleanup_srcu_struct(&kvm->irq_srcu);
out_err_no_irq_srcu:
	cleanup_srcu_struct(&kvm->srcu);
out_err_no_srcu:
	kvm_arch_free_vm(kvm);
	mmdrop(current->mm);
	return ERR_PTR(r);
}

static void kvm_destroy_devices(struct kvm *kvm)
{
	struct kvm_device *dev, *tmp;

	/*
	 * We do not need to take the kvm->lock here, because nobody else
	 * has a reference to the struct kvm at this point and therefore
	 * cannot access the devices list anyhow.
	 *
	 * The device list is generally managed as an rculist, but list_del()
	 * is used intentionally here. If a bug in KVM introduced a reader that
	 * was not backed by a reference on the kvm struct, the hope is that
	 * it'd consume the poisoned forward pointer instead of suffering a
	 * use-after-free, even though this cannot be guaranteed.
	 */
	list_for_each_entry_safe(dev, tmp, &kvm->devices, vm_node) {
		list_del(&dev->vm_node);
		dev->ops->destroy(dev);
	}
}

static void kvm_destroy_vm(struct kvm *kvm)
{
	int i;
	struct mm_struct *mm = kvm->mm;

	kvm_destroy_pm_notifier(kvm);
	kvm_uevent_notify_change(KVM_EVENT_DESTROY_VM, kvm);
	kvm_destroy_vm_debugfs(kvm);
	kvm_arch_sync_events(kvm);
	mutex_lock(&kvm_lock);
	list_del(&kvm->vm_list);
	mutex_unlock(&kvm_lock);
	kvm_arch_pre_destroy_vm(kvm);

	kvm_free_irq_routing(kvm);
	for (i = 0; i < KVM_NR_BUSES; i++) {
		struct kvm_io_bus *bus = kvm_get_bus(kvm, i);

		if (bus)
			kvm_io_bus_destroy(bus);
		kvm->buses[i] = NULL;
	}
	kvm_coalesced_mmio_free(kvm);
#ifdef CONFIG_KVM_GENERIC_MMU_NOTIFIER
	mmu_notifier_unregister(&kvm->mmu_notifier, kvm->mm);
	/*
	 * At this point, pending calls to invalidate_range_start()
	 * have completed but no more MMU notifiers will run, so
	 * mn_active_invalidate_count may remain unbalanced.
	 * No threads can be waiting in kvm_swap_active_memslots() as the
	 * last reference on KVM has been dropped, but freeing
	 * memslots would deadlock without this manual intervention.
	 *
	 * If the count isn't unbalanced, i.e. KVM did NOT unregister its MMU
	 * notifier between a start() and end(), then there shouldn't be any
	 * in-progress invalidations.
	 */
	WARN_ON(rcuwait_active(&kvm->mn_memslots_update_rcuwait));
	if (kvm->mn_active_invalidate_count)
		kvm->mn_active_invalidate_count = 0;
	else
		WARN_ON(kvm->mmu_invalidate_in_progress);
#else
	kvm_flush_shadow_all(kvm);
#endif
	kvm_arch_destroy_vm(kvm);
	kvm_destroy_devices(kvm);
	for (i = 0; i < kvm_arch_nr_memslot_as_ids(kvm); i++) {
		kvm_free_memslots(kvm, &kvm->__memslots[i][0]);
		kvm_free_memslots(kvm, &kvm->__memslots[i][1]);
	}
	cleanup_srcu_struct(&kvm->irq_srcu);
	cleanup_srcu_struct(&kvm->srcu);
#ifdef CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES
	xa_destroy(&kvm->mem_attr_array);
#endif
	kvm_arch_free_vm(kvm);
	preempt_notifier_dec();
	kvm_disable_virtualization();
	mmdrop(mm);
}

void kvm_get_kvm(struct kvm *kvm)
{
	refcount_inc(&kvm->users_count);
}
EXPORT_SYMBOL_GPL(kvm_get_kvm);

/*
 * Make sure the vm is not during destruction, which is a safe version of
 * kvm_get_kvm().  Return true if kvm referenced successfully, false otherwise.
 */
bool kvm_get_kvm_safe(struct kvm *kvm)
{
	return refcount_inc_not_zero(&kvm->users_count);
}
EXPORT_SYMBOL_GPL(kvm_get_kvm_safe);

void kvm_put_kvm(struct kvm *kvm)
{
	if (refcount_dec_and_test(&kvm->users_count))
		kvm_destroy_vm(kvm);
}
EXPORT_SYMBOL_GPL(kvm_put_kvm);

/*
 * Used to put a reference that was taken on behalf of an object associated
 * with a user-visible file descriptor, e.g. a vcpu or device, if installation
 * of the new file descriptor fails and the reference cannot be transferred to
 * its final owner.  In such cases, the caller is still actively using @kvm and
 * will fail miserably if the refcount unexpectedly hits zero.
 */
void kvm_put_kvm_no_destroy(struct kvm *kvm)
{
	WARN_ON(refcount_dec_and_test(&kvm->users_count));
}
EXPORT_SYMBOL_GPL(kvm_put_kvm_no_destroy);

static int kvm_vm_release(struct inode *inode, struct file *filp)
{
	struct kvm *kvm = filp->private_data;

	kvm_irqfd_release(kvm);

	kvm_put_kvm(kvm);
	return 0;
}

/*
 * Allocation size is twice as large as the actual dirty bitmap size.
 * See kvm_vm_ioctl_get_dirty_log() why this is needed.
 */
static int kvm_alloc_dirty_bitmap(struct kvm_memory_slot *memslot)
{
	unsigned long dirty_bytes = kvm_dirty_bitmap_bytes(memslot);

	memslot->dirty_bitmap = __vcalloc(2, dirty_bytes, GFP_KERNEL_ACCOUNT);
	if (!memslot->dirty_bitmap)
		return -ENOMEM;

	return 0;
}

static struct kvm_memslots *kvm_get_inactive_memslots(struct kvm *kvm, int as_id)
{
	struct kvm_memslots *active = __kvm_memslots(kvm, as_id);
	int node_idx_inactive = active->node_idx ^ 1;

	return &kvm->__memslots[as_id][node_idx_inactive];
}

/*
 * Helper to get the address space ID when one of memslot pointers may be NULL.
 * This also serves as a sanity that at least one of the pointers is non-NULL,
 * and that their address space IDs don't diverge.
 */
static int kvm_memslots_get_as_id(struct kvm_memory_slot *a,
				  struct kvm_memory_slot *b)
{
	if (WARN_ON_ONCE(!a && !b))
		return 0;

	if (!a)
		return b->as_id;
	if (!b)
		return a->as_id;

	WARN_ON_ONCE(a->as_id != b->as_id);
	return a->as_id;
}

static void kvm_insert_gfn_node(struct kvm_memslots *slots,
				struct kvm_memory_slot *slot)
{
	struct rb_root *gfn_tree = &slots->gfn_tree;
	struct rb_node **node, *parent;
	int idx = slots->node_idx;

	parent = NULL;
	for (node = &gfn_tree->rb_node; *node; ) {
		struct kvm_memory_slot *tmp;

		tmp = container_of(*node, struct kvm_memory_slot, gfn_node[idx]);
		parent = *node;
		if (slot->base_gfn < tmp->base_gfn)
			node = &(*node)->rb_left;
		else if (slot->base_gfn > tmp->base_gfn)
			node = &(*node)->rb_right;
		else
			BUG();
	}

	rb_link_node(&slot->gfn_node[idx], parent, node);
	rb_insert_color(&slot->gfn_node[idx], gfn_tree);
}

static void kvm_erase_gfn_node(struct kvm_memslots *slots,
			       struct kvm_memory_slot *slot)
{
	rb_erase(&slot->gfn_node[slots->node_idx], &slots->gfn_tree);
}

static void kvm_replace_gfn_node(struct kvm_memslots *slots,
				 struct kvm_memory_slot *old,
				 struct kvm_memory_slot *new)
{
	int idx = slots->node_idx;

	WARN_ON_ONCE(old->base_gfn != new->base_gfn);

	rb_replace_node(&old->gfn_node[idx], &new->gfn_node[idx],
			&slots->gfn_tree);
}

/*
 * Replace @old with @new in the inactive memslots.
 *
 * With NULL @old this simply adds @new.
 * With NULL @new this simply removes @old.
 *
 * If @new is non-NULL its hva_node[slots_idx] range has to be set
 * appropriately.
 */
static void kvm_replace_memslot(struct kvm *kvm,
				struct kvm_memory_slot *old,
				struct kvm_memory_slot *new)
{
	int as_id = kvm_memslots_get_as_id(old, new);
	struct kvm_memslots *slots = kvm_get_inactive_memslots(kvm, as_id);
	int idx = slots->node_idx;

	if (old) {
		hash_del(&old->id_node[idx]);
		interval_tree_remove(&old->hva_node[idx], &slots->hva_tree);

		if ((long)old == atomic_long_read(&slots->last_used_slot))
			atomic_long_set(&slots->last_used_slot, (long)new);

		if (!new) {
			kvm_erase_gfn_node(slots, old);
			return;
		}
	}

	/*
	 * Initialize @new's hva range.  Do this even when replacing an @old
	 * slot, kvm_copy_memslot() deliberately does not touch node data.
	 */
	new->hva_node[idx].start = new->userspace_addr;
	new->hva_node[idx].last = new->userspace_addr +
				  (new->npages << PAGE_SHIFT) - 1;

	/*
	 * (Re)Add the new memslot.  There is no O(1) interval_tree_replace(),
	 * hva_node needs to be swapped with remove+insert even though hva can't
	 * change when replacing an existing slot.
	 */
	hash_add(slots->id_hash, &new->id_node[idx], new->id);
	interval_tree_insert(&new->hva_node[idx], &slots->hva_tree);

	/*
	 * If the memslot gfn is unchanged, rb_replace_node() can be used to
	 * switch the node in the gfn tree instead of removing the old and
	 * inserting the new as two separate operations. Replacement is a
	 * single O(1) operation versus two O(log(n)) operations for
	 * remove+insert.
	 */
	if (old && old->base_gfn == new->base_gfn) {
		kvm_replace_gfn_node(slots, old, new);
	} else {
		if (old)
			kvm_erase_gfn_node(slots, old);
		kvm_insert_gfn_node(slots, new);
	}
}

/*
 * Flags that do not access any of the extra space of struct
 * kvm_userspace_memory_region2.  KVM_SET_USER_MEMORY_REGION_V1_FLAGS
 * only allows these.
 */
#define KVM_SET_USER_MEMORY_REGION_V1_FLAGS \
	(KVM_MEM_LOG_DIRTY_PAGES | KVM_MEM_READONLY)

static int check_memory_region_flags(struct kvm *kvm,
				     const struct kvm_userspace_memory_region2 *mem)
{
	u32 valid_flags = KVM_MEM_LOG_DIRTY_PAGES;

	if (kvm_arch_has_private_mem(kvm))
		valid_flags |= KVM_MEM_GUEST_MEMFD;

	/* Dirty logging private memory is not currently supported. */
	if (mem->flags & KVM_MEM_GUEST_MEMFD)
		valid_flags &= ~KVM_MEM_LOG_DIRTY_PAGES;

	/*
	 * GUEST_MEMFD is incompatible with read-only memslots, as writes to
	 * read-only memslots have emulated MMIO, not page fault, semantics,
	 * and KVM doesn't allow emulated MMIO for private memory.
	 */
	if (kvm_arch_has_readonly_mem(kvm) &&
	    !(mem->flags & KVM_MEM_GUEST_MEMFD))
		valid_flags |= KVM_MEM_READONLY;

	if (mem->flags & ~valid_flags)
		return -EINVAL;

	return 0;
}

static void kvm_swap_active_memslots(struct kvm *kvm, int as_id)
{
	struct kvm_memslots *slots = kvm_get_inactive_memslots(kvm, as_id);

	/* Grab the generation from the activate memslots. */
	u64 gen = __kvm_memslots(kvm, as_id)->generation;

	WARN_ON(gen & KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS);
	slots->generation = gen | KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS;

	/*
	 * Do not store the new memslots while there are invalidations in
	 * progress, otherwise the locking in invalidate_range_start and
	 * invalidate_range_end will be unbalanced.
	 */
	spin_lock(&kvm->mn_invalidate_lock);
	prepare_to_rcuwait(&kvm->mn_memslots_update_rcuwait);
	while (kvm->mn_active_invalidate_count) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock(&kvm->mn_invalidate_lock);
		schedule();
		spin_lock(&kvm->mn_invalidate_lock);
	}
	finish_rcuwait(&kvm->mn_memslots_update_rcuwait);
	rcu_assign_pointer(kvm->memslots[as_id], slots);
	spin_unlock(&kvm->mn_invalidate_lock);

	/*
	 * Acquired in kvm_set_memslot. Must be released before synchronize
	 * SRCU below in order to avoid deadlock with another thread
	 * acquiring the slots_arch_lock in an srcu critical section.
	 */
	mutex_unlock(&kvm->slots_arch_lock);

	synchronize_srcu_expedited(&kvm->srcu);

	/*
	 * Increment the new memslot generation a second time, dropping the
	 * update in-progress flag and incrementing the generation based on
	 * the number of address spaces.  This provides a unique and easily
	 * identifiable generation number while the memslots are in flux.
	 */
	gen = slots->generation & ~KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS;

	/*
	 * Generations must be unique even across address spaces.  We do not need
	 * a global counter for that, instead the generation space is evenly split
	 * across address spaces.  For example, with two address spaces, address
	 * space 0 will use generations 0, 2, 4, ... while address space 1 will
	 * use generations 1, 3, 5, ...
	 */
	gen += kvm_arch_nr_memslot_as_ids(kvm);

	kvm_arch_memslots_updated(kvm, gen);

	slots->generation = gen;
}

static int kvm_prepare_memory_region(struct kvm *kvm,
				     const struct kvm_memory_slot *old,
				     struct kvm_memory_slot *new,
				     enum kvm_mr_change change)
{
	int r;

	/*
	 * If dirty logging is disabled, nullify the bitmap; the old bitmap
	 * will be freed on "commit".  If logging is enabled in both old and
	 * new, reuse the existing bitmap.  If logging is enabled only in the
	 * new and KVM isn't using a ring buffer, allocate and initialize a
	 * new bitmap.
	 */
	if (change != KVM_MR_DELETE) {
		if (!(new->flags & KVM_MEM_LOG_DIRTY_PAGES))
			new->dirty_bitmap = NULL;
		else if (old && old->dirty_bitmap)
			new->dirty_bitmap = old->dirty_bitmap;
		else if (kvm_use_dirty_bitmap(kvm)) {
			r = kvm_alloc_dirty_bitmap(new);
			if (r)
				return r;

			if (kvm_dirty_log_manual_protect_and_init_set(kvm))
				bitmap_set(new->dirty_bitmap, 0, new->npages);
		}
	}

	r = kvm_arch_prepare_memory_region(kvm, old, new, change);

	/* Free the bitmap on failure if it was allocated above. */
	if (r && new && new->dirty_bitmap && (!old || !old->dirty_bitmap))
		kvm_destroy_dirty_bitmap(new);

	return r;
}

static void kvm_commit_memory_region(struct kvm *kvm,
				     struct kvm_memory_slot *old,
				     const struct kvm_memory_slot *new,
				     enum kvm_mr_change change)
{
	int old_flags = old ? old->flags : 0;
	int new_flags = new ? new->flags : 0;
	/*
	 * Update the total number of memslot pages before calling the arch
	 * hook so that architectures can consume the result directly.
	 */
	if (change == KVM_MR_DELETE)
		kvm->nr_memslot_pages -= old->npages;
	else if (change == KVM_MR_CREATE)
		kvm->nr_memslot_pages += new->npages;

	if ((old_flags ^ new_flags) & KVM_MEM_LOG_DIRTY_PAGES) {
		int change = (new_flags & KVM_MEM_LOG_DIRTY_PAGES) ? 1 : -1;
		atomic_set(&kvm->nr_memslots_dirty_logging,
			   atomic_read(&kvm->nr_memslots_dirty_logging) + change);
	}

	kvm_arch_commit_memory_region(kvm, old, new, change);

	switch (change) {
	case KVM_MR_CREATE:
		/* Nothing more to do. */
		break;
	case KVM_MR_DELETE:
		/* Free the old memslot and all its metadata. */
		kvm_free_memslot(kvm, old);
		break;
	case KVM_MR_MOVE:
	case KVM_MR_FLAGS_ONLY:
		/*
		 * Free the dirty bitmap as needed; the below check encompasses
		 * both the flags and whether a ring buffer is being used)
		 */
		if (old->dirty_bitmap && !new->dirty_bitmap)
			kvm_destroy_dirty_bitmap(old);

		/*
		 * The final quirk.  Free the detached, old slot, but only its
		 * memory, not any metadata.  Metadata, including arch specific
		 * data, may be reused by @new.
		 */
		kfree(old);
		break;
	default:
		BUG();
	}
}

/*
 * Activate @new, which must be installed in the inactive slots by the caller,
 * by swapping the active slots and then propagating @new to @old once @old is
 * unreachable and can be safely modified.
 *
 * With NULL @old this simply adds @new to @active (while swapping the sets).
 * With NULL @new this simply removes @old from @active and frees it
 * (while also swapping the sets).
 */
static void kvm_activate_memslot(struct kvm *kvm,
				 struct kvm_memory_slot *old,
				 struct kvm_memory_slot *new)
{
	int as_id = kvm_memslots_get_as_id(old, new);

	kvm_swap_active_memslots(kvm, as_id);

	/* Propagate the new memslot to the now inactive memslots. */
	kvm_replace_memslot(kvm, old, new);
}

static void kvm_copy_memslot(struct kvm_memory_slot *dest,
			     const struct kvm_memory_slot *src)
{
	dest->base_gfn = src->base_gfn;
	dest->npages = src->npages;
	dest->dirty_bitmap = src->dirty_bitmap;
	dest->arch = src->arch;
	dest->userspace_addr = src->userspace_addr;
	dest->flags = src->flags;
	dest->id = src->id;
	dest->as_id = src->as_id;
}

static void kvm_invalidate_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *old,
				   struct kvm_memory_slot *invalid_slot)
{
	/*
	 * Mark the current slot INVALID.  As with all memslot modifications,
	 * this must be done on an unreachable slot to avoid modifying the
	 * current slot in the active tree.
	 */
	kvm_copy_memslot(invalid_slot, old);
	invalid_slot->flags |= KVM_MEMSLOT_INVALID;
	kvm_replace_memslot(kvm, old, invalid_slot);

	/*
	 * Activate the slot that is now marked INVALID, but don't propagate
	 * the slot to the now inactive slots. The slot is either going to be
	 * deleted or recreated as a new slot.
	 */
	kvm_swap_active_memslots(kvm, old->as_id);

	/*
	 * From this point no new shadow pages pointing to a deleted, or moved,
	 * memslot will be created.  Validation of sp->gfn happens in:
	 *	- gfn_to_hva (kvm_read_guest, gfn_to_pfn)
	 *	- kvm_is_visible_gfn (mmu_check_root)
	 */
	kvm_arch_flush_shadow_memslot(kvm, old);
	kvm_arch_guest_memory_reclaimed(kvm);

	/* Was released by kvm_swap_active_memslots(), reacquire. */
	mutex_lock(&kvm->slots_arch_lock);

	/*
	 * Copy the arch-specific field of the newly-installed slot back to the
	 * old slot as the arch data could have changed between releasing
	 * slots_arch_lock in kvm_swap_active_memslots() and re-acquiring the lock
	 * above.  Writers are required to retrieve memslots *after* acquiring
	 * slots_arch_lock, thus the active slot's data is guaranteed to be fresh.
	 */
	old->arch = invalid_slot->arch;
}

static void kvm_create_memslot(struct kvm *kvm,
			       struct kvm_memory_slot *new)
{
	/* Add the new memslot to the inactive set and activate. */
	kvm_replace_memslot(kvm, NULL, new);
	kvm_activate_memslot(kvm, NULL, new);
}

static void kvm_delete_memslot(struct kvm *kvm,
			       struct kvm_memory_slot *old,
			       struct kvm_memory_slot *invalid_slot)
{
	/*
	 * Remove the old memslot (in the inactive memslots) by passing NULL as
	 * the "new" slot, and for the invalid version in the active slots.
	 */
	kvm_replace_memslot(kvm, old, NULL);
	kvm_activate_memslot(kvm, invalid_slot, NULL);
}

static void kvm_move_memslot(struct kvm *kvm,
			     struct kvm_memory_slot *old,
			     struct kvm_memory_slot *new,
			     struct kvm_memory_slot *invalid_slot)
{
	/*
	 * Replace the old memslot in the inactive slots, and then swap slots
	 * and replace the current INVALID with the new as well.
	 */
	kvm_replace_memslot(kvm, old, new);
	kvm_activate_memslot(kvm, invalid_slot, new);
}

static void kvm_update_flags_memslot(struct kvm *kvm,
				     struct kvm_memory_slot *old,
				     struct kvm_memory_slot *new)
{
	/*
	 * Similar to the MOVE case, but the slot doesn't need to be zapped as
	 * an intermediate step. Instead, the old memslot is simply replaced
	 * with a new, updated copy in both memslot sets.
	 */
	kvm_replace_memslot(kvm, old, new);
	kvm_activate_memslot(kvm, old, new);
}

static int kvm_set_memslot(struct kvm *kvm,
			   struct kvm_memory_slot *old,
			   struct kvm_memory_slot *new,
			   enum kvm_mr_change change)
{
	struct kvm_memory_slot *invalid_slot;
	int r;

	/*
	 * Released in kvm_swap_active_memslots().
	 *
	 * Must be held from before the current memslots are copied until after
	 * the new memslots are installed with rcu_assign_pointer, then
	 * released before the synchronize srcu in kvm_swap_active_memslots().
	 *
	 * When modifying memslots outside of the slots_lock, must be held
	 * before reading the pointer to the current memslots until after all
	 * changes to those memslots are complete.
	 *
	 * These rules ensure that installing new memslots does not lose
	 * changes made to the previous memslots.
	 */
	mutex_lock(&kvm->slots_arch_lock);

	/*
	 * Invalidate the old slot if it's being deleted or moved.  This is
	 * done prior to actually deleting/moving the memslot to allow vCPUs to
	 * continue running by ensuring there are no mappings or shadow pages
	 * for the memslot when it is deleted/moved.  Without pre-invalidation
	 * (and without a lock), a window would exist between effecting the
	 * delete/move and committing the changes in arch code where KVM or a
	 * guest could access a non-existent memslot.
	 *
	 * Modifications are done on a temporary, unreachable slot.  The old
	 * slot needs to be preserved in case a later step fails and the
	 * invalidation needs to be reverted.
	 */
	if (change == KVM_MR_DELETE || change == KVM_MR_MOVE) {
		invalid_slot = kzalloc(sizeof(*invalid_slot), GFP_KERNEL_ACCOUNT);
		if (!invalid_slot) {
			mutex_unlock(&kvm->slots_arch_lock);
			return -ENOMEM;
		}
		kvm_invalidate_memslot(kvm, old, invalid_slot);
	}

	r = kvm_prepare_memory_region(kvm, old, new, change);
	if (r) {
		/*
		 * For DELETE/MOVE, revert the above INVALID change.  No
		 * modifications required since the original slot was preserved
		 * in the inactive slots.  Changing the active memslots also
		 * release slots_arch_lock.
		 */
		if (change == KVM_MR_DELETE || change == KVM_MR_MOVE) {
			kvm_activate_memslot(kvm, invalid_slot, old);
			kfree(invalid_slot);
		} else {
			mutex_unlock(&kvm->slots_arch_lock);
		}
		return r;
	}

	/*
	 * For DELETE and MOVE, the working slot is now active as the INVALID
	 * version of the old slot.  MOVE is particularly special as it reuses
	 * the old slot and returns a copy of the old slot (in working_slot).
	 * For CREATE, there is no old slot.  For DELETE and FLAGS_ONLY, the
	 * old slot is detached but otherwise preserved.
	 */
	if (change == KVM_MR_CREATE)
		kvm_create_memslot(kvm, new);
	else if (change == KVM_MR_DELETE)
		kvm_delete_memslot(kvm, old, invalid_slot);
	else if (change == KVM_MR_MOVE)
		kvm_move_memslot(kvm, old, new, invalid_slot);
	else if (change == KVM_MR_FLAGS_ONLY)
		kvm_update_flags_memslot(kvm, old, new);
	else
		BUG();

	/* Free the temporary INVALID slot used for DELETE and MOVE. */
	if (change == KVM_MR_DELETE || change == KVM_MR_MOVE)
		kfree(invalid_slot);

	/*
	 * No need to refresh new->arch, changes after dropping slots_arch_lock
	 * will directly hit the final, active memslot.  Architectures are
	 * responsible for knowing that new->arch may be stale.
	 */
	kvm_commit_memory_region(kvm, old, new, change);

	return 0;
}

static bool kvm_check_memslot_overlap(struct kvm_memslots *slots, int id,
				      gfn_t start, gfn_t end)
{
	struct kvm_memslot_iter iter;

	kvm_for_each_memslot_in_gfn_range(&iter, slots, start, end) {
		if (iter.slot->id != id)
			return true;
	}

	return false;
}

static int kvm_set_memory_region(struct kvm *kvm,
				 const struct kvm_userspace_memory_region2 *mem)
{
	struct kvm_memory_slot *old, *new;
	struct kvm_memslots *slots;
	enum kvm_mr_change change;
	unsigned long npages;
	gfn_t base_gfn;
	int as_id, id;
	int r;

	lockdep_assert_held(&kvm->slots_lock);

	r = check_memory_region_flags(kvm, mem);
	if (r)
		return r;

	as_id = mem->slot >> 16;
	id = (u16)mem->slot;

	/* General sanity checks */
	if ((mem->memory_size & (PAGE_SIZE - 1)) ||
	    (mem->memory_size != (unsigned long)mem->memory_size))
		return -EINVAL;
	if (mem->guest_phys_addr & (PAGE_SIZE - 1))
		return -EINVAL;
	/* We can read the guest memory with __xxx_user() later on. */
	if ((mem->userspace_addr & (PAGE_SIZE - 1)) ||
	    (mem->userspace_addr != untagged_addr(mem->userspace_addr)) ||
	     !access_ok((void __user *)(unsigned long)mem->userspace_addr,
			mem->memory_size))
		return -EINVAL;
	if (mem->flags & KVM_MEM_GUEST_MEMFD &&
	    (mem->guest_memfd_offset & (PAGE_SIZE - 1) ||
	     mem->guest_memfd_offset + mem->memory_size < mem->guest_memfd_offset))
		return -EINVAL;
	if (as_id >= kvm_arch_nr_memslot_as_ids(kvm) || id >= KVM_MEM_SLOTS_NUM)
		return -EINVAL;
	if (mem->guest_phys_addr + mem->memory_size < mem->guest_phys_addr)
		return -EINVAL;

	/*
	 * The size of userspace-defined memory regions is restricted in order
	 * to play nice with dirty bitmap operations, which are indexed with an
	 * "unsigned int".  KVM's internal memory regions don't support dirty
	 * logging, and so are exempt.
	 */
	if (id < KVM_USER_MEM_SLOTS &&
	    (mem->memory_size >> PAGE_SHIFT) > KVM_MEM_MAX_NR_PAGES)
		return -EINVAL;

	slots = __kvm_memslots(kvm, as_id);

	/*
	 * Note, the old memslot (and the pointer itself!) may be invalidated
	 * and/or destroyed by kvm_set_memslot().
	 */
	old = id_to_memslot(slots, id);

	if (!mem->memory_size) {
		if (!old || !old->npages)
			return -EINVAL;

		if (WARN_ON_ONCE(kvm->nr_memslot_pages < old->npages))
			return -EIO;

		return kvm_set_memslot(kvm, old, NULL, KVM_MR_DELETE);
	}

	base_gfn = (mem->guest_phys_addr >> PAGE_SHIFT);
	npages = (mem->memory_size >> PAGE_SHIFT);

	if (!old || !old->npages) {
		change = KVM_MR_CREATE;

		/*
		 * To simplify KVM internals, the total number of pages across
		 * all memslots must fit in an unsigned long.
		 */
		if ((kvm->nr_memslot_pages + npages) < kvm->nr_memslot_pages)
			return -EINVAL;
	} else { /* Modify an existing slot. */
		/* Private memslots are immutable, they can only be deleted. */
		if (mem->flags & KVM_MEM_GUEST_MEMFD)
			return -EINVAL;
		if ((mem->userspace_addr != old->userspace_addr) ||
		    (npages != old->npages) ||
		    ((mem->flags ^ old->flags) & KVM_MEM_READONLY))
			return -EINVAL;

		if (base_gfn != old->base_gfn)
			change = KVM_MR_MOVE;
		else if (mem->flags != old->flags)
			change = KVM_MR_FLAGS_ONLY;
		else /* Nothing to change. */
			return 0;
	}

	if ((change == KVM_MR_CREATE || change == KVM_MR_MOVE) &&
	    kvm_check_memslot_overlap(slots, id, base_gfn, base_gfn + npages))
		return -EEXIST;

	/* Allocate a slot that will persist in the memslot. */
	new = kzalloc(sizeof(*new), GFP_KERNEL_ACCOUNT);
	if (!new)
		return -ENOMEM;

	new->as_id = as_id;
	new->id = id;
	new->base_gfn = base_gfn;
	new->npages = npages;
	new->flags = mem->flags;
	new->userspace_addr = mem->userspace_addr;
	if (mem->flags & KVM_MEM_GUEST_MEMFD) {
		r = kvm_gmem_bind(kvm, new, mem->guest_memfd, mem->guest_memfd_offset);
		if (r)
			goto out;
	}

	r = kvm_set_memslot(kvm, old, new, change);
	if (r)
		goto out_unbind;

	return 0;

out_unbind:
	if (mem->flags & KVM_MEM_GUEST_MEMFD)
		kvm_gmem_unbind(new);
out:
	kfree(new);
	return r;
}

int kvm_set_internal_memslot(struct kvm *kvm,
			     const struct kvm_userspace_memory_region2 *mem)
{
	if (WARN_ON_ONCE(mem->slot < KVM_USER_MEM_SLOTS))
		return -EINVAL;

	if (WARN_ON_ONCE(mem->flags))
		return -EINVAL;

	return kvm_set_memory_region(kvm, mem);
}
EXPORT_SYMBOL_GPL(kvm_set_internal_memslot);

static int kvm_vm_ioctl_set_memory_region(struct kvm *kvm,
					  struct kvm_userspace_memory_region2 *mem)
{
	if ((u16)mem->slot >= KVM_USER_MEM_SLOTS)
		return -EINVAL;

	guard(mutex)(&kvm->slots_lock);
	return kvm_set_memory_region(kvm, mem);
}

#ifndef CONFIG_KVM_GENERIC_DIRTYLOG_READ_PROTECT
/**
 * kvm_get_dirty_log - get a snapshot of dirty pages
 * @kvm:	pointer to kvm instance
 * @log:	slot id and address to which we copy the log
 * @is_dirty:	set to '1' if any dirty pages were found
 * @memslot:	set to the associated memslot, always valid on success
 */
int kvm_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log,
		      int *is_dirty, struct kvm_memory_slot **memslot)
{
	struct kvm_memslots *slots;
	int i, as_id, id;
	unsigned long n;
	unsigned long any = 0;

	/* Dirty ring tracking may be exclusive to dirty log tracking */
	if (!kvm_use_dirty_bitmap(kvm))
		return -ENXIO;

	*memslot = NULL;
	*is_dirty = 0;

	as_id = log->slot >> 16;
	id = (u16)log->slot;
	if (as_id >= kvm_arch_nr_memslot_as_ids(kvm) || id >= KVM_USER_MEM_SLOTS)
		return -EINVAL;

	slots = __kvm_memslots(kvm, as_id);
	*memslot = id_to_memslot(slots, id);
	if (!(*memslot) || !(*memslot)->dirty_bitmap)
		return -ENOENT;

	kvm_arch_sync_dirty_log(kvm, *memslot);

	n = kvm_dirty_bitmap_bytes(*memslot);

	for (i = 0; !any && i < n/sizeof(long); ++i)
		any = (*memslot)->dirty_bitmap[i];

	if (copy_to_user(log->dirty_bitmap, (*memslot)->dirty_bitmap, n))
		return -EFAULT;

	if (any)
		*is_dirty = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_get_dirty_log);

#else /* CONFIG_KVM_GENERIC_DIRTYLOG_READ_PROTECT */
/**
 * kvm_get_dirty_log_protect - get a snapshot of dirty pages
 *	and reenable dirty page tracking for the corresponding pages.
 * @kvm:	pointer to kvm instance
 * @log:	slot id and address to which we copy the log
 *
 * We need to keep it in mind that VCPU threads can write to the bitmap
 * concurrently. So, to avoid losing track of dirty pages we keep the
 * following order:
 *
 *    1. Take a snapshot of the bit and clear it if needed.
 *    2. Write protect the corresponding page.
 *    3. Copy the snapshot to the userspace.
 *    4. Upon return caller flushes TLB's if needed.
 *
 * Between 2 and 4, the guest may write to the page using the remaining TLB
 * entry.  This is not a problem because the page is reported dirty using
 * the snapshot taken before and step 4 ensures that writes done after
 * exiting to userspace will be logged for the next call.
 *
 */
static int kvm_get_dirty_log_protect(struct kvm *kvm, struct kvm_dirty_log *log)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int i, as_id, id;
	unsigned long n;
	unsigned long *dirty_bitmap;
	unsigned long *dirty_bitmap_buffer;
	bool flush;

	/* Dirty ring tracking may be exclusive to dirty log tracking */
	if (!kvm_use_dirty_bitmap(kvm))
		return -ENXIO;

	as_id = log->slot >> 16;
	id = (u16)log->slot;
	if (as_id >= kvm_arch_nr_memslot_as_ids(kvm) || id >= KVM_USER_MEM_SLOTS)
		return -EINVAL;

	slots = __kvm_memslots(kvm, as_id);
	memslot = id_to_memslot(slots, id);
	if (!memslot || !memslot->dirty_bitmap)
		return -ENOENT;

	dirty_bitmap = memslot->dirty_bitmap;

	kvm_arch_sync_dirty_log(kvm, memslot);

	n = kvm_dirty_bitmap_bytes(memslot);
	flush = false;
	if (kvm->manual_dirty_log_protect) {
		/*
		 * Unlike kvm_get_dirty_log, we always return false in *flush,
		 * because no flush is needed until KVM_CLEAR_DIRTY_LOG.  There
		 * is some code duplication between this function and
		 * kvm_get_dirty_log, but hopefully all architecture
		 * transition to kvm_get_dirty_log_protect and kvm_get_dirty_log
		 * can be eliminated.
		 */
		dirty_bitmap_buffer = dirty_bitmap;
	} else {
		dirty_bitmap_buffer = kvm_second_dirty_bitmap(memslot);
		memset(dirty_bitmap_buffer, 0, n);

		KVM_MMU_LOCK(kvm);
		for (i = 0; i < n / sizeof(long); i++) {
			unsigned long mask;
			gfn_t offset;

			if (!dirty_bitmap[i])
				continue;

			flush = true;
			mask = xchg(&dirty_bitmap[i], 0);
			dirty_bitmap_buffer[i] = mask;

			offset = i * BITS_PER_LONG;
			kvm_arch_mmu_enable_log_dirty_pt_masked(kvm, memslot,
								offset, mask);
		}
		KVM_MMU_UNLOCK(kvm);
	}

	if (flush)
		kvm_flush_remote_tlbs_memslot(kvm, memslot);

	if (copy_to_user(log->dirty_bitmap, dirty_bitmap_buffer, n))
		return -EFAULT;
	return 0;
}


/**
 * kvm_vm_ioctl_get_dirty_log - get and clear the log of dirty pages in a slot
 * @kvm: kvm instance
 * @log: slot id and address to which we copy the log
 *
 * Steps 1-4 below provide general overview of dirty page logging. See
 * kvm_get_dirty_log_protect() function description for additional details.
 *
 * We call kvm_get_dirty_log_protect() to handle steps 1-3, upon return we
 * always flush the TLB (step 4) even if previous step failed  and the dirty
 * bitmap may be corrupt. Regardless of previous outcome the KVM logging API
 * does not preclude user space subsequent dirty log read. Flushing TLB ensures
 * writes will be marked dirty for next log read.
 *
 *   1. Take a snapshot of the bit and clear it if needed.
 *   2. Write protect the corresponding page.
 *   3. Copy the snapshot to the userspace.
 *   4. Flush TLB's if needed.
 */
static int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
				      struct kvm_dirty_log *log)
{
	int r;

	mutex_lock(&kvm->slots_lock);

	r = kvm_get_dirty_log_protect(kvm, log);

	mutex_unlock(&kvm->slots_lock);
	return r;
}

/**
 * kvm_clear_dirty_log_protect - clear dirty bits in the bitmap
 *	and reenable dirty page tracking for the corresponding pages.
 * @kvm:	pointer to kvm instance
 * @log:	slot id and address from which to fetch the bitmap of dirty pages
 */
static int kvm_clear_dirty_log_protect(struct kvm *kvm,
				       struct kvm_clear_dirty_log *log)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int as_id, id;
	gfn_t offset;
	unsigned long i, n;
	unsigned long *dirty_bitmap;
	unsigned long *dirty_bitmap_buffer;
	bool flush;

	/* Dirty ring tracking may be exclusive to dirty log tracking */
	if (!kvm_use_dirty_bitmap(kvm))
		return -ENXIO;

	as_id = log->slot >> 16;
	id = (u16)log->slot;
	if (as_id >= kvm_arch_nr_memslot_as_ids(kvm) || id >= KVM_USER_MEM_SLOTS)
		return -EINVAL;

	if (log->first_page & 63)
		return -EINVAL;

	slots = __kvm_memslots(kvm, as_id);
	memslot = id_to_memslot(slots, id);
	if (!memslot || !memslot->dirty_bitmap)
		return -ENOENT;

	dirty_bitmap = memslot->dirty_bitmap;

	n = ALIGN(log->num_pages, BITS_PER_LONG) / 8;

	if (log->first_page > memslot->npages ||
	    log->num_pages > memslot->npages - log->first_page ||
	    (log->num_pages < memslot->npages - log->first_page && (log->num_pages & 63)))
	    return -EINVAL;

	kvm_arch_sync_dirty_log(kvm, memslot);

	flush = false;
	dirty_bitmap_buffer = kvm_second_dirty_bitmap(memslot);
	if (copy_from_user(dirty_bitmap_buffer, log->dirty_bitmap, n))
		return -EFAULT;

	KVM_MMU_LOCK(kvm);
	for (offset = log->first_page, i = offset / BITS_PER_LONG,
		 n = DIV_ROUND_UP(log->num_pages, BITS_PER_LONG); n--;
	     i++, offset += BITS_PER_LONG) {
		unsigned long mask = *dirty_bitmap_buffer++;
		atomic_long_t *p = (atomic_long_t *) &dirty_bitmap[i];
		if (!mask)
			continue;

		mask &= atomic_long_fetch_andnot(mask, p);

		/*
		 * mask contains the bits that really have been cleared.  This
		 * never includes any bits beyond the length of the memslot (if
		 * the length is not aligned to 64 pages), therefore it is not
		 * a problem if userspace sets them in log->dirty_bitmap.
		*/
		if (mask) {
			flush = true;
			kvm_arch_mmu_enable_log_dirty_pt_masked(kvm, memslot,
								offset, mask);
		}
	}
	KVM_MMU_UNLOCK(kvm);

	if (flush)
		kvm_flush_remote_tlbs_memslot(kvm, memslot);

	return 0;
}

static int kvm_vm_ioctl_clear_dirty_log(struct kvm *kvm,
					struct kvm_clear_dirty_log *log)
{
	int r;

	mutex_lock(&kvm->slots_lock);

	r = kvm_clear_dirty_log_protect(kvm, log);

	mutex_unlock(&kvm->slots_lock);
	return r;
}
#endif /* CONFIG_KVM_GENERIC_DIRTYLOG_READ_PROTECT */

#ifdef CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES
static u64 kvm_supported_mem_attributes(struct kvm *kvm)
{
	if (!kvm || kvm_arch_has_private_mem(kvm))
		return KVM_MEMORY_ATTRIBUTE_PRIVATE;

	return 0;
}

/*
 * Returns true if _all_ gfns in the range [@start, @end) have attributes
 * such that the bits in @mask match @attrs.
 */
bool kvm_range_has_memory_attributes(struct kvm *kvm, gfn_t start, gfn_t end,
				     unsigned long mask, unsigned long attrs)
{
	XA_STATE(xas, &kvm->mem_attr_array, start);
	unsigned long index;
	void *entry;

	mask &= kvm_supported_mem_attributes(kvm);
	if (attrs & ~mask)
		return false;

	if (end == start + 1)
		return (kvm_get_memory_attributes(kvm, start) & mask) == attrs;

	guard(rcu)();
	if (!attrs)
		return !xas_find(&xas, end - 1);

	for (index = start; index < end; index++) {
		do {
			entry = xas_next(&xas);
		} while (xas_retry(&xas, entry));

		if (xas.xa_index != index ||
		    (xa_to_value(entry) & mask) != attrs)
			return false;
	}

	return true;
}

static __always_inline void kvm_handle_gfn_range(struct kvm *kvm,
						 struct kvm_mmu_notifier_range *range)
{
	struct kvm_gfn_range gfn_range;
	struct kvm_memory_slot *slot;
	struct kvm_memslots *slots;
	struct kvm_memslot_iter iter;
	bool found_memslot = false;
	bool ret = false;
	int i;

	gfn_range.arg = range->arg;
	gfn_range.may_block = range->may_block;

	/*
	 * If/when KVM supports more attributes beyond private .vs shared, this
	 * _could_ set KVM_FILTER_{SHARED,PRIVATE} appropriately if the entire target
	 * range already has the desired private vs. shared state (it's unclear
	 * if that is a net win).  For now, KVM reaches this point if and only
	 * if the private flag is being toggled, i.e. all mappings are in play.
	 */

	for (i = 0; i < kvm_arch_nr_memslot_as_ids(kvm); i++) {
		slots = __kvm_memslots(kvm, i);

		kvm_for_each_memslot_in_gfn_range(&iter, slots, range->start, range->end) {
			slot = iter.slot;
			gfn_range.slot = slot;

			gfn_range.start = max(range->start, slot->base_gfn);
			gfn_range.end = min(range->end, slot->base_gfn + slot->npages);
			if (gfn_range.start >= gfn_range.end)
				continue;

			if (!found_memslot) {
				found_memslot = true;
				KVM_MMU_LOCK(kvm);
				if (!IS_KVM_NULL_FN(range->on_lock))
					range->on_lock(kvm);
			}

			ret |= range->handler(kvm, &gfn_range);
		}
	}

	if (range->flush_on_ret && ret)
		kvm_flush_remote_tlbs(kvm);

	if (found_memslot)
		KVM_MMU_UNLOCK(kvm);
}

static bool kvm_pre_set_memory_attributes(struct kvm *kvm,
					  struct kvm_gfn_range *range)
{
	/*
	 * Unconditionally add the range to the invalidation set, regardless of
	 * whether or not the arch callback actually needs to zap SPTEs.  E.g.
	 * if KVM supports RWX attributes in the future and the attributes are
	 * going from R=>RW, zapping isn't strictly necessary.  Unconditionally
	 * adding the range allows KVM to require that MMU invalidations add at
	 * least one range between begin() and end(), e.g. allows KVM to detect
	 * bugs where the add() is missed.  Relaxing the rule *might* be safe,
	 * but it's not obvious that allowing new mappings while the attributes
	 * are in flux is desirable or worth the complexity.
	 */
	kvm_mmu_invalidate_range_add(kvm, range->start, range->end);

	return kvm_arch_pre_set_memory_attributes(kvm, range);
}

/* Set @attributes for the gfn range [@start, @end). */
static int kvm_vm_set_mem_attributes(struct kvm *kvm, gfn_t start, gfn_t end,
				     unsigned long attributes)
{
	struct kvm_mmu_notifier_range pre_set_range = {
		.start = start,
		.end = end,
		.arg.attributes = attributes,
		.handler = kvm_pre_set_memory_attributes,
		.on_lock = kvm_mmu_invalidate_begin,
		.flush_on_ret = true,
		.may_block = true,
	};
	struct kvm_mmu_notifier_range post_set_range = {
		.start = start,
		.end = end,
		.arg.attributes = attributes,
		.handler = kvm_arch_post_set_memory_attributes,
		.on_lock = kvm_mmu_invalidate_end,
		.may_block = true,
	};
	unsigned long i;
	void *entry;
	int r = 0;

	entry = attributes ? xa_mk_value(attributes) : NULL;

	mutex_lock(&kvm->slots_lock);

	/* Nothing to do if the entire range as the desired attributes. */
	if (kvm_range_has_memory_attributes(kvm, start, end, ~0, attributes))
		goto out_unlock;

	/*
	 * Reserve memory ahead of time to avoid having to deal with failures
	 * partway through setting the new attributes.
	 */
	for (i = start; i < end; i++) {
		r = xa_reserve(&kvm->mem_attr_array, i, GFP_KERNEL_ACCOUNT);
		if (r)
			goto out_unlock;
	}

	kvm_handle_gfn_range(kvm, &pre_set_range);

	for (i = start; i < end; i++) {
		r = xa_err(xa_store(&kvm->mem_attr_array, i, entry,
				    GFP_KERNEL_ACCOUNT));
		KVM_BUG_ON(r, kvm);
	}

	kvm_handle_gfn_range(kvm, &post_set_range);

out_unlock:
	mutex_unlock(&kvm->slots_lock);

	return r;
}
static int kvm_vm_ioctl_set_mem_attributes(struct kvm *kvm,
					   struct kvm_memory_attributes *attrs)
{
	gfn_t start, end;

	/* flags is currently not used. */
	if (attrs->flags)
		return -EINVAL;
	if (attrs->attributes & ~kvm_supported_mem_attributes(kvm))
		return -EINVAL;
	if (attrs->size == 0 || attrs->address + attrs->size < attrs->address)
		return -EINVAL;
	if (!PAGE_ALIGNED(attrs->address) || !PAGE_ALIGNED(attrs->size))
		return -EINVAL;

	start = attrs->address >> PAGE_SHIFT;
	end = (attrs->address + attrs->size) >> PAGE_SHIFT;

	/*
	 * xarray tracks data using "unsigned long", and as a result so does
	 * KVM.  For simplicity, supports generic attributes only on 64-bit
	 * architectures.
	 */
	BUILD_BUG_ON(sizeof(attrs->attributes) != sizeof(unsigned long));

	return kvm_vm_set_mem_attributes(kvm, start, end, attrs->attributes);
}
#endif /* CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES */

struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn)
{
	return __gfn_to_memslot(kvm_memslots(kvm), gfn);
}
EXPORT_SYMBOL_GPL(gfn_to_memslot);

struct kvm_memory_slot *kvm_vcpu_gfn_to_memslot(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	struct kvm_memslots *slots = kvm_vcpu_memslots(vcpu);
	u64 gen = slots->generation;
	struct kvm_memory_slot *slot;

	/*
	 * This also protects against using a memslot from a different address space,
	 * since different address spaces have different generation numbers.
	 */
	if (unlikely(gen != vcpu->last_used_slot_gen)) {
		vcpu->last_used_slot = NULL;
		vcpu->last_used_slot_gen = gen;
	}

	slot = try_get_memslot(vcpu->last_used_slot, gfn);
	if (slot)
		return slot;

	/*
	 * Fall back to searching all memslots. We purposely use
	 * search_memslots() instead of __gfn_to_memslot() to avoid
	 * thrashing the VM-wide last_used_slot in kvm_memslots.
	 */
	slot = search_memslots(slots, gfn, false);
	if (slot) {
		vcpu->last_used_slot = slot;
		return slot;
	}

	return NULL;
}

bool kvm_is_visible_gfn(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *memslot = gfn_to_memslot(kvm, gfn);

	return kvm_is_visible_memslot(memslot);
}
EXPORT_SYMBOL_GPL(kvm_is_visible_gfn);

bool kvm_vcpu_is_visible_gfn(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	struct kvm_memory_slot *memslot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);

	return kvm_is_visible_memslot(memslot);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_is_visible_gfn);

unsigned long kvm_host_page_size(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	struct vm_area_struct *vma;
	unsigned long addr, size;

	size = PAGE_SIZE;

	addr = kvm_vcpu_gfn_to_hva_prot(vcpu, gfn, NULL);
	if (kvm_is_error_hva(addr))
		return PAGE_SIZE;

	mmap_read_lock(current->mm);
	vma = find_vma(current->mm, addr);
	if (!vma)
		goto out;

	size = vma_kernel_pagesize(vma);

out:
	mmap_read_unlock(current->mm);

	return size;
}

static bool memslot_is_readonly(const struct kvm_memory_slot *slot)
{
	return slot->flags & KVM_MEM_READONLY;
}

static unsigned long __gfn_to_hva_many(const struct kvm_memory_slot *slot, gfn_t gfn,
				       gfn_t *nr_pages, bool write)
{
	if (!slot || slot->flags & KVM_MEMSLOT_INVALID)
		return KVM_HVA_ERR_BAD;

	if (memslot_is_readonly(slot) && write)
		return KVM_HVA_ERR_RO_BAD;

	if (nr_pages)
		*nr_pages = slot->npages - (gfn - slot->base_gfn);

	return __gfn_to_hva_memslot(slot, gfn);
}

static unsigned long gfn_to_hva_many(struct kvm_memory_slot *slot, gfn_t gfn,
				     gfn_t *nr_pages)
{
	return __gfn_to_hva_many(slot, gfn, nr_pages, true);
}

unsigned long gfn_to_hva_memslot(struct kvm_memory_slot *slot,
					gfn_t gfn)
{
	return gfn_to_hva_many(slot, gfn, NULL);
}
EXPORT_SYMBOL_GPL(gfn_to_hva_memslot);

unsigned long gfn_to_hva(struct kvm *kvm, gfn_t gfn)
{
	return gfn_to_hva_many(gfn_to_memslot(kvm, gfn), gfn, NULL);
}
EXPORT_SYMBOL_GPL(gfn_to_hva);

unsigned long kvm_vcpu_gfn_to_hva(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	return gfn_to_hva_many(kvm_vcpu_gfn_to_memslot(vcpu, gfn), gfn, NULL);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_gfn_to_hva);

/*
 * Return the hva of a @gfn and the R/W attribute if possible.
 *
 * @slot: the kvm_memory_slot which contains @gfn
 * @gfn: the gfn to be translated
 * @writable: used to return the read/write attribute of the @slot if the hva
 * is valid and @writable is not NULL
 */
unsigned long gfn_to_hva_memslot_prot(struct kvm_memory_slot *slot,
				      gfn_t gfn, bool *writable)
{
	unsigned long hva = __gfn_to_hva_many(slot, gfn, NULL, false);

	if (!kvm_is_error_hva(hva) && writable)
		*writable = !memslot_is_readonly(slot);

	return hva;
}

unsigned long gfn_to_hva_prot(struct kvm *kvm, gfn_t gfn, bool *writable)
{
	struct kvm_memory_slot *slot = gfn_to_memslot(kvm, gfn);

	return gfn_to_hva_memslot_prot(slot, gfn, writable);
}

unsigned long kvm_vcpu_gfn_to_hva_prot(struct kvm_vcpu *vcpu, gfn_t gfn, bool *writable)
{
	struct kvm_memory_slot *slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);

	return gfn_to_hva_memslot_prot(slot, gfn, writable);
}

static bool kvm_is_ad_tracked_page(struct page *page)
{
	/*
	 * Per page-flags.h, pages tagged PG_reserved "should in general not be
	 * touched (e.g. set dirty) except by its owner".
	 */
	return !PageReserved(page);
}

static void kvm_set_page_dirty(struct page *page)
{
	if (kvm_is_ad_tracked_page(page))
		SetPageDirty(page);
}

static void kvm_set_page_accessed(struct page *page)
{
	if (kvm_is_ad_tracked_page(page))
		mark_page_accessed(page);
}

void kvm_release_page_clean(struct page *page)
{
	if (!page)
		return;

	kvm_set_page_accessed(page);
	put_page(page);
}
EXPORT_SYMBOL_GPL(kvm_release_page_clean);

void kvm_release_page_dirty(struct page *page)
{
	if (!page)
		return;

	kvm_set_page_dirty(page);
	kvm_release_page_clean(page);
}
EXPORT_SYMBOL_GPL(kvm_release_page_dirty);

static kvm_pfn_t kvm_resolve_pfn(struct kvm_follow_pfn *kfp, struct page *page,
				 struct follow_pfnmap_args *map, bool writable)
{
	kvm_pfn_t pfn;

	WARN_ON_ONCE(!!page == !!map);

	if (kfp->map_writable)
		*kfp->map_writable = writable;

	if (map)
		pfn = map->pfn;
	else
		pfn = page_to_pfn(page);

	*kfp->refcounted_page = page;

	return pfn;
}

/*
 * The fast path to get the writable pfn which will be stored in @pfn,
 * true indicates success, otherwise false is returned.
 */
static bool hva_to_pfn_fast(struct kvm_follow_pfn *kfp, kvm_pfn_t *pfn)
{
	struct page *page;
	bool r;

	/*
	 * Try the fast-only path when the caller wants to pin/get the page for
	 * writing.  If the caller only wants to read the page, KVM must go
	 * down the full, slow path in order to avoid racing an operation that
	 * breaks Copy-on-Write (CoW), e.g. so that KVM doesn't end up pointing
	 * at the old, read-only page while mm/ points at a new, writable page.
	 */
	if (!((kfp->flags & FOLL_WRITE) || kfp->map_writable))
		return false;

	if (kfp->pin)
		r = pin_user_pages_fast(kfp->hva, 1, FOLL_WRITE, &page) == 1;
	else
		r = get_user_page_fast_only(kfp->hva, FOLL_WRITE, &page);

	if (r) {
		*pfn = kvm_resolve_pfn(kfp, page, NULL, true);
		return true;
	}

	return false;
}

/*
 * The slow path to get the pfn of the specified host virtual address,
 * 1 indicates success, -errno is returned if error is detected.
 */
static int hva_to_pfn_slow(struct kvm_follow_pfn *kfp, kvm_pfn_t *pfn)
{
	/*
	 * When a VCPU accesses a page that is not mapped into the secondary
	 * MMU, we lookup the page using GUP to map it, so the guest VCPU can
	 * make progress. We always want to honor NUMA hinting faults in that
	 * case, because GUP usage corresponds to memory accesses from the VCPU.
	 * Otherwise, we'd not trigger NUMA hinting faults once a page is
	 * mapped into the secondary MMU and gets accessed by a VCPU.
	 *
	 * Note that get_user_page_fast_only() and FOLL_WRITE for now
	 * implicitly honor NUMA hinting faults and don't need this flag.
	 */
	unsigned int flags = FOLL_HWPOISON | FOLL_HONOR_NUMA_FAULT | kfp->flags;
	struct page *page, *wpage;
	int npages;

	if (kfp->pin)
		npages = pin_user_pages_unlocked(kfp->hva, 1, &page, flags);
	else
		npages = get_user_pages_unlocked(kfp->hva, 1, &page, flags);
	if (npages != 1)
		return npages;

	/*
	 * Pinning is mutually exclusive with opportunistically mapping a read
	 * fault as writable, as KVM should never pin pages when mapping memory
	 * into the guest (pinning is only for direct accesses from KVM).
	 */
	if (WARN_ON_ONCE(kfp->map_writable && kfp->pin))
		goto out;

	/* map read fault as writable if possible */
	if (!(flags & FOLL_WRITE) && kfp->map_writable &&
	    get_user_page_fast_only(kfp->hva, FOLL_WRITE, &wpage)) {
		put_page(page);
		page = wpage;
		flags |= FOLL_WRITE;
	}

out:
	*pfn = kvm_resolve_pfn(kfp, page, NULL, flags & FOLL_WRITE);
	return npages;
}

static bool vma_is_valid(struct vm_area_struct *vma, bool write_fault)
{
	if (unlikely(!(vma->vm_flags & VM_READ)))
		return false;

	if (write_fault && (unlikely(!(vma->vm_flags & VM_WRITE))))
		return false;

	return true;
}

static int hva_to_pfn_remapped(struct vm_area_struct *vma,
			       struct kvm_follow_pfn *kfp, kvm_pfn_t *p_pfn)
{
	struct follow_pfnmap_args args = { .vma = vma, .address = kfp->hva };
	bool write_fault = kfp->flags & FOLL_WRITE;
	int r;

	/*
	 * Remapped memory cannot be pinned in any meaningful sense.  Bail if
	 * the caller wants to pin the page, i.e. access the page outside of
	 * MMU notifier protection, and unsafe umappings are disallowed.
	 */
	if (kfp->pin && !allow_unsafe_mappings)
		return -EINVAL;

	r = follow_pfnmap_start(&args);
	if (r) {
		/*
		 * get_user_pages fails for VM_IO and VM_PFNMAP vmas and does
		 * not call the fault handler, so do it here.
		 */
		bool unlocked = false;
		r = fixup_user_fault(current->mm, kfp->hva,
				     (write_fault ? FAULT_FLAG_WRITE : 0),
				     &unlocked);
		if (unlocked)
			return -EAGAIN;
		if (r)
			return r;

		r = follow_pfnmap_start(&args);
		if (r)
			return r;
	}

	if (write_fault && !args.writable) {
		*p_pfn = KVM_PFN_ERR_RO_FAULT;
		goto out;
	}

	*p_pfn = kvm_resolve_pfn(kfp, NULL, &args, args.writable);
out:
	follow_pfnmap_end(&args);
	return r;
}

kvm_pfn_t hva_to_pfn(struct kvm_follow_pfn *kfp)
{
	struct vm_area_struct *vma;
	kvm_pfn_t pfn;
	int npages, r;

	might_sleep();

	if (WARN_ON_ONCE(!kfp->refcounted_page))
		return KVM_PFN_ERR_FAULT;

	if (hva_to_pfn_fast(kfp, &pfn))
		return pfn;

	npages = hva_to_pfn_slow(kfp, &pfn);
	if (npages == 1)
		return pfn;
	if (npages == -EINTR || npages == -EAGAIN)
		return KVM_PFN_ERR_SIGPENDING;
	if (npages == -EHWPOISON)
		return KVM_PFN_ERR_HWPOISON;

	mmap_read_lock(current->mm);
retry:
	vma = vma_lookup(current->mm, kfp->hva);

	if (vma == NULL)
		pfn = KVM_PFN_ERR_FAULT;
	else if (vma->vm_flags & (VM_IO | VM_PFNMAP)) {
		r = hva_to_pfn_remapped(vma, kfp, &pfn);
		if (r == -EAGAIN)
			goto retry;
		if (r < 0)
			pfn = KVM_PFN_ERR_FAULT;
	} else {
		if ((kfp->flags & FOLL_NOWAIT) &&
		    vma_is_valid(vma, kfp->flags & FOLL_WRITE))
			pfn = KVM_PFN_ERR_NEEDS_IO;
		else
			pfn = KVM_PFN_ERR_FAULT;
	}
	mmap_read_unlock(current->mm);
	return pfn;
}

static kvm_pfn_t kvm_follow_pfn(struct kvm_follow_pfn *kfp)
{
	kfp->hva = __gfn_to_hva_many(kfp->slot, kfp->gfn, NULL,
				     kfp->flags & FOLL_WRITE);

	if (kfp->hva == KVM_HVA_ERR_RO_BAD)
		return KVM_PFN_ERR_RO_FAULT;

	if (kvm_is_error_hva(kfp->hva))
		return KVM_PFN_NOSLOT;

	if (memslot_is_readonly(kfp->slot) && kfp->map_writable) {
		*kfp->map_writable = false;
		kfp->map_writable = NULL;
	}

	return hva_to_pfn(kfp);
}

kvm_pfn_t __kvm_faultin_pfn(const struct kvm_memory_slot *slot, gfn_t gfn,
			    unsigned int foll, bool *writable,
			    struct page **refcounted_page)
{
	struct kvm_follow_pfn kfp = {
		.slot = slot,
		.gfn = gfn,
		.flags = foll,
		.map_writable = writable,
		.refcounted_page = refcounted_page,
	};

	if (WARN_ON_ONCE(!writable || !refcounted_page))
		return KVM_PFN_ERR_FAULT;

	*writable = false;
	*refcounted_page = NULL;

	return kvm_follow_pfn(&kfp);
}
EXPORT_SYMBOL_GPL(__kvm_faultin_pfn);

int kvm_prefetch_pages(struct kvm_memory_slot *slot, gfn_t gfn,
		       struct page **pages, int nr_pages)
{
	unsigned long addr;
	gfn_t entry = 0;

	addr = gfn_to_hva_many(slot, gfn, &entry);
	if (kvm_is_error_hva(addr))
		return -1;

	if (entry < nr_pages)
		return 0;

	return get_user_pages_fast_only(addr, nr_pages, FOLL_WRITE, pages);
}
EXPORT_SYMBOL_GPL(kvm_prefetch_pages);

/*
 * Don't use this API unless you are absolutely, positively certain that KVM
 * needs to get a struct page, e.g. to pin the page for firmware DMA.
 *
 * FIXME: Users of this API likely need to FOLL_PIN the page, not just elevate
 *	  its refcount.
 */
struct page *__gfn_to_page(struct kvm *kvm, gfn_t gfn, bool write)
{
	struct page *refcounted_page = NULL;
	struct kvm_follow_pfn kfp = {
		.slot = gfn_to_memslot(kvm, gfn),
		.gfn = gfn,
		.flags = write ? FOLL_WRITE : 0,
		.refcounted_page = &refcounted_page,
	};

	(void)kvm_follow_pfn(&kfp);
	return refcounted_page;
}
EXPORT_SYMBOL_GPL(__gfn_to_page);

int __kvm_vcpu_map(struct kvm_vcpu *vcpu, gfn_t gfn, struct kvm_host_map *map,
		   bool writable)
{
	struct kvm_follow_pfn kfp = {
		.slot = gfn_to_memslot(vcpu->kvm, gfn),
		.gfn = gfn,
		.flags = writable ? FOLL_WRITE : 0,
		.refcounted_page = &map->pinned_page,
		.pin = true,
	};

	map->pinned_page = NULL;
	map->page = NULL;
	map->hva = NULL;
	map->gfn = gfn;
	map->writable = writable;

	map->pfn = kvm_follow_pfn(&kfp);
	if (is_error_noslot_pfn(map->pfn))
		return -EINVAL;

	if (pfn_valid(map->pfn)) {
		map->page = pfn_to_page(map->pfn);
		map->hva = kmap(map->page);
#ifdef CONFIG_HAS_IOMEM
	} else {
		map->hva = memremap(pfn_to_hpa(map->pfn), PAGE_SIZE, MEMREMAP_WB);
#endif
	}

	return map->hva ? 0 : -EFAULT;
}
EXPORT_SYMBOL_GPL(__kvm_vcpu_map);

void kvm_vcpu_unmap(struct kvm_vcpu *vcpu, struct kvm_host_map *map)
{
	if (!map->hva)
		return;

	if (map->page)
		kunmap(map->page);
#ifdef CONFIG_HAS_IOMEM
	else
		memunmap(map->hva);
#endif

	if (map->writable)
		kvm_vcpu_mark_page_dirty(vcpu, map->gfn);

	if (map->pinned_page) {
		if (map->writable)
			kvm_set_page_dirty(map->pinned_page);
		kvm_set_page_accessed(map->pinned_page);
		unpin_user_page(map->pinned_page);
	}

	map->hva = NULL;
	map->page = NULL;
	map->pinned_page = NULL;
}
EXPORT_SYMBOL_GPL(kvm_vcpu_unmap);

static int next_segment(unsigned long len, int offset)
{
	if (len > PAGE_SIZE - offset)
		return PAGE_SIZE - offset;
	else
		return len;
}

/* Copy @len bytes from guest memory at '(@gfn * PAGE_SIZE) + @offset' to @data */
static int __kvm_read_guest_page(struct kvm_memory_slot *slot, gfn_t gfn,
				 void *data, int offset, int len)
{
	int r;
	unsigned long addr;

	if (WARN_ON_ONCE(offset + len > PAGE_SIZE))
		return -EFAULT;

	addr = gfn_to_hva_memslot_prot(slot, gfn, NULL);
	if (kvm_is_error_hva(addr))
		return -EFAULT;
	r = __copy_from_user(data, (void __user *)addr + offset, len);
	if (r)
		return -EFAULT;
	return 0;
}

int kvm_read_guest_page(struct kvm *kvm, gfn_t gfn, void *data, int offset,
			int len)
{
	struct kvm_memory_slot *slot = gfn_to_memslot(kvm, gfn);

	return __kvm_read_guest_page(slot, gfn, data, offset, len);
}
EXPORT_SYMBOL_GPL(kvm_read_guest_page);

int kvm_vcpu_read_guest_page(struct kvm_vcpu *vcpu, gfn_t gfn, void *data,
			     int offset, int len)
{
	struct kvm_memory_slot *slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);

	return __kvm_read_guest_page(slot, gfn, data, offset, len);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_read_guest_page);

int kvm_read_guest(struct kvm *kvm, gpa_t gpa, void *data, unsigned long len)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int seg;
	int offset = offset_in_page(gpa);
	int ret;

	while ((seg = next_segment(len, offset)) != 0) {
		ret = kvm_read_guest_page(kvm, gfn, data, offset, seg);
		if (ret < 0)
			return ret;
		offset = 0;
		len -= seg;
		data += seg;
		++gfn;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_read_guest);

int kvm_vcpu_read_guest(struct kvm_vcpu *vcpu, gpa_t gpa, void *data, unsigned long len)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int seg;
	int offset = offset_in_page(gpa);
	int ret;

	while ((seg = next_segment(len, offset)) != 0) {
		ret = kvm_vcpu_read_guest_page(vcpu, gfn, data, offset, seg);
		if (ret < 0)
			return ret;
		offset = 0;
		len -= seg;
		data += seg;
		++gfn;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_vcpu_read_guest);

static int __kvm_read_guest_atomic(struct kvm_memory_slot *slot, gfn_t gfn,
			           void *data, int offset, unsigned long len)
{
	int r;
	unsigned long addr;

	if (WARN_ON_ONCE(offset + len > PAGE_SIZE))
		return -EFAULT;

	addr = gfn_to_hva_memslot_prot(slot, gfn, NULL);
	if (kvm_is_error_hva(addr))
		return -EFAULT;
	pagefault_disable();
	r = __copy_from_user_inatomic(data, (void __user *)addr + offset, len);
	pagefault_enable();
	if (r)
		return -EFAULT;
	return 0;
}

int kvm_vcpu_read_guest_atomic(struct kvm_vcpu *vcpu, gpa_t gpa,
			       void *data, unsigned long len)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct kvm_memory_slot *slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	int offset = offset_in_page(gpa);

	return __kvm_read_guest_atomic(slot, gfn, data, offset, len);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_read_guest_atomic);

/* Copy @len bytes from @data into guest memory at '(@gfn * PAGE_SIZE) + @offset' */
static int __kvm_write_guest_page(struct kvm *kvm,
				  struct kvm_memory_slot *memslot, gfn_t gfn,
			          const void *data, int offset, int len)
{
	int r;
	unsigned long addr;

	if (WARN_ON_ONCE(offset + len > PAGE_SIZE))
		return -EFAULT;

	addr = gfn_to_hva_memslot(memslot, gfn);
	if (kvm_is_error_hva(addr))
		return -EFAULT;
	r = __copy_to_user((void __user *)addr + offset, data, len);
	if (r)
		return -EFAULT;
	mark_page_dirty_in_slot(kvm, memslot, gfn);
	return 0;
}

int kvm_write_guest_page(struct kvm *kvm, gfn_t gfn,
			 const void *data, int offset, int len)
{
	struct kvm_memory_slot *slot = gfn_to_memslot(kvm, gfn);

	return __kvm_write_guest_page(kvm, slot, gfn, data, offset, len);
}
EXPORT_SYMBOL_GPL(kvm_write_guest_page);

int kvm_vcpu_write_guest_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			      const void *data, int offset, int len)
{
	struct kvm_memory_slot *slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);

	return __kvm_write_guest_page(vcpu->kvm, slot, gfn, data, offset, len);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_write_guest_page);

int kvm_write_guest(struct kvm *kvm, gpa_t gpa, const void *data,
		    unsigned long len)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int seg;
	int offset = offset_in_page(gpa);
	int ret;

	while ((seg = next_segment(len, offset)) != 0) {
		ret = kvm_write_guest_page(kvm, gfn, data, offset, seg);
		if (ret < 0)
			return ret;
		offset = 0;
		len -= seg;
		data += seg;
		++gfn;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_write_guest);

int kvm_vcpu_write_guest(struct kvm_vcpu *vcpu, gpa_t gpa, const void *data,
		         unsigned long len)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int seg;
	int offset = offset_in_page(gpa);
	int ret;

	while ((seg = next_segment(len, offset)) != 0) {
		ret = kvm_vcpu_write_guest_page(vcpu, gfn, data, offset, seg);
		if (ret < 0)
			return ret;
		offset = 0;
		len -= seg;
		data += seg;
		++gfn;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_vcpu_write_guest);

static int __kvm_gfn_to_hva_cache_init(struct kvm_memslots *slots,
				       struct gfn_to_hva_cache *ghc,
				       gpa_t gpa, unsigned long len)
{
	int offset = offset_in_page(gpa);
	gfn_t start_gfn = gpa >> PAGE_SHIFT;
	gfn_t end_gfn = (gpa + len - 1) >> PAGE_SHIFT;
	gfn_t nr_pages_needed = end_gfn - start_gfn + 1;
	gfn_t nr_pages_avail;

	/* Update ghc->generation before performing any error checks. */
	ghc->generation = slots->generation;

	if (start_gfn > end_gfn) {
		ghc->hva = KVM_HVA_ERR_BAD;
		return -EINVAL;
	}

	/*
	 * If the requested region crosses two memslots, we still
	 * verify that the entire region is valid here.
	 */
	for ( ; start_gfn <= end_gfn; start_gfn += nr_pages_avail) {
		ghc->memslot = __gfn_to_memslot(slots, start_gfn);
		ghc->hva = gfn_to_hva_many(ghc->memslot, start_gfn,
					   &nr_pages_avail);
		if (kvm_is_error_hva(ghc->hva))
			return -EFAULT;
	}

	/* Use the slow path for cross page reads and writes. */
	if (nr_pages_needed == 1)
		ghc->hva += offset;
	else
		ghc->memslot = NULL;

	ghc->gpa = gpa;
	ghc->len = len;
	return 0;
}

int kvm_gfn_to_hva_cache_init(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			      gpa_t gpa, unsigned long len)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);
	return __kvm_gfn_to_hva_cache_init(slots, ghc, gpa, len);
}
EXPORT_SYMBOL_GPL(kvm_gfn_to_hva_cache_init);

int kvm_write_guest_offset_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
				  void *data, unsigned int offset,
				  unsigned long len)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);
	int r;
	gpa_t gpa = ghc->gpa + offset;

	if (WARN_ON_ONCE(len + offset > ghc->len))
		return -EINVAL;

	if (slots->generation != ghc->generation) {
		if (__kvm_gfn_to_hva_cache_init(slots, ghc, ghc->gpa, ghc->len))
			return -EFAULT;
	}

	if (kvm_is_error_hva(ghc->hva))
		return -EFAULT;

	if (unlikely(!ghc->memslot))
		return kvm_write_guest(kvm, gpa, data, len);

	r = __copy_to_user((void __user *)ghc->hva + offset, data, len);
	if (r)
		return -EFAULT;
	mark_page_dirty_in_slot(kvm, ghc->memslot, gpa >> PAGE_SHIFT);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_write_guest_offset_cached);

int kvm_write_guest_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			   void *data, unsigned long len)
{
	return kvm_write_guest_offset_cached(kvm, ghc, data, 0, len);
}
EXPORT_SYMBOL_GPL(kvm_write_guest_cached);

int kvm_read_guest_offset_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
				 void *data, unsigned int offset,
				 unsigned long len)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);
	int r;
	gpa_t gpa = ghc->gpa + offset;

	if (WARN_ON_ONCE(len + offset > ghc->len))
		return -EINVAL;

	if (slots->generation != ghc->generation) {
		if (__kvm_gfn_to_hva_cache_init(slots, ghc, ghc->gpa, ghc->len))
			return -EFAULT;
	}

	if (kvm_is_error_hva(ghc->hva))
		return -EFAULT;

	if (unlikely(!ghc->memslot))
		return kvm_read_guest(kvm, gpa, data, len);

	r = __copy_from_user(data, (void __user *)ghc->hva + offset, len);
	if (r)
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_read_guest_offset_cached);

int kvm_read_guest_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			  void *data, unsigned long len)
{
	return kvm_read_guest_offset_cached(kvm, ghc, data, 0, len);
}
EXPORT_SYMBOL_GPL(kvm_read_guest_cached);

int kvm_clear_guest(struct kvm *kvm, gpa_t gpa, unsigned long len)
{
	const void *zero_page = (const void *) __va(page_to_phys(ZERO_PAGE(0)));
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int seg;
	int offset = offset_in_page(gpa);
	int ret;

	while ((seg = next_segment(len, offset)) != 0) {
		ret = kvm_write_guest_page(kvm, gfn, zero_page, offset, seg);
		if (ret < 0)
			return ret;
		offset = 0;
		len -= seg;
		++gfn;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_clear_guest);

void mark_page_dirty_in_slot(struct kvm *kvm,
			     const struct kvm_memory_slot *memslot,
		 	     gfn_t gfn)
{
	struct kvm_vcpu *vcpu = kvm_get_running_vcpu();

#ifdef CONFIG_HAVE_KVM_DIRTY_RING
	if (WARN_ON_ONCE(vcpu && vcpu->kvm != kvm))
		return;

	WARN_ON_ONCE(!vcpu && !kvm_arch_allow_write_without_running_vcpu(kvm));
#endif

	if (memslot && kvm_slot_dirty_track_enabled(memslot)) {
		unsigned long rel_gfn = gfn - memslot->base_gfn;
		u32 slot = (memslot->as_id << 16) | memslot->id;

		if (kvm->dirty_ring_size && vcpu)
			kvm_dirty_ring_push(vcpu, slot, rel_gfn);
		else if (memslot->dirty_bitmap)
			set_bit_le(rel_gfn, memslot->dirty_bitmap);
	}
}
EXPORT_SYMBOL_GPL(mark_page_dirty_in_slot);

void mark_page_dirty(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *memslot;

	memslot = gfn_to_memslot(kvm, gfn);
	mark_page_dirty_in_slot(kvm, memslot, gfn);
}
EXPORT_SYMBOL_GPL(mark_page_dirty);

void kvm_vcpu_mark_page_dirty(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	struct kvm_memory_slot *memslot;

	memslot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	mark_page_dirty_in_slot(vcpu->kvm, memslot, gfn);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_mark_page_dirty);

void kvm_sigset_activate(struct kvm_vcpu *vcpu)
{
	if (!vcpu->sigset_active)
		return;

	/*
	 * This does a lockless modification of ->real_blocked, which is fine
	 * because, only current can change ->real_blocked and all readers of
	 * ->real_blocked don't care as long ->real_blocked is always a subset
	 * of ->blocked.
	 */
	sigprocmask(SIG_SETMASK, &vcpu->sigset, &current->real_blocked);
}

void kvm_sigset_deactivate(struct kvm_vcpu *vcpu)
{
	if (!vcpu->sigset_active)
		return;

	sigprocmask(SIG_SETMASK, &current->real_blocked, NULL);
	sigemptyset(&current->real_blocked);
}

static void grow_halt_poll_ns(struct kvm_vcpu *vcpu)
{
	unsigned int old, val, grow, grow_start;

	old = val = vcpu->halt_poll_ns;
	grow_start = READ_ONCE(halt_poll_ns_grow_start);
	grow = READ_ONCE(halt_poll_ns_grow);
	if (!grow)
		goto out;

	val *= grow;
	if (val < grow_start)
		val = grow_start;

	vcpu->halt_poll_ns = val;
out:
	trace_kvm_halt_poll_ns_grow(vcpu->vcpu_id, val, old);
}

static void shrink_halt_poll_ns(struct kvm_vcpu *vcpu)
{
	unsigned int old, val, shrink, grow_start;

	old = val = vcpu->halt_poll_ns;
	shrink = READ_ONCE(halt_poll_ns_shrink);
	grow_start = READ_ONCE(halt_poll_ns_grow_start);
	if (shrink == 0)
		val = 0;
	else
		val /= shrink;

	if (val < grow_start)
		val = 0;

	vcpu->halt_poll_ns = val;
	trace_kvm_halt_poll_ns_shrink(vcpu->vcpu_id, val, old);
}

static int kvm_vcpu_check_block(struct kvm_vcpu *vcpu)
{
	int ret = -EINTR;
	int idx = srcu_read_lock(&vcpu->kvm->srcu);

	if (kvm_arch_vcpu_runnable(vcpu))
		goto out;
	if (kvm_cpu_has_pending_timer(vcpu))
		goto out;
	if (signal_pending(current))
		goto out;
	if (kvm_check_request(KVM_REQ_UNBLOCK, vcpu))
		goto out;

	ret = 0;
out:
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	return ret;
}

/*
 * Block the vCPU until the vCPU is runnable, an event arrives, or a signal is
 * pending.  This is mostly used when halting a vCPU, but may also be used
 * directly for other vCPU non-runnable states, e.g. x86's Wait-For-SIPI.
 */
bool kvm_vcpu_block(struct kvm_vcpu *vcpu)
{
	struct rcuwait *wait = kvm_arch_vcpu_get_wait(vcpu);
	bool waited = false;

	vcpu->stat.generic.blocking = 1;

	preempt_disable();
	kvm_arch_vcpu_blocking(vcpu);
	prepare_to_rcuwait(wait);
	preempt_enable();

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (kvm_vcpu_check_block(vcpu) < 0)
			break;

		waited = true;
		schedule();
	}

	preempt_disable();
	finish_rcuwait(wait);
	kvm_arch_vcpu_unblocking(vcpu);
	preempt_enable();

	vcpu->stat.generic.blocking = 0;

	return waited;
}

static inline void update_halt_poll_stats(struct kvm_vcpu *vcpu, ktime_t start,
					  ktime_t end, bool success)
{
	struct kvm_vcpu_stat_generic *stats = &vcpu->stat.generic;
	u64 poll_ns = ktime_to_ns(ktime_sub(end, start));

	++vcpu->stat.generic.halt_attempted_poll;

	if (success) {
		++vcpu->stat.generic.halt_successful_poll;

		if (!vcpu_valid_wakeup(vcpu))
			++vcpu->stat.generic.halt_poll_invalid;

		stats->halt_poll_success_ns += poll_ns;
		KVM_STATS_LOG_HIST_UPDATE(stats->halt_poll_success_hist, poll_ns);
	} else {
		stats->halt_poll_fail_ns += poll_ns;
		KVM_STATS_LOG_HIST_UPDATE(stats->halt_poll_fail_hist, poll_ns);
	}
}

static unsigned int kvm_vcpu_max_halt_poll_ns(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	if (kvm->override_halt_poll_ns) {
		/*
		 * Ensure kvm->max_halt_poll_ns is not read before
		 * kvm->override_halt_poll_ns.
		 *
		 * Pairs with the smp_wmb() when enabling KVM_CAP_HALT_POLL.
		 */
		smp_rmb();
		return READ_ONCE(kvm->max_halt_poll_ns);
	}

	return READ_ONCE(halt_poll_ns);
}

/*
 * Emulate a vCPU halt condition, e.g. HLT on x86, WFI on arm, etc...  If halt
 * polling is enabled, busy wait for a short time before blocking to avoid the
 * expensive block+unblock sequence if a wake event arrives soon after the vCPU
 * is halted.
 */
void kvm_vcpu_halt(struct kvm_vcpu *vcpu)
{
	unsigned int max_halt_poll_ns = kvm_vcpu_max_halt_poll_ns(vcpu);
	bool halt_poll_allowed = !kvm_arch_no_poll(vcpu);
	ktime_t start, cur, poll_end;
	bool waited = false;
	bool do_halt_poll;
	u64 halt_ns;

	if (vcpu->halt_poll_ns > max_halt_poll_ns)
		vcpu->halt_poll_ns = max_halt_poll_ns;

	do_halt_poll = halt_poll_allowed && vcpu->halt_poll_ns;

	start = cur = poll_end = ktime_get();
	if (do_halt_poll) {
		ktime_t stop = ktime_add_ns(start, vcpu->halt_poll_ns);

		do {
			if (kvm_vcpu_check_block(vcpu) < 0)
				goto out;
			cpu_relax();
			poll_end = cur = ktime_get();
		} while (kvm_vcpu_can_poll(cur, stop));
	}

	waited = kvm_vcpu_block(vcpu);

	cur = ktime_get();
	if (waited) {
		vcpu->stat.generic.halt_wait_ns +=
			ktime_to_ns(cur) - ktime_to_ns(poll_end);
		KVM_STATS_LOG_HIST_UPDATE(vcpu->stat.generic.halt_wait_hist,
				ktime_to_ns(cur) - ktime_to_ns(poll_end));
	}
out:
	/* The total time the vCPU was "halted", including polling time. */
	halt_ns = ktime_to_ns(cur) - ktime_to_ns(start);

	/*
	 * Note, halt-polling is considered successful so long as the vCPU was
	 * never actually scheduled out, i.e. even if the wake event arrived
	 * after of the halt-polling loop itself, but before the full wait.
	 */
	if (do_halt_poll)
		update_halt_poll_stats(vcpu, start, poll_end, !waited);

	if (halt_poll_allowed) {
		/* Recompute the max halt poll time in case it changed. */
		max_halt_poll_ns = kvm_vcpu_max_halt_poll_ns(vcpu);

		if (!vcpu_valid_wakeup(vcpu)) {
			shrink_halt_poll_ns(vcpu);
		} else if (max_halt_poll_ns) {
			if (halt_ns <= vcpu->halt_poll_ns)
				;
			/* we had a long block, shrink polling */
			else if (vcpu->halt_poll_ns &&
				 halt_ns > max_halt_poll_ns)
				shrink_halt_poll_ns(vcpu);
			/* we had a short halt and our poll time is too small */
			else if (vcpu->halt_poll_ns < max_halt_poll_ns &&
				 halt_ns < max_halt_poll_ns)
				grow_halt_poll_ns(vcpu);
		} else {
			vcpu->halt_poll_ns = 0;
		}
	}

	trace_kvm_vcpu_wakeup(halt_ns, waited, vcpu_valid_wakeup(vcpu));
}
EXPORT_SYMBOL_GPL(kvm_vcpu_halt);

bool kvm_vcpu_wake_up(struct kvm_vcpu *vcpu)
{
	if (__kvm_vcpu_wake_up(vcpu)) {
		WRITE_ONCE(vcpu->ready, true);
		++vcpu->stat.generic.halt_wakeup;
		return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(kvm_vcpu_wake_up);

#ifndef CONFIG_S390
/*
 * Kick a sleeping VCPU, or a guest VCPU in guest mode, into host kernel mode.
 */
void kvm_vcpu_kick(struct kvm_vcpu *vcpu)
{
	int me, cpu;

	if (kvm_vcpu_wake_up(vcpu))
		return;

	me = get_cpu();
	/*
	 * The only state change done outside the vcpu mutex is IN_GUEST_MODE
	 * to EXITING_GUEST_MODE.  Therefore the moderately expensive "should
	 * kick" check does not need atomic operations if kvm_vcpu_kick is used
	 * within the vCPU thread itself.
	 */
	if (vcpu == __this_cpu_read(kvm_running_vcpu)) {
		if (vcpu->mode == IN_GUEST_MODE)
			WRITE_ONCE(vcpu->mode, EXITING_GUEST_MODE);
		goto out;
	}

	/*
	 * Note, the vCPU could get migrated to a different pCPU at any point
	 * after kvm_arch_vcpu_should_kick(), which could result in sending an
	 * IPI to the previous pCPU.  But, that's ok because the purpose of the
	 * IPI is to force the vCPU to leave IN_GUEST_MODE, and migrating the
	 * vCPU also requires it to leave IN_GUEST_MODE.
	 */
	if (kvm_arch_vcpu_should_kick(vcpu)) {
		cpu = READ_ONCE(vcpu->cpu);
		if (cpu != me && (unsigned)cpu < nr_cpu_ids && cpu_online(cpu))
			smp_send_reschedule(cpu);
	}
out:
	put_cpu();
}
EXPORT_SYMBOL_GPL(kvm_vcpu_kick);
#endif /* !CONFIG_S390 */

int kvm_vcpu_yield_to(struct kvm_vcpu *target)
{
	struct task_struct *task = NULL;
	int ret;

	if (!read_trylock(&target->pid_lock))
		return 0;

	if (target->pid)
		task = get_pid_task(target->pid, PIDTYPE_PID);

	read_unlock(&target->pid_lock);

	if (!task)
		return 0;
	ret = yield_to(task, 1);
	put_task_struct(task);

	return ret;
}
EXPORT_SYMBOL_GPL(kvm_vcpu_yield_to);

/*
 * Helper that checks whether a VCPU is eligible for directed yield.
 * Most eligible candidate to yield is decided by following heuristics:
 *
 *  (a) VCPU which has not done pl-exit or cpu relax intercepted recently
 *  (preempted lock holder), indicated by @in_spin_loop.
 *  Set at the beginning and cleared at the end of interception/PLE handler.
 *
 *  (b) VCPU which has done pl-exit/ cpu relax intercepted but did not get
 *  chance last time (mostly it has become eligible now since we have probably
 *  yielded to lockholder in last iteration. This is done by toggling
 *  @dy_eligible each time a VCPU checked for eligibility.)
 *
 *  Yielding to a recently pl-exited/cpu relax intercepted VCPU before yielding
 *  to preempted lock-holder could result in wrong VCPU selection and CPU
 *  burning. Giving priority for a potential lock-holder increases lock
 *  progress.
 *
 *  Since algorithm is based on heuristics, accessing another VCPU data without
 *  locking does not harm. It may result in trying to yield to  same VCPU, fail
 *  and continue with next VCPU and so on.
 */
static bool kvm_vcpu_eligible_for_directed_yield(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_HAVE_KVM_CPU_RELAX_INTERCEPT
	bool eligible;

	eligible = !vcpu->spin_loop.in_spin_loop ||
		    vcpu->spin_loop.dy_eligible;

	if (vcpu->spin_loop.in_spin_loop)
		kvm_vcpu_set_dy_eligible(vcpu, !vcpu->spin_loop.dy_eligible);

	return eligible;
#else
	return true;
#endif
}

/*
 * Unlike kvm_arch_vcpu_runnable, this function is called outside
 * a vcpu_load/vcpu_put pair.  However, for most architectures
 * kvm_arch_vcpu_runnable does not require vcpu_load.
 */
bool __weak kvm_arch_dy_runnable(struct kvm_vcpu *vcpu)
{
	return kvm_arch_vcpu_runnable(vcpu);
}

static bool vcpu_dy_runnable(struct kvm_vcpu *vcpu)
{
	if (kvm_arch_dy_runnable(vcpu))
		return true;

#ifdef CONFIG_KVM_ASYNC_PF
	if (!list_empty_careful(&vcpu->async_pf.done))
		return true;
#endif

	return false;
}

/*
 * By default, simply query the target vCPU's current mode when checking if a
 * vCPU was preempted in kernel mode.  All architectures except x86 (or more
 * specifical, except VMX) allow querying whether or not a vCPU is in kernel
 * mode even if the vCPU is NOT loaded, i.e. using kvm_arch_vcpu_in_kernel()
 * directly for cross-vCPU checks is functionally correct and accurate.
 */
bool __weak kvm_arch_vcpu_preempted_in_kernel(struct kvm_vcpu *vcpu)
{
	return kvm_arch_vcpu_in_kernel(vcpu);
}

bool __weak kvm_arch_dy_has_pending_interrupt(struct kvm_vcpu *vcpu)
{
	return false;
}

void kvm_vcpu_on_spin(struct kvm_vcpu *me, bool yield_to_kernel_mode)
{
	int nr_vcpus, start, i, idx, yielded;
	struct kvm *kvm = me->kvm;
	struct kvm_vcpu *vcpu;
	int try = 3;

	nr_vcpus = atomic_read(&kvm->online_vcpus);
	if (nr_vcpus < 2)
		return;

	/* Pairs with the smp_wmb() in kvm_vm_ioctl_create_vcpu(). */
	smp_rmb();

	kvm_vcpu_set_in_spin_loop(me, true);

	/*
	 * The current vCPU ("me") is spinning in kernel mode, i.e. is likely
	 * waiting for a resource to become available.  Attempt to yield to a
	 * vCPU that is runnable, but not currently running, e.g. because the
	 * vCPU was preempted by a higher priority task.  With luck, the vCPU
	 * that was preempted is holding a lock or some other resource that the
	 * current vCPU is waiting to acquire, and yielding to the other vCPU
	 * will allow it to make forward progress and release the lock (or kick
	 * the spinning vCPU, etc).
	 *
	 * Since KVM has no insight into what exactly the guest is doing,
	 * approximate a round-robin selection by iterating over all vCPUs,
	 * starting at the last boosted vCPU.  I.e. if N=kvm->last_boosted_vcpu,
	 * iterate over vCPU[N+1]..vCPU[N-1], wrapping as needed.
	 *
	 * Note, this is inherently racy, e.g. if multiple vCPUs are spinning,
	 * they may all try to yield to the same vCPU(s).  But as above, this
	 * is all best effort due to KVM's lack of visibility into the guest.
	 */
	start = READ_ONCE(kvm->last_boosted_vcpu) + 1;
	for (i = 0; i < nr_vcpus; i++) {
		idx = (start + i) % nr_vcpus;
		if (idx == me->vcpu_idx)
			continue;

		vcpu = xa_load(&kvm->vcpu_array, idx);
		if (!READ_ONCE(vcpu->ready))
			continue;
		if (kvm_vcpu_is_blocking(vcpu) && !vcpu_dy_runnable(vcpu))
			continue;

		/*
		 * Treat the target vCPU as being in-kernel if it has a pending
		 * interrupt, as the vCPU trying to yield may be spinning
		 * waiting on IPI delivery, i.e. the target vCPU is in-kernel
		 * for the purposes of directed yield.
		 */
		if (READ_ONCE(vcpu->preempted) && yield_to_kernel_mode &&
		    !kvm_arch_dy_has_pending_interrupt(vcpu) &&
		    !kvm_arch_vcpu_preempted_in_kernel(vcpu))
			continue;

		if (!kvm_vcpu_eligible_for_directed_yield(vcpu))
			continue;

		yielded = kvm_vcpu_yield_to(vcpu);
		if (yielded > 0) {
			WRITE_ONCE(kvm->last_boosted_vcpu, i);
			break;
		} else if (yielded < 0 && !--try) {
			break;
		}
	}
	kvm_vcpu_set_in_spin_loop(me, false);

	/* Ensure vcpu is not eligible during next spinloop */
	kvm_vcpu_set_dy_eligible(me, false);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_on_spin);

static bool kvm_page_in_dirty_ring(struct kvm *kvm, unsigned long pgoff)
{
#ifdef CONFIG_HAVE_KVM_DIRTY_RING
	return (pgoff >= KVM_DIRTY_LOG_PAGE_OFFSET) &&
	    (pgoff < KVM_DIRTY_LOG_PAGE_OFFSET +
	     kvm->dirty_ring_size / PAGE_SIZE);
#else
	return false;
#endif
}

static vm_fault_t kvm_vcpu_fault(struct vm_fault *vmf)
{
	struct kvm_vcpu *vcpu = vmf->vma->vm_file->private_data;
	struct page *page;

	if (vmf->pgoff == 0)
		page = virt_to_page(vcpu->run);
#ifdef CONFIG_X86
	else if (vmf->pgoff == KVM_PIO_PAGE_OFFSET)
		page = virt_to_page(vcpu->arch.pio_data);
#endif
#ifdef CONFIG_KVM_MMIO
	else if (vmf->pgoff == KVM_COALESCED_MMIO_PAGE_OFFSET)
		page = virt_to_page(vcpu->kvm->coalesced_mmio_ring);
#endif
	else if (kvm_page_in_dirty_ring(vcpu->kvm, vmf->pgoff))
		page = kvm_dirty_ring_get_page(
		    &vcpu->dirty_ring,
		    vmf->pgoff - KVM_DIRTY_LOG_PAGE_OFFSET);
	else
		return kvm_arch_vcpu_fault(vcpu, vmf);
	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct kvm_vcpu_vm_ops = {
	.fault = kvm_vcpu_fault,
};

static int kvm_vcpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct kvm_vcpu *vcpu = file->private_data;
	unsigned long pages = vma_pages(vma);

	if ((kvm_page_in_dirty_ring(vcpu->kvm, vma->vm_pgoff) ||
	     kvm_page_in_dirty_ring(vcpu->kvm, vma->vm_pgoff + pages - 1)) &&
	    ((vma->vm_flags & VM_EXEC) || !(vma->vm_flags & VM_SHARED)))
		return -EINVAL;

	vma->vm_ops = &kvm_vcpu_vm_ops;
	return 0;
}

static int kvm_vcpu_release(struct inode *inode, struct file *filp)
{
	struct kvm_vcpu *vcpu = filp->private_data;

	kvm_put_kvm(vcpu->kvm);
	return 0;
}

static struct file_operations kvm_vcpu_fops = {
	.release        = kvm_vcpu_release,
	.unlocked_ioctl = kvm_vcpu_ioctl,
	.mmap           = kvm_vcpu_mmap,
	.llseek		= noop_llseek,
	KVM_COMPAT(kvm_vcpu_compat_ioctl),
};

/*
 * Allocates an inode for the vcpu.
 */
static int create_vcpu_fd(struct kvm_vcpu *vcpu)
{
	char name[8 + 1 + ITOA_MAX_LEN + 1];

	snprintf(name, sizeof(name), "kvm-vcpu:%d", vcpu->vcpu_id);
	return anon_inode_getfd(name, &kvm_vcpu_fops, vcpu, O_RDWR | O_CLOEXEC);
}

#ifdef __KVM_HAVE_ARCH_VCPU_DEBUGFS
static int vcpu_get_pid(void *data, u64 *val)
{
	struct kvm_vcpu *vcpu = data;

	read_lock(&vcpu->pid_lock);
	*val = pid_nr(vcpu->pid);
	read_unlock(&vcpu->pid_lock);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vcpu_get_pid_fops, vcpu_get_pid, NULL, "%llu\n");

static void kvm_create_vcpu_debugfs(struct kvm_vcpu *vcpu)
{
	struct dentry *debugfs_dentry;
	char dir_name[ITOA_MAX_LEN * 2];

	if (!debugfs_initialized())
		return;

	snprintf(dir_name, sizeof(dir_name), "vcpu%d", vcpu->vcpu_id);
	debugfs_dentry = debugfs_create_dir(dir_name,
					    vcpu->kvm->debugfs_dentry);
	debugfs_create_file("pid", 0444, debugfs_dentry, vcpu,
			    &vcpu_get_pid_fops);

	kvm_arch_create_vcpu_debugfs(vcpu, debugfs_dentry);
}
#endif

/*
 * Creates some virtual cpus.  Good luck creating more than one.
 */
static int kvm_vm_ioctl_create_vcpu(struct kvm *kvm, unsigned long id)
{
	int r;
	struct kvm_vcpu *vcpu;
	struct page *page;

	/*
	 * KVM tracks vCPU IDs as 'int', be kind to userspace and reject
	 * too-large values instead of silently truncating.
	 *
	 * Ensure KVM_MAX_VCPU_IDS isn't pushed above INT_MAX without first
	 * changing the storage type (at the very least, IDs should be tracked
	 * as unsigned ints).
	 */
	BUILD_BUG_ON(KVM_MAX_VCPU_IDS > INT_MAX);
	if (id >= KVM_MAX_VCPU_IDS)
		return -EINVAL;

	mutex_lock(&kvm->lock);
	if (kvm->created_vcpus >= kvm->max_vcpus) {
		mutex_unlock(&kvm->lock);
		return -EINVAL;
	}

	r = kvm_arch_vcpu_precreate(kvm, id);
	if (r) {
		mutex_unlock(&kvm->lock);
		return r;
	}

	kvm->created_vcpus++;
	mutex_unlock(&kvm->lock);

	vcpu = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL_ACCOUNT);
	if (!vcpu) {
		r = -ENOMEM;
		goto vcpu_decrement;
	}

	BUILD_BUG_ON(sizeof(struct kvm_run) > PAGE_SIZE);
	page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!page) {
		r = -ENOMEM;
		goto vcpu_free;
	}
	vcpu->run = page_address(page);

	kvm_vcpu_init(vcpu, kvm, id);

	r = kvm_arch_vcpu_create(vcpu);
	if (r)
		goto vcpu_free_run_page;

	if (kvm->dirty_ring_size) {
		r = kvm_dirty_ring_alloc(&vcpu->dirty_ring,
					 id, kvm->dirty_ring_size);
		if (r)
			goto arch_vcpu_destroy;
	}

	mutex_lock(&kvm->lock);

	if (kvm_get_vcpu_by_id(kvm, id)) {
		r = -EEXIST;
		goto unlock_vcpu_destroy;
	}

	vcpu->vcpu_idx = atomic_read(&kvm->online_vcpus);
	r = xa_insert(&kvm->vcpu_array, vcpu->vcpu_idx, vcpu, GFP_KERNEL_ACCOUNT);
	WARN_ON_ONCE(r == -EBUSY);
	if (r)
		goto unlock_vcpu_destroy;

	/*
	 * Now it's all set up, let userspace reach it.  Grab the vCPU's mutex
	 * so that userspace can't invoke vCPU ioctl()s until the vCPU is fully
	 * visible (per online_vcpus), e.g. so that KVM doesn't get tricked
	 * into a NULL-pointer dereference because KVM thinks the _current_
	 * vCPU doesn't exist.  As a bonus, taking vcpu->mutex ensures lockdep
	 * knows it's taken *inside* kvm->lock.
	 */
	mutex_lock(&vcpu->mutex);
	kvm_get_kvm(kvm);
	r = create_vcpu_fd(vcpu);
	if (r < 0)
		goto kvm_put_xa_erase;

	/*
	 * Pairs with smp_rmb() in kvm_get_vcpu.  Store the vcpu
	 * pointer before kvm->online_vcpu's incremented value.
	 */
	smp_wmb();
	atomic_inc(&kvm->online_vcpus);
	mutex_unlock(&vcpu->mutex);

	mutex_unlock(&kvm->lock);
	kvm_arch_vcpu_postcreate(vcpu);
	kvm_create_vcpu_debugfs(vcpu);
	return r;

kvm_put_xa_erase:
	mutex_unlock(&vcpu->mutex);
	kvm_put_kvm_no_destroy(kvm);
	xa_erase(&kvm->vcpu_array, vcpu->vcpu_idx);
unlock_vcpu_destroy:
	mutex_unlock(&kvm->lock);
	kvm_dirty_ring_free(&vcpu->dirty_ring);
arch_vcpu_destroy:
	kvm_arch_vcpu_destroy(vcpu);
vcpu_free_run_page:
	free_page((unsigned long)vcpu->run);
vcpu_free:
	kmem_cache_free(kvm_vcpu_cache, vcpu);
vcpu_decrement:
	mutex_lock(&kvm->lock);
	kvm->created_vcpus--;
	mutex_unlock(&kvm->lock);
	return r;
}

static int kvm_vcpu_ioctl_set_sigmask(struct kvm_vcpu *vcpu, sigset_t *sigset)
{
	if (sigset) {
		sigdelsetmask(sigset, sigmask(SIGKILL)|sigmask(SIGSTOP));
		vcpu->sigset_active = 1;
		vcpu->sigset = *sigset;
	} else
		vcpu->sigset_active = 0;
	return 0;
}

static ssize_t kvm_vcpu_stats_read(struct file *file, char __user *user_buffer,
			      size_t size, loff_t *offset)
{
	struct kvm_vcpu *vcpu = file->private_data;

	return kvm_stats_read(vcpu->stats_id, &kvm_vcpu_stats_header,
			&kvm_vcpu_stats_desc[0], &vcpu->stat,
			sizeof(vcpu->stat), user_buffer, size, offset);
}

static int kvm_vcpu_stats_release(struct inode *inode, struct file *file)
{
	struct kvm_vcpu *vcpu = file->private_data;

	kvm_put_kvm(vcpu->kvm);
	return 0;
}

static const struct file_operations kvm_vcpu_stats_fops = {
	.owner = THIS_MODULE,
	.read = kvm_vcpu_stats_read,
	.release = kvm_vcpu_stats_release,
	.llseek = noop_llseek,
};

static int kvm_vcpu_ioctl_get_stats_fd(struct kvm_vcpu *vcpu)
{
	int fd;
	struct file *file;
	char name[15 + ITOA_MAX_LEN + 1];

	snprintf(name, sizeof(name), "kvm-vcpu-stats:%d", vcpu->vcpu_id);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	file = anon_inode_getfile_fmode(name, &kvm_vcpu_stats_fops, vcpu,
					O_RDONLY, FMODE_PREAD);
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		return PTR_ERR(file);
	}

	kvm_get_kvm(vcpu->kvm);
	fd_install(fd, file);

	return fd;
}

#ifdef CONFIG_KVM_GENERIC_PRE_FAULT_MEMORY
static int kvm_vcpu_pre_fault_memory(struct kvm_vcpu *vcpu,
				     struct kvm_pre_fault_memory *range)
{
	int idx;
	long r;
	u64 full_size;

	if (range->flags)
		return -EINVAL;

	if (!PAGE_ALIGNED(range->gpa) ||
	    !PAGE_ALIGNED(range->size) ||
	    range->gpa + range->size <= range->gpa)
		return -EINVAL;

	vcpu_load(vcpu);
	idx = srcu_read_lock(&vcpu->kvm->srcu);

	full_size = range->size;
	do {
		if (signal_pending(current)) {
			r = -EINTR;
			break;
		}

		r = kvm_arch_vcpu_pre_fault_memory(vcpu, range);
		if (WARN_ON_ONCE(r == 0 || r == -EIO))
			break;

		if (r < 0)
			break;

		range->size -= r;
		range->gpa += r;
		cond_resched();
	} while (range->size);

	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	vcpu_put(vcpu);

	/* Return success if at least one page was mapped successfully.  */
	return full_size == range->size ? r : 0;
}
#endif

static int kvm_wait_for_vcpu_online(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	/*
	 * In practice, this happy path will always be taken, as a well-behaved
	 * VMM will never invoke a vCPU ioctl() before KVM_CREATE_VCPU returns.
	 */
	if (likely(vcpu->vcpu_idx < atomic_read(&kvm->online_vcpus)))
		return 0;

	/*
	 * Acquire and release the vCPU's mutex to wait for vCPU creation to
	 * complete (kvm_vm_ioctl_create_vcpu() holds the mutex until the vCPU
	 * is fully online).
	 */
	if (mutex_lock_killable(&vcpu->mutex))
		return -EINTR;

	mutex_unlock(&vcpu->mutex);

	if (WARN_ON_ONCE(!kvm_get_vcpu(kvm, vcpu->vcpu_idx)))
		return -EIO;

	return 0;
}

static long kvm_vcpu_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;
	struct kvm_fpu *fpu = NULL;
	struct kvm_sregs *kvm_sregs = NULL;

	if (vcpu->kvm->mm != current->mm || vcpu->kvm->vm_dead)
		return -EIO;

	if (unlikely(_IOC_TYPE(ioctl) != KVMIO))
		return -EINVAL;

	/*
	 * Wait for the vCPU to be online before handling the ioctl(), as KVM
	 * assumes the vCPU is reachable via vcpu_array, i.e. may dereference
	 * a NULL pointer if userspace invokes an ioctl() before KVM is ready.
	 */
	r = kvm_wait_for_vcpu_online(vcpu);
	if (r)
		return r;

	/*
	 * Some architectures have vcpu ioctls that are asynchronous to vcpu
	 * execution; mutex_lock() would break them.
	 */
	r = kvm_arch_vcpu_async_ioctl(filp, ioctl, arg);
	if (r != -ENOIOCTLCMD)
		return r;

	if (mutex_lock_killable(&vcpu->mutex))
		return -EINTR;
	switch (ioctl) {
	case KVM_RUN: {
		struct pid *oldpid;
		r = -EINVAL;
		if (arg)
			goto out;

		/*
		 * Note, vcpu->pid is primarily protected by vcpu->mutex. The
		 * dedicated r/w lock allows other tasks, e.g. other vCPUs, to
		 * read vcpu->pid while this vCPU is in KVM_RUN, e.g. to yield
		 * directly to this vCPU
		 */
		oldpid = vcpu->pid;
		if (unlikely(oldpid != task_pid(current))) {
			/* The thread running this VCPU changed. */
			struct pid *newpid;

			r = kvm_arch_vcpu_run_pid_change(vcpu);
			if (r)
				break;

			newpid = get_task_pid(current, PIDTYPE_PID);
			write_lock(&vcpu->pid_lock);
			vcpu->pid = newpid;
			write_unlock(&vcpu->pid_lock);

			put_pid(oldpid);
		}
		vcpu->wants_to_run = !READ_ONCE(vcpu->run->immediate_exit__unsafe);
		r = kvm_arch_vcpu_ioctl_run(vcpu);
		vcpu->wants_to_run = false;

		trace_kvm_userspace_exit(vcpu->run->exit_reason, r);
		break;
	}
	case KVM_GET_REGS: {
		struct kvm_regs *kvm_regs;

		r = -ENOMEM;
		kvm_regs = kzalloc(sizeof(struct kvm_regs), GFP_KERNEL);
		if (!kvm_regs)
			goto out;
		r = kvm_arch_vcpu_ioctl_get_regs(vcpu, kvm_regs);
		if (r)
			goto out_free1;
		r = -EFAULT;
		if (copy_to_user(argp, kvm_regs, sizeof(struct kvm_regs)))
			goto out_free1;
		r = 0;
out_free1:
		kfree(kvm_regs);
		break;
	}
	case KVM_SET_REGS: {
		struct kvm_regs *kvm_regs;

		kvm_regs = memdup_user(argp, sizeof(*kvm_regs));
		if (IS_ERR(kvm_regs)) {
			r = PTR_ERR(kvm_regs);
			goto out;
		}
		r = kvm_arch_vcpu_ioctl_set_regs(vcpu, kvm_regs);
		kfree(kvm_regs);
		break;
	}
	case KVM_GET_SREGS: {
		kvm_sregs = kzalloc(sizeof(struct kvm_sregs), GFP_KERNEL);
		r = -ENOMEM;
		if (!kvm_sregs)
			goto out;
		r = kvm_arch_vcpu_ioctl_get_sregs(vcpu, kvm_sregs);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, kvm_sregs, sizeof(struct kvm_sregs)))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_SREGS: {
		kvm_sregs = memdup_user(argp, sizeof(*kvm_sregs));
		if (IS_ERR(kvm_sregs)) {
			r = PTR_ERR(kvm_sregs);
			kvm_sregs = NULL;
			goto out;
		}
		r = kvm_arch_vcpu_ioctl_set_sregs(vcpu, kvm_sregs);
		break;
	}
	case KVM_GET_MP_STATE: {
		struct kvm_mp_state mp_state;

		r = kvm_arch_vcpu_ioctl_get_mpstate(vcpu, &mp_state);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &mp_state, sizeof(mp_state)))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_MP_STATE: {
		struct kvm_mp_state mp_state;

		r = -EFAULT;
		if (copy_from_user(&mp_state, argp, sizeof(mp_state)))
			goto out;
		r = kvm_arch_vcpu_ioctl_set_mpstate(vcpu, &mp_state);
		break;
	}
	case KVM_TRANSLATE: {
		struct kvm_translation tr;

		r = -EFAULT;
		if (copy_from_user(&tr, argp, sizeof(tr)))
			goto out;
		r = kvm_arch_vcpu_ioctl_translate(vcpu, &tr);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &tr, sizeof(tr)))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_GUEST_DEBUG: {
		struct kvm_guest_debug dbg;

		r = -EFAULT;
		if (copy_from_user(&dbg, argp, sizeof(dbg)))
			goto out;
		r = kvm_arch_vcpu_ioctl_set_guest_debug(vcpu, &dbg);
		break;
	}
	case KVM_SET_SIGNAL_MASK: {
		struct kvm_signal_mask __user *sigmask_arg = argp;
		struct kvm_signal_mask kvm_sigmask;
		sigset_t sigset, *p;

		p = NULL;
		if (argp) {
			r = -EFAULT;
			if (copy_from_user(&kvm_sigmask, argp,
					   sizeof(kvm_sigmask)))
				goto out;
			r = -EINVAL;
			if (kvm_sigmask.len != sizeof(sigset))
				goto out;
			r = -EFAULT;
			if (copy_from_user(&sigset, sigmask_arg->sigset,
					   sizeof(sigset)))
				goto out;
			p = &sigset;
		}
		r = kvm_vcpu_ioctl_set_sigmask(vcpu, p);
		break;
	}
	case KVM_GET_FPU: {
		fpu = kzalloc(sizeof(struct kvm_fpu), GFP_KERNEL);
		r = -ENOMEM;
		if (!fpu)
			goto out;
		r = kvm_arch_vcpu_ioctl_get_fpu(vcpu, fpu);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, fpu, sizeof(struct kvm_fpu)))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_FPU: {
		fpu = memdup_user(argp, sizeof(*fpu));
		if (IS_ERR(fpu)) {
			r = PTR_ERR(fpu);
			fpu = NULL;
			goto out;
		}
		r = kvm_arch_vcpu_ioctl_set_fpu(vcpu, fpu);
		break;
	}
	case KVM_GET_STATS_FD: {
		r = kvm_vcpu_ioctl_get_stats_fd(vcpu);
		break;
	}
#ifdef CONFIG_KVM_GENERIC_PRE_FAULT_MEMORY
	case KVM_PRE_FAULT_MEMORY: {
		struct kvm_pre_fault_memory range;

		r = -EFAULT;
		if (copy_from_user(&range, argp, sizeof(range)))
			break;
		r = kvm_vcpu_pre_fault_memory(vcpu, &range);
		/* Pass back leftover range. */
		if (copy_to_user(argp, &range, sizeof(range)))
			r = -EFAULT;
		break;
	}
#endif
	default:
		r = kvm_arch_vcpu_ioctl(filp, ioctl, arg);
	}
out:
	mutex_unlock(&vcpu->mutex);
	kfree(fpu);
	kfree(kvm_sregs);
	return r;
}

#ifdef CONFIG_KVM_COMPAT
static long kvm_vcpu_compat_ioctl(struct file *filp,
				  unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = compat_ptr(arg);
	int r;

	if (vcpu->kvm->mm != current->mm || vcpu->kvm->vm_dead)
		return -EIO;

	switch (ioctl) {
	case KVM_SET_SIGNAL_MASK: {
		struct kvm_signal_mask __user *sigmask_arg = argp;
		struct kvm_signal_mask kvm_sigmask;
		sigset_t sigset;

		if (argp) {
			r = -EFAULT;
			if (copy_from_user(&kvm_sigmask, argp,
					   sizeof(kvm_sigmask)))
				goto out;
			r = -EINVAL;
			if (kvm_sigmask.len != sizeof(compat_sigset_t))
				goto out;
			r = -EFAULT;
			if (get_compat_sigset(&sigset,
					      (compat_sigset_t __user *)sigmask_arg->sigset))
				goto out;
			r = kvm_vcpu_ioctl_set_sigmask(vcpu, &sigset);
		} else
			r = kvm_vcpu_ioctl_set_sigmask(vcpu, NULL);
		break;
	}
	default:
		r = kvm_vcpu_ioctl(filp, ioctl, arg);
	}

out:
	return r;
}
#endif

static int kvm_device_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct kvm_device *dev = filp->private_data;

	if (dev->ops->mmap)
		return dev->ops->mmap(dev, vma);

	return -ENODEV;
}

static int kvm_device_ioctl_attr(struct kvm_device *dev,
				 int (*accessor)(struct kvm_device *dev,
						 struct kvm_device_attr *attr),
				 unsigned long arg)
{
	struct kvm_device_attr attr;

	if (!accessor)
		return -EPERM;

	if (copy_from_user(&attr, (void __user *)arg, sizeof(attr)))
		return -EFAULT;

	return accessor(dev, &attr);
}

static long kvm_device_ioctl(struct file *filp, unsigned int ioctl,
			     unsigned long arg)
{
	struct kvm_device *dev = filp->private_data;

	if (dev->kvm->mm != current->mm || dev->kvm->vm_dead)
		return -EIO;

	switch (ioctl) {
	case KVM_SET_DEVICE_ATTR:
		return kvm_device_ioctl_attr(dev, dev->ops->set_attr, arg);
	case KVM_GET_DEVICE_ATTR:
		return kvm_device_ioctl_attr(dev, dev->ops->get_attr, arg);
	case KVM_HAS_DEVICE_ATTR:
		return kvm_device_ioctl_attr(dev, dev->ops->has_attr, arg);
	default:
		if (dev->ops->ioctl)
			return dev->ops->ioctl(dev, ioctl, arg);

		return -ENOTTY;
	}
}

static int kvm_device_release(struct inode *inode, struct file *filp)
{
	struct kvm_device *dev = filp->private_data;
	struct kvm *kvm = dev->kvm;

	if (dev->ops->release) {
		mutex_lock(&kvm->lock);
		list_del_rcu(&dev->vm_node);
		synchronize_rcu();
		dev->ops->release(dev);
		mutex_unlock(&kvm->lock);
	}

	kvm_put_kvm(kvm);
	return 0;
}

static struct file_operations kvm_device_fops = {
	.unlocked_ioctl = kvm_device_ioctl,
	.release = kvm_device_release,
	KVM_COMPAT(kvm_device_ioctl),
	.mmap = kvm_device_mmap,
};

struct kvm_device *kvm_device_from_filp(struct file *filp)
{
	if (filp->f_op != &kvm_device_fops)
		return NULL;

	return filp->private_data;
}

static const struct kvm_device_ops *kvm_device_ops_table[KVM_DEV_TYPE_MAX] = {
#ifdef CONFIG_KVM_MPIC
	[KVM_DEV_TYPE_FSL_MPIC_20]	= &kvm_mpic_ops,
	[KVM_DEV_TYPE_FSL_MPIC_42]	= &kvm_mpic_ops,
#endif
};

int kvm_register_device_ops(const struct kvm_device_ops *ops, u32 type)
{
	if (type >= ARRAY_SIZE(kvm_device_ops_table))
		return -ENOSPC;

	if (kvm_device_ops_table[type] != NULL)
		return -EEXIST;

	kvm_device_ops_table[type] = ops;
	return 0;
}

void kvm_unregister_device_ops(u32 type)
{
	if (kvm_device_ops_table[type] != NULL)
		kvm_device_ops_table[type] = NULL;
}

static int kvm_ioctl_create_device(struct kvm *kvm,
				   struct kvm_create_device *cd)
{
	const struct kvm_device_ops *ops;
	struct kvm_device *dev;
	bool test = cd->flags & KVM_CREATE_DEVICE_TEST;
	int type;
	int ret;

	if (cd->type >= ARRAY_SIZE(kvm_device_ops_table))
		return -ENODEV;

	type = array_index_nospec(cd->type, ARRAY_SIZE(kvm_device_ops_table));
	ops = kvm_device_ops_table[type];
	if (ops == NULL)
		return -ENODEV;

	if (test)
		return 0;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL_ACCOUNT);
	if (!dev)
		return -ENOMEM;

	dev->ops = ops;
	dev->kvm = kvm;

	mutex_lock(&kvm->lock);
	ret = ops->create(dev, type);
	if (ret < 0) {
		mutex_unlock(&kvm->lock);
		kfree(dev);
		return ret;
	}
	list_add_rcu(&dev->vm_node, &kvm->devices);
	mutex_unlock(&kvm->lock);

	if (ops->init)
		ops->init(dev);

	kvm_get_kvm(kvm);
	ret = anon_inode_getfd(ops->name, &kvm_device_fops, dev, O_RDWR | O_CLOEXEC);
	if (ret < 0) {
		kvm_put_kvm_no_destroy(kvm);
		mutex_lock(&kvm->lock);
		list_del_rcu(&dev->vm_node);
		synchronize_rcu();
		if (ops->release)
			ops->release(dev);
		mutex_unlock(&kvm->lock);
		if (ops->destroy)
			ops->destroy(dev);
		return ret;
	}

	cd->fd = ret;
	return 0;
}

static int kvm_vm_ioctl_check_extension_generic(struct kvm *kvm, long arg)
{
	switch (arg) {
	case KVM_CAP_USER_MEMORY:
	case KVM_CAP_USER_MEMORY2:
	case KVM_CAP_DESTROY_MEMORY_REGION_WORKS:
	case KVM_CAP_JOIN_MEMORY_REGIONS_WORKS:
	case KVM_CAP_INTERNAL_ERROR_DATA:
#ifdef CONFIG_HAVE_KVM_MSI
	case KVM_CAP_SIGNAL_MSI:
#endif
#ifdef CONFIG_HAVE_KVM_IRQCHIP
	case KVM_CAP_IRQFD:
#endif
	case KVM_CAP_IOEVENTFD_ANY_LENGTH:
	case KVM_CAP_CHECK_EXTENSION_VM:
	case KVM_CAP_ENABLE_CAP_VM:
	case KVM_CAP_HALT_POLL:
		return 1;
#ifdef CONFIG_KVM_MMIO
	case KVM_CAP_COALESCED_MMIO:
		return KVM_COALESCED_MMIO_PAGE_OFFSET;
	case KVM_CAP_COALESCED_PIO:
		return 1;
#endif
#ifdef CONFIG_KVM_GENERIC_DIRTYLOG_READ_PROTECT
	case KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2:
		return KVM_DIRTY_LOG_MANUAL_CAPS;
#endif
#ifdef CONFIG_HAVE_KVM_IRQ_ROUTING
	case KVM_CAP_IRQ_ROUTING:
		return KVM_MAX_IRQ_ROUTES;
#endif
#if KVM_MAX_NR_ADDRESS_SPACES > 1
	case KVM_CAP_MULTI_ADDRESS_SPACE:
		if (kvm)
			return kvm_arch_nr_memslot_as_ids(kvm);
		return KVM_MAX_NR_ADDRESS_SPACES;
#endif
	case KVM_CAP_NR_MEMSLOTS:
		return KVM_USER_MEM_SLOTS;
	case KVM_CAP_DIRTY_LOG_RING:
#ifdef CONFIG_HAVE_KVM_DIRTY_RING_TSO
		return KVM_DIRTY_RING_MAX_ENTRIES * sizeof(struct kvm_dirty_gfn);
#else
		return 0;
#endif
	case KVM_CAP_DIRTY_LOG_RING_ACQ_REL:
#ifdef CONFIG_HAVE_KVM_DIRTY_RING_ACQ_REL
		return KVM_DIRTY_RING_MAX_ENTRIES * sizeof(struct kvm_dirty_gfn);
#else
		return 0;
#endif
#ifdef CONFIG_NEED_KVM_DIRTY_RING_WITH_BITMAP
	case KVM_CAP_DIRTY_LOG_RING_WITH_BITMAP:
#endif
	case KVM_CAP_BINARY_STATS_FD:
	case KVM_CAP_SYSTEM_EVENT_DATA:
	case KVM_CAP_DEVICE_CTRL:
		return 1;
#ifdef CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES
	case KVM_CAP_MEMORY_ATTRIBUTES:
		return kvm_supported_mem_attributes(kvm);
#endif
#ifdef CONFIG_KVM_PRIVATE_MEM
	case KVM_CAP_GUEST_MEMFD:
		return !kvm || kvm_arch_has_private_mem(kvm);
#endif
	default:
		break;
	}
	return kvm_vm_ioctl_check_extension(kvm, arg);
}

static int kvm_vm_ioctl_enable_dirty_log_ring(struct kvm *kvm, u32 size)
{
	int r;

	if (!KVM_DIRTY_LOG_PAGE_OFFSET)
		return -EINVAL;

	/* the size should be power of 2 */
	if (!size || (size & (size - 1)))
		return -EINVAL;

	/* Should be bigger to keep the reserved entries, or a page */
	if (size < kvm_dirty_ring_get_rsvd_entries() *
	    sizeof(struct kvm_dirty_gfn) || size < PAGE_SIZE)
		return -EINVAL;

	if (size > KVM_DIRTY_RING_MAX_ENTRIES *
	    sizeof(struct kvm_dirty_gfn))
		return -E2BIG;

	/* We only allow it to set once */
	if (kvm->dirty_ring_size)
		return -EINVAL;

	mutex_lock(&kvm->lock);

	if (kvm->created_vcpus) {
		/* We don't allow to change this value after vcpu created */
		r = -EINVAL;
	} else {
		kvm->dirty_ring_size = size;
		r = 0;
	}

	mutex_unlock(&kvm->lock);
	return r;
}

static int kvm_vm_ioctl_reset_dirty_pages(struct kvm *kvm)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;
	int cleared = 0;

	if (!kvm->dirty_ring_size)
		return -EINVAL;

	mutex_lock(&kvm->slots_lock);

	kvm_for_each_vcpu(i, vcpu, kvm)
		cleared += kvm_dirty_ring_reset(vcpu->kvm, &vcpu->dirty_ring);

	mutex_unlock(&kvm->slots_lock);

	if (cleared)
		kvm_flush_remote_tlbs(kvm);

	return cleared;
}

int __attribute__((weak)) kvm_vm_ioctl_enable_cap(struct kvm *kvm,
						  struct kvm_enable_cap *cap)
{
	return -EINVAL;
}

bool kvm_are_all_memslots_empty(struct kvm *kvm)
{
	int i;

	lockdep_assert_held(&kvm->slots_lock);

	for (i = 0; i < kvm_arch_nr_memslot_as_ids(kvm); i++) {
		if (!kvm_memslots_empty(__kvm_memslots(kvm, i)))
			return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(kvm_are_all_memslots_empty);

static int kvm_vm_ioctl_enable_cap_generic(struct kvm *kvm,
					   struct kvm_enable_cap *cap)
{
	switch (cap->cap) {
#ifdef CONFIG_KVM_GENERIC_DIRTYLOG_READ_PROTECT
	case KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2: {
		u64 allowed_options = KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE;

		if (cap->args[0] & KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE)
			allowed_options = KVM_DIRTY_LOG_MANUAL_CAPS;

		if (cap->flags || (cap->args[0] & ~allowed_options))
			return -EINVAL;
		kvm->manual_dirty_log_protect = cap->args[0];
		return 0;
	}
#endif
	case KVM_CAP_HALT_POLL: {
		if (cap->flags || cap->args[0] != (unsigned int)cap->args[0])
			return -EINVAL;

		kvm->max_halt_poll_ns = cap->args[0];

		/*
		 * Ensure kvm->override_halt_poll_ns does not become visible
		 * before kvm->max_halt_poll_ns.
		 *
		 * Pairs with the smp_rmb() in kvm_vcpu_max_halt_poll_ns().
		 */
		smp_wmb();
		kvm->override_halt_poll_ns = true;

		return 0;
	}
	case KVM_CAP_DIRTY_LOG_RING:
	case KVM_CAP_DIRTY_LOG_RING_ACQ_REL:
		if (!kvm_vm_ioctl_check_extension_generic(kvm, cap->cap))
			return -EINVAL;

		return kvm_vm_ioctl_enable_dirty_log_ring(kvm, cap->args[0]);
	case KVM_CAP_DIRTY_LOG_RING_WITH_BITMAP: {
		int r = -EINVAL;

		if (!IS_ENABLED(CONFIG_NEED_KVM_DIRTY_RING_WITH_BITMAP) ||
		    !kvm->dirty_ring_size || cap->flags)
			return r;

		mutex_lock(&kvm->slots_lock);

		/*
		 * For simplicity, allow enabling ring+bitmap if and only if
		 * there are no memslots, e.g. to ensure all memslots allocate
		 * a bitmap after the capability is enabled.
		 */
		if (kvm_are_all_memslots_empty(kvm)) {
			kvm->dirty_ring_with_bitmap = true;
			r = 0;
		}

		mutex_unlock(&kvm->slots_lock);

		return r;
	}
	default:
		return kvm_vm_ioctl_enable_cap(kvm, cap);
	}
}

static ssize_t kvm_vm_stats_read(struct file *file, char __user *user_buffer,
			      size_t size, loff_t *offset)
{
	struct kvm *kvm = file->private_data;

	return kvm_stats_read(kvm->stats_id, &kvm_vm_stats_header,
				&kvm_vm_stats_desc[0], &kvm->stat,
				sizeof(kvm->stat), user_buffer, size, offset);
}

static int kvm_vm_stats_release(struct inode *inode, struct file *file)
{
	struct kvm *kvm = file->private_data;

	kvm_put_kvm(kvm);
	return 0;
}

static const struct file_operations kvm_vm_stats_fops = {
	.owner = THIS_MODULE,
	.read = kvm_vm_stats_read,
	.release = kvm_vm_stats_release,
	.llseek = noop_llseek,
};

static int kvm_vm_ioctl_get_stats_fd(struct kvm *kvm)
{
	int fd;
	struct file *file;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	file = anon_inode_getfile_fmode("kvm-vm-stats",
			&kvm_vm_stats_fops, kvm, O_RDONLY, FMODE_PREAD);
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		return PTR_ERR(file);
	}

	kvm_get_kvm(kvm);
	fd_install(fd, file);

	return fd;
}

#define SANITY_CHECK_MEM_REGION_FIELD(field)					\
do {										\
	BUILD_BUG_ON(offsetof(struct kvm_userspace_memory_region, field) !=		\
		     offsetof(struct kvm_userspace_memory_region2, field));	\
	BUILD_BUG_ON(sizeof_field(struct kvm_userspace_memory_region, field) !=		\
		     sizeof_field(struct kvm_userspace_memory_region2, field));	\
} while (0)

static long kvm_vm_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;

	if (kvm->mm != current->mm || kvm->vm_dead)
		return -EIO;
	switch (ioctl) {
	case KVM_CREATE_VCPU:
		r = kvm_vm_ioctl_create_vcpu(kvm, arg);
		break;
	case KVM_ENABLE_CAP: {
		struct kvm_enable_cap cap;

		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			goto out;
		r = kvm_vm_ioctl_enable_cap_generic(kvm, &cap);
		break;
	}
	case KVM_SET_USER_MEMORY_REGION2:
	case KVM_SET_USER_MEMORY_REGION: {
		struct kvm_userspace_memory_region2 mem;
		unsigned long size;

		if (ioctl == KVM_SET_USER_MEMORY_REGION) {
			/*
			 * Fields beyond struct kvm_userspace_memory_region shouldn't be
			 * accessed, but avoid leaking kernel memory in case of a bug.
			 */
			memset(&mem, 0, sizeof(mem));
			size = sizeof(struct kvm_userspace_memory_region);
		} else {
			size = sizeof(struct kvm_userspace_memory_region2);
		}

		/* Ensure the common parts of the two structs are identical. */
		SANITY_CHECK_MEM_REGION_FIELD(slot);
		SANITY_CHECK_MEM_REGION_FIELD(flags);
		SANITY_CHECK_MEM_REGION_FIELD(guest_phys_addr);
		SANITY_CHECK_MEM_REGION_FIELD(memory_size);
		SANITY_CHECK_MEM_REGION_FIELD(userspace_addr);

		r = -EFAULT;
		if (copy_from_user(&mem, argp, size))
			goto out;

		r = -EINVAL;
		if (ioctl == KVM_SET_USER_MEMORY_REGION &&
		    (mem.flags & ~KVM_SET_USER_MEMORY_REGION_V1_FLAGS))
			goto out;

		r = kvm_vm_ioctl_set_memory_region(kvm, &mem);
		break;
	}
	case KVM_GET_DIRTY_LOG: {
		struct kvm_dirty_log log;

		r = -EFAULT;
		if (copy_from_user(&log, argp, sizeof(log)))
			goto out;
		r = kvm_vm_ioctl_get_dirty_log(kvm, &log);
		break;
	}
#ifdef CONFIG_KVM_GENERIC_DIRTYLOG_READ_PROTECT
	case KVM_CLEAR_DIRTY_LOG: {
		struct kvm_clear_dirty_log log;

		r = -EFAULT;
		if (copy_from_user(&log, argp, sizeof(log)))
			goto out;
		r = kvm_vm_ioctl_clear_dirty_log(kvm, &log);
		break;
	}
#endif
#ifdef CONFIG_KVM_MMIO
	case KVM_REGISTER_COALESCED_MMIO: {
		struct kvm_coalesced_mmio_zone zone;

		r = -EFAULT;
		if (copy_from_user(&zone, argp, sizeof(zone)))
			goto out;
		r = kvm_vm_ioctl_register_coalesced_mmio(kvm, &zone);
		break;
	}
	case KVM_UNREGISTER_COALESCED_MMIO: {
		struct kvm_coalesced_mmio_zone zone;

		r = -EFAULT;
		if (copy_from_user(&zone, argp, sizeof(zone)))
			goto out;
		r = kvm_vm_ioctl_unregister_coalesced_mmio(kvm, &zone);
		break;
	}
#endif
	case KVM_IRQFD: {
		struct kvm_irqfd data;

		r = -EFAULT;
		if (copy_from_user(&data, argp, sizeof(data)))
			goto out;
		r = kvm_irqfd(kvm, &data);
		break;
	}
	case KVM_IOEVENTFD: {
		struct kvm_ioeventfd data;

		r = -EFAULT;
		if (copy_from_user(&data, argp, sizeof(data)))
			goto out;
		r = kvm_ioeventfd(kvm, &data);
		break;
	}
#ifdef CONFIG_HAVE_KVM_MSI
	case KVM_SIGNAL_MSI: {
		struct kvm_msi msi;

		r = -EFAULT;
		if (copy_from_user(&msi, argp, sizeof(msi)))
			goto out;
		r = kvm_send_userspace_msi(kvm, &msi);
		break;
	}
#endif
#ifdef __KVM_HAVE_IRQ_LINE
	case KVM_IRQ_LINE_STATUS:
	case KVM_IRQ_LINE: {
		struct kvm_irq_level irq_event;

		r = -EFAULT;
		if (copy_from_user(&irq_event, argp, sizeof(irq_event)))
			goto out;

		r = kvm_vm_ioctl_irq_line(kvm, &irq_event,
					ioctl == KVM_IRQ_LINE_STATUS);
		if (r)
			goto out;

		r = -EFAULT;
		if (ioctl == KVM_IRQ_LINE_STATUS) {
			if (copy_to_user(argp, &irq_event, sizeof(irq_event)))
				goto out;
		}

		r = 0;
		break;
	}
#endif
#ifdef CONFIG_HAVE_KVM_IRQ_ROUTING
	case KVM_SET_GSI_ROUTING: {
		struct kvm_irq_routing routing;
		struct kvm_irq_routing __user *urouting;
		struct kvm_irq_routing_entry *entries = NULL;

		r = -EFAULT;
		if (copy_from_user(&routing, argp, sizeof(routing)))
			goto out;
		r = -EINVAL;
		if (!kvm_arch_can_set_irq_routing(kvm))
			goto out;
		if (routing.nr > KVM_MAX_IRQ_ROUTES)
			goto out;
		if (routing.flags)
			goto out;
		if (routing.nr) {
			urouting = argp;
			entries = vmemdup_array_user(urouting->entries,
						     routing.nr, sizeof(*entries));
			if (IS_ERR(entries)) {
				r = PTR_ERR(entries);
				goto out;
			}
		}
		r = kvm_set_irq_routing(kvm, entries, routing.nr,
					routing.flags);
		kvfree(entries);
		break;
	}
#endif /* CONFIG_HAVE_KVM_IRQ_ROUTING */
#ifdef CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES
	case KVM_SET_MEMORY_ATTRIBUTES: {
		struct kvm_memory_attributes attrs;

		r = -EFAULT;
		if (copy_from_user(&attrs, argp, sizeof(attrs)))
			goto out;

		r = kvm_vm_ioctl_set_mem_attributes(kvm, &attrs);
		break;
	}
#endif /* CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES */
	case KVM_CREATE_DEVICE: {
		struct kvm_create_device cd;

		r = -EFAULT;
		if (copy_from_user(&cd, argp, sizeof(cd)))
			goto out;

		r = kvm_ioctl_create_device(kvm, &cd);
		if (r)
			goto out;

		r = -EFAULT;
		if (copy_to_user(argp, &cd, sizeof(cd)))
			goto out;

		r = 0;
		break;
	}
	case KVM_CHECK_EXTENSION:
		r = kvm_vm_ioctl_check_extension_generic(kvm, arg);
		break;
	case KVM_RESET_DIRTY_RINGS:
		r = kvm_vm_ioctl_reset_dirty_pages(kvm);
		break;
	case KVM_GET_STATS_FD:
		r = kvm_vm_ioctl_get_stats_fd(kvm);
		break;
#ifdef CONFIG_KVM_PRIVATE_MEM
	case KVM_CREATE_GUEST_MEMFD: {
		struct kvm_create_guest_memfd guest_memfd;

		r = -EFAULT;
		if (copy_from_user(&guest_memfd, argp, sizeof(guest_memfd)))
			goto out;

		r = kvm_gmem_create(kvm, &guest_memfd);
		break;
	}
#endif
	default:
		r = kvm_arch_vm_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}

#ifdef CONFIG_KVM_COMPAT
struct compat_kvm_dirty_log {
	__u32 slot;
	__u32 padding1;
	union {
		compat_uptr_t dirty_bitmap; /* one bit per page */
		__u64 padding2;
	};
};

struct compat_kvm_clear_dirty_log {
	__u32 slot;
	__u32 num_pages;
	__u64 first_page;
	union {
		compat_uptr_t dirty_bitmap; /* one bit per page */
		__u64 padding2;
	};
};

long __weak kvm_arch_vm_compat_ioctl(struct file *filp, unsigned int ioctl,
				     unsigned long arg)
{
	return -ENOTTY;
}

static long kvm_vm_compat_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	int r;

	if (kvm->mm != current->mm || kvm->vm_dead)
		return -EIO;

	r = kvm_arch_vm_compat_ioctl(filp, ioctl, arg);
	if (r != -ENOTTY)
		return r;

	switch (ioctl) {
#ifdef CONFIG_KVM_GENERIC_DIRTYLOG_READ_PROTECT
	case KVM_CLEAR_DIRTY_LOG: {
		struct compat_kvm_clear_dirty_log compat_log;
		struct kvm_clear_dirty_log log;

		if (copy_from_user(&compat_log, (void __user *)arg,
				   sizeof(compat_log)))
			return -EFAULT;
		log.slot	 = compat_log.slot;
		log.num_pages	 = compat_log.num_pages;
		log.first_page	 = compat_log.first_page;
		log.padding2	 = compat_log.padding2;
		log.dirty_bitmap = compat_ptr(compat_log.dirty_bitmap);

		r = kvm_vm_ioctl_clear_dirty_log(kvm, &log);
		break;
	}
#endif
	case KVM_GET_DIRTY_LOG: {
		struct compat_kvm_dirty_log compat_log;
		struct kvm_dirty_log log;

		if (copy_from_user(&compat_log, (void __user *)arg,
				   sizeof(compat_log)))
			return -EFAULT;
		log.slot	 = compat_log.slot;
		log.padding1	 = compat_log.padding1;
		log.padding2	 = compat_log.padding2;
		log.dirty_bitmap = compat_ptr(compat_log.dirty_bitmap);

		r = kvm_vm_ioctl_get_dirty_log(kvm, &log);
		break;
	}
	default:
		r = kvm_vm_ioctl(filp, ioctl, arg);
	}
	return r;
}
#endif

static struct file_operations kvm_vm_fops = {
	.release        = kvm_vm_release,
	.unlocked_ioctl = kvm_vm_ioctl,
	.llseek		= noop_llseek,
	KVM_COMPAT(kvm_vm_compat_ioctl),
};

bool file_is_kvm(struct file *file)
{
	return file && file->f_op == &kvm_vm_fops;
}
EXPORT_SYMBOL_GPL(file_is_kvm);

static int kvm_dev_ioctl_create_vm(unsigned long type)
{
	char fdname[ITOA_MAX_LEN + 1];
	int r, fd;
	struct kvm *kvm;
	struct file *file;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	snprintf(fdname, sizeof(fdname), "%d", fd);

	kvm = kvm_create_vm(type, fdname);
	if (IS_ERR(kvm)) {
		r = PTR_ERR(kvm);
		goto put_fd;
	}

	file = anon_inode_getfile("kvm-vm", &kvm_vm_fops, kvm, O_RDWR);
	if (IS_ERR(file)) {
		r = PTR_ERR(file);
		goto put_kvm;
	}

	/*
	 * Don't call kvm_put_kvm anymore at this point; file->f_op is
	 * already set, with ->release() being kvm_vm_release().  In error
	 * cases it will be called by the final fput(file) and will take
	 * care of doing kvm_put_kvm(kvm).
	 */
	kvm_uevent_notify_change(KVM_EVENT_CREATE_VM, kvm);

	fd_install(fd, file);
	return fd;

put_kvm:
	kvm_put_kvm(kvm);
put_fd:
	put_unused_fd(fd);
	return r;
}

static long kvm_dev_ioctl(struct file *filp,
			  unsigned int ioctl, unsigned long arg)
{
	int r = -EINVAL;

	switch (ioctl) {
	case KVM_GET_API_VERSION:
		if (arg)
			goto out;
		r = KVM_API_VERSION;
		break;
	case KVM_CREATE_VM:
		r = kvm_dev_ioctl_create_vm(arg);
		break;
	case KVM_CHECK_EXTENSION:
		r = kvm_vm_ioctl_check_extension_generic(NULL, arg);
		break;
	case KVM_GET_VCPU_MMAP_SIZE:
		if (arg)
			goto out;
		r = PAGE_SIZE;     /* struct kvm_run */
#ifdef CONFIG_X86
		r += PAGE_SIZE;    /* pio data page */
#endif
#ifdef CONFIG_KVM_MMIO
		r += PAGE_SIZE;    /* coalesced mmio ring page */
#endif
		break;
	default:
		return kvm_arch_dev_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}

static struct file_operations kvm_chardev_ops = {
	.unlocked_ioctl = kvm_dev_ioctl,
	.llseek		= noop_llseek,
	KVM_COMPAT(kvm_dev_ioctl),
};

static struct miscdevice kvm_dev = {
	KVM_MINOR,
	"kvm",
	&kvm_chardev_ops,
};

#ifdef CONFIG_KVM_GENERIC_HARDWARE_ENABLING
static bool enable_virt_at_load = true;
module_param(enable_virt_at_load, bool, 0444);

__visible bool kvm_rebooting;
EXPORT_SYMBOL_GPL(kvm_rebooting);

static DEFINE_PER_CPU(bool, virtualization_enabled);
static DEFINE_MUTEX(kvm_usage_lock);
static int kvm_usage_count;

__weak void kvm_arch_enable_virtualization(void)
{

}

__weak void kvm_arch_disable_virtualization(void)
{

}

static int kvm_enable_virtualization_cpu(void)
{
	if (__this_cpu_read(virtualization_enabled))
		return 0;

	if (kvm_arch_enable_virtualization_cpu()) {
		pr_info("kvm: enabling virtualization on CPU%d failed\n",
			raw_smp_processor_id());
		return -EIO;
	}

	__this_cpu_write(virtualization_enabled, true);
	return 0;
}

static int kvm_online_cpu(unsigned int cpu)
{
	/*
	 * Abort the CPU online process if hardware virtualization cannot
	 * be enabled. Otherwise running VMs would encounter unrecoverable
	 * errors when scheduled to this CPU.
	 */
	return kvm_enable_virtualization_cpu();
}

static void kvm_disable_virtualization_cpu(void *ign)
{
	if (!__this_cpu_read(virtualization_enabled))
		return;

	kvm_arch_disable_virtualization_cpu();

	__this_cpu_write(virtualization_enabled, false);
}

static int kvm_offline_cpu(unsigned int cpu)
{
	kvm_disable_virtualization_cpu(NULL);
	return 0;
}

static void kvm_shutdown(void)
{
	/*
	 * Disable hardware virtualization and set kvm_rebooting to indicate
	 * that KVM has asynchronously disabled hardware virtualization, i.e.
	 * that relevant errors and exceptions aren't entirely unexpected.
	 * Some flavors of hardware virtualization need to be disabled before
	 * transferring control to firmware (to perform shutdown/reboot), e.g.
	 * on x86, virtualization can block INIT interrupts, which are used by
	 * firmware to pull APs back under firmware control.  Note, this path
	 * is used for both shutdown and reboot scenarios, i.e. neither name is
	 * 100% comprehensive.
	 */
	pr_info("kvm: exiting hardware virtualization\n");
	kvm_rebooting = true;
	on_each_cpu(kvm_disable_virtualization_cpu, NULL, 1);
}

static int kvm_suspend(void)
{
	/*
	 * Secondary CPUs and CPU hotplug are disabled across the suspend/resume
	 * callbacks, i.e. no need to acquire kvm_usage_lock to ensure the usage
	 * count is stable.  Assert that kvm_usage_lock is not held to ensure
	 * the system isn't suspended while KVM is enabling hardware.  Hardware
	 * enabling can be preempted, but the task cannot be frozen until it has
	 * dropped all locks (userspace tasks are frozen via a fake signal).
	 */
	lockdep_assert_not_held(&kvm_usage_lock);
	lockdep_assert_irqs_disabled();

	kvm_disable_virtualization_cpu(NULL);
	return 0;
}

static void kvm_resume(void)
{
	lockdep_assert_not_held(&kvm_usage_lock);
	lockdep_assert_irqs_disabled();

	WARN_ON_ONCE(kvm_enable_virtualization_cpu());
}

static struct syscore_ops kvm_syscore_ops = {
	.suspend = kvm_suspend,
	.resume = kvm_resume,
	.shutdown = kvm_shutdown,
};

static int kvm_enable_virtualization(void)
{
	int r;

	guard(mutex)(&kvm_usage_lock);

	if (kvm_usage_count++)
		return 0;

	kvm_arch_enable_virtualization();

	r = cpuhp_setup_state(CPUHP_AP_KVM_ONLINE, "kvm/cpu:online",
			      kvm_online_cpu, kvm_offline_cpu);
	if (r)
		goto err_cpuhp;

	register_syscore_ops(&kvm_syscore_ops);

	/*
	 * Undo virtualization enabling and bail if the system is going down.
	 * If userspace initiated a forced reboot, e.g. reboot -f, then it's
	 * possible for an in-flight operation to enable virtualization after
	 * syscore_shutdown() is called, i.e. without kvm_shutdown() being
	 * invoked.  Note, this relies on system_state being set _before_
	 * kvm_shutdown(), e.g. to ensure either kvm_shutdown() is invoked
	 * or this CPU observes the impending shutdown.  Which is why KVM uses
	 * a syscore ops hook instead of registering a dedicated reboot
	 * notifier (the latter runs before system_state is updated).
	 */
	if (system_state == SYSTEM_HALT || system_state == SYSTEM_POWER_OFF ||
	    system_state == SYSTEM_RESTART) {
		r = -EBUSY;
		goto err_rebooting;
	}

	return 0;

err_rebooting:
	unregister_syscore_ops(&kvm_syscore_ops);
	cpuhp_remove_state(CPUHP_AP_KVM_ONLINE);
err_cpuhp:
	kvm_arch_disable_virtualization();
	--kvm_usage_count;
	return r;
}

static void kvm_disable_virtualization(void)
{
	guard(mutex)(&kvm_usage_lock);

	if (--kvm_usage_count)
		return;

	unregister_syscore_ops(&kvm_syscore_ops);
	cpuhp_remove_state(CPUHP_AP_KVM_ONLINE);
	kvm_arch_disable_virtualization();
}

static int kvm_init_virtualization(void)
{
	if (enable_virt_at_load)
		return kvm_enable_virtualization();

	return 0;
}

static void kvm_uninit_virtualization(void)
{
	if (enable_virt_at_load)
		kvm_disable_virtualization();
}
#else /* CONFIG_KVM_GENERIC_HARDWARE_ENABLING */
static int kvm_enable_virtualization(void)
{
	return 0;
}

static int kvm_init_virtualization(void)
{
	return 0;
}

static void kvm_disable_virtualization(void)
{

}

static void kvm_uninit_virtualization(void)
{

}
#endif /* CONFIG_KVM_GENERIC_HARDWARE_ENABLING */

static void kvm_iodevice_destructor(struct kvm_io_device *dev)
{
	if (dev->ops->destructor)
		dev->ops->destructor(dev);
}

static void kvm_io_bus_destroy(struct kvm_io_bus *bus)
{
	int i;

	for (i = 0; i < bus->dev_count; i++) {
		struct kvm_io_device *pos = bus->range[i].dev;

		kvm_iodevice_destructor(pos);
	}
	kfree(bus);
}

static inline int kvm_io_bus_cmp(const struct kvm_io_range *r1,
				 const struct kvm_io_range *r2)
{
	gpa_t addr1 = r1->addr;
	gpa_t addr2 = r2->addr;

	if (addr1 < addr2)
		return -1;

	/* If r2->len == 0, match the exact address.  If r2->len != 0,
	 * accept any overlapping write.  Any order is acceptable for
	 * overlapping ranges, because kvm_io_bus_get_first_dev ensures
	 * we process all of them.
	 */
	if (r2->len) {
		addr1 += r1->len;
		addr2 += r2->len;
	}

	if (addr1 > addr2)
		return 1;

	return 0;
}

static int kvm_io_bus_sort_cmp(const void *p1, const void *p2)
{
	return kvm_io_bus_cmp(p1, p2);
}

static int kvm_io_bus_get_first_dev(struct kvm_io_bus *bus,
			     gpa_t addr, int len)
{
	struct kvm_io_range *range, key;
	int off;

	key = (struct kvm_io_range) {
		.addr = addr,
		.len = len,
	};

	range = bsearch(&key, bus->range, bus->dev_count,
			sizeof(struct kvm_io_range), kvm_io_bus_sort_cmp);
	if (range == NULL)
		return -ENOENT;

	off = range - bus->range;

	while (off > 0 && kvm_io_bus_cmp(&key, &bus->range[off-1]) == 0)
		off--;

	return off;
}

static int __kvm_io_bus_write(struct kvm_vcpu *vcpu, struct kvm_io_bus *bus,
			      struct kvm_io_range *range, const void *val)
{
	int idx;

	idx = kvm_io_bus_get_first_dev(bus, range->addr, range->len);
	if (idx < 0)
		return -EOPNOTSUPP;

	while (idx < bus->dev_count &&
		kvm_io_bus_cmp(range, &bus->range[idx]) == 0) {
		if (!kvm_iodevice_write(vcpu, bus->range[idx].dev, range->addr,
					range->len, val))
			return idx;
		idx++;
	}

	return -EOPNOTSUPP;
}

/* kvm_io_bus_write - called under kvm->slots_lock */
int kvm_io_bus_write(struct kvm_vcpu *vcpu, enum kvm_bus bus_idx, gpa_t addr,
		     int len, const void *val)
{
	struct kvm_io_bus *bus;
	struct kvm_io_range range;
	int r;

	range = (struct kvm_io_range) {
		.addr = addr,
		.len = len,
	};

	bus = srcu_dereference(vcpu->kvm->buses[bus_idx], &vcpu->kvm->srcu);
	if (!bus)
		return -ENOMEM;
	r = __kvm_io_bus_write(vcpu, bus, &range, val);
	return r < 0 ? r : 0;
}
EXPORT_SYMBOL_GPL(kvm_io_bus_write);

/* kvm_io_bus_write_cookie - called under kvm->slots_lock */
int kvm_io_bus_write_cookie(struct kvm_vcpu *vcpu, enum kvm_bus bus_idx,
			    gpa_t addr, int len, const void *val, long cookie)
{
	struct kvm_io_bus *bus;
	struct kvm_io_range range;

	range = (struct kvm_io_range) {
		.addr = addr,
		.len = len,
	};

	bus = srcu_dereference(vcpu->kvm->buses[bus_idx], &vcpu->kvm->srcu);
	if (!bus)
		return -ENOMEM;

	/* First try the device referenced by cookie. */
	if ((cookie >= 0) && (cookie < bus->dev_count) &&
	    (kvm_io_bus_cmp(&range, &bus->range[cookie]) == 0))
		if (!kvm_iodevice_write(vcpu, bus->range[cookie].dev, addr, len,
					val))
			return cookie;

	/*
	 * cookie contained garbage; fall back to search and return the
	 * correct cookie value.
	 */
	return __kvm_io_bus_write(vcpu, bus, &range, val);
}

static int __kvm_io_bus_read(struct kvm_vcpu *vcpu, struct kvm_io_bus *bus,
			     struct kvm_io_range *range, void *val)
{
	int idx;

	idx = kvm_io_bus_get_first_dev(bus, range->addr, range->len);
	if (idx < 0)
		return -EOPNOTSUPP;

	while (idx < bus->dev_count &&
		kvm_io_bus_cmp(range, &bus->range[idx]) == 0) {
		if (!kvm_iodevice_read(vcpu, bus->range[idx].dev, range->addr,
				       range->len, val))
			return idx;
		idx++;
	}

	return -EOPNOTSUPP;
}

/* kvm_io_bus_read - called under kvm->slots_lock */
int kvm_io_bus_read(struct kvm_vcpu *vcpu, enum kvm_bus bus_idx, gpa_t addr,
		    int len, void *val)
{
	struct kvm_io_bus *bus;
	struct kvm_io_range range;
	int r;

	range = (struct kvm_io_range) {
		.addr = addr,
		.len = len,
	};

	bus = srcu_dereference(vcpu->kvm->buses[bus_idx], &vcpu->kvm->srcu);
	if (!bus)
		return -ENOMEM;
	r = __kvm_io_bus_read(vcpu, bus, &range, val);
	return r < 0 ? r : 0;
}

int kvm_io_bus_register_dev(struct kvm *kvm, enum kvm_bus bus_idx, gpa_t addr,
			    int len, struct kvm_io_device *dev)
{
	int i;
	struct kvm_io_bus *new_bus, *bus;
	struct kvm_io_range range;

	lockdep_assert_held(&kvm->slots_lock);

	bus = kvm_get_bus(kvm, bus_idx);
	if (!bus)
		return -ENOMEM;

	/* exclude ioeventfd which is limited by maximum fd */
	if (bus->dev_count - bus->ioeventfd_count > NR_IOBUS_DEVS - 1)
		return -ENOSPC;

	new_bus = kmalloc(struct_size(bus, range, bus->dev_count + 1),
			  GFP_KERNEL_ACCOUNT);
	if (!new_bus)
		return -ENOMEM;

	range = (struct kvm_io_range) {
		.addr = addr,
		.len = len,
		.dev = dev,
	};

	for (i = 0; i < bus->dev_count; i++)
		if (kvm_io_bus_cmp(&bus->range[i], &range) > 0)
			break;

	memcpy(new_bus, bus, sizeof(*bus) + i * sizeof(struct kvm_io_range));
	new_bus->dev_count++;
	new_bus->range[i] = range;
	memcpy(new_bus->range + i + 1, bus->range + i,
		(bus->dev_count - i) * sizeof(struct kvm_io_range));
	rcu_assign_pointer(kvm->buses[bus_idx], new_bus);
	synchronize_srcu_expedited(&kvm->srcu);
	kfree(bus);

	return 0;
}

int kvm_io_bus_unregister_dev(struct kvm *kvm, enum kvm_bus bus_idx,
			      struct kvm_io_device *dev)
{
	int i;
	struct kvm_io_bus *new_bus, *bus;

	lockdep_assert_held(&kvm->slots_lock);

	bus = kvm_get_bus(kvm, bus_idx);
	if (!bus)
		return 0;

	for (i = 0; i < bus->dev_count; i++) {
		if (bus->range[i].dev == dev) {
			break;
		}
	}

	if (i == bus->dev_count)
		return 0;

	new_bus = kmalloc(struct_size(bus, range, bus->dev_count - 1),
			  GFP_KERNEL_ACCOUNT);
	if (new_bus) {
		memcpy(new_bus, bus, struct_size(bus, range, i));
		new_bus->dev_count--;
		memcpy(new_bus->range + i, bus->range + i + 1,
				flex_array_size(new_bus, range, new_bus->dev_count - i));
	}

	rcu_assign_pointer(kvm->buses[bus_idx], new_bus);
	synchronize_srcu_expedited(&kvm->srcu);

	/*
	 * If NULL bus is installed, destroy the old bus, including all the
	 * attached devices. Otherwise, destroy the caller's device only.
	 */
	if (!new_bus) {
		pr_err("kvm: failed to shrink bus, removing it completely\n");
		kvm_io_bus_destroy(bus);
		return -ENOMEM;
	}

	kvm_iodevice_destructor(dev);
	kfree(bus);
	return 0;
}

struct kvm_io_device *kvm_io_bus_get_dev(struct kvm *kvm, enum kvm_bus bus_idx,
					 gpa_t addr)
{
	struct kvm_io_bus *bus;
	int dev_idx, srcu_idx;
	struct kvm_io_device *iodev = NULL;

	srcu_idx = srcu_read_lock(&kvm->srcu);

	bus = srcu_dereference(kvm->buses[bus_idx], &kvm->srcu);
	if (!bus)
		goto out_unlock;

	dev_idx = kvm_io_bus_get_first_dev(bus, addr, 1);
	if (dev_idx < 0)
		goto out_unlock;

	iodev = bus->range[dev_idx].dev;

out_unlock:
	srcu_read_unlock(&kvm->srcu, srcu_idx);

	return iodev;
}
EXPORT_SYMBOL_GPL(kvm_io_bus_get_dev);

static int kvm_debugfs_open(struct inode *inode, struct file *file,
			   int (*get)(void *, u64 *), int (*set)(void *, u64),
			   const char *fmt)
{
	int ret;
	struct kvm_stat_data *stat_data = inode->i_private;

	/*
	 * The debugfs files are a reference to the kvm struct which
        * is still valid when kvm_destroy_vm is called.  kvm_get_kvm_safe
        * avoids the race between open and the removal of the debugfs directory.
	 */
	if (!kvm_get_kvm_safe(stat_data->kvm))
		return -ENOENT;

	ret = simple_attr_open(inode, file, get,
			       kvm_stats_debugfs_mode(stat_data->desc) & 0222
			       ? set : NULL, fmt);
	if (ret)
		kvm_put_kvm(stat_data->kvm);

	return ret;
}

static int kvm_debugfs_release(struct inode *inode, struct file *file)
{
	struct kvm_stat_data *stat_data = inode->i_private;

	simple_attr_release(inode, file);
	kvm_put_kvm(stat_data->kvm);

	return 0;
}

static int kvm_get_stat_per_vm(struct kvm *kvm, size_t offset, u64 *val)
{
	*val = *(u64 *)((void *)(&kvm->stat) + offset);

	return 0;
}

static int kvm_clear_stat_per_vm(struct kvm *kvm, size_t offset)
{
	*(u64 *)((void *)(&kvm->stat) + offset) = 0;

	return 0;
}

static int kvm_get_stat_per_vcpu(struct kvm *kvm, size_t offset, u64 *val)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	*val = 0;

	kvm_for_each_vcpu(i, vcpu, kvm)
		*val += *(u64 *)((void *)(&vcpu->stat) + offset);

	return 0;
}

static int kvm_clear_stat_per_vcpu(struct kvm *kvm, size_t offset)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm)
		*(u64 *)((void *)(&vcpu->stat) + offset) = 0;

	return 0;
}

static int kvm_stat_data_get(void *data, u64 *val)
{
	int r = -EFAULT;
	struct kvm_stat_data *stat_data = data;

	switch (stat_data->kind) {
	case KVM_STAT_VM:
		r = kvm_get_stat_per_vm(stat_data->kvm,
					stat_data->desc->desc.offset, val);
		break;
	case KVM_STAT_VCPU:
		r = kvm_get_stat_per_vcpu(stat_data->kvm,
					  stat_data->desc->desc.offset, val);
		break;
	}

	return r;
}

static int kvm_stat_data_clear(void *data, u64 val)
{
	int r = -EFAULT;
	struct kvm_stat_data *stat_data = data;

	if (val)
		return -EINVAL;

	switch (stat_data->kind) {
	case KVM_STAT_VM:
		r = kvm_clear_stat_per_vm(stat_data->kvm,
					  stat_data->desc->desc.offset);
		break;
	case KVM_STAT_VCPU:
		r = kvm_clear_stat_per_vcpu(stat_data->kvm,
					    stat_data->desc->desc.offset);
		break;
	}

	return r;
}

static int kvm_stat_data_open(struct inode *inode, struct file *file)
{
	__simple_attr_check_format("%llu\n", 0ull);
	return kvm_debugfs_open(inode, file, kvm_stat_data_get,
				kvm_stat_data_clear, "%llu\n");
}

static const struct file_operations stat_fops_per_vm = {
	.owner = THIS_MODULE,
	.open = kvm_stat_data_open,
	.release = kvm_debugfs_release,
	.read = simple_attr_read,
	.write = simple_attr_write,
};

static int vm_stat_get(void *_offset, u64 *val)
{
	unsigned offset = (long)_offset;
	struct kvm *kvm;
	u64 tmp_val;

	*val = 0;
	mutex_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list) {
		kvm_get_stat_per_vm(kvm, offset, &tmp_val);
		*val += tmp_val;
	}
	mutex_unlock(&kvm_lock);
	return 0;
}

static int vm_stat_clear(void *_offset, u64 val)
{
	unsigned offset = (long)_offset;
	struct kvm *kvm;

	if (val)
		return -EINVAL;

	mutex_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list) {
		kvm_clear_stat_per_vm(kvm, offset);
	}
	mutex_unlock(&kvm_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vm_stat_fops, vm_stat_get, vm_stat_clear, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(vm_stat_readonly_fops, vm_stat_get, NULL, "%llu\n");

static int vcpu_stat_get(void *_offset, u64 *val)
{
	unsigned offset = (long)_offset;
	struct kvm *kvm;
	u64 tmp_val;

	*val = 0;
	mutex_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list) {
		kvm_get_stat_per_vcpu(kvm, offset, &tmp_val);
		*val += tmp_val;
	}
	mutex_unlock(&kvm_lock);
	return 0;
}

static int vcpu_stat_clear(void *_offset, u64 val)
{
	unsigned offset = (long)_offset;
	struct kvm *kvm;

	if (val)
		return -EINVAL;

	mutex_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list) {
		kvm_clear_stat_per_vcpu(kvm, offset);
	}
	mutex_unlock(&kvm_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vcpu_stat_fops, vcpu_stat_get, vcpu_stat_clear,
			"%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(vcpu_stat_readonly_fops, vcpu_stat_get, NULL, "%llu\n");

static void kvm_uevent_notify_change(unsigned int type, struct kvm *kvm)
{
	struct kobj_uevent_env *env;
	unsigned long long created, active;

	if (!kvm_dev.this_device || !kvm)
		return;

	mutex_lock(&kvm_lock);
	if (type == KVM_EVENT_CREATE_VM) {
		kvm_createvm_count++;
		kvm_active_vms++;
	} else if (type == KVM_EVENT_DESTROY_VM) {
		kvm_active_vms--;
	}
	created = kvm_createvm_count;
	active = kvm_active_vms;
	mutex_unlock(&kvm_lock);

	env = kzalloc(sizeof(*env), GFP_KERNEL);
	if (!env)
		return;

	add_uevent_var(env, "CREATED=%llu", created);
	add_uevent_var(env, "COUNT=%llu", active);

	if (type == KVM_EVENT_CREATE_VM) {
		add_uevent_var(env, "EVENT=create");
		kvm->userspace_pid = task_pid_nr(current);
	} else if (type == KVM_EVENT_DESTROY_VM) {
		add_uevent_var(env, "EVENT=destroy");
	}
	add_uevent_var(env, "PID=%d", kvm->userspace_pid);

	if (!IS_ERR(kvm->debugfs_dentry)) {
		char *tmp, *p = kmalloc(PATH_MAX, GFP_KERNEL);

		if (p) {
			tmp = dentry_path_raw(kvm->debugfs_dentry, p, PATH_MAX);
			if (!IS_ERR(tmp))
				add_uevent_var(env, "STATS_PATH=%s", tmp);
			kfree(p);
		}
	}
	/* no need for checks, since we are adding at most only 5 keys */
	env->envp[env->envp_idx++] = NULL;
	kobject_uevent_env(&kvm_dev.this_device->kobj, KOBJ_CHANGE, env->envp);
	kfree(env);
}

static void kvm_init_debug(void)
{
	const struct file_operations *fops;
	const struct _kvm_stats_desc *pdesc;
	int i;

	kvm_debugfs_dir = debugfs_create_dir("kvm", NULL);

	for (i = 0; i < kvm_vm_stats_header.num_desc; ++i) {
		pdesc = &kvm_vm_stats_desc[i];
		if (kvm_stats_debugfs_mode(pdesc) & 0222)
			fops = &vm_stat_fops;
		else
			fops = &vm_stat_readonly_fops;
		debugfs_create_file(pdesc->name, kvm_stats_debugfs_mode(pdesc),
				kvm_debugfs_dir,
				(void *)(long)pdesc->desc.offset, fops);
	}

	for (i = 0; i < kvm_vcpu_stats_header.num_desc; ++i) {
		pdesc = &kvm_vcpu_stats_desc[i];
		if (kvm_stats_debugfs_mode(pdesc) & 0222)
			fops = &vcpu_stat_fops;
		else
			fops = &vcpu_stat_readonly_fops;
		debugfs_create_file(pdesc->name, kvm_stats_debugfs_mode(pdesc),
				kvm_debugfs_dir,
				(void *)(long)pdesc->desc.offset, fops);
	}
}

static inline
struct kvm_vcpu *preempt_notifier_to_vcpu(struct preempt_notifier *pn)
{
	return container_of(pn, struct kvm_vcpu, preempt_notifier);
}

static void kvm_sched_in(struct preempt_notifier *pn, int cpu)
{
	struct kvm_vcpu *vcpu = preempt_notifier_to_vcpu(pn);

	WRITE_ONCE(vcpu->preempted, false);
	WRITE_ONCE(vcpu->ready, false);

	__this_cpu_write(kvm_running_vcpu, vcpu);
	kvm_arch_vcpu_load(vcpu, cpu);

	WRITE_ONCE(vcpu->scheduled_out, false);
}

static void kvm_sched_out(struct preempt_notifier *pn,
			  struct task_struct *next)
{
	struct kvm_vcpu *vcpu = preempt_notifier_to_vcpu(pn);

	WRITE_ONCE(vcpu->scheduled_out, true);

	if (task_is_runnable(current) && vcpu->wants_to_run) {
		WRITE_ONCE(vcpu->preempted, true);
		WRITE_ONCE(vcpu->ready, true);
	}
	kvm_arch_vcpu_put(vcpu);
	__this_cpu_write(kvm_running_vcpu, NULL);
}

/**
 * kvm_get_running_vcpu - get the vcpu running on the current CPU.
 *
 * We can disable preemption locally around accessing the per-CPU variable,
 * and use the resolved vcpu pointer after enabling preemption again,
 * because even if the current thread is migrated to another CPU, reading
 * the per-CPU value later will give us the same value as we update the
 * per-CPU variable in the preempt notifier handlers.
 */
struct kvm_vcpu *kvm_get_running_vcpu(void)
{
	struct kvm_vcpu *vcpu;

	preempt_disable();
	vcpu = __this_cpu_read(kvm_running_vcpu);
	preempt_enable();

	return vcpu;
}
EXPORT_SYMBOL_GPL(kvm_get_running_vcpu);

/**
 * kvm_get_running_vcpus - get the per-CPU array of currently running vcpus.
 */
struct kvm_vcpu * __percpu *kvm_get_running_vcpus(void)
{
        return &kvm_running_vcpu;
}

#ifdef CONFIG_GUEST_PERF_EVENTS
static unsigned int kvm_guest_state(void)
{
	struct kvm_vcpu *vcpu = kvm_get_running_vcpu();
	unsigned int state;

	if (!kvm_arch_pmi_in_guest(vcpu))
		return 0;

	state = PERF_GUEST_ACTIVE;
	if (!kvm_arch_vcpu_in_kernel(vcpu))
		state |= PERF_GUEST_USER;

	return state;
}

static unsigned long kvm_guest_get_ip(void)
{
	struct kvm_vcpu *vcpu = kvm_get_running_vcpu();

	/* Retrieving the IP must be guarded by a call to kvm_guest_state(). */
	if (WARN_ON_ONCE(!kvm_arch_pmi_in_guest(vcpu)))
		return 0;

	return kvm_arch_vcpu_get_ip(vcpu);
}

static struct perf_guest_info_callbacks kvm_guest_cbs = {
	.state			= kvm_guest_state,
	.get_ip			= kvm_guest_get_ip,
	.handle_intel_pt_intr	= NULL,
};

void kvm_register_perf_callbacks(unsigned int (*pt_intr_handler)(void))
{
	kvm_guest_cbs.handle_intel_pt_intr = pt_intr_handler;
	perf_register_guest_info_callbacks(&kvm_guest_cbs);
}
void kvm_unregister_perf_callbacks(void)
{
	perf_unregister_guest_info_callbacks(&kvm_guest_cbs);
}
#endif

int kvm_init(unsigned vcpu_size, unsigned vcpu_align, struct module *module)
{
	int r;
	int cpu;

	/* A kmem cache lets us meet the alignment requirements of fx_save. */
	if (!vcpu_align)
		vcpu_align = __alignof__(struct kvm_vcpu);
	kvm_vcpu_cache =
		kmem_cache_create_usercopy("kvm_vcpu", vcpu_size, vcpu_align,
					   SLAB_ACCOUNT,
					   offsetof(struct kvm_vcpu, arch),
					   offsetofend(struct kvm_vcpu, stats_id)
					   - offsetof(struct kvm_vcpu, arch),
					   NULL);
	if (!kvm_vcpu_cache)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		if (!alloc_cpumask_var_node(&per_cpu(cpu_kick_mask, cpu),
					    GFP_KERNEL, cpu_to_node(cpu))) {
			r = -ENOMEM;
			goto err_cpu_kick_mask;
		}
	}

	r = kvm_irqfd_init();
	if (r)
		goto err_irqfd;

	r = kvm_async_pf_init();
	if (r)
		goto err_async_pf;

	kvm_chardev_ops.owner = module;
	kvm_vm_fops.owner = module;
	kvm_vcpu_fops.owner = module;
	kvm_device_fops.owner = module;

	kvm_preempt_ops.sched_in = kvm_sched_in;
	kvm_preempt_ops.sched_out = kvm_sched_out;

	kvm_init_debug();

	r = kvm_vfio_ops_init();
	if (WARN_ON_ONCE(r))
		goto err_vfio;

	kvm_gmem_init(module);

	r = kvm_init_virtualization();
	if (r)
		goto err_virt;

	/*
	 * Registration _must_ be the very last thing done, as this exposes
	 * /dev/kvm to userspace, i.e. all infrastructure must be setup!
	 */
	r = misc_register(&kvm_dev);
	if (r) {
		pr_err("kvm: misc device register failed\n");
		goto err_register;
	}

	return 0;

err_register:
	kvm_uninit_virtualization();
err_virt:
	kvm_vfio_ops_exit();
err_vfio:
	kvm_async_pf_deinit();
err_async_pf:
	kvm_irqfd_exit();
err_irqfd:
err_cpu_kick_mask:
	for_each_possible_cpu(cpu)
		free_cpumask_var(per_cpu(cpu_kick_mask, cpu));
	kmem_cache_destroy(kvm_vcpu_cache);
	return r;
}
EXPORT_SYMBOL_GPL(kvm_init);

void kvm_exit(void)
{
	int cpu;

	/*
	 * Note, unregistering /dev/kvm doesn't strictly need to come first,
	 * fops_get(), a.k.a. try_module_get(), prevents acquiring references
	 * to KVM while the module is being stopped.
	 */
	misc_deregister(&kvm_dev);

	kvm_uninit_virtualization();

	debugfs_remove_recursive(kvm_debugfs_dir);
	for_each_possible_cpu(cpu)
		free_cpumask_var(per_cpu(cpu_kick_mask, cpu));
	kmem_cache_destroy(kvm_vcpu_cache);
	kvm_vfio_ops_exit();
	kvm_async_pf_deinit();
	kvm_irqfd_exit();
}
EXPORT_SYMBOL_GPL(kvm_exit);

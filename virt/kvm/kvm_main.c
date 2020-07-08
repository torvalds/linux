// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
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

#include <asm/processor.h>
#include <asm/ioctl.h>
#include <linux/uaccess.h>

#include "coalesced_mmio.h"
#include "async_pf.h"
#include "vfio.h"

#define CREATE_TRACE_POINTS
#include <trace/events/kvm.h>

/* Worst case buffer size needed for holding an integer. */
#define ITOA_MAX_LEN 12

MODULE_AUTHOR("Qumranet");
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

/* Default resets per-vcpu halt_poll_ns . */
unsigned int halt_poll_ns_shrink;
module_param(halt_poll_ns_shrink, uint, 0644);
EXPORT_SYMBOL_GPL(halt_poll_ns_shrink);

/*
 * Ordering of locks:
 *
 *	kvm->lock --> kvm->slots_lock --> kvm->irq_lock
 */

DEFINE_MUTEX(kvm_lock);
static DEFINE_RAW_SPINLOCK(kvm_count_lock);
LIST_HEAD(vm_list);

static cpumask_var_t cpus_hardware_enabled;
static int kvm_usage_count;
static atomic_t hardware_enable_failed;

static struct kmem_cache *kvm_vcpu_cache;

static __read_mostly struct preempt_ops kvm_preempt_ops;
static DEFINE_PER_CPU(struct kvm_vcpu *, kvm_running_vcpu);

struct dentry *kvm_debugfs_dir;
EXPORT_SYMBOL_GPL(kvm_debugfs_dir);

static int kvm_debugfs_num_entries;
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
static int hardware_enable_all(void);
static void hardware_disable_all(void);

static void kvm_io_bus_destroy(struct kvm_io_bus *bus);

static void mark_page_dirty_in_slot(struct kvm_memory_slot *memslot, gfn_t gfn);

__visible bool kvm_rebooting;
EXPORT_SYMBOL_GPL(kvm_rebooting);

#define KVM_EVENT_CREATE_VM 0
#define KVM_EVENT_DESTROY_VM 1
static void kvm_uevent_notify_change(unsigned int type, struct kvm *kvm);
static unsigned long long kvm_createvm_count;
static unsigned long long kvm_active_vms;

__weak void kvm_arch_mmu_notifier_invalidate_range(struct kvm *kvm,
						   unsigned long start, unsigned long end)
{
}

bool kvm_is_zone_device_pfn(kvm_pfn_t pfn)
{
	/*
	 * The metadata used by is_zone_device_page() to determine whether or
	 * not a page is ZONE_DEVICE is guaranteed to be valid if and only if
	 * the device has been pinned, e.g. by get_user_pages().  WARN if the
	 * page_count() is zero to help detect bad usage of this helper.
	 */
	if (!pfn_valid(pfn) || WARN_ON_ONCE(!page_count(pfn_to_page(pfn))))
		return false;

	return is_zone_device_page(pfn_to_page(pfn));
}

bool kvm_is_reserved_pfn(kvm_pfn_t pfn)
{
	/*
	 * ZONE_DEVICE pages currently set PG_reserved, but from a refcounting
	 * perspective they are "normal" pages, albeit with slightly different
	 * usage rules.
	 */
	if (pfn_valid(pfn))
		return PageReserved(pfn_to_page(pfn)) &&
		       !is_zero_pfn(pfn) &&
		       !kvm_is_zone_device_pfn(pfn);

	return true;
}

bool kvm_is_transparent_hugepage(kvm_pfn_t pfn)
{
	struct page *page = pfn_to_page(pfn);

	if (!PageTransCompoundMap(page))
		return false;

	return is_transparent_hugepage(compound_head(page));
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

static void ack_flush(void *_completed)
{
}

static inline bool kvm_kick_many_cpus(const struct cpumask *cpus, bool wait)
{
	if (unlikely(!cpus))
		cpus = cpu_online_mask;

	if (cpumask_empty(cpus))
		return false;

	smp_call_function_many(cpus, ack_flush, NULL, wait);
	return true;
}

bool kvm_make_vcpus_request_mask(struct kvm *kvm, unsigned int req,
				 struct kvm_vcpu *except,
				 unsigned long *vcpu_bitmap, cpumask_var_t tmp)
{
	int i, cpu, me;
	struct kvm_vcpu *vcpu;
	bool called;

	me = get_cpu();

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if ((vcpu_bitmap && !test_bit(i, vcpu_bitmap)) ||
		    vcpu == except)
			continue;

		kvm_make_request(req, vcpu);
		cpu = vcpu->cpu;

		if (!(req & KVM_REQUEST_NO_WAKEUP) && kvm_vcpu_wake_up(vcpu))
			continue;

		if (tmp != NULL && cpu != -1 && cpu != me &&
		    kvm_request_needs_ipi(vcpu, req))
			__cpumask_set_cpu(cpu, tmp);
	}

	called = kvm_kick_many_cpus(tmp, !!(req & KVM_REQUEST_WAIT));
	put_cpu();

	return called;
}

bool kvm_make_all_cpus_request_except(struct kvm *kvm, unsigned int req,
				      struct kvm_vcpu *except)
{
	cpumask_var_t cpus;
	bool called;

	zalloc_cpumask_var(&cpus, GFP_ATOMIC);

	called = kvm_make_vcpus_request_mask(kvm, req, except, NULL, cpus);

	free_cpumask_var(cpus);
	return called;
}

bool kvm_make_all_cpus_request(struct kvm *kvm, unsigned int req)
{
	return kvm_make_all_cpus_request_except(kvm, req, NULL);
}

#ifndef CONFIG_HAVE_KVM_ARCH_TLB_FLUSH_ALL
void kvm_flush_remote_tlbs(struct kvm *kvm)
{
	/*
	 * Read tlbs_dirty before setting KVM_REQ_TLB_FLUSH in
	 * kvm_make_all_cpus_request.
	 */
	long dirty_count = smp_load_acquire(&kvm->tlbs_dirty);

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
	if (!kvm_arch_flush_remote_tlb(kvm)
	    || kvm_make_all_cpus_request(kvm, KVM_REQ_TLB_FLUSH))
		++kvm->stat.remote_tlb_flush;
	cmpxchg(&kvm->tlbs_dirty, dirty_count, 0);
}
EXPORT_SYMBOL_GPL(kvm_flush_remote_tlbs);
#endif

void kvm_reload_remote_mmus(struct kvm *kvm)
{
	kvm_make_all_cpus_request(kvm, KVM_REQ_MMU_RELOAD);
}

static void kvm_vcpu_init(struct kvm_vcpu *vcpu, struct kvm *kvm, unsigned id)
{
	mutex_init(&vcpu->mutex);
	vcpu->cpu = -1;
	vcpu->kvm = kvm;
	vcpu->vcpu_id = id;
	vcpu->pid = NULL;
	rcuwait_init(&vcpu->wait);
	kvm_async_pf_vcpu_init(vcpu);

	vcpu->pre_pcpu = -1;
	INIT_LIST_HEAD(&vcpu->blocked_vcpu_list);

	kvm_vcpu_set_in_spin_loop(vcpu, false);
	kvm_vcpu_set_dy_eligible(vcpu, false);
	vcpu->preempted = false;
	vcpu->ready = false;
	preempt_notifier_init(&vcpu->preempt_notifier, &kvm_preempt_ops);
}

void kvm_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_arch_vcpu_destroy(vcpu);

	/*
	 * No need for rcu_read_lock as VCPU_RUN is the only place that changes
	 * the vcpu->pid pointer, and at destruction time all file descriptors
	 * are already gone.
	 */
	put_pid(rcu_dereference_protected(vcpu->pid, 1));

	free_page((unsigned long)vcpu->run);
	kmem_cache_free(kvm_vcpu_cache, vcpu);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_destroy);

#if defined(CONFIG_MMU_NOTIFIER) && defined(KVM_ARCH_WANT_MMU_NOTIFIER)
static inline struct kvm *mmu_notifier_to_kvm(struct mmu_notifier *mn)
{
	return container_of(mn, struct kvm, mmu_notifier);
}

static void kvm_mmu_notifier_invalidate_range(struct mmu_notifier *mn,
					      struct mm_struct *mm,
					      unsigned long start, unsigned long end)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	kvm_arch_mmu_notifier_invalidate_range(kvm, start, end);
	srcu_read_unlock(&kvm->srcu, idx);
}

static void kvm_mmu_notifier_change_pte(struct mmu_notifier *mn,
					struct mm_struct *mm,
					unsigned long address,
					pte_t pte)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	spin_lock(&kvm->mmu_lock);
	kvm->mmu_notifier_seq++;

	if (kvm_set_spte_hva(kvm, address, pte))
		kvm_flush_remote_tlbs(kvm);

	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, idx);
}

static int kvm_mmu_notifier_invalidate_range_start(struct mmu_notifier *mn,
					const struct mmu_notifier_range *range)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int need_tlb_flush = 0, idx;

	idx = srcu_read_lock(&kvm->srcu);
	spin_lock(&kvm->mmu_lock);
	/*
	 * The count increase must become visible at unlock time as no
	 * spte can be established without taking the mmu_lock and
	 * count is also read inside the mmu_lock critical section.
	 */
	kvm->mmu_notifier_count++;
	need_tlb_flush = kvm_unmap_hva_range(kvm, range->start, range->end);
	need_tlb_flush |= kvm->tlbs_dirty;
	/* we've to flush the tlb before the pages can be freed */
	if (need_tlb_flush)
		kvm_flush_remote_tlbs(kvm);

	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, idx);

	return 0;
}

static void kvm_mmu_notifier_invalidate_range_end(struct mmu_notifier *mn,
					const struct mmu_notifier_range *range)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);

	spin_lock(&kvm->mmu_lock);
	/*
	 * This sequence increase will notify the kvm page fault that
	 * the page that is going to be mapped in the spte could have
	 * been freed.
	 */
	kvm->mmu_notifier_seq++;
	smp_wmb();
	/*
	 * The above sequence increase must be visible before the
	 * below count decrease, which is ensured by the smp_wmb above
	 * in conjunction with the smp_rmb in mmu_notifier_retry().
	 */
	kvm->mmu_notifier_count--;
	spin_unlock(&kvm->mmu_lock);

	BUG_ON(kvm->mmu_notifier_count < 0);
}

static int kvm_mmu_notifier_clear_flush_young(struct mmu_notifier *mn,
					      struct mm_struct *mm,
					      unsigned long start,
					      unsigned long end)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int young, idx;

	idx = srcu_read_lock(&kvm->srcu);
	spin_lock(&kvm->mmu_lock);

	young = kvm_age_hva(kvm, start, end);
	if (young)
		kvm_flush_remote_tlbs(kvm);

	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, idx);

	return young;
}

static int kvm_mmu_notifier_clear_young(struct mmu_notifier *mn,
					struct mm_struct *mm,
					unsigned long start,
					unsigned long end)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int young, idx;

	idx = srcu_read_lock(&kvm->srcu);
	spin_lock(&kvm->mmu_lock);
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
	young = kvm_age_hva(kvm, start, end);
	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, idx);

	return young;
}

static int kvm_mmu_notifier_test_young(struct mmu_notifier *mn,
				       struct mm_struct *mm,
				       unsigned long address)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int young, idx;

	idx = srcu_read_lock(&kvm->srcu);
	spin_lock(&kvm->mmu_lock);
	young = kvm_test_age_hva(kvm, address);
	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, idx);

	return young;
}

static void kvm_mmu_notifier_release(struct mmu_notifier *mn,
				     struct mm_struct *mm)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	kvm_arch_flush_shadow_all(kvm);
	srcu_read_unlock(&kvm->srcu, idx);
}

static const struct mmu_notifier_ops kvm_mmu_notifier_ops = {
	.invalidate_range	= kvm_mmu_notifier_invalidate_range,
	.invalidate_range_start	= kvm_mmu_notifier_invalidate_range_start,
	.invalidate_range_end	= kvm_mmu_notifier_invalidate_range_end,
	.clear_flush_young	= kvm_mmu_notifier_clear_flush_young,
	.clear_young		= kvm_mmu_notifier_clear_young,
	.test_young		= kvm_mmu_notifier_test_young,
	.change_pte		= kvm_mmu_notifier_change_pte,
	.release		= kvm_mmu_notifier_release,
};

static int kvm_init_mmu_notifier(struct kvm *kvm)
{
	kvm->mmu_notifier.ops = &kvm_mmu_notifier_ops;
	return mmu_notifier_register(&kvm->mmu_notifier, current->mm);
}

#else  /* !(CONFIG_MMU_NOTIFIER && KVM_ARCH_WANT_MMU_NOTIFIER) */

static int kvm_init_mmu_notifier(struct kvm *kvm)
{
	return 0;
}

#endif /* CONFIG_MMU_NOTIFIER && KVM_ARCH_WANT_MMU_NOTIFIER */

static struct kvm_memslots *kvm_alloc_memslots(void)
{
	int i;
	struct kvm_memslots *slots;

	slots = kvzalloc(sizeof(struct kvm_memslots), GFP_KERNEL_ACCOUNT);
	if (!slots)
		return NULL;

	for (i = 0; i < KVM_MEM_SLOTS_NUM; i++)
		slots->id_to_index[i] = -1;

	return slots;
}

static void kvm_destroy_dirty_bitmap(struct kvm_memory_slot *memslot)
{
	if (!memslot->dirty_bitmap)
		return;

	kvfree(memslot->dirty_bitmap);
	memslot->dirty_bitmap = NULL;
}

static void kvm_free_memslot(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	kvm_destroy_dirty_bitmap(slot);

	kvm_arch_free_memslot(kvm, slot);

	slot->flags = 0;
	slot->npages = 0;
}

static void kvm_free_memslots(struct kvm *kvm, struct kvm_memslots *slots)
{
	struct kvm_memory_slot *memslot;

	if (!slots)
		return;

	kvm_for_each_memslot(memslot, slots)
		kvm_free_memslot(kvm, memslot);

	kvfree(slots);
}

static void kvm_destroy_vm_debugfs(struct kvm *kvm)
{
	int i;

	if (!kvm->debugfs_dentry)
		return;

	debugfs_remove_recursive(kvm->debugfs_dentry);

	if (kvm->debugfs_stat_data) {
		for (i = 0; i < kvm_debugfs_num_entries; i++)
			kfree(kvm->debugfs_stat_data[i]);
		kfree(kvm->debugfs_stat_data);
	}
}

static int kvm_create_vm_debugfs(struct kvm *kvm, int fd)
{
	char dir_name[ITOA_MAX_LEN * 2];
	struct kvm_stat_data *stat_data;
	struct kvm_stats_debugfs_item *p;

	if (!debugfs_initialized())
		return 0;

	snprintf(dir_name, sizeof(dir_name), "%d-%d", task_pid_nr(current), fd);
	kvm->debugfs_dentry = debugfs_create_dir(dir_name, kvm_debugfs_dir);

	kvm->debugfs_stat_data = kcalloc(kvm_debugfs_num_entries,
					 sizeof(*kvm->debugfs_stat_data),
					 GFP_KERNEL_ACCOUNT);
	if (!kvm->debugfs_stat_data)
		return -ENOMEM;

	for (p = debugfs_entries; p->name; p++) {
		stat_data = kzalloc(sizeof(*stat_data), GFP_KERNEL_ACCOUNT);
		if (!stat_data)
			return -ENOMEM;

		stat_data->kvm = kvm;
		stat_data->dbgfs_item = p;
		kvm->debugfs_stat_data[p - debugfs_entries] = stat_data;
		debugfs_create_file(p->name, KVM_DBGFS_GET_MODE(p),
				    kvm->debugfs_dentry, stat_data,
				    &stat_fops_per_vm);
	}
	return 0;
}

/*
 * Called after the VM is otherwise initialized, but just before adding it to
 * the vm_list.
 */
int __weak kvm_arch_post_init_vm(struct kvm *kvm)
{
	return 0;
}

/*
 * Called just after removing the VM from the vm_list, but before doing any
 * other destruction.
 */
void __weak kvm_arch_pre_destroy_vm(struct kvm *kvm)
{
}

static struct kvm *kvm_create_vm(unsigned long type)
{
	struct kvm *kvm = kvm_arch_alloc_vm();
	int r = -ENOMEM;
	int i;

	if (!kvm)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&kvm->mmu_lock);
	mmgrab(current->mm);
	kvm->mm = current->mm;
	kvm_eventfd_init(kvm);
	mutex_init(&kvm->lock);
	mutex_init(&kvm->irq_lock);
	mutex_init(&kvm->slots_lock);
	INIT_LIST_HEAD(&kvm->devices);

	BUILD_BUG_ON(KVM_MEM_SLOTS_NUM > SHRT_MAX);

	if (init_srcu_struct(&kvm->srcu))
		goto out_err_no_srcu;
	if (init_srcu_struct(&kvm->irq_srcu))
		goto out_err_no_irq_srcu;

	refcount_set(&kvm->users_count, 1);
	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		struct kvm_memslots *slots = kvm_alloc_memslots();

		if (!slots)
			goto out_err_no_arch_destroy_vm;
		/* Generations must be different for each address space. */
		slots->generation = i;
		rcu_assign_pointer(kvm->memslots[i], slots);
	}

	for (i = 0; i < KVM_NR_BUSES; i++) {
		rcu_assign_pointer(kvm->buses[i],
			kzalloc(sizeof(struct kvm_io_bus), GFP_KERNEL_ACCOUNT));
		if (!kvm->buses[i])
			goto out_err_no_arch_destroy_vm;
	}

	kvm->max_halt_poll_ns = halt_poll_ns;

	r = kvm_arch_init_vm(kvm, type);
	if (r)
		goto out_err_no_arch_destroy_vm;

	r = hardware_enable_all();
	if (r)
		goto out_err_no_disable;

#ifdef CONFIG_HAVE_KVM_IRQFD
	INIT_HLIST_HEAD(&kvm->irq_ack_notifier_list);
#endif

	r = kvm_init_mmu_notifier(kvm);
	if (r)
		goto out_err_no_mmu_notifier;

	r = kvm_arch_post_init_vm(kvm);
	if (r)
		goto out_err;

	mutex_lock(&kvm_lock);
	list_add(&kvm->vm_list, &vm_list);
	mutex_unlock(&kvm_lock);

	preempt_notifier_inc();

	return kvm;

out_err:
#if defined(CONFIG_MMU_NOTIFIER) && defined(KVM_ARCH_WANT_MMU_NOTIFIER)
	if (kvm->mmu_notifier.ops)
		mmu_notifier_unregister(&kvm->mmu_notifier, current->mm);
#endif
out_err_no_mmu_notifier:
	hardware_disable_all();
out_err_no_disable:
	kvm_arch_destroy_vm(kvm);
out_err_no_arch_destroy_vm:
	WARN_ON_ONCE(!refcount_dec_and_test(&kvm->users_count));
	for (i = 0; i < KVM_NR_BUSES; i++)
		kfree(kvm_get_bus(kvm, i));
	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++)
		kvm_free_memslots(kvm, __kvm_memslots(kvm, i));
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
#if defined(CONFIG_MMU_NOTIFIER) && defined(KVM_ARCH_WANT_MMU_NOTIFIER)
	mmu_notifier_unregister(&kvm->mmu_notifier, kvm->mm);
#else
	kvm_arch_flush_shadow_all(kvm);
#endif
	kvm_arch_destroy_vm(kvm);
	kvm_destroy_devices(kvm);
	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++)
		kvm_free_memslots(kvm, __kvm_memslots(kvm, i));
	cleanup_srcu_struct(&kvm->irq_srcu);
	cleanup_srcu_struct(&kvm->srcu);
	kvm_arch_free_vm(kvm);
	preempt_notifier_dec();
	hardware_disable_all();
	mmdrop(mm);
}

void kvm_get_kvm(struct kvm *kvm)
{
	refcount_inc(&kvm->users_count);
}
EXPORT_SYMBOL_GPL(kvm_get_kvm);

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
	unsigned long dirty_bytes = 2 * kvm_dirty_bitmap_bytes(memslot);

	memslot->dirty_bitmap = kvzalloc(dirty_bytes, GFP_KERNEL_ACCOUNT);
	if (!memslot->dirty_bitmap)
		return -ENOMEM;

	return 0;
}

/*
 * Delete a memslot by decrementing the number of used slots and shifting all
 * other entries in the array forward one spot.
 */
static inline void kvm_memslot_delete(struct kvm_memslots *slots,
				      struct kvm_memory_slot *memslot)
{
	struct kvm_memory_slot *mslots = slots->memslots;
	int i;

	if (WARN_ON(slots->id_to_index[memslot->id] == -1))
		return;

	slots->used_slots--;

	if (atomic_read(&slots->lru_slot) >= slots->used_slots)
		atomic_set(&slots->lru_slot, 0);

	for (i = slots->id_to_index[memslot->id]; i < slots->used_slots; i++) {
		mslots[i] = mslots[i + 1];
		slots->id_to_index[mslots[i].id] = i;
	}
	mslots[i] = *memslot;
	slots->id_to_index[memslot->id] = -1;
}

/*
 * "Insert" a new memslot by incrementing the number of used slots.  Returns
 * the new slot's initial index into the memslots array.
 */
static inline int kvm_memslot_insert_back(struct kvm_memslots *slots)
{
	return slots->used_slots++;
}

/*
 * Move a changed memslot backwards in the array by shifting existing slots
 * with a higher GFN toward the front of the array.  Note, the changed memslot
 * itself is not preserved in the array, i.e. not swapped at this time, only
 * its new index into the array is tracked.  Returns the changed memslot's
 * current index into the memslots array.
 */
static inline int kvm_memslot_move_backward(struct kvm_memslots *slots,
					    struct kvm_memory_slot *memslot)
{
	struct kvm_memory_slot *mslots = slots->memslots;
	int i;

	if (WARN_ON_ONCE(slots->id_to_index[memslot->id] == -1) ||
	    WARN_ON_ONCE(!slots->used_slots))
		return -1;

	/*
	 * Move the target memslot backward in the array by shifting existing
	 * memslots with a higher GFN (than the target memslot) towards the
	 * front of the array.
	 */
	for (i = slots->id_to_index[memslot->id]; i < slots->used_slots - 1; i++) {
		if (memslot->base_gfn > mslots[i + 1].base_gfn)
			break;

		WARN_ON_ONCE(memslot->base_gfn == mslots[i + 1].base_gfn);

		/* Shift the next memslot forward one and update its index. */
		mslots[i] = mslots[i + 1];
		slots->id_to_index[mslots[i].id] = i;
	}
	return i;
}

/*
 * Move a changed memslot forwards in the array by shifting existing slots with
 * a lower GFN toward the back of the array.  Note, the changed memslot itself
 * is not preserved in the array, i.e. not swapped at this time, only its new
 * index into the array is tracked.  Returns the changed memslot's final index
 * into the memslots array.
 */
static inline int kvm_memslot_move_forward(struct kvm_memslots *slots,
					   struct kvm_memory_slot *memslot,
					   int start)
{
	struct kvm_memory_slot *mslots = slots->memslots;
	int i;

	for (i = start; i > 0; i--) {
		if (memslot->base_gfn < mslots[i - 1].base_gfn)
			break;

		WARN_ON_ONCE(memslot->base_gfn == mslots[i - 1].base_gfn);

		/* Shift the next memslot back one and update its index. */
		mslots[i] = mslots[i - 1];
		slots->id_to_index[mslots[i].id] = i;
	}
	return i;
}

/*
 * Re-sort memslots based on their GFN to account for an added, deleted, or
 * moved memslot.  Sorting memslots by GFN allows using a binary search during
 * memslot lookup.
 *
 * IMPORTANT: Slots are sorted from highest GFN to lowest GFN!  I.e. the entry
 * at memslots[0] has the highest GFN.
 *
 * The sorting algorithm takes advantage of having initially sorted memslots
 * and knowing the position of the changed memslot.  Sorting is also optimized
 * by not swapping the updated memslot and instead only shifting other memslots
 * and tracking the new index for the update memslot.  Only once its final
 * index is known is the updated memslot copied into its position in the array.
 *
 *  - When deleting a memslot, the deleted memslot simply needs to be moved to
 *    the end of the array.
 *
 *  - When creating a memslot, the algorithm "inserts" the new memslot at the
 *    end of the array and then it forward to its correct location.
 *
 *  - When moving a memslot, the algorithm first moves the updated memslot
 *    backward to handle the scenario where the memslot's GFN was changed to a
 *    lower value.  update_memslots() then falls through and runs the same flow
 *    as creating a memslot to move the memslot forward to handle the scenario
 *    where its GFN was changed to a higher value.
 *
 * Note, slots are sorted from highest->lowest instead of lowest->highest for
 * historical reasons.  Originally, invalid memslots where denoted by having
 * GFN=0, thus sorting from highest->lowest naturally sorted invalid memslots
 * to the end of the array.  The current algorithm uses dedicated logic to
 * delete a memslot and thus does not rely on invalid memslots having GFN=0.
 *
 * The other historical motiviation for highest->lowest was to improve the
 * performance of memslot lookup.  KVM originally used a linear search starting
 * at memslots[0].  On x86, the largest memslot usually has one of the highest,
 * if not *the* highest, GFN, as the bulk of the guest's RAM is located in a
 * single memslot above the 4gb boundary.  As the largest memslot is also the
 * most likely to be referenced, sorting it to the front of the array was
 * advantageous.  The current binary search starts from the middle of the array
 * and uses an LRU pointer to improve performance for all memslots and GFNs.
 */
static void update_memslots(struct kvm_memslots *slots,
			    struct kvm_memory_slot *memslot,
			    enum kvm_mr_change change)
{
	int i;

	if (change == KVM_MR_DELETE) {
		kvm_memslot_delete(slots, memslot);
	} else {
		if (change == KVM_MR_CREATE)
			i = kvm_memslot_insert_back(slots);
		else
			i = kvm_memslot_move_backward(slots, memslot);
		i = kvm_memslot_move_forward(slots, memslot, i);

		/*
		 * Copy the memslot to its new position in memslots and update
		 * its index accordingly.
		 */
		slots->memslots[i] = *memslot;
		slots->id_to_index[memslot->id] = i;
	}
}

static int check_memory_region_flags(const struct kvm_userspace_memory_region *mem)
{
	u32 valid_flags = KVM_MEM_LOG_DIRTY_PAGES;

#ifdef __KVM_HAVE_READONLY_MEM
	valid_flags |= KVM_MEM_READONLY;
#endif

	if (mem->flags & ~valid_flags)
		return -EINVAL;

	return 0;
}

static struct kvm_memslots *install_new_memslots(struct kvm *kvm,
		int as_id, struct kvm_memslots *slots)
{
	struct kvm_memslots *old_memslots = __kvm_memslots(kvm, as_id);
	u64 gen = old_memslots->generation;

	WARN_ON(gen & KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS);
	slots->generation = gen | KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS;

	rcu_assign_pointer(kvm->memslots[as_id], slots);
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
	gen += KVM_ADDRESS_SPACE_NUM;

	kvm_arch_memslots_updated(kvm, gen);

	slots->generation = gen;

	return old_memslots;
}

/*
 * Note, at a minimum, the current number of used slots must be allocated, even
 * when deleting a memslot, as we need a complete duplicate of the memslots for
 * use when invalidating a memslot prior to deleting/moving the memslot.
 */
static struct kvm_memslots *kvm_dup_memslots(struct kvm_memslots *old,
					     enum kvm_mr_change change)
{
	struct kvm_memslots *slots;
	size_t old_size, new_size;

	old_size = sizeof(struct kvm_memslots) +
		   (sizeof(struct kvm_memory_slot) * old->used_slots);

	if (change == KVM_MR_CREATE)
		new_size = old_size + sizeof(struct kvm_memory_slot);
	else
		new_size = old_size;

	slots = kvzalloc(new_size, GFP_KERNEL_ACCOUNT);
	if (likely(slots))
		memcpy(slots, old, old_size);

	return slots;
}

static int kvm_set_memslot(struct kvm *kvm,
			   const struct kvm_userspace_memory_region *mem,
			   struct kvm_memory_slot *old,
			   struct kvm_memory_slot *new, int as_id,
			   enum kvm_mr_change change)
{
	struct kvm_memory_slot *slot;
	struct kvm_memslots *slots;
	int r;

	slots = kvm_dup_memslots(__kvm_memslots(kvm, as_id), change);
	if (!slots)
		return -ENOMEM;

	if (change == KVM_MR_DELETE || change == KVM_MR_MOVE) {
		/*
		 * Note, the INVALID flag needs to be in the appropriate entry
		 * in the freshly allocated memslots, not in @old or @new.
		 */
		slot = id_to_memslot(slots, old->id);
		slot->flags |= KVM_MEMSLOT_INVALID;

		/*
		 * We can re-use the old memslots, the only difference from the
		 * newly installed memslots is the invalid flag, which will get
		 * dropped by update_memslots anyway.  We'll also revert to the
		 * old memslots if preparing the new memory region fails.
		 */
		slots = install_new_memslots(kvm, as_id, slots);

		/* From this point no new shadow pages pointing to a deleted,
		 * or moved, memslot will be created.
		 *
		 * validation of sp->gfn happens in:
		 *	- gfn_to_hva (kvm_read_guest, gfn_to_pfn)
		 *	- kvm_is_visible_gfn (mmu_check_root)
		 */
		kvm_arch_flush_shadow_memslot(kvm, slot);
	}

	r = kvm_arch_prepare_memory_region(kvm, new, mem, change);
	if (r)
		goto out_slots;

	update_memslots(slots, new, change);
	slots = install_new_memslots(kvm, as_id, slots);

	kvm_arch_commit_memory_region(kvm, mem, old, new, change);

	kvfree(slots);
	return 0;

out_slots:
	if (change == KVM_MR_DELETE || change == KVM_MR_MOVE)
		slots = install_new_memslots(kvm, as_id, slots);
	kvfree(slots);
	return r;
}

static int kvm_delete_memslot(struct kvm *kvm,
			      const struct kvm_userspace_memory_region *mem,
			      struct kvm_memory_slot *old, int as_id)
{
	struct kvm_memory_slot new;
	int r;

	if (!old->npages)
		return -EINVAL;

	memset(&new, 0, sizeof(new));
	new.id = old->id;

	r = kvm_set_memslot(kvm, mem, old, &new, as_id, KVM_MR_DELETE);
	if (r)
		return r;

	kvm_free_memslot(kvm, old);
	return 0;
}

/*
 * Allocate some memory and give it an address in the guest physical address
 * space.
 *
 * Discontiguous memory is allowed, mostly for framebuffers.
 *
 * Must be called holding kvm->slots_lock for write.
 */
int __kvm_set_memory_region(struct kvm *kvm,
			    const struct kvm_userspace_memory_region *mem)
{
	struct kvm_memory_slot old, new;
	struct kvm_memory_slot *tmp;
	enum kvm_mr_change change;
	int as_id, id;
	int r;

	r = check_memory_region_flags(mem);
	if (r)
		return r;

	as_id = mem->slot >> 16;
	id = (u16)mem->slot;

	/* General sanity checks */
	if (mem->memory_size & (PAGE_SIZE - 1))
		return -EINVAL;
	if (mem->guest_phys_addr & (PAGE_SIZE - 1))
		return -EINVAL;
	/* We can read the guest memory with __xxx_user() later on. */
	if ((mem->userspace_addr & (PAGE_SIZE - 1)) ||
	     !access_ok((void __user *)(unsigned long)mem->userspace_addr,
			mem->memory_size))
		return -EINVAL;
	if (as_id >= KVM_ADDRESS_SPACE_NUM || id >= KVM_MEM_SLOTS_NUM)
		return -EINVAL;
	if (mem->guest_phys_addr + mem->memory_size < mem->guest_phys_addr)
		return -EINVAL;

	/*
	 * Make a full copy of the old memslot, the pointer will become stale
	 * when the memslots are re-sorted by update_memslots(), and the old
	 * memslot needs to be referenced after calling update_memslots(), e.g.
	 * to free its resources and for arch specific behavior.
	 */
	tmp = id_to_memslot(__kvm_memslots(kvm, as_id), id);
	if (tmp) {
		old = *tmp;
		tmp = NULL;
	} else {
		memset(&old, 0, sizeof(old));
		old.id = id;
	}

	if (!mem->memory_size)
		return kvm_delete_memslot(kvm, mem, &old, as_id);

	new.id = id;
	new.base_gfn = mem->guest_phys_addr >> PAGE_SHIFT;
	new.npages = mem->memory_size >> PAGE_SHIFT;
	new.flags = mem->flags;
	new.userspace_addr = mem->userspace_addr;

	if (new.npages > KVM_MEM_MAX_NR_PAGES)
		return -EINVAL;

	if (!old.npages) {
		change = KVM_MR_CREATE;
		new.dirty_bitmap = NULL;
		memset(&new.arch, 0, sizeof(new.arch));
	} else { /* Modify an existing slot. */
		if ((new.userspace_addr != old.userspace_addr) ||
		    (new.npages != old.npages) ||
		    ((new.flags ^ old.flags) & KVM_MEM_READONLY))
			return -EINVAL;

		if (new.base_gfn != old.base_gfn)
			change = KVM_MR_MOVE;
		else if (new.flags != old.flags)
			change = KVM_MR_FLAGS_ONLY;
		else /* Nothing to change. */
			return 0;

		/* Copy dirty_bitmap and arch from the current memslot. */
		new.dirty_bitmap = old.dirty_bitmap;
		memcpy(&new.arch, &old.arch, sizeof(new.arch));
	}

	if ((change == KVM_MR_CREATE) || (change == KVM_MR_MOVE)) {
		/* Check for overlaps */
		kvm_for_each_memslot(tmp, __kvm_memslots(kvm, as_id)) {
			if (tmp->id == id)
				continue;
			if (!((new.base_gfn + new.npages <= tmp->base_gfn) ||
			      (new.base_gfn >= tmp->base_gfn + tmp->npages)))
				return -EEXIST;
		}
	}

	/* Allocate/free page dirty bitmap as needed */
	if (!(new.flags & KVM_MEM_LOG_DIRTY_PAGES))
		new.dirty_bitmap = NULL;
	else if (!new.dirty_bitmap) {
		r = kvm_alloc_dirty_bitmap(&new);
		if (r)
			return r;

		if (kvm_dirty_log_manual_protect_and_init_set(kvm))
			bitmap_set(new.dirty_bitmap, 0, new.npages);
	}

	r = kvm_set_memslot(kvm, mem, &old, &new, as_id, change);
	if (r)
		goto out_bitmap;

	if (old.dirty_bitmap && !new.dirty_bitmap)
		kvm_destroy_dirty_bitmap(&old);
	return 0;

out_bitmap:
	if (new.dirty_bitmap && !old.dirty_bitmap)
		kvm_destroy_dirty_bitmap(&new);
	return r;
}
EXPORT_SYMBOL_GPL(__kvm_set_memory_region);

int kvm_set_memory_region(struct kvm *kvm,
			  const struct kvm_userspace_memory_region *mem)
{
	int r;

	mutex_lock(&kvm->slots_lock);
	r = __kvm_set_memory_region(kvm, mem);
	mutex_unlock(&kvm->slots_lock);
	return r;
}
EXPORT_SYMBOL_GPL(kvm_set_memory_region);

static int kvm_vm_ioctl_set_memory_region(struct kvm *kvm,
					  struct kvm_userspace_memory_region *mem)
{
	if ((u16)mem->slot >= KVM_USER_MEM_SLOTS)
		return -EINVAL;

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

	*memslot = NULL;
	*is_dirty = 0;

	as_id = log->slot >> 16;
	id = (u16)log->slot;
	if (as_id >= KVM_ADDRESS_SPACE_NUM || id >= KVM_USER_MEM_SLOTS)
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

	as_id = log->slot >> 16;
	id = (u16)log->slot;
	if (as_id >= KVM_ADDRESS_SPACE_NUM || id >= KVM_USER_MEM_SLOTS)
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

		spin_lock(&kvm->mmu_lock);
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
		spin_unlock(&kvm->mmu_lock);
	}

	if (flush)
		kvm_arch_flush_remote_tlbs_memslot(kvm, memslot);

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

	as_id = log->slot >> 16;
	id = (u16)log->slot;
	if (as_id >= KVM_ADDRESS_SPACE_NUM || id >= KVM_USER_MEM_SLOTS)
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

	spin_lock(&kvm->mmu_lock);
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
	spin_unlock(&kvm->mmu_lock);

	if (flush)
		kvm_arch_flush_remote_tlbs_memslot(kvm, memslot);

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

struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn)
{
	return __gfn_to_memslot(kvm_memslots(kvm), gfn);
}
EXPORT_SYMBOL_GPL(gfn_to_memslot);

struct kvm_memory_slot *kvm_vcpu_gfn_to_memslot(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	return __gfn_to_memslot(kvm_vcpu_memslots(vcpu), gfn);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_gfn_to_memslot);

bool kvm_is_visible_gfn(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *memslot = gfn_to_memslot(kvm, gfn);

	return kvm_is_visible_memslot(memslot);
}
EXPORT_SYMBOL_GPL(kvm_is_visible_gfn);

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

static bool memslot_is_readonly(struct kvm_memory_slot *slot)
{
	return slot->flags & KVM_MEM_READONLY;
}

static unsigned long __gfn_to_hva_many(struct kvm_memory_slot *slot, gfn_t gfn,
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

static inline int check_user_page_hwpoison(unsigned long addr)
{
	int rc, flags = FOLL_HWPOISON | FOLL_WRITE;

	rc = get_user_pages(addr, 1, flags, NULL, NULL);
	return rc == -EHWPOISON;
}

/*
 * The fast path to get the writable pfn which will be stored in @pfn,
 * true indicates success, otherwise false is returned.  It's also the
 * only part that runs if we can in atomic context.
 */
static bool hva_to_pfn_fast(unsigned long addr, bool write_fault,
			    bool *writable, kvm_pfn_t *pfn)
{
	struct page *page[1];

	/*
	 * Fast pin a writable pfn only if it is a write fault request
	 * or the caller allows to map a writable pfn for a read fault
	 * request.
	 */
	if (!(write_fault || writable))
		return false;

	if (get_user_page_fast_only(addr, FOLL_WRITE, page)) {
		*pfn = page_to_pfn(page[0]);

		if (writable)
			*writable = true;
		return true;
	}

	return false;
}

/*
 * The slow path to get the pfn of the specified host virtual address,
 * 1 indicates success, -errno is returned if error is detected.
 */
static int hva_to_pfn_slow(unsigned long addr, bool *async, bool write_fault,
			   bool *writable, kvm_pfn_t *pfn)
{
	unsigned int flags = FOLL_HWPOISON;
	struct page *page;
	int npages = 0;

	might_sleep();

	if (writable)
		*writable = write_fault;

	if (write_fault)
		flags |= FOLL_WRITE;
	if (async)
		flags |= FOLL_NOWAIT;

	npages = get_user_pages_unlocked(addr, 1, &page, flags);
	if (npages != 1)
		return npages;

	/* map read fault as writable if possible */
	if (unlikely(!write_fault) && writable) {
		struct page *wpage;

		if (get_user_page_fast_only(addr, FOLL_WRITE, &wpage)) {
			*writable = true;
			put_page(page);
			page = wpage;
		}
	}
	*pfn = page_to_pfn(page);
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
			       unsigned long addr, bool *async,
			       bool write_fault, bool *writable,
			       kvm_pfn_t *p_pfn)
{
	unsigned long pfn;
	int r;

	r = follow_pfn(vma, addr, &pfn);
	if (r) {
		/*
		 * get_user_pages fails for VM_IO and VM_PFNMAP vmas and does
		 * not call the fault handler, so do it here.
		 */
		bool unlocked = false;
		r = fixup_user_fault(current, current->mm, addr,
				     (write_fault ? FAULT_FLAG_WRITE : 0),
				     &unlocked);
		if (unlocked)
			return -EAGAIN;
		if (r)
			return r;

		r = follow_pfn(vma, addr, &pfn);
		if (r)
			return r;

	}

	if (writable)
		*writable = true;

	/*
	 * Get a reference here because callers of *hva_to_pfn* and
	 * *gfn_to_pfn* ultimately call kvm_release_pfn_clean on the
	 * returned pfn.  This is only needed if the VMA has VM_MIXEDMAP
	 * set, but the kvm_get_pfn/kvm_release_pfn_clean pair will
	 * simply do nothing for reserved pfns.
	 *
	 * Whoever called remap_pfn_range is also going to call e.g.
	 * unmap_mapping_range before the underlying pages are freed,
	 * causing a call to our MMU notifier.
	 */ 
	kvm_get_pfn(pfn);

	*p_pfn = pfn;
	return 0;
}

/*
 * Pin guest page in memory and return its pfn.
 * @addr: host virtual address which maps memory to the guest
 * @atomic: whether this function can sleep
 * @async: whether this function need to wait IO complete if the
 *         host page is not in the memory
 * @write_fault: whether we should get a writable host page
 * @writable: whether it allows to map a writable host page for !@write_fault
 *
 * The function will map a writable host page for these two cases:
 * 1): @write_fault = true
 * 2): @write_fault = false && @writable, @writable will tell the caller
 *     whether the mapping is writable.
 */
static kvm_pfn_t hva_to_pfn(unsigned long addr, bool atomic, bool *async,
			bool write_fault, bool *writable)
{
	struct vm_area_struct *vma;
	kvm_pfn_t pfn = 0;
	int npages, r;

	/* we can do it either atomically or asynchronously, not both */
	BUG_ON(atomic && async);

	if (hva_to_pfn_fast(addr, write_fault, writable, &pfn))
		return pfn;

	if (atomic)
		return KVM_PFN_ERR_FAULT;

	npages = hva_to_pfn_slow(addr, async, write_fault, writable, &pfn);
	if (npages == 1)
		return pfn;

	mmap_read_lock(current->mm);
	if (npages == -EHWPOISON ||
	      (!async && check_user_page_hwpoison(addr))) {
		pfn = KVM_PFN_ERR_HWPOISON;
		goto exit;
	}

retry:
	vma = find_vma_intersection(current->mm, addr, addr + 1);

	if (vma == NULL)
		pfn = KVM_PFN_ERR_FAULT;
	else if (vma->vm_flags & (VM_IO | VM_PFNMAP)) {
		r = hva_to_pfn_remapped(vma, addr, async, write_fault, writable, &pfn);
		if (r == -EAGAIN)
			goto retry;
		if (r < 0)
			pfn = KVM_PFN_ERR_FAULT;
	} else {
		if (async && vma_is_valid(vma, write_fault))
			*async = true;
		pfn = KVM_PFN_ERR_FAULT;
	}
exit:
	mmap_read_unlock(current->mm);
	return pfn;
}

kvm_pfn_t __gfn_to_pfn_memslot(struct kvm_memory_slot *slot, gfn_t gfn,
			       bool atomic, bool *async, bool write_fault,
			       bool *writable)
{
	unsigned long addr = __gfn_to_hva_many(slot, gfn, NULL, write_fault);

	if (addr == KVM_HVA_ERR_RO_BAD) {
		if (writable)
			*writable = false;
		return KVM_PFN_ERR_RO_FAULT;
	}

	if (kvm_is_error_hva(addr)) {
		if (writable)
			*writable = false;
		return KVM_PFN_NOSLOT;
	}

	/* Do not map writable pfn in the readonly memslot. */
	if (writable && memslot_is_readonly(slot)) {
		*writable = false;
		writable = NULL;
	}

	return hva_to_pfn(addr, atomic, async, write_fault,
			  writable);
}
EXPORT_SYMBOL_GPL(__gfn_to_pfn_memslot);

kvm_pfn_t gfn_to_pfn_prot(struct kvm *kvm, gfn_t gfn, bool write_fault,
		      bool *writable)
{
	return __gfn_to_pfn_memslot(gfn_to_memslot(kvm, gfn), gfn, false, NULL,
				    write_fault, writable);
}
EXPORT_SYMBOL_GPL(gfn_to_pfn_prot);

kvm_pfn_t gfn_to_pfn_memslot(struct kvm_memory_slot *slot, gfn_t gfn)
{
	return __gfn_to_pfn_memslot(slot, gfn, false, NULL, true, NULL);
}
EXPORT_SYMBOL_GPL(gfn_to_pfn_memslot);

kvm_pfn_t gfn_to_pfn_memslot_atomic(struct kvm_memory_slot *slot, gfn_t gfn)
{
	return __gfn_to_pfn_memslot(slot, gfn, true, NULL, true, NULL);
}
EXPORT_SYMBOL_GPL(gfn_to_pfn_memslot_atomic);

kvm_pfn_t kvm_vcpu_gfn_to_pfn_atomic(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	return gfn_to_pfn_memslot_atomic(kvm_vcpu_gfn_to_memslot(vcpu, gfn), gfn);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_gfn_to_pfn_atomic);

kvm_pfn_t gfn_to_pfn(struct kvm *kvm, gfn_t gfn)
{
	return gfn_to_pfn_memslot(gfn_to_memslot(kvm, gfn), gfn);
}
EXPORT_SYMBOL_GPL(gfn_to_pfn);

kvm_pfn_t kvm_vcpu_gfn_to_pfn(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	return gfn_to_pfn_memslot(kvm_vcpu_gfn_to_memslot(vcpu, gfn), gfn);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_gfn_to_pfn);

int gfn_to_page_many_atomic(struct kvm_memory_slot *slot, gfn_t gfn,
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
EXPORT_SYMBOL_GPL(gfn_to_page_many_atomic);

static struct page *kvm_pfn_to_page(kvm_pfn_t pfn)
{
	if (is_error_noslot_pfn(pfn))
		return KVM_ERR_PTR_BAD_PAGE;

	if (kvm_is_reserved_pfn(pfn)) {
		WARN_ON(1);
		return KVM_ERR_PTR_BAD_PAGE;
	}

	return pfn_to_page(pfn);
}

struct page *gfn_to_page(struct kvm *kvm, gfn_t gfn)
{
	kvm_pfn_t pfn;

	pfn = gfn_to_pfn(kvm, gfn);

	return kvm_pfn_to_page(pfn);
}
EXPORT_SYMBOL_GPL(gfn_to_page);

void kvm_release_pfn(kvm_pfn_t pfn, bool dirty, struct gfn_to_pfn_cache *cache)
{
	if (pfn == 0)
		return;

	if (cache)
		cache->pfn = cache->gfn = 0;

	if (dirty)
		kvm_release_pfn_dirty(pfn);
	else
		kvm_release_pfn_clean(pfn);
}

static void kvm_cache_gfn_to_pfn(struct kvm_memory_slot *slot, gfn_t gfn,
				 struct gfn_to_pfn_cache *cache, u64 gen)
{
	kvm_release_pfn(cache->pfn, cache->dirty, cache);

	cache->pfn = gfn_to_pfn_memslot(slot, gfn);
	cache->gfn = gfn;
	cache->dirty = false;
	cache->generation = gen;
}

static int __kvm_map_gfn(struct kvm_memslots *slots, gfn_t gfn,
			 struct kvm_host_map *map,
			 struct gfn_to_pfn_cache *cache,
			 bool atomic)
{
	kvm_pfn_t pfn;
	void *hva = NULL;
	struct page *page = KVM_UNMAPPED_PAGE;
	struct kvm_memory_slot *slot = __gfn_to_memslot(slots, gfn);
	u64 gen = slots->generation;

	if (!map)
		return -EINVAL;

	if (cache) {
		if (!cache->pfn || cache->gfn != gfn ||
			cache->generation != gen) {
			if (atomic)
				return -EAGAIN;
			kvm_cache_gfn_to_pfn(slot, gfn, cache, gen);
		}
		pfn = cache->pfn;
	} else {
		if (atomic)
			return -EAGAIN;
		pfn = gfn_to_pfn_memslot(slot, gfn);
	}
	if (is_error_noslot_pfn(pfn))
		return -EINVAL;

	if (pfn_valid(pfn)) {
		page = pfn_to_page(pfn);
		if (atomic)
			hva = kmap_atomic(page);
		else
			hva = kmap(page);
#ifdef CONFIG_HAS_IOMEM
	} else if (!atomic) {
		hva = memremap(pfn_to_hpa(pfn), PAGE_SIZE, MEMREMAP_WB);
	} else {
		return -EINVAL;
#endif
	}

	if (!hva)
		return -EFAULT;

	map->page = page;
	map->hva = hva;
	map->pfn = pfn;
	map->gfn = gfn;

	return 0;
}

int kvm_map_gfn(struct kvm_vcpu *vcpu, gfn_t gfn, struct kvm_host_map *map,
		struct gfn_to_pfn_cache *cache, bool atomic)
{
	return __kvm_map_gfn(kvm_memslots(vcpu->kvm), gfn, map,
			cache, atomic);
}
EXPORT_SYMBOL_GPL(kvm_map_gfn);

int kvm_vcpu_map(struct kvm_vcpu *vcpu, gfn_t gfn, struct kvm_host_map *map)
{
	return __kvm_map_gfn(kvm_vcpu_memslots(vcpu), gfn, map,
		NULL, false);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_map);

static void __kvm_unmap_gfn(struct kvm_memory_slot *memslot,
			struct kvm_host_map *map,
			struct gfn_to_pfn_cache *cache,
			bool dirty, bool atomic)
{
	if (!map)
		return;

	if (!map->hva)
		return;

	if (map->page != KVM_UNMAPPED_PAGE) {
		if (atomic)
			kunmap_atomic(map->hva);
		else
			kunmap(map->page);
	}
#ifdef CONFIG_HAS_IOMEM
	else if (!atomic)
		memunmap(map->hva);
	else
		WARN_ONCE(1, "Unexpected unmapping in atomic context");
#endif

	if (dirty)
		mark_page_dirty_in_slot(memslot, map->gfn);

	if (cache)
		cache->dirty |= dirty;
	else
		kvm_release_pfn(map->pfn, dirty, NULL);

	map->hva = NULL;
	map->page = NULL;
}

int kvm_unmap_gfn(struct kvm_vcpu *vcpu, struct kvm_host_map *map, 
		  struct gfn_to_pfn_cache *cache, bool dirty, bool atomic)
{
	__kvm_unmap_gfn(gfn_to_memslot(vcpu->kvm, map->gfn), map,
			cache, dirty, atomic);
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_unmap_gfn);

void kvm_vcpu_unmap(struct kvm_vcpu *vcpu, struct kvm_host_map *map, bool dirty)
{
	__kvm_unmap_gfn(kvm_vcpu_gfn_to_memslot(vcpu, map->gfn), map, NULL,
			dirty, false);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_unmap);

struct page *kvm_vcpu_gfn_to_page(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	kvm_pfn_t pfn;

	pfn = kvm_vcpu_gfn_to_pfn(vcpu, gfn);

	return kvm_pfn_to_page(pfn);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_gfn_to_page);

void kvm_release_page_clean(struct page *page)
{
	WARN_ON(is_error_page(page));

	kvm_release_pfn_clean(page_to_pfn(page));
}
EXPORT_SYMBOL_GPL(kvm_release_page_clean);

void kvm_release_pfn_clean(kvm_pfn_t pfn)
{
	if (!is_error_noslot_pfn(pfn) && !kvm_is_reserved_pfn(pfn))
		put_page(pfn_to_page(pfn));
}
EXPORT_SYMBOL_GPL(kvm_release_pfn_clean);

void kvm_release_page_dirty(struct page *page)
{
	WARN_ON(is_error_page(page));

	kvm_release_pfn_dirty(page_to_pfn(page));
}
EXPORT_SYMBOL_GPL(kvm_release_page_dirty);

void kvm_release_pfn_dirty(kvm_pfn_t pfn)
{
	kvm_set_pfn_dirty(pfn);
	kvm_release_pfn_clean(pfn);
}
EXPORT_SYMBOL_GPL(kvm_release_pfn_dirty);

void kvm_set_pfn_dirty(kvm_pfn_t pfn)
{
	if (!kvm_is_reserved_pfn(pfn) && !kvm_is_zone_device_pfn(pfn))
		SetPageDirty(pfn_to_page(pfn));
}
EXPORT_SYMBOL_GPL(kvm_set_pfn_dirty);

void kvm_set_pfn_accessed(kvm_pfn_t pfn)
{
	if (!kvm_is_reserved_pfn(pfn) && !kvm_is_zone_device_pfn(pfn))
		mark_page_accessed(pfn_to_page(pfn));
}
EXPORT_SYMBOL_GPL(kvm_set_pfn_accessed);

void kvm_get_pfn(kvm_pfn_t pfn)
{
	if (!kvm_is_reserved_pfn(pfn))
		get_page(pfn_to_page(pfn));
}
EXPORT_SYMBOL_GPL(kvm_get_pfn);

static int next_segment(unsigned long len, int offset)
{
	if (len > PAGE_SIZE - offset)
		return PAGE_SIZE - offset;
	else
		return len;
}

static int __kvm_read_guest_page(struct kvm_memory_slot *slot, gfn_t gfn,
				 void *data, int offset, int len)
{
	int r;
	unsigned long addr;

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

static int __kvm_write_guest_page(struct kvm_memory_slot *memslot, gfn_t gfn,
			          const void *data, int offset, int len)
{
	int r;
	unsigned long addr;

	addr = gfn_to_hva_memslot(memslot, gfn);
	if (kvm_is_error_hva(addr))
		return -EFAULT;
	r = __copy_to_user((void __user *)addr + offset, data, len);
	if (r)
		return -EFAULT;
	mark_page_dirty_in_slot(memslot, gfn);
	return 0;
}

int kvm_write_guest_page(struct kvm *kvm, gfn_t gfn,
			 const void *data, int offset, int len)
{
	struct kvm_memory_slot *slot = gfn_to_memslot(kvm, gfn);

	return __kvm_write_guest_page(slot, gfn, data, offset, len);
}
EXPORT_SYMBOL_GPL(kvm_write_guest_page);

int kvm_vcpu_write_guest_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			      const void *data, int offset, int len)
{
	struct kvm_memory_slot *slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);

	return __kvm_write_guest_page(slot, gfn, data, offset, len);
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

	BUG_ON(len + offset > ghc->len);

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
	mark_page_dirty_in_slot(ghc->memslot, gpa >> PAGE_SHIFT);

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

	BUG_ON(len + offset > ghc->len);

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

int kvm_clear_guest_page(struct kvm *kvm, gfn_t gfn, int offset, int len)
{
	const void *zero_page = (const void *) __va(page_to_phys(ZERO_PAGE(0)));

	return kvm_write_guest_page(kvm, gfn, zero_page, offset, len);
}
EXPORT_SYMBOL_GPL(kvm_clear_guest_page);

int kvm_clear_guest(struct kvm *kvm, gpa_t gpa, unsigned long len)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int seg;
	int offset = offset_in_page(gpa);
	int ret;

	while ((seg = next_segment(len, offset)) != 0) {
		ret = kvm_clear_guest_page(kvm, gfn, offset, seg);
		if (ret < 0)
			return ret;
		offset = 0;
		len -= seg;
		++gfn;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_clear_guest);

static void mark_page_dirty_in_slot(struct kvm_memory_slot *memslot,
				    gfn_t gfn)
{
	if (memslot && memslot->dirty_bitmap) {
		unsigned long rel_gfn = gfn - memslot->base_gfn;

		set_bit_le(rel_gfn, memslot->dirty_bitmap);
	}
}

void mark_page_dirty(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *memslot;

	memslot = gfn_to_memslot(kvm, gfn);
	mark_page_dirty_in_slot(memslot, gfn);
}
EXPORT_SYMBOL_GPL(mark_page_dirty);

void kvm_vcpu_mark_page_dirty(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	struct kvm_memory_slot *memslot;

	memslot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	mark_page_dirty_in_slot(memslot, gfn);
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

	if (val > halt_poll_ns)
		val = halt_poll_ns;

	vcpu->halt_poll_ns = val;
out:
	trace_kvm_halt_poll_ns_grow(vcpu->vcpu_id, val, old);
}

static void shrink_halt_poll_ns(struct kvm_vcpu *vcpu)
{
	unsigned int old, val, shrink;

	old = val = vcpu->halt_poll_ns;
	shrink = READ_ONCE(halt_poll_ns_shrink);
	if (shrink == 0)
		val = 0;
	else
		val /= shrink;

	vcpu->halt_poll_ns = val;
	trace_kvm_halt_poll_ns_shrink(vcpu->vcpu_id, val, old);
}

static int kvm_vcpu_check_block(struct kvm_vcpu *vcpu)
{
	int ret = -EINTR;
	int idx = srcu_read_lock(&vcpu->kvm->srcu);

	if (kvm_arch_vcpu_runnable(vcpu)) {
		kvm_make_request(KVM_REQ_UNHALT, vcpu);
		goto out;
	}
	if (kvm_cpu_has_pending_timer(vcpu))
		goto out;
	if (signal_pending(current))
		goto out;

	ret = 0;
out:
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	return ret;
}

static inline void
update_halt_poll_stats(struct kvm_vcpu *vcpu, u64 poll_ns, bool waited)
{
	if (waited)
		vcpu->stat.halt_poll_fail_ns += poll_ns;
	else
		vcpu->stat.halt_poll_success_ns += poll_ns;
}

/*
 * The vCPU has executed a HLT instruction with in-kernel mode enabled.
 */
void kvm_vcpu_block(struct kvm_vcpu *vcpu)
{
	ktime_t start, cur, poll_end;
	bool waited = false;
	u64 block_ns;

	kvm_arch_vcpu_blocking(vcpu);

	start = cur = poll_end = ktime_get();
	if (vcpu->halt_poll_ns && !kvm_arch_no_poll(vcpu)) {
		ktime_t stop = ktime_add_ns(ktime_get(), vcpu->halt_poll_ns);

		++vcpu->stat.halt_attempted_poll;
		do {
			/*
			 * This sets KVM_REQ_UNHALT if an interrupt
			 * arrives.
			 */
			if (kvm_vcpu_check_block(vcpu) < 0) {
				++vcpu->stat.halt_successful_poll;
				if (!vcpu_valid_wakeup(vcpu))
					++vcpu->stat.halt_poll_invalid;
				goto out;
			}
			poll_end = cur = ktime_get();
		} while (single_task_running() && ktime_before(cur, stop));
	}

	prepare_to_rcuwait(&vcpu->wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (kvm_vcpu_check_block(vcpu) < 0)
			break;

		waited = true;
		schedule();
	}
	finish_rcuwait(&vcpu->wait);
	cur = ktime_get();
out:
	kvm_arch_vcpu_unblocking(vcpu);
	block_ns = ktime_to_ns(cur) - ktime_to_ns(start);

	update_halt_poll_stats(
		vcpu, ktime_to_ns(ktime_sub(poll_end, start)), waited);

	if (!kvm_arch_no_poll(vcpu)) {
		if (!vcpu_valid_wakeup(vcpu)) {
			shrink_halt_poll_ns(vcpu);
		} else if (vcpu->kvm->max_halt_poll_ns) {
			if (block_ns <= vcpu->halt_poll_ns)
				;
			/* we had a long block, shrink polling */
			else if (vcpu->halt_poll_ns &&
					block_ns > vcpu->kvm->max_halt_poll_ns)
				shrink_halt_poll_ns(vcpu);
			/* we had a short halt and our poll time is too small */
			else if (vcpu->halt_poll_ns < vcpu->kvm->max_halt_poll_ns &&
					block_ns < vcpu->kvm->max_halt_poll_ns)
				grow_halt_poll_ns(vcpu);
		} else {
			vcpu->halt_poll_ns = 0;
		}
	}

	trace_kvm_vcpu_wakeup(block_ns, waited, vcpu_valid_wakeup(vcpu));
	kvm_arch_vcpu_block_finish(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_block);

bool kvm_vcpu_wake_up(struct kvm_vcpu *vcpu)
{
	struct rcuwait *waitp;

	waitp = kvm_arch_vcpu_get_wait(vcpu);
	if (rcuwait_wake_up(waitp)) {
		WRITE_ONCE(vcpu->ready, true);
		++vcpu->stat.halt_wakeup;
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
	int me;
	int cpu = vcpu->cpu;

	if (kvm_vcpu_wake_up(vcpu))
		return;

	me = get_cpu();
	if (cpu != me && (unsigned)cpu < nr_cpu_ids && cpu_online(cpu))
		if (kvm_arch_vcpu_should_kick(vcpu))
			smp_send_reschedule(cpu);
	put_cpu();
}
EXPORT_SYMBOL_GPL(kvm_vcpu_kick);
#endif /* !CONFIG_S390 */

int kvm_vcpu_yield_to(struct kvm_vcpu *target)
{
	struct pid *pid;
	struct task_struct *task = NULL;
	int ret = 0;

	rcu_read_lock();
	pid = rcu_dereference(target->pid);
	if (pid)
		task = get_pid_task(pid, PIDTYPE_PID);
	rcu_read_unlock();
	if (!task)
		return ret;
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

void kvm_vcpu_on_spin(struct kvm_vcpu *me, bool yield_to_kernel_mode)
{
	struct kvm *kvm = me->kvm;
	struct kvm_vcpu *vcpu;
	int last_boosted_vcpu = me->kvm->last_boosted_vcpu;
	int yielded = 0;
	int try = 3;
	int pass;
	int i;

	kvm_vcpu_set_in_spin_loop(me, true);
	/*
	 * We boost the priority of a VCPU that is runnable but not
	 * currently running, because it got preempted by something
	 * else and called schedule in __vcpu_run.  Hopefully that
	 * VCPU is holding the lock that we need and will release it.
	 * We approximate round-robin by starting at the last boosted VCPU.
	 */
	for (pass = 0; pass < 2 && !yielded && try; pass++) {
		kvm_for_each_vcpu(i, vcpu, kvm) {
			if (!pass && i <= last_boosted_vcpu) {
				i = last_boosted_vcpu;
				continue;
			} else if (pass && i > last_boosted_vcpu)
				break;
			if (!READ_ONCE(vcpu->ready))
				continue;
			if (vcpu == me)
				continue;
			if (rcuwait_active(&vcpu->wait) &&
			    !vcpu_dy_runnable(vcpu))
				continue;
			if (READ_ONCE(vcpu->preempted) && yield_to_kernel_mode &&
				!kvm_arch_vcpu_in_kernel(vcpu))
				continue;
			if (!kvm_vcpu_eligible_for_directed_yield(vcpu))
				continue;

			yielded = kvm_vcpu_yield_to(vcpu);
			if (yielded > 0) {
				kvm->last_boosted_vcpu = i;
				break;
			} else if (yielded < 0) {
				try--;
				if (!try)
					break;
			}
		}
	}
	kvm_vcpu_set_in_spin_loop(me, false);

	/* Ensure vcpu is not eligible during next spinloop */
	kvm_vcpu_set_dy_eligible(me, false);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_on_spin);

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

static void kvm_create_vcpu_debugfs(struct kvm_vcpu *vcpu)
{
#ifdef __KVM_HAVE_ARCH_VCPU_DEBUGFS
	struct dentry *debugfs_dentry;
	char dir_name[ITOA_MAX_LEN * 2];

	if (!debugfs_initialized())
		return;

	snprintf(dir_name, sizeof(dir_name), "vcpu%d", vcpu->vcpu_id);
	debugfs_dentry = debugfs_create_dir(dir_name,
					    vcpu->kvm->debugfs_dentry);

	kvm_arch_create_vcpu_debugfs(vcpu, debugfs_dentry);
#endif
}

/*
 * Creates some virtual cpus.  Good luck creating more than one.
 */
static int kvm_vm_ioctl_create_vcpu(struct kvm *kvm, u32 id)
{
	int r;
	struct kvm_vcpu *vcpu;
	struct page *page;

	if (id >= KVM_MAX_VCPU_ID)
		return -EINVAL;

	mutex_lock(&kvm->lock);
	if (kvm->created_vcpus == KVM_MAX_VCPUS) {
		mutex_unlock(&kvm->lock);
		return -EINVAL;
	}

	kvm->created_vcpus++;
	mutex_unlock(&kvm->lock);

	r = kvm_arch_vcpu_precreate(kvm, id);
	if (r)
		goto vcpu_decrement;

	vcpu = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!vcpu) {
		r = -ENOMEM;
		goto vcpu_decrement;
	}

	BUILD_BUG_ON(sizeof(struct kvm_run) > PAGE_SIZE);
	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page) {
		r = -ENOMEM;
		goto vcpu_free;
	}
	vcpu->run = page_address(page);

	kvm_vcpu_init(vcpu, kvm, id);

	r = kvm_arch_vcpu_create(vcpu);
	if (r)
		goto vcpu_free_run_page;

	mutex_lock(&kvm->lock);
	if (kvm_get_vcpu_by_id(kvm, id)) {
		r = -EEXIST;
		goto unlock_vcpu_destroy;
	}

	vcpu->vcpu_idx = atomic_read(&kvm->online_vcpus);
	BUG_ON(kvm->vcpus[vcpu->vcpu_idx]);

	/* Now it's all set up, let userspace reach it */
	kvm_get_kvm(kvm);
	r = create_vcpu_fd(vcpu);
	if (r < 0) {
		kvm_put_kvm_no_destroy(kvm);
		goto unlock_vcpu_destroy;
	}

	kvm->vcpus[vcpu->vcpu_idx] = vcpu;

	/*
	 * Pairs with smp_rmb() in kvm_get_vcpu.  Write kvm->vcpus
	 * before kvm->online_vcpu's incremented value.
	 */
	smp_wmb();
	atomic_inc(&kvm->online_vcpus);

	mutex_unlock(&kvm->lock);
	kvm_arch_vcpu_postcreate(vcpu);
	kvm_create_vcpu_debugfs(vcpu);
	return r;

unlock_vcpu_destroy:
	mutex_unlock(&kvm->lock);
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

static long kvm_vcpu_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;
	struct kvm_fpu *fpu = NULL;
	struct kvm_sregs *kvm_sregs = NULL;

	if (vcpu->kvm->mm != current->mm)
		return -EIO;

	if (unlikely(_IOC_TYPE(ioctl) != KVMIO))
		return -EINVAL;

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
		oldpid = rcu_access_pointer(vcpu->pid);
		if (unlikely(oldpid != task_pid(current))) {
			/* The thread running this VCPU changed. */
			struct pid *newpid;

			r = kvm_arch_vcpu_run_pid_change(vcpu);
			if (r)
				break;

			newpid = get_task_pid(current, PIDTYPE_PID);
			rcu_assign_pointer(vcpu->pid, newpid);
			if (oldpid)
				synchronize_rcu();
			put_pid(oldpid);
		}
		r = kvm_arch_vcpu_ioctl_run(vcpu);
		trace_kvm_userspace_exit(vcpu->run->exit_reason, r);
		break;
	}
	case KVM_GET_REGS: {
		struct kvm_regs *kvm_regs;

		r = -ENOMEM;
		kvm_regs = kzalloc(sizeof(struct kvm_regs), GFP_KERNEL_ACCOUNT);
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
		kvm_sregs = kzalloc(sizeof(struct kvm_sregs),
				    GFP_KERNEL_ACCOUNT);
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
		fpu = kzalloc(sizeof(struct kvm_fpu), GFP_KERNEL_ACCOUNT);
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

	if (vcpu->kvm->mm != current->mm)
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

	if (dev->kvm->mm != current->mm)
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
		list_del(&dev->vm_node);
		dev->ops->release(dev);
		mutex_unlock(&kvm->lock);
	}

	kvm_put_kvm(kvm);
	return 0;
}

static const struct file_operations kvm_device_fops = {
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
	const struct kvm_device_ops *ops = NULL;
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
	list_add(&dev->vm_node, &kvm->devices);
	mutex_unlock(&kvm->lock);

	if (ops->init)
		ops->init(dev);

	kvm_get_kvm(kvm);
	ret = anon_inode_getfd(ops->name, &kvm_device_fops, dev, O_RDWR | O_CLOEXEC);
	if (ret < 0) {
		kvm_put_kvm_no_destroy(kvm);
		mutex_lock(&kvm->lock);
		list_del(&dev->vm_node);
		mutex_unlock(&kvm->lock);
		ops->destroy(dev);
		return ret;
	}

	cd->fd = ret;
	return 0;
}

static long kvm_vm_ioctl_check_extension_generic(struct kvm *kvm, long arg)
{
	switch (arg) {
	case KVM_CAP_USER_MEMORY:
	case KVM_CAP_DESTROY_MEMORY_REGION_WORKS:
	case KVM_CAP_JOIN_MEMORY_REGIONS_WORKS:
	case KVM_CAP_INTERNAL_ERROR_DATA:
#ifdef CONFIG_HAVE_KVM_MSI
	case KVM_CAP_SIGNAL_MSI:
#endif
#ifdef CONFIG_HAVE_KVM_IRQFD
	case KVM_CAP_IRQFD:
	case KVM_CAP_IRQFD_RESAMPLE:
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
#if KVM_ADDRESS_SPACE_NUM > 1
	case KVM_CAP_MULTI_ADDRESS_SPACE:
		return KVM_ADDRESS_SPACE_NUM;
#endif
	case KVM_CAP_NR_MEMSLOTS:
		return KVM_USER_MEM_SLOTS;
	default:
		break;
	}
	return kvm_vm_ioctl_check_extension(kvm, arg);
}

int __attribute__((weak)) kvm_vm_ioctl_enable_cap(struct kvm *kvm,
						  struct kvm_enable_cap *cap)
{
	return -EINVAL;
}

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
		return 0;
	}
	default:
		return kvm_vm_ioctl_enable_cap(kvm, cap);
	}
}

static long kvm_vm_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;

	if (kvm->mm != current->mm)
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
	case KVM_SET_USER_MEMORY_REGION: {
		struct kvm_userspace_memory_region kvm_userspace_mem;

		r = -EFAULT;
		if (copy_from_user(&kvm_userspace_mem, argp,
						sizeof(kvm_userspace_mem)))
			goto out;

		r = kvm_vm_ioctl_set_memory_region(kvm, &kvm_userspace_mem);
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
			entries = vmemdup_user(urouting->entries,
					       array_size(sizeof(*entries),
							  routing.nr));
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

static long kvm_vm_compat_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	int r;

	if (kvm->mm != current->mm)
		return -EIO;
	switch (ioctl) {
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

static int kvm_dev_ioctl_create_vm(unsigned long type)
{
	int r;
	struct kvm *kvm;
	struct file *file;

	kvm = kvm_create_vm(type);
	if (IS_ERR(kvm))
		return PTR_ERR(kvm);
#ifdef CONFIG_KVM_MMIO
	r = kvm_coalesced_mmio_init(kvm);
	if (r < 0)
		goto put_kvm;
#endif
	r = get_unused_fd_flags(O_CLOEXEC);
	if (r < 0)
		goto put_kvm;

	file = anon_inode_getfile("kvm-vm", &kvm_vm_fops, kvm, O_RDWR);
	if (IS_ERR(file)) {
		put_unused_fd(r);
		r = PTR_ERR(file);
		goto put_kvm;
	}

	/*
	 * Don't call kvm_put_kvm anymore at this point; file->f_op is
	 * already set, with ->release() being kvm_vm_release().  In error
	 * cases it will be called by the final fput(file) and will take
	 * care of doing kvm_put_kvm(kvm).
	 */
	if (kvm_create_vm_debugfs(kvm, r) < 0) {
		put_unused_fd(r);
		fput(file);
		return -ENOMEM;
	}
	kvm_uevent_notify_change(KVM_EVENT_CREATE_VM, kvm);

	fd_install(r, file);
	return r;

put_kvm:
	kvm_put_kvm(kvm);
	return r;
}

static long kvm_dev_ioctl(struct file *filp,
			  unsigned int ioctl, unsigned long arg)
{
	long r = -EINVAL;

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
	case KVM_TRACE_ENABLE:
	case KVM_TRACE_PAUSE:
	case KVM_TRACE_DISABLE:
		r = -EOPNOTSUPP;
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

static void hardware_enable_nolock(void *junk)
{
	int cpu = raw_smp_processor_id();
	int r;

	if (cpumask_test_cpu(cpu, cpus_hardware_enabled))
		return;

	cpumask_set_cpu(cpu, cpus_hardware_enabled);

	r = kvm_arch_hardware_enable();

	if (r) {
		cpumask_clear_cpu(cpu, cpus_hardware_enabled);
		atomic_inc(&hardware_enable_failed);
		pr_info("kvm: enabling virtualization on CPU%d failed\n", cpu);
	}
}

static int kvm_starting_cpu(unsigned int cpu)
{
	raw_spin_lock(&kvm_count_lock);
	if (kvm_usage_count)
		hardware_enable_nolock(NULL);
	raw_spin_unlock(&kvm_count_lock);
	return 0;
}

static void hardware_disable_nolock(void *junk)
{
	int cpu = raw_smp_processor_id();

	if (!cpumask_test_cpu(cpu, cpus_hardware_enabled))
		return;
	cpumask_clear_cpu(cpu, cpus_hardware_enabled);
	kvm_arch_hardware_disable();
}

static int kvm_dying_cpu(unsigned int cpu)
{
	raw_spin_lock(&kvm_count_lock);
	if (kvm_usage_count)
		hardware_disable_nolock(NULL);
	raw_spin_unlock(&kvm_count_lock);
	return 0;
}

static void hardware_disable_all_nolock(void)
{
	BUG_ON(!kvm_usage_count);

	kvm_usage_count--;
	if (!kvm_usage_count)
		on_each_cpu(hardware_disable_nolock, NULL, 1);
}

static void hardware_disable_all(void)
{
	raw_spin_lock(&kvm_count_lock);
	hardware_disable_all_nolock();
	raw_spin_unlock(&kvm_count_lock);
}

static int hardware_enable_all(void)
{
	int r = 0;

	raw_spin_lock(&kvm_count_lock);

	kvm_usage_count++;
	if (kvm_usage_count == 1) {
		atomic_set(&hardware_enable_failed, 0);
		on_each_cpu(hardware_enable_nolock, NULL, 1);

		if (atomic_read(&hardware_enable_failed)) {
			hardware_disable_all_nolock();
			r = -EBUSY;
		}
	}

	raw_spin_unlock(&kvm_count_lock);

	return r;
}

static int kvm_reboot(struct notifier_block *notifier, unsigned long val,
		      void *v)
{
	/*
	 * Some (well, at least mine) BIOSes hang on reboot if
	 * in vmx root mode.
	 *
	 * And Intel TXT required VMX off for all cpu when system shutdown.
	 */
	pr_info("kvm: exiting hardware virtualization\n");
	kvm_rebooting = true;
	on_each_cpu(hardware_disable_nolock, NULL, 1);
	return NOTIFY_OK;
}

static struct notifier_block kvm_reboot_notifier = {
	.notifier_call = kvm_reboot,
	.priority = 0,
};

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

/* Caller must hold slots_lock. */
int kvm_io_bus_register_dev(struct kvm *kvm, enum kvm_bus bus_idx, gpa_t addr,
			    int len, struct kvm_io_device *dev)
{
	int i;
	struct kvm_io_bus *new_bus, *bus;
	struct kvm_io_range range;

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

/* Caller must hold slots_lock. */
void kvm_io_bus_unregister_dev(struct kvm *kvm, enum kvm_bus bus_idx,
			       struct kvm_io_device *dev)
{
	int i;
	struct kvm_io_bus *new_bus, *bus;

	bus = kvm_get_bus(kvm, bus_idx);
	if (!bus)
		return;

	for (i = 0; i < bus->dev_count; i++)
		if (bus->range[i].dev == dev) {
			break;
		}

	if (i == bus->dev_count)
		return;

	new_bus = kmalloc(struct_size(bus, range, bus->dev_count - 1),
			  GFP_KERNEL_ACCOUNT);
	if (!new_bus)  {
		pr_err("kvm: failed to shrink bus, removing it completely\n");
		goto broken;
	}

	memcpy(new_bus, bus, sizeof(*bus) + i * sizeof(struct kvm_io_range));
	new_bus->dev_count--;
	memcpy(new_bus->range + i, bus->range + i + 1,
	       (new_bus->dev_count - i) * sizeof(struct kvm_io_range));

broken:
	rcu_assign_pointer(kvm->buses[bus_idx], new_bus);
	synchronize_srcu_expedited(&kvm->srcu);
	kfree(bus);
	return;
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
	struct kvm_stat_data *stat_data = (struct kvm_stat_data *)
					  inode->i_private;

	/* The debugfs files are a reference to the kvm struct which
	 * is still valid when kvm_destroy_vm is called.
	 * To avoid the race between open and the removal of the debugfs
	 * directory we test against the users count.
	 */
	if (!refcount_inc_not_zero(&stat_data->kvm->users_count))
		return -ENOENT;

	if (simple_attr_open(inode, file, get,
		    KVM_DBGFS_GET_MODE(stat_data->dbgfs_item) & 0222
		    ? set : NULL,
		    fmt)) {
		kvm_put_kvm(stat_data->kvm);
		return -ENOMEM;
	}

	return 0;
}

static int kvm_debugfs_release(struct inode *inode, struct file *file)
{
	struct kvm_stat_data *stat_data = (struct kvm_stat_data *)
					  inode->i_private;

	simple_attr_release(inode, file);
	kvm_put_kvm(stat_data->kvm);

	return 0;
}

static int kvm_get_stat_per_vm(struct kvm *kvm, size_t offset, u64 *val)
{
	*val = *(ulong *)((void *)kvm + offset);

	return 0;
}

static int kvm_clear_stat_per_vm(struct kvm *kvm, size_t offset)
{
	*(ulong *)((void *)kvm + offset) = 0;

	return 0;
}

static int kvm_get_stat_per_vcpu(struct kvm *kvm, size_t offset, u64 *val)
{
	int i;
	struct kvm_vcpu *vcpu;

	*val = 0;

	kvm_for_each_vcpu(i, vcpu, kvm)
		*val += *(u64 *)((void *)vcpu + offset);

	return 0;
}

static int kvm_clear_stat_per_vcpu(struct kvm *kvm, size_t offset)
{
	int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm)
		*(u64 *)((void *)vcpu + offset) = 0;

	return 0;
}

static int kvm_stat_data_get(void *data, u64 *val)
{
	int r = -EFAULT;
	struct kvm_stat_data *stat_data = (struct kvm_stat_data *)data;

	switch (stat_data->dbgfs_item->kind) {
	case KVM_STAT_VM:
		r = kvm_get_stat_per_vm(stat_data->kvm,
					stat_data->dbgfs_item->offset, val);
		break;
	case KVM_STAT_VCPU:
		r = kvm_get_stat_per_vcpu(stat_data->kvm,
					  stat_data->dbgfs_item->offset, val);
		break;
	}

	return r;
}

static int kvm_stat_data_clear(void *data, u64 val)
{
	int r = -EFAULT;
	struct kvm_stat_data *stat_data = (struct kvm_stat_data *)data;

	if (val)
		return -EINVAL;

	switch (stat_data->dbgfs_item->kind) {
	case KVM_STAT_VM:
		r = kvm_clear_stat_per_vm(stat_data->kvm,
					  stat_data->dbgfs_item->offset);
		break;
	case KVM_STAT_VCPU:
		r = kvm_clear_stat_per_vcpu(stat_data->kvm,
					    stat_data->dbgfs_item->offset);
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
	.llseek = no_llseek,
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

static const struct file_operations *stat_fops[] = {
	[KVM_STAT_VCPU] = &vcpu_stat_fops,
	[KVM_STAT_VM]   = &vm_stat_fops,
};

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

	env = kzalloc(sizeof(*env), GFP_KERNEL_ACCOUNT);
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

	if (!IS_ERR_OR_NULL(kvm->debugfs_dentry)) {
		char *tmp, *p = kmalloc(PATH_MAX, GFP_KERNEL_ACCOUNT);

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
	struct kvm_stats_debugfs_item *p;

	kvm_debugfs_dir = debugfs_create_dir("kvm", NULL);

	kvm_debugfs_num_entries = 0;
	for (p = debugfs_entries; p->name; ++p, kvm_debugfs_num_entries++) {
		debugfs_create_file(p->name, KVM_DBGFS_GET_MODE(p),
				    kvm_debugfs_dir, (void *)(long)p->offset,
				    stat_fops[p->kind]);
	}
}

static int kvm_suspend(void)
{
	if (kvm_usage_count)
		hardware_disable_nolock(NULL);
	return 0;
}

static void kvm_resume(void)
{
	if (kvm_usage_count) {
#ifdef CONFIG_LOCKDEP
		WARN_ON(lockdep_is_held(&kvm_count_lock));
#endif
		hardware_enable_nolock(NULL);
	}
}

static struct syscore_ops kvm_syscore_ops = {
	.suspend = kvm_suspend,
	.resume = kvm_resume,
};

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
	kvm_arch_sched_in(vcpu, cpu);
	kvm_arch_vcpu_load(vcpu, cpu);
}

static void kvm_sched_out(struct preempt_notifier *pn,
			  struct task_struct *next)
{
	struct kvm_vcpu *vcpu = preempt_notifier_to_vcpu(pn);

	if (current->state == TASK_RUNNING) {
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

struct kvm_cpu_compat_check {
	void *opaque;
	int *ret;
};

static void check_processor_compat(void *data)
{
	struct kvm_cpu_compat_check *c = data;

	*c->ret = kvm_arch_check_processor_compat(c->opaque);
}

int kvm_init(void *opaque, unsigned vcpu_size, unsigned vcpu_align,
		  struct module *module)
{
	struct kvm_cpu_compat_check c;
	int r;
	int cpu;

	r = kvm_arch_init(opaque);
	if (r)
		goto out_fail;

	/*
	 * kvm_arch_init makes sure there's at most one caller
	 * for architectures that support multiple implementations,
	 * like intel and amd on x86.
	 * kvm_arch_init must be called before kvm_irqfd_init to avoid creating
	 * conflicts in case kvm is already setup for another implementation.
	 */
	r = kvm_irqfd_init();
	if (r)
		goto out_irqfd;

	if (!zalloc_cpumask_var(&cpus_hardware_enabled, GFP_KERNEL)) {
		r = -ENOMEM;
		goto out_free_0;
	}

	r = kvm_arch_hardware_setup(opaque);
	if (r < 0)
		goto out_free_1;

	c.ret = &r;
	c.opaque = opaque;
	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu, check_processor_compat, &c, 1);
		if (r < 0)
			goto out_free_2;
	}

	r = cpuhp_setup_state_nocalls(CPUHP_AP_KVM_STARTING, "kvm/cpu:starting",
				      kvm_starting_cpu, kvm_dying_cpu);
	if (r)
		goto out_free_2;
	register_reboot_notifier(&kvm_reboot_notifier);

	/* A kmem cache lets us meet the alignment requirements of fx_save. */
	if (!vcpu_align)
		vcpu_align = __alignof__(struct kvm_vcpu);
	kvm_vcpu_cache =
		kmem_cache_create_usercopy("kvm_vcpu", vcpu_size, vcpu_align,
					   SLAB_ACCOUNT,
					   offsetof(struct kvm_vcpu, arch),
					   sizeof_field(struct kvm_vcpu, arch),
					   NULL);
	if (!kvm_vcpu_cache) {
		r = -ENOMEM;
		goto out_free_3;
	}

	r = kvm_async_pf_init();
	if (r)
		goto out_free;

	kvm_chardev_ops.owner = module;
	kvm_vm_fops.owner = module;
	kvm_vcpu_fops.owner = module;

	r = misc_register(&kvm_dev);
	if (r) {
		pr_err("kvm: misc device register failed\n");
		goto out_unreg;
	}

	register_syscore_ops(&kvm_syscore_ops);

	kvm_preempt_ops.sched_in = kvm_sched_in;
	kvm_preempt_ops.sched_out = kvm_sched_out;

	kvm_init_debug();

	r = kvm_vfio_ops_init();
	WARN_ON(r);

	return 0;

out_unreg:
	kvm_async_pf_deinit();
out_free:
	kmem_cache_destroy(kvm_vcpu_cache);
out_free_3:
	unregister_reboot_notifier(&kvm_reboot_notifier);
	cpuhp_remove_state_nocalls(CPUHP_AP_KVM_STARTING);
out_free_2:
	kvm_arch_hardware_unsetup();
out_free_1:
	free_cpumask_var(cpus_hardware_enabled);
out_free_0:
	kvm_irqfd_exit();
out_irqfd:
	kvm_arch_exit();
out_fail:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_init);

void kvm_exit(void)
{
	debugfs_remove_recursive(kvm_debugfs_dir);
	misc_deregister(&kvm_dev);
	kmem_cache_destroy(kvm_vcpu_cache);
	kvm_async_pf_deinit();
	unregister_syscore_ops(&kvm_syscore_ops);
	unregister_reboot_notifier(&kvm_reboot_notifier);
	cpuhp_remove_state_nocalls(CPUHP_AP_KVM_STARTING);
	on_each_cpu(hardware_disable_nolock, NULL, 1);
	kvm_arch_hardware_unsetup();
	kvm_arch_exit();
	kvm_irqfd_exit();
	free_cpumask_var(cpus_hardware_enabled);
	kvm_vfio_ops_exit();
}
EXPORT_SYMBOL_GPL(kvm_exit);

struct kvm_vm_worker_thread_context {
	struct kvm *kvm;
	struct task_struct *parent;
	struct completion init_done;
	kvm_vm_thread_fn_t thread_fn;
	uintptr_t data;
	int err;
};

static int kvm_vm_worker_thread(void *context)
{
	/*
	 * The init_context is allocated on the stack of the parent thread, so
	 * we have to locally copy anything that is needed beyond initialization
	 */
	struct kvm_vm_worker_thread_context *init_context = context;
	struct kvm *kvm = init_context->kvm;
	kvm_vm_thread_fn_t thread_fn = init_context->thread_fn;
	uintptr_t data = init_context->data;
	int err;

	err = kthread_park(current);
	/* kthread_park(current) is never supposed to return an error */
	WARN_ON(err != 0);
	if (err)
		goto init_complete;

	err = cgroup_attach_task_all(init_context->parent, current);
	if (err) {
		kvm_err("%s: cgroup_attach_task_all failed with err %d\n",
			__func__, err);
		goto init_complete;
	}

	set_user_nice(current, task_nice(init_context->parent));

init_complete:
	init_context->err = err;
	complete(&init_context->init_done);
	init_context = NULL;

	if (err)
		return err;

	/* Wait to be woken up by the spawner before proceeding. */
	kthread_parkme();

	if (!kthread_should_stop())
		err = thread_fn(kvm, data);

	return err;
}

int kvm_vm_create_worker_thread(struct kvm *kvm, kvm_vm_thread_fn_t thread_fn,
				uintptr_t data, const char *name,
				struct task_struct **thread_ptr)
{
	struct kvm_vm_worker_thread_context init_context = {};
	struct task_struct *thread;

	*thread_ptr = NULL;
	init_context.kvm = kvm;
	init_context.parent = current;
	init_context.thread_fn = thread_fn;
	init_context.data = data;
	init_completion(&init_context.init_done);

	thread = kthread_run(kvm_vm_worker_thread, &init_context,
			     "%s-%d", name, task_pid_nr(current));
	if (IS_ERR(thread))
		return PTR_ERR(thread);

	/* kthread_run is never supposed to return NULL */
	WARN_ON(thread == NULL);

	wait_for_completion(&init_context.init_done);

	if (!init_context.err)
		*thread_ptr = thread;

	return init_context.err;
}

/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "iodev.h"

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/percpu.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/reboot.h>
#include <linux/debugfs.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/sched.h>
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

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>

#ifdef KVM_COALESCED_MMIO_PAGE_OFFSET
#include "coalesced_mmio.h"
#endif

#ifdef KVM_CAP_DEVICE_ASSIGNMENT
#include <linux/pci.h>
#include <linux/interrupt.h>
#include "irq.h"
#endif

#define CREATE_TRACE_POINTS
#include <trace/events/kvm.h>

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

/*
 * Ordering of locks:
 *
 * 		kvm->slots_lock --> kvm->lock --> kvm->irq_lock
 */

DEFINE_SPINLOCK(kvm_lock);
LIST_HEAD(vm_list);

static cpumask_var_t cpus_hardware_enabled;

struct kmem_cache *kvm_vcpu_cache;
EXPORT_SYMBOL_GPL(kvm_vcpu_cache);

static __read_mostly struct preempt_ops kvm_preempt_ops;

struct dentry *kvm_debugfs_dir;

static long kvm_vcpu_ioctl(struct file *file, unsigned int ioctl,
			   unsigned long arg);

static bool kvm_rebooting;

static bool largepages_enabled = true;

#ifdef KVM_CAP_DEVICE_ASSIGNMENT
static struct kvm_assigned_dev_kernel *kvm_find_assigned_dev(struct list_head *head,
						      int assigned_dev_id)
{
	struct list_head *ptr;
	struct kvm_assigned_dev_kernel *match;

	list_for_each(ptr, head) {
		match = list_entry(ptr, struct kvm_assigned_dev_kernel, list);
		if (match->assigned_dev_id == assigned_dev_id)
			return match;
	}
	return NULL;
}

static int find_index_from_host_irq(struct kvm_assigned_dev_kernel
				    *assigned_dev, int irq)
{
	int i, index;
	struct msix_entry *host_msix_entries;

	host_msix_entries = assigned_dev->host_msix_entries;

	index = -1;
	for (i = 0; i < assigned_dev->entries_nr; i++)
		if (irq == host_msix_entries[i].vector) {
			index = i;
			break;
		}
	if (index < 0) {
		printk(KERN_WARNING "Fail to find correlated MSI-X entry!\n");
		return 0;
	}

	return index;
}

static void kvm_assigned_dev_interrupt_work_handler(struct work_struct *work)
{
	struct kvm_assigned_dev_kernel *assigned_dev;
	struct kvm *kvm;
	int i;

	assigned_dev = container_of(work, struct kvm_assigned_dev_kernel,
				    interrupt_work);
	kvm = assigned_dev->kvm;

	mutex_lock(&kvm->irq_lock);
	spin_lock_irq(&assigned_dev->assigned_dev_lock);
	if (assigned_dev->irq_requested_type & KVM_DEV_IRQ_HOST_MSIX) {
		struct kvm_guest_msix_entry *guest_entries =
			assigned_dev->guest_msix_entries;
		for (i = 0; i < assigned_dev->entries_nr; i++) {
			if (!(guest_entries[i].flags &
					KVM_ASSIGNED_MSIX_PENDING))
				continue;
			guest_entries[i].flags &= ~KVM_ASSIGNED_MSIX_PENDING;
			kvm_set_irq(assigned_dev->kvm,
				    assigned_dev->irq_source_id,
				    guest_entries[i].vector, 1);
		}
	} else
		kvm_set_irq(assigned_dev->kvm, assigned_dev->irq_source_id,
			    assigned_dev->guest_irq, 1);

	spin_unlock_irq(&assigned_dev->assigned_dev_lock);
	mutex_unlock(&assigned_dev->kvm->irq_lock);
}

static irqreturn_t kvm_assigned_dev_intr(int irq, void *dev_id)
{
	unsigned long flags;
	struct kvm_assigned_dev_kernel *assigned_dev =
		(struct kvm_assigned_dev_kernel *) dev_id;

	spin_lock_irqsave(&assigned_dev->assigned_dev_lock, flags);
	if (assigned_dev->irq_requested_type & KVM_DEV_IRQ_HOST_MSIX) {
		int index = find_index_from_host_irq(assigned_dev, irq);
		if (index < 0)
			goto out;
		assigned_dev->guest_msix_entries[index].flags |=
			KVM_ASSIGNED_MSIX_PENDING;
	}

	schedule_work(&assigned_dev->interrupt_work);

	if (assigned_dev->irq_requested_type & KVM_DEV_IRQ_GUEST_INTX) {
		disable_irq_nosync(irq);
		assigned_dev->host_irq_disabled = true;
	}

out:
	spin_unlock_irqrestore(&assigned_dev->assigned_dev_lock, flags);
	return IRQ_HANDLED;
}

/* Ack the irq line for an assigned device */
static void kvm_assigned_dev_ack_irq(struct kvm_irq_ack_notifier *kian)
{
	struct kvm_assigned_dev_kernel *dev;
	unsigned long flags;

	if (kian->gsi == -1)
		return;

	dev = container_of(kian, struct kvm_assigned_dev_kernel,
			   ack_notifier);

	kvm_set_irq(dev->kvm, dev->irq_source_id, dev->guest_irq, 0);

	/* The guest irq may be shared so this ack may be
	 * from another device.
	 */
	spin_lock_irqsave(&dev->assigned_dev_lock, flags);
	if (dev->host_irq_disabled) {
		enable_irq(dev->host_irq);
		dev->host_irq_disabled = false;
	}
	spin_unlock_irqrestore(&dev->assigned_dev_lock, flags);
}

static void deassign_guest_irq(struct kvm *kvm,
			       struct kvm_assigned_dev_kernel *assigned_dev)
{
	kvm_unregister_irq_ack_notifier(kvm, &assigned_dev->ack_notifier);
	assigned_dev->ack_notifier.gsi = -1;

	if (assigned_dev->irq_source_id != -1)
		kvm_free_irq_source_id(kvm, assigned_dev->irq_source_id);
	assigned_dev->irq_source_id = -1;
	assigned_dev->irq_requested_type &= ~(KVM_DEV_IRQ_GUEST_MASK);
}

/* The function implicit hold kvm->lock mutex due to cancel_work_sync() */
static void deassign_host_irq(struct kvm *kvm,
			      struct kvm_assigned_dev_kernel *assigned_dev)
{
	/*
	 * In kvm_free_device_irq, cancel_work_sync return true if:
	 * 1. work is scheduled, and then cancelled.
	 * 2. work callback is executed.
	 *
	 * The first one ensured that the irq is disabled and no more events
	 * would happen. But for the second one, the irq may be enabled (e.g.
	 * for MSI). So we disable irq here to prevent further events.
	 *
	 * Notice this maybe result in nested disable if the interrupt type is
	 * INTx, but it's OK for we are going to free it.
	 *
	 * If this function is a part of VM destroy, please ensure that till
	 * now, the kvm state is still legal for probably we also have to wait
	 * interrupt_work done.
	 */
	if (assigned_dev->irq_requested_type & KVM_DEV_IRQ_HOST_MSIX) {
		int i;
		for (i = 0; i < assigned_dev->entries_nr; i++)
			disable_irq_nosync(assigned_dev->
					   host_msix_entries[i].vector);

		cancel_work_sync(&assigned_dev->interrupt_work);

		for (i = 0; i < assigned_dev->entries_nr; i++)
			free_irq(assigned_dev->host_msix_entries[i].vector,
				 (void *)assigned_dev);

		assigned_dev->entries_nr = 0;
		kfree(assigned_dev->host_msix_entries);
		kfree(assigned_dev->guest_msix_entries);
		pci_disable_msix(assigned_dev->dev);
	} else {
		/* Deal with MSI and INTx */
		disable_irq_nosync(assigned_dev->host_irq);
		cancel_work_sync(&assigned_dev->interrupt_work);

		free_irq(assigned_dev->host_irq, (void *)assigned_dev);

		if (assigned_dev->irq_requested_type & KVM_DEV_IRQ_HOST_MSI)
			pci_disable_msi(assigned_dev->dev);
	}

	assigned_dev->irq_requested_type &= ~(KVM_DEV_IRQ_HOST_MASK);
}

static int kvm_deassign_irq(struct kvm *kvm,
			    struct kvm_assigned_dev_kernel *assigned_dev,
			    unsigned long irq_requested_type)
{
	unsigned long guest_irq_type, host_irq_type;

	if (!irqchip_in_kernel(kvm))
		return -EINVAL;
	/* no irq assignment to deassign */
	if (!assigned_dev->irq_requested_type)
		return -ENXIO;

	host_irq_type = irq_requested_type & KVM_DEV_IRQ_HOST_MASK;
	guest_irq_type = irq_requested_type & KVM_DEV_IRQ_GUEST_MASK;

	if (host_irq_type)
		deassign_host_irq(kvm, assigned_dev);
	if (guest_irq_type)
		deassign_guest_irq(kvm, assigned_dev);

	return 0;
}

static void kvm_free_assigned_irq(struct kvm *kvm,
				  struct kvm_assigned_dev_kernel *assigned_dev)
{
	kvm_deassign_irq(kvm, assigned_dev, assigned_dev->irq_requested_type);
}

static void kvm_free_assigned_device(struct kvm *kvm,
				     struct kvm_assigned_dev_kernel
				     *assigned_dev)
{
	kvm_free_assigned_irq(kvm, assigned_dev);

	pci_reset_function(assigned_dev->dev);

	pci_release_regions(assigned_dev->dev);
	pci_disable_device(assigned_dev->dev);
	pci_dev_put(assigned_dev->dev);

	list_del(&assigned_dev->list);
	kfree(assigned_dev);
}

void kvm_free_all_assigned_devices(struct kvm *kvm)
{
	struct list_head *ptr, *ptr2;
	struct kvm_assigned_dev_kernel *assigned_dev;

	list_for_each_safe(ptr, ptr2, &kvm->arch.assigned_dev_head) {
		assigned_dev = list_entry(ptr,
					  struct kvm_assigned_dev_kernel,
					  list);

		kvm_free_assigned_device(kvm, assigned_dev);
	}
}

static int assigned_device_enable_host_intx(struct kvm *kvm,
					    struct kvm_assigned_dev_kernel *dev)
{
	dev->host_irq = dev->dev->irq;
	/* Even though this is PCI, we don't want to use shared
	 * interrupts. Sharing host devices with guest-assigned devices
	 * on the same interrupt line is not a happy situation: there
	 * are going to be long delays in accepting, acking, etc.
	 */
	if (request_irq(dev->host_irq, kvm_assigned_dev_intr,
			0, "kvm_assigned_intx_device", (void *)dev))
		return -EIO;
	return 0;
}

#ifdef __KVM_HAVE_MSI
static int assigned_device_enable_host_msi(struct kvm *kvm,
					   struct kvm_assigned_dev_kernel *dev)
{
	int r;

	if (!dev->dev->msi_enabled) {
		r = pci_enable_msi(dev->dev);
		if (r)
			return r;
	}

	dev->host_irq = dev->dev->irq;
	if (request_irq(dev->host_irq, kvm_assigned_dev_intr, 0,
			"kvm_assigned_msi_device", (void *)dev)) {
		pci_disable_msi(dev->dev);
		return -EIO;
	}

	return 0;
}
#endif

#ifdef __KVM_HAVE_MSIX
static int assigned_device_enable_host_msix(struct kvm *kvm,
					    struct kvm_assigned_dev_kernel *dev)
{
	int i, r = -EINVAL;

	/* host_msix_entries and guest_msix_entries should have been
	 * initialized */
	if (dev->entries_nr == 0)
		return r;

	r = pci_enable_msix(dev->dev, dev->host_msix_entries, dev->entries_nr);
	if (r)
		return r;

	for (i = 0; i < dev->entries_nr; i++) {
		r = request_irq(dev->host_msix_entries[i].vector,
				kvm_assigned_dev_intr, 0,
				"kvm_assigned_msix_device",
				(void *)dev);
		/* FIXME: free requested_irq's on failure */
		if (r)
			return r;
	}

	return 0;
}

#endif

static int assigned_device_enable_guest_intx(struct kvm *kvm,
				struct kvm_assigned_dev_kernel *dev,
				struct kvm_assigned_irq *irq)
{
	dev->guest_irq = irq->guest_irq;
	dev->ack_notifier.gsi = irq->guest_irq;
	return 0;
}

#ifdef __KVM_HAVE_MSI
static int assigned_device_enable_guest_msi(struct kvm *kvm,
			struct kvm_assigned_dev_kernel *dev,
			struct kvm_assigned_irq *irq)
{
	dev->guest_irq = irq->guest_irq;
	dev->ack_notifier.gsi = -1;
	dev->host_irq_disabled = false;
	return 0;
}
#endif
#ifdef __KVM_HAVE_MSIX
static int assigned_device_enable_guest_msix(struct kvm *kvm,
			struct kvm_assigned_dev_kernel *dev,
			struct kvm_assigned_irq *irq)
{
	dev->guest_irq = irq->guest_irq;
	dev->ack_notifier.gsi = -1;
	dev->host_irq_disabled = false;
	return 0;
}
#endif

static int assign_host_irq(struct kvm *kvm,
			   struct kvm_assigned_dev_kernel *dev,
			   __u32 host_irq_type)
{
	int r = -EEXIST;

	if (dev->irq_requested_type & KVM_DEV_IRQ_HOST_MASK)
		return r;

	switch (host_irq_type) {
	case KVM_DEV_IRQ_HOST_INTX:
		r = assigned_device_enable_host_intx(kvm, dev);
		break;
#ifdef __KVM_HAVE_MSI
	case KVM_DEV_IRQ_HOST_MSI:
		r = assigned_device_enable_host_msi(kvm, dev);
		break;
#endif
#ifdef __KVM_HAVE_MSIX
	case KVM_DEV_IRQ_HOST_MSIX:
		r = assigned_device_enable_host_msix(kvm, dev);
		break;
#endif
	default:
		r = -EINVAL;
	}

	if (!r)
		dev->irq_requested_type |= host_irq_type;

	return r;
}

static int assign_guest_irq(struct kvm *kvm,
			    struct kvm_assigned_dev_kernel *dev,
			    struct kvm_assigned_irq *irq,
			    unsigned long guest_irq_type)
{
	int id;
	int r = -EEXIST;

	if (dev->irq_requested_type & KVM_DEV_IRQ_GUEST_MASK)
		return r;

	id = kvm_request_irq_source_id(kvm);
	if (id < 0)
		return id;

	dev->irq_source_id = id;

	switch (guest_irq_type) {
	case KVM_DEV_IRQ_GUEST_INTX:
		r = assigned_device_enable_guest_intx(kvm, dev, irq);
		break;
#ifdef __KVM_HAVE_MSI
	case KVM_DEV_IRQ_GUEST_MSI:
		r = assigned_device_enable_guest_msi(kvm, dev, irq);
		break;
#endif
#ifdef __KVM_HAVE_MSIX
	case KVM_DEV_IRQ_GUEST_MSIX:
		r = assigned_device_enable_guest_msix(kvm, dev, irq);
		break;
#endif
	default:
		r = -EINVAL;
	}

	if (!r) {
		dev->irq_requested_type |= guest_irq_type;
		kvm_register_irq_ack_notifier(kvm, &dev->ack_notifier);
	} else
		kvm_free_irq_source_id(kvm, dev->irq_source_id);

	return r;
}

/* TODO Deal with KVM_DEV_IRQ_ASSIGNED_MASK_MSIX */
static int kvm_vm_ioctl_assign_irq(struct kvm *kvm,
				   struct kvm_assigned_irq *assigned_irq)
{
	int r = -EINVAL;
	struct kvm_assigned_dev_kernel *match;
	unsigned long host_irq_type, guest_irq_type;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	if (!irqchip_in_kernel(kvm))
		return r;

	mutex_lock(&kvm->lock);
	r = -ENODEV;
	match = kvm_find_assigned_dev(&kvm->arch.assigned_dev_head,
				      assigned_irq->assigned_dev_id);
	if (!match)
		goto out;

	host_irq_type = (assigned_irq->flags & KVM_DEV_IRQ_HOST_MASK);
	guest_irq_type = (assigned_irq->flags & KVM_DEV_IRQ_GUEST_MASK);

	r = -EINVAL;
	/* can only assign one type at a time */
	if (hweight_long(host_irq_type) > 1)
		goto out;
	if (hweight_long(guest_irq_type) > 1)
		goto out;
	if (host_irq_type == 0 && guest_irq_type == 0)
		goto out;

	r = 0;
	if (host_irq_type)
		r = assign_host_irq(kvm, match, host_irq_type);
	if (r)
		goto out;

	if (guest_irq_type)
		r = assign_guest_irq(kvm, match, assigned_irq, guest_irq_type);
out:
	mutex_unlock(&kvm->lock);
	return r;
}

static int kvm_vm_ioctl_deassign_dev_irq(struct kvm *kvm,
					 struct kvm_assigned_irq
					 *assigned_irq)
{
	int r = -ENODEV;
	struct kvm_assigned_dev_kernel *match;

	mutex_lock(&kvm->lock);

	match = kvm_find_assigned_dev(&kvm->arch.assigned_dev_head,
				      assigned_irq->assigned_dev_id);
	if (!match)
		goto out;

	r = kvm_deassign_irq(kvm, match, assigned_irq->flags);
out:
	mutex_unlock(&kvm->lock);
	return r;
}

static int kvm_vm_ioctl_assign_device(struct kvm *kvm,
				      struct kvm_assigned_pci_dev *assigned_dev)
{
	int r = 0;
	struct kvm_assigned_dev_kernel *match;
	struct pci_dev *dev;

	down_read(&kvm->slots_lock);
	mutex_lock(&kvm->lock);

	match = kvm_find_assigned_dev(&kvm->arch.assigned_dev_head,
				      assigned_dev->assigned_dev_id);
	if (match) {
		/* device already assigned */
		r = -EEXIST;
		goto out;
	}

	match = kzalloc(sizeof(struct kvm_assigned_dev_kernel), GFP_KERNEL);
	if (match == NULL) {
		printk(KERN_INFO "%s: Couldn't allocate memory\n",
		       __func__);
		r = -ENOMEM;
		goto out;
	}
	dev = pci_get_bus_and_slot(assigned_dev->busnr,
				   assigned_dev->devfn);
	if (!dev) {
		printk(KERN_INFO "%s: host device not found\n", __func__);
		r = -EINVAL;
		goto out_free;
	}
	if (pci_enable_device(dev)) {
		printk(KERN_INFO "%s: Could not enable PCI device\n", __func__);
		r = -EBUSY;
		goto out_put;
	}
	r = pci_request_regions(dev, "kvm_assigned_device");
	if (r) {
		printk(KERN_INFO "%s: Could not get access to device regions\n",
		       __func__);
		goto out_disable;
	}

	pci_reset_function(dev);

	match->assigned_dev_id = assigned_dev->assigned_dev_id;
	match->host_busnr = assigned_dev->busnr;
	match->host_devfn = assigned_dev->devfn;
	match->flags = assigned_dev->flags;
	match->dev = dev;
	spin_lock_init(&match->assigned_dev_lock);
	match->irq_source_id = -1;
	match->kvm = kvm;
	match->ack_notifier.irq_acked = kvm_assigned_dev_ack_irq;
	INIT_WORK(&match->interrupt_work,
		  kvm_assigned_dev_interrupt_work_handler);

	list_add(&match->list, &kvm->arch.assigned_dev_head);

	if (assigned_dev->flags & KVM_DEV_ASSIGN_ENABLE_IOMMU) {
		if (!kvm->arch.iommu_domain) {
			r = kvm_iommu_map_guest(kvm);
			if (r)
				goto out_list_del;
		}
		r = kvm_assign_device(kvm, match);
		if (r)
			goto out_list_del;
	}

out:
	mutex_unlock(&kvm->lock);
	up_read(&kvm->slots_lock);
	return r;
out_list_del:
	list_del(&match->list);
	pci_release_regions(dev);
out_disable:
	pci_disable_device(dev);
out_put:
	pci_dev_put(dev);
out_free:
	kfree(match);
	mutex_unlock(&kvm->lock);
	up_read(&kvm->slots_lock);
	return r;
}
#endif

#ifdef KVM_CAP_DEVICE_DEASSIGNMENT
static int kvm_vm_ioctl_deassign_device(struct kvm *kvm,
		struct kvm_assigned_pci_dev *assigned_dev)
{
	int r = 0;
	struct kvm_assigned_dev_kernel *match;

	mutex_lock(&kvm->lock);

	match = kvm_find_assigned_dev(&kvm->arch.assigned_dev_head,
				      assigned_dev->assigned_dev_id);
	if (!match) {
		printk(KERN_INFO "%s: device hasn't been assigned before, "
		  "so cannot be deassigned\n", __func__);
		r = -EINVAL;
		goto out;
	}

	if (match->flags & KVM_DEV_ASSIGN_ENABLE_IOMMU)
		kvm_deassign_device(kvm, match);

	kvm_free_assigned_device(kvm, match);

out:
	mutex_unlock(&kvm->lock);
	return r;
}
#endif

inline int kvm_is_mmio_pfn(pfn_t pfn)
{
	if (pfn_valid(pfn)) {
		struct page *page = compound_head(pfn_to_page(pfn));
		return PageReserved(page);
	}

	return true;
}

/*
 * Switches to specified vcpu, until a matching vcpu_put()
 */
void vcpu_load(struct kvm_vcpu *vcpu)
{
	int cpu;

	mutex_lock(&vcpu->mutex);
	cpu = get_cpu();
	preempt_notifier_register(&vcpu->preempt_notifier);
	kvm_arch_vcpu_load(vcpu, cpu);
	put_cpu();
}

void vcpu_put(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	kvm_arch_vcpu_put(vcpu);
	preempt_notifier_unregister(&vcpu->preempt_notifier);
	preempt_enable();
	mutex_unlock(&vcpu->mutex);
}

static void ack_flush(void *_completed)
{
}

static bool make_all_cpus_request(struct kvm *kvm, unsigned int req)
{
	int i, cpu, me;
	cpumask_var_t cpus;
	bool called = true;
	struct kvm_vcpu *vcpu;

	zalloc_cpumask_var(&cpus, GFP_ATOMIC);

	spin_lock(&kvm->requests_lock);
	me = smp_processor_id();
	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (test_and_set_bit(req, &vcpu->requests))
			continue;
		cpu = vcpu->cpu;
		if (cpus != NULL && cpu != -1 && cpu != me)
			cpumask_set_cpu(cpu, cpus);
	}
	if (unlikely(cpus == NULL))
		smp_call_function_many(cpu_online_mask, ack_flush, NULL, 1);
	else if (!cpumask_empty(cpus))
		smp_call_function_many(cpus, ack_flush, NULL, 1);
	else
		called = false;
	spin_unlock(&kvm->requests_lock);
	free_cpumask_var(cpus);
	return called;
}

void kvm_flush_remote_tlbs(struct kvm *kvm)
{
	if (make_all_cpus_request(kvm, KVM_REQ_TLB_FLUSH))
		++kvm->stat.remote_tlb_flush;
}

void kvm_reload_remote_mmus(struct kvm *kvm)
{
	make_all_cpus_request(kvm, KVM_REQ_MMU_RELOAD);
}

int kvm_vcpu_init(struct kvm_vcpu *vcpu, struct kvm *kvm, unsigned id)
{
	struct page *page;
	int r;

	mutex_init(&vcpu->mutex);
	vcpu->cpu = -1;
	vcpu->kvm = kvm;
	vcpu->vcpu_id = id;
	init_waitqueue_head(&vcpu->wq);

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page) {
		r = -ENOMEM;
		goto fail;
	}
	vcpu->run = page_address(page);

	r = kvm_arch_vcpu_init(vcpu);
	if (r < 0)
		goto fail_free_run;
	return 0;

fail_free_run:
	free_page((unsigned long)vcpu->run);
fail:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_vcpu_init);

void kvm_vcpu_uninit(struct kvm_vcpu *vcpu)
{
	kvm_arch_vcpu_uninit(vcpu);
	free_page((unsigned long)vcpu->run);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_uninit);

#if defined(CONFIG_MMU_NOTIFIER) && defined(KVM_ARCH_WANT_MMU_NOTIFIER)
static inline struct kvm *mmu_notifier_to_kvm(struct mmu_notifier *mn)
{
	return container_of(mn, struct kvm, mmu_notifier);
}

static void kvm_mmu_notifier_invalidate_page(struct mmu_notifier *mn,
					     struct mm_struct *mm,
					     unsigned long address)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int need_tlb_flush;

	/*
	 * When ->invalidate_page runs, the linux pte has been zapped
	 * already but the page is still allocated until
	 * ->invalidate_page returns. So if we increase the sequence
	 * here the kvm page fault will notice if the spte can't be
	 * established because the page is going to be freed. If
	 * instead the kvm page fault establishes the spte before
	 * ->invalidate_page runs, kvm_unmap_hva will release it
	 * before returning.
	 *
	 * The sequence increase only need to be seen at spin_unlock
	 * time, and not at spin_lock time.
	 *
	 * Increasing the sequence after the spin_unlock would be
	 * unsafe because the kvm page fault could then establish the
	 * pte after kvm_unmap_hva returned, without noticing the page
	 * is going to be freed.
	 */
	spin_lock(&kvm->mmu_lock);
	kvm->mmu_notifier_seq++;
	need_tlb_flush = kvm_unmap_hva(kvm, address);
	spin_unlock(&kvm->mmu_lock);

	/* we've to flush the tlb before the pages can be freed */
	if (need_tlb_flush)
		kvm_flush_remote_tlbs(kvm);

}

static void kvm_mmu_notifier_invalidate_range_start(struct mmu_notifier *mn,
						    struct mm_struct *mm,
						    unsigned long start,
						    unsigned long end)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int need_tlb_flush = 0;

	spin_lock(&kvm->mmu_lock);
	/*
	 * The count increase must become visible at unlock time as no
	 * spte can be established without taking the mmu_lock and
	 * count is also read inside the mmu_lock critical section.
	 */
	kvm->mmu_notifier_count++;
	for (; start < end; start += PAGE_SIZE)
		need_tlb_flush |= kvm_unmap_hva(kvm, start);
	spin_unlock(&kvm->mmu_lock);

	/* we've to flush the tlb before the pages can be freed */
	if (need_tlb_flush)
		kvm_flush_remote_tlbs(kvm);
}

static void kvm_mmu_notifier_invalidate_range_end(struct mmu_notifier *mn,
						  struct mm_struct *mm,
						  unsigned long start,
						  unsigned long end)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);

	spin_lock(&kvm->mmu_lock);
	/*
	 * This sequence increase will notify the kvm page fault that
	 * the page that is going to be mapped in the spte could have
	 * been freed.
	 */
	kvm->mmu_notifier_seq++;
	/*
	 * The above sequence increase must be visible before the
	 * below count decrease but both values are read by the kvm
	 * page fault under mmu_lock spinlock so we don't need to add
	 * a smb_wmb() here in between the two.
	 */
	kvm->mmu_notifier_count--;
	spin_unlock(&kvm->mmu_lock);

	BUG_ON(kvm->mmu_notifier_count < 0);
}

static int kvm_mmu_notifier_clear_flush_young(struct mmu_notifier *mn,
					      struct mm_struct *mm,
					      unsigned long address)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	int young;

	spin_lock(&kvm->mmu_lock);
	young = kvm_age_hva(kvm, address);
	spin_unlock(&kvm->mmu_lock);

	if (young)
		kvm_flush_remote_tlbs(kvm);

	return young;
}

static void kvm_mmu_notifier_release(struct mmu_notifier *mn,
				     struct mm_struct *mm)
{
	struct kvm *kvm = mmu_notifier_to_kvm(mn);
	kvm_arch_flush_shadow(kvm);
}

static const struct mmu_notifier_ops kvm_mmu_notifier_ops = {
	.invalidate_page	= kvm_mmu_notifier_invalidate_page,
	.invalidate_range_start	= kvm_mmu_notifier_invalidate_range_start,
	.invalidate_range_end	= kvm_mmu_notifier_invalidate_range_end,
	.clear_flush_young	= kvm_mmu_notifier_clear_flush_young,
	.release		= kvm_mmu_notifier_release,
};
#endif /* CONFIG_MMU_NOTIFIER && KVM_ARCH_WANT_MMU_NOTIFIER */

static struct kvm *kvm_create_vm(void)
{
	struct kvm *kvm = kvm_arch_create_vm();
#ifdef KVM_COALESCED_MMIO_PAGE_OFFSET
	struct page *page;
#endif

	if (IS_ERR(kvm))
		goto out;
#ifdef CONFIG_HAVE_KVM_IRQCHIP
	INIT_LIST_HEAD(&kvm->irq_routing);
	INIT_HLIST_HEAD(&kvm->mask_notifier_list);
#endif

#ifdef KVM_COALESCED_MMIO_PAGE_OFFSET
	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page) {
		kfree(kvm);
		return ERR_PTR(-ENOMEM);
	}
	kvm->coalesced_mmio_ring =
			(struct kvm_coalesced_mmio_ring *)page_address(page);
#endif

#if defined(CONFIG_MMU_NOTIFIER) && defined(KVM_ARCH_WANT_MMU_NOTIFIER)
	{
		int err;
		kvm->mmu_notifier.ops = &kvm_mmu_notifier_ops;
		err = mmu_notifier_register(&kvm->mmu_notifier, current->mm);
		if (err) {
#ifdef KVM_COALESCED_MMIO_PAGE_OFFSET
			put_page(page);
#endif
			kfree(kvm);
			return ERR_PTR(err);
		}
	}
#endif

	kvm->mm = current->mm;
	atomic_inc(&kvm->mm->mm_count);
	spin_lock_init(&kvm->mmu_lock);
	spin_lock_init(&kvm->requests_lock);
	kvm_io_bus_init(&kvm->pio_bus);
	kvm_eventfd_init(kvm);
	mutex_init(&kvm->lock);
	mutex_init(&kvm->irq_lock);
	kvm_io_bus_init(&kvm->mmio_bus);
	init_rwsem(&kvm->slots_lock);
	atomic_set(&kvm->users_count, 1);
	spin_lock(&kvm_lock);
	list_add(&kvm->vm_list, &vm_list);
	spin_unlock(&kvm_lock);
#ifdef KVM_COALESCED_MMIO_PAGE_OFFSET
	kvm_coalesced_mmio_init(kvm);
#endif
out:
	return kvm;
}

/*
 * Free any memory in @free but not in @dont.
 */
static void kvm_free_physmem_slot(struct kvm_memory_slot *free,
				  struct kvm_memory_slot *dont)
{
	int i;

	if (!dont || free->rmap != dont->rmap)
		vfree(free->rmap);

	if (!dont || free->dirty_bitmap != dont->dirty_bitmap)
		vfree(free->dirty_bitmap);


	for (i = 0; i < KVM_NR_PAGE_SIZES - 1; ++i) {
		if (!dont || free->lpage_info[i] != dont->lpage_info[i]) {
			vfree(free->lpage_info[i]);
			free->lpage_info[i] = NULL;
		}
	}

	free->npages = 0;
	free->dirty_bitmap = NULL;
	free->rmap = NULL;
}

void kvm_free_physmem(struct kvm *kvm)
{
	int i;

	for (i = 0; i < kvm->nmemslots; ++i)
		kvm_free_physmem_slot(&kvm->memslots[i], NULL);
}

static void kvm_destroy_vm(struct kvm *kvm)
{
	struct mm_struct *mm = kvm->mm;

	kvm_arch_sync_events(kvm);
	spin_lock(&kvm_lock);
	list_del(&kvm->vm_list);
	spin_unlock(&kvm_lock);
	kvm_free_irq_routing(kvm);
	kvm_io_bus_destroy(&kvm->pio_bus);
	kvm_io_bus_destroy(&kvm->mmio_bus);
#ifdef KVM_COALESCED_MMIO_PAGE_OFFSET
	if (kvm->coalesced_mmio_ring != NULL)
		free_page((unsigned long)kvm->coalesced_mmio_ring);
#endif
#if defined(CONFIG_MMU_NOTIFIER) && defined(KVM_ARCH_WANT_MMU_NOTIFIER)
	mmu_notifier_unregister(&kvm->mmu_notifier, kvm->mm);
#else
	kvm_arch_flush_shadow(kvm);
#endif
	kvm_arch_destroy_vm(kvm);
	mmdrop(mm);
}

void kvm_get_kvm(struct kvm *kvm)
{
	atomic_inc(&kvm->users_count);
}
EXPORT_SYMBOL_GPL(kvm_get_kvm);

void kvm_put_kvm(struct kvm *kvm)
{
	if (atomic_dec_and_test(&kvm->users_count))
		kvm_destroy_vm(kvm);
}
EXPORT_SYMBOL_GPL(kvm_put_kvm);


static int kvm_vm_release(struct inode *inode, struct file *filp)
{
	struct kvm *kvm = filp->private_data;

	kvm_irqfd_release(kvm);

	kvm_put_kvm(kvm);
	return 0;
}

/*
 * Allocate some memory and give it an address in the guest physical address
 * space.
 *
 * Discontiguous memory is allowed, mostly for framebuffers.
 *
 * Must be called holding mmap_sem for write.
 */
int __kvm_set_memory_region(struct kvm *kvm,
			    struct kvm_userspace_memory_region *mem,
			    int user_alloc)
{
	int r;
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long i;
	struct kvm_memory_slot *memslot;
	struct kvm_memory_slot old, new;

	r = -EINVAL;
	/* General sanity checks */
	if (mem->memory_size & (PAGE_SIZE - 1))
		goto out;
	if (mem->guest_phys_addr & (PAGE_SIZE - 1))
		goto out;
	if (user_alloc && (mem->userspace_addr & (PAGE_SIZE - 1)))
		goto out;
	if (mem->slot >= KVM_MEMORY_SLOTS + KVM_PRIVATE_MEM_SLOTS)
		goto out;
	if (mem->guest_phys_addr + mem->memory_size < mem->guest_phys_addr)
		goto out;

	memslot = &kvm->memslots[mem->slot];
	base_gfn = mem->guest_phys_addr >> PAGE_SHIFT;
	npages = mem->memory_size >> PAGE_SHIFT;

	if (!npages)
		mem->flags &= ~KVM_MEM_LOG_DIRTY_PAGES;

	new = old = *memslot;

	new.base_gfn = base_gfn;
	new.npages = npages;
	new.flags = mem->flags;

	/* Disallow changing a memory slot's size. */
	r = -EINVAL;
	if (npages && old.npages && npages != old.npages)
		goto out_free;

	/* Check for overlaps */
	r = -EEXIST;
	for (i = 0; i < KVM_MEMORY_SLOTS; ++i) {
		struct kvm_memory_slot *s = &kvm->memslots[i];

		if (s == memslot || !s->npages)
			continue;
		if (!((base_gfn + npages <= s->base_gfn) ||
		      (base_gfn >= s->base_gfn + s->npages)))
			goto out_free;
	}

	/* Free page dirty bitmap if unneeded */
	if (!(new.flags & KVM_MEM_LOG_DIRTY_PAGES))
		new.dirty_bitmap = NULL;

	r = -ENOMEM;

	/* Allocate if a slot is being created */
#ifndef CONFIG_S390
	if (npages && !new.rmap) {
		new.rmap = vmalloc(npages * sizeof(struct page *));

		if (!new.rmap)
			goto out_free;

		memset(new.rmap, 0, npages * sizeof(*new.rmap));

		new.user_alloc = user_alloc;
		/*
		 * hva_to_rmmap() serialzies with the mmu_lock and to be
		 * safe it has to ignore memslots with !user_alloc &&
		 * !userspace_addr.
		 */
		if (user_alloc)
			new.userspace_addr = mem->userspace_addr;
		else
			new.userspace_addr = 0;
	}
	if (!npages)
		goto skip_lpage;

	for (i = 0; i < KVM_NR_PAGE_SIZES - 1; ++i) {
		unsigned long ugfn;
		unsigned long j;
		int lpages;
		int level = i + 2;

		/* Avoid unused variable warning if no large pages */
		(void)level;

		if (new.lpage_info[i])
			continue;

		lpages = 1 + (base_gfn + npages - 1) /
			     KVM_PAGES_PER_HPAGE(level);
		lpages -= base_gfn / KVM_PAGES_PER_HPAGE(level);

		new.lpage_info[i] = vmalloc(lpages * sizeof(*new.lpage_info[i]));

		if (!new.lpage_info[i])
			goto out_free;

		memset(new.lpage_info[i], 0,
		       lpages * sizeof(*new.lpage_info[i]));

		if (base_gfn % KVM_PAGES_PER_HPAGE(level))
			new.lpage_info[i][0].write_count = 1;
		if ((base_gfn+npages) % KVM_PAGES_PER_HPAGE(level))
			new.lpage_info[i][lpages - 1].write_count = 1;
		ugfn = new.userspace_addr >> PAGE_SHIFT;
		/*
		 * If the gfn and userspace address are not aligned wrt each
		 * other, or if explicitly asked to, disable large page
		 * support for this slot
		 */
		if ((base_gfn ^ ugfn) & (KVM_PAGES_PER_HPAGE(level) - 1) ||
		    !largepages_enabled)
			for (j = 0; j < lpages; ++j)
				new.lpage_info[i][j].write_count = 1;
	}

skip_lpage:

	/* Allocate page dirty bitmap if needed */
	if ((new.flags & KVM_MEM_LOG_DIRTY_PAGES) && !new.dirty_bitmap) {
		unsigned dirty_bytes = ALIGN(npages, BITS_PER_LONG) / 8;

		new.dirty_bitmap = vmalloc(dirty_bytes);
		if (!new.dirty_bitmap)
			goto out_free;
		memset(new.dirty_bitmap, 0, dirty_bytes);
		if (old.npages)
			kvm_arch_flush_shadow(kvm);
	}
#else  /* not defined CONFIG_S390 */
	new.user_alloc = user_alloc;
	if (user_alloc)
		new.userspace_addr = mem->userspace_addr;
#endif /* not defined CONFIG_S390 */

	if (!npages)
		kvm_arch_flush_shadow(kvm);

	spin_lock(&kvm->mmu_lock);
	if (mem->slot >= kvm->nmemslots)
		kvm->nmemslots = mem->slot + 1;

	*memslot = new;
	spin_unlock(&kvm->mmu_lock);

	r = kvm_arch_set_memory_region(kvm, mem, old, user_alloc);
	if (r) {
		spin_lock(&kvm->mmu_lock);
		*memslot = old;
		spin_unlock(&kvm->mmu_lock);
		goto out_free;
	}

	kvm_free_physmem_slot(&old, npages ? &new : NULL);
	/* Slot deletion case: we have to update the current slot */
	spin_lock(&kvm->mmu_lock);
	if (!npages)
		*memslot = old;
	spin_unlock(&kvm->mmu_lock);
#ifdef CONFIG_DMAR
	/* map the pages in iommu page table */
	r = kvm_iommu_map_pages(kvm, base_gfn, npages);
	if (r)
		goto out;
#endif
	return 0;

out_free:
	kvm_free_physmem_slot(&new, &old);
out:
	return r;

}
EXPORT_SYMBOL_GPL(__kvm_set_memory_region);

int kvm_set_memory_region(struct kvm *kvm,
			  struct kvm_userspace_memory_region *mem,
			  int user_alloc)
{
	int r;

	down_write(&kvm->slots_lock);
	r = __kvm_set_memory_region(kvm, mem, user_alloc);
	up_write(&kvm->slots_lock);
	return r;
}
EXPORT_SYMBOL_GPL(kvm_set_memory_region);

int kvm_vm_ioctl_set_memory_region(struct kvm *kvm,
				   struct
				   kvm_userspace_memory_region *mem,
				   int user_alloc)
{
	if (mem->slot >= KVM_MEMORY_SLOTS)
		return -EINVAL;
	return kvm_set_memory_region(kvm, mem, user_alloc);
}

int kvm_get_dirty_log(struct kvm *kvm,
			struct kvm_dirty_log *log, int *is_dirty)
{
	struct kvm_memory_slot *memslot;
	int r, i;
	int n;
	unsigned long any = 0;

	r = -EINVAL;
	if (log->slot >= KVM_MEMORY_SLOTS)
		goto out;

	memslot = &kvm->memslots[log->slot];
	r = -ENOENT;
	if (!memslot->dirty_bitmap)
		goto out;

	n = ALIGN(memslot->npages, BITS_PER_LONG) / 8;

	for (i = 0; !any && i < n/sizeof(long); ++i)
		any = memslot->dirty_bitmap[i];

	r = -EFAULT;
	if (copy_to_user(log->dirty_bitmap, memslot->dirty_bitmap, n))
		goto out;

	if (any)
		*is_dirty = 1;

	r = 0;
out:
	return r;
}

void kvm_disable_largepages(void)
{
	largepages_enabled = false;
}
EXPORT_SYMBOL_GPL(kvm_disable_largepages);

int is_error_page(struct page *page)
{
	return page == bad_page;
}
EXPORT_SYMBOL_GPL(is_error_page);

int is_error_pfn(pfn_t pfn)
{
	return pfn == bad_pfn;
}
EXPORT_SYMBOL_GPL(is_error_pfn);

static inline unsigned long bad_hva(void)
{
	return PAGE_OFFSET;
}

int kvm_is_error_hva(unsigned long addr)
{
	return addr == bad_hva();
}
EXPORT_SYMBOL_GPL(kvm_is_error_hva);

struct kvm_memory_slot *gfn_to_memslot_unaliased(struct kvm *kvm, gfn_t gfn)
{
	int i;

	for (i = 0; i < kvm->nmemslots; ++i) {
		struct kvm_memory_slot *memslot = &kvm->memslots[i];

		if (gfn >= memslot->base_gfn
		    && gfn < memslot->base_gfn + memslot->npages)
			return memslot;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(gfn_to_memslot_unaliased);

struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn)
{
	gfn = unalias_gfn(kvm, gfn);
	return gfn_to_memslot_unaliased(kvm, gfn);
}

int kvm_is_visible_gfn(struct kvm *kvm, gfn_t gfn)
{
	int i;

	gfn = unalias_gfn(kvm, gfn);
	for (i = 0; i < KVM_MEMORY_SLOTS; ++i) {
		struct kvm_memory_slot *memslot = &kvm->memslots[i];

		if (gfn >= memslot->base_gfn
		    && gfn < memslot->base_gfn + memslot->npages)
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_is_visible_gfn);

unsigned long gfn_to_hva(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *slot;

	gfn = unalias_gfn(kvm, gfn);
	slot = gfn_to_memslot_unaliased(kvm, gfn);
	if (!slot)
		return bad_hva();
	return (slot->userspace_addr + (gfn - slot->base_gfn) * PAGE_SIZE);
}
EXPORT_SYMBOL_GPL(gfn_to_hva);

pfn_t gfn_to_pfn(struct kvm *kvm, gfn_t gfn)
{
	struct page *page[1];
	unsigned long addr;
	int npages;
	pfn_t pfn;

	might_sleep();

	addr = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(addr)) {
		get_page(bad_page);
		return page_to_pfn(bad_page);
	}

	npages = get_user_pages_fast(addr, 1, 1, page);

	if (unlikely(npages != 1)) {
		struct vm_area_struct *vma;

		down_read(&current->mm->mmap_sem);
		vma = find_vma(current->mm, addr);

		if (vma == NULL || addr < vma->vm_start ||
		    !(vma->vm_flags & VM_PFNMAP)) {
			up_read(&current->mm->mmap_sem);
			get_page(bad_page);
			return page_to_pfn(bad_page);
		}

		pfn = ((addr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
		up_read(&current->mm->mmap_sem);
		BUG_ON(!kvm_is_mmio_pfn(pfn));
	} else
		pfn = page_to_pfn(page[0]);

	return pfn;
}

EXPORT_SYMBOL_GPL(gfn_to_pfn);

struct page *gfn_to_page(struct kvm *kvm, gfn_t gfn)
{
	pfn_t pfn;

	pfn = gfn_to_pfn(kvm, gfn);
	if (!kvm_is_mmio_pfn(pfn))
		return pfn_to_page(pfn);

	WARN_ON(kvm_is_mmio_pfn(pfn));

	get_page(bad_page);
	return bad_page;
}

EXPORT_SYMBOL_GPL(gfn_to_page);

void kvm_release_page_clean(struct page *page)
{
	kvm_release_pfn_clean(page_to_pfn(page));
}
EXPORT_SYMBOL_GPL(kvm_release_page_clean);

void kvm_release_pfn_clean(pfn_t pfn)
{
	if (!kvm_is_mmio_pfn(pfn))
		put_page(pfn_to_page(pfn));
}
EXPORT_SYMBOL_GPL(kvm_release_pfn_clean);

void kvm_release_page_dirty(struct page *page)
{
	kvm_release_pfn_dirty(page_to_pfn(page));
}
EXPORT_SYMBOL_GPL(kvm_release_page_dirty);

void kvm_release_pfn_dirty(pfn_t pfn)
{
	kvm_set_pfn_dirty(pfn);
	kvm_release_pfn_clean(pfn);
}
EXPORT_SYMBOL_GPL(kvm_release_pfn_dirty);

void kvm_set_page_dirty(struct page *page)
{
	kvm_set_pfn_dirty(page_to_pfn(page));
}
EXPORT_SYMBOL_GPL(kvm_set_page_dirty);

void kvm_set_pfn_dirty(pfn_t pfn)
{
	if (!kvm_is_mmio_pfn(pfn)) {
		struct page *page = pfn_to_page(pfn);
		if (!PageReserved(page))
			SetPageDirty(page);
	}
}
EXPORT_SYMBOL_GPL(kvm_set_pfn_dirty);

void kvm_set_pfn_accessed(pfn_t pfn)
{
	if (!kvm_is_mmio_pfn(pfn))
		mark_page_accessed(pfn_to_page(pfn));
}
EXPORT_SYMBOL_GPL(kvm_set_pfn_accessed);

void kvm_get_pfn(pfn_t pfn)
{
	if (!kvm_is_mmio_pfn(pfn))
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

int kvm_read_guest_page(struct kvm *kvm, gfn_t gfn, void *data, int offset,
			int len)
{
	int r;
	unsigned long addr;

	addr = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(addr))
		return -EFAULT;
	r = copy_from_user(data, (void __user *)addr + offset, len);
	if (r)
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_read_guest_page);

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

int kvm_read_guest_atomic(struct kvm *kvm, gpa_t gpa, void *data,
			  unsigned long len)
{
	int r;
	unsigned long addr;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int offset = offset_in_page(gpa);

	addr = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(addr))
		return -EFAULT;
	pagefault_disable();
	r = __copy_from_user_inatomic(data, (void __user *)addr + offset, len);
	pagefault_enable();
	if (r)
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL(kvm_read_guest_atomic);

int kvm_write_guest_page(struct kvm *kvm, gfn_t gfn, const void *data,
			 int offset, int len)
{
	int r;
	unsigned long addr;

	addr = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(addr))
		return -EFAULT;
	r = copy_to_user((void __user *)addr + offset, data, len);
	if (r)
		return -EFAULT;
	mark_page_dirty(kvm, gfn);
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_write_guest_page);

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

int kvm_clear_guest_page(struct kvm *kvm, gfn_t gfn, int offset, int len)
{
	return kvm_write_guest_page(kvm, gfn, empty_zero_page, offset, len);
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

void mark_page_dirty(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *memslot;

	gfn = unalias_gfn(kvm, gfn);
	memslot = gfn_to_memslot_unaliased(kvm, gfn);
	if (memslot && memslot->dirty_bitmap) {
		unsigned long rel_gfn = gfn - memslot->base_gfn;

		/* avoid RMW */
		if (!test_bit(rel_gfn, memslot->dirty_bitmap))
			set_bit(rel_gfn, memslot->dirty_bitmap);
	}
}

/*
 * The vCPU has executed a HLT instruction with in-kernel mode enabled.
 */
void kvm_vcpu_block(struct kvm_vcpu *vcpu)
{
	DEFINE_WAIT(wait);

	for (;;) {
		prepare_to_wait(&vcpu->wq, &wait, TASK_INTERRUPTIBLE);

		if (kvm_arch_vcpu_runnable(vcpu)) {
			set_bit(KVM_REQ_UNHALT, &vcpu->requests);
			break;
		}
		if (kvm_cpu_has_pending_timer(vcpu))
			break;
		if (signal_pending(current))
			break;

		vcpu_put(vcpu);
		schedule();
		vcpu_load(vcpu);
	}

	finish_wait(&vcpu->wq, &wait);
}

void kvm_resched(struct kvm_vcpu *vcpu)
{
	if (!need_resched())
		return;
	cond_resched();
}
EXPORT_SYMBOL_GPL(kvm_resched);

static int kvm_vcpu_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct kvm_vcpu *vcpu = vma->vm_file->private_data;
	struct page *page;

	if (vmf->pgoff == 0)
		page = virt_to_page(vcpu->run);
#ifdef CONFIG_X86
	else if (vmf->pgoff == KVM_PIO_PAGE_OFFSET)
		page = virt_to_page(vcpu->arch.pio_data);
#endif
#ifdef KVM_COALESCED_MMIO_PAGE_OFFSET
	else if (vmf->pgoff == KVM_COALESCED_MMIO_PAGE_OFFSET)
		page = virt_to_page(vcpu->kvm->coalesced_mmio_ring);
#endif
	else
		return VM_FAULT_SIGBUS;
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
	.compat_ioctl   = kvm_vcpu_ioctl,
	.mmap           = kvm_vcpu_mmap,
};

/*
 * Allocates an inode for the vcpu.
 */
static int create_vcpu_fd(struct kvm_vcpu *vcpu)
{
	return anon_inode_getfd("kvm-vcpu", &kvm_vcpu_fops, vcpu, 0);
}

/*
 * Creates some virtual cpus.  Good luck creating more than one.
 */
static int kvm_vm_ioctl_create_vcpu(struct kvm *kvm, u32 id)
{
	int r;
	struct kvm_vcpu *vcpu, *v;

	vcpu = kvm_arch_vcpu_create(kvm, id);
	if (IS_ERR(vcpu))
		return PTR_ERR(vcpu);

	preempt_notifier_init(&vcpu->preempt_notifier, &kvm_preempt_ops);

	r = kvm_arch_vcpu_setup(vcpu);
	if (r)
		return r;

	mutex_lock(&kvm->lock);
	if (atomic_read(&kvm->online_vcpus) == KVM_MAX_VCPUS) {
		r = -EINVAL;
		goto vcpu_destroy;
	}

	kvm_for_each_vcpu(r, v, kvm)
		if (v->vcpu_id == id) {
			r = -EEXIST;
			goto vcpu_destroy;
		}

	BUG_ON(kvm->vcpus[atomic_read(&kvm->online_vcpus)]);

	/* Now it's all set up, let userspace reach it */
	kvm_get_kvm(kvm);
	r = create_vcpu_fd(vcpu);
	if (r < 0) {
		kvm_put_kvm(kvm);
		goto vcpu_destroy;
	}

	kvm->vcpus[atomic_read(&kvm->online_vcpus)] = vcpu;
	smp_wmb();
	atomic_inc(&kvm->online_vcpus);

#ifdef CONFIG_KVM_APIC_ARCHITECTURE
	if (kvm->bsp_vcpu_id == id)
		kvm->bsp_vcpu = vcpu;
#endif
	mutex_unlock(&kvm->lock);
	return r;

vcpu_destroy:
	mutex_unlock(&kvm->lock);
	kvm_arch_vcpu_destroy(vcpu);
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

#ifdef __KVM_HAVE_MSIX
static int kvm_vm_ioctl_set_msix_nr(struct kvm *kvm,
				    struct kvm_assigned_msix_nr *entry_nr)
{
	int r = 0;
	struct kvm_assigned_dev_kernel *adev;

	mutex_lock(&kvm->lock);

	adev = kvm_find_assigned_dev(&kvm->arch.assigned_dev_head,
				      entry_nr->assigned_dev_id);
	if (!adev) {
		r = -EINVAL;
		goto msix_nr_out;
	}

	if (adev->entries_nr == 0) {
		adev->entries_nr = entry_nr->entry_nr;
		if (adev->entries_nr == 0 ||
		    adev->entries_nr >= KVM_MAX_MSIX_PER_DEV) {
			r = -EINVAL;
			goto msix_nr_out;
		}

		adev->host_msix_entries = kzalloc(sizeof(struct msix_entry) *
						entry_nr->entry_nr,
						GFP_KERNEL);
		if (!adev->host_msix_entries) {
			r = -ENOMEM;
			goto msix_nr_out;
		}
		adev->guest_msix_entries = kzalloc(
				sizeof(struct kvm_guest_msix_entry) *
				entry_nr->entry_nr, GFP_KERNEL);
		if (!adev->guest_msix_entries) {
			kfree(adev->host_msix_entries);
			r = -ENOMEM;
			goto msix_nr_out;
		}
	} else /* Not allowed set MSI-X number twice */
		r = -EINVAL;
msix_nr_out:
	mutex_unlock(&kvm->lock);
	return r;
}

static int kvm_vm_ioctl_set_msix_entry(struct kvm *kvm,
				       struct kvm_assigned_msix_entry *entry)
{
	int r = 0, i;
	struct kvm_assigned_dev_kernel *adev;

	mutex_lock(&kvm->lock);

	adev = kvm_find_assigned_dev(&kvm->arch.assigned_dev_head,
				      entry->assigned_dev_id);

	if (!adev) {
		r = -EINVAL;
		goto msix_entry_out;
	}

	for (i = 0; i < adev->entries_nr; i++)
		if (adev->guest_msix_entries[i].vector == 0 ||
		    adev->guest_msix_entries[i].entry == entry->entry) {
			adev->guest_msix_entries[i].entry = entry->entry;
			adev->guest_msix_entries[i].vector = entry->gsi;
			adev->host_msix_entries[i].entry = entry->entry;
			break;
		}
	if (i == adev->entries_nr) {
		r = -ENOSPC;
		goto msix_entry_out;
	}

msix_entry_out:
	mutex_unlock(&kvm->lock);

	return r;
}
#endif

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
	switch (ioctl) {
	case KVM_RUN:
		r = -EINVAL;
		if (arg)
			goto out;
		r = kvm_arch_vcpu_ioctl_run(vcpu, vcpu->run);
		break;
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

		r = -ENOMEM;
		kvm_regs = kzalloc(sizeof(struct kvm_regs), GFP_KERNEL);
		if (!kvm_regs)
			goto out;
		r = -EFAULT;
		if (copy_from_user(kvm_regs, argp, sizeof(struct kvm_regs)))
			goto out_free2;
		r = kvm_arch_vcpu_ioctl_set_regs(vcpu, kvm_regs);
		if (r)
			goto out_free2;
		r = 0;
out_free2:
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
		kvm_sregs = kmalloc(sizeof(struct kvm_sregs), GFP_KERNEL);
		r = -ENOMEM;
		if (!kvm_sregs)
			goto out;
		r = -EFAULT;
		if (copy_from_user(kvm_sregs, argp, sizeof(struct kvm_sregs)))
			goto out;
		r = kvm_arch_vcpu_ioctl_set_sregs(vcpu, kvm_sregs);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_MP_STATE: {
		struct kvm_mp_state mp_state;

		r = kvm_arch_vcpu_ioctl_get_mpstate(vcpu, &mp_state);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &mp_state, sizeof mp_state))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_MP_STATE: {
		struct kvm_mp_state mp_state;

		r = -EFAULT;
		if (copy_from_user(&mp_state, argp, sizeof mp_state))
			goto out;
		r = kvm_arch_vcpu_ioctl_set_mpstate(vcpu, &mp_state);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_TRANSLATE: {
		struct kvm_translation tr;

		r = -EFAULT;
		if (copy_from_user(&tr, argp, sizeof tr))
			goto out;
		r = kvm_arch_vcpu_ioctl_translate(vcpu, &tr);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &tr, sizeof tr))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_GUEST_DEBUG: {
		struct kvm_guest_debug dbg;

		r = -EFAULT;
		if (copy_from_user(&dbg, argp, sizeof dbg))
			goto out;
		r = kvm_arch_vcpu_ioctl_set_guest_debug(vcpu, &dbg);
		if (r)
			goto out;
		r = 0;
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
					   sizeof kvm_sigmask))
				goto out;
			r = -EINVAL;
			if (kvm_sigmask.len != sizeof sigset)
				goto out;
			r = -EFAULT;
			if (copy_from_user(&sigset, sigmask_arg->sigset,
					   sizeof sigset))
				goto out;
			p = &sigset;
		}
		r = kvm_vcpu_ioctl_set_sigmask(vcpu, &sigset);
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
		fpu = kmalloc(sizeof(struct kvm_fpu), GFP_KERNEL);
		r = -ENOMEM;
		if (!fpu)
			goto out;
		r = -EFAULT;
		if (copy_from_user(fpu, argp, sizeof(struct kvm_fpu)))
			goto out;
		r = kvm_arch_vcpu_ioctl_set_fpu(vcpu, fpu);
		if (r)
			goto out;
		r = 0;
		break;
	}
	default:
		r = kvm_arch_vcpu_ioctl(filp, ioctl, arg);
	}
out:
	kfree(fpu);
	kfree(kvm_sregs);
	return r;
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
		if (r < 0)
			goto out;
		break;
	case KVM_SET_USER_MEMORY_REGION: {
		struct kvm_userspace_memory_region kvm_userspace_mem;

		r = -EFAULT;
		if (copy_from_user(&kvm_userspace_mem, argp,
						sizeof kvm_userspace_mem))
			goto out;

		r = kvm_vm_ioctl_set_memory_region(kvm, &kvm_userspace_mem, 1);
		if (r)
			goto out;
		break;
	}
	case KVM_GET_DIRTY_LOG: {
		struct kvm_dirty_log log;

		r = -EFAULT;
		if (copy_from_user(&log, argp, sizeof log))
			goto out;
		r = kvm_vm_ioctl_get_dirty_log(kvm, &log);
		if (r)
			goto out;
		break;
	}
#ifdef KVM_COALESCED_MMIO_PAGE_OFFSET
	case KVM_REGISTER_COALESCED_MMIO: {
		struct kvm_coalesced_mmio_zone zone;
		r = -EFAULT;
		if (copy_from_user(&zone, argp, sizeof zone))
			goto out;
		r = -ENXIO;
		r = kvm_vm_ioctl_register_coalesced_mmio(kvm, &zone);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_UNREGISTER_COALESCED_MMIO: {
		struct kvm_coalesced_mmio_zone zone;
		r = -EFAULT;
		if (copy_from_user(&zone, argp, sizeof zone))
			goto out;
		r = -ENXIO;
		r = kvm_vm_ioctl_unregister_coalesced_mmio(kvm, &zone);
		if (r)
			goto out;
		r = 0;
		break;
	}
#endif
#ifdef KVM_CAP_DEVICE_ASSIGNMENT
	case KVM_ASSIGN_PCI_DEVICE: {
		struct kvm_assigned_pci_dev assigned_dev;

		r = -EFAULT;
		if (copy_from_user(&assigned_dev, argp, sizeof assigned_dev))
			goto out;
		r = kvm_vm_ioctl_assign_device(kvm, &assigned_dev);
		if (r)
			goto out;
		break;
	}
	case KVM_ASSIGN_IRQ: {
		r = -EOPNOTSUPP;
		break;
	}
#ifdef KVM_CAP_ASSIGN_DEV_IRQ
	case KVM_ASSIGN_DEV_IRQ: {
		struct kvm_assigned_irq assigned_irq;

		r = -EFAULT;
		if (copy_from_user(&assigned_irq, argp, sizeof assigned_irq))
			goto out;
		r = kvm_vm_ioctl_assign_irq(kvm, &assigned_irq);
		if (r)
			goto out;
		break;
	}
	case KVM_DEASSIGN_DEV_IRQ: {
		struct kvm_assigned_irq assigned_irq;

		r = -EFAULT;
		if (copy_from_user(&assigned_irq, argp, sizeof assigned_irq))
			goto out;
		r = kvm_vm_ioctl_deassign_dev_irq(kvm, &assigned_irq);
		if (r)
			goto out;
		break;
	}
#endif
#endif
#ifdef KVM_CAP_DEVICE_DEASSIGNMENT
	case KVM_DEASSIGN_PCI_DEVICE: {
		struct kvm_assigned_pci_dev assigned_dev;

		r = -EFAULT;
		if (copy_from_user(&assigned_dev, argp, sizeof assigned_dev))
			goto out;
		r = kvm_vm_ioctl_deassign_device(kvm, &assigned_dev);
		if (r)
			goto out;
		break;
	}
#endif
#ifdef KVM_CAP_IRQ_ROUTING
	case KVM_SET_GSI_ROUTING: {
		struct kvm_irq_routing routing;
		struct kvm_irq_routing __user *urouting;
		struct kvm_irq_routing_entry *entries;

		r = -EFAULT;
		if (copy_from_user(&routing, argp, sizeof(routing)))
			goto out;
		r = -EINVAL;
		if (routing.nr >= KVM_MAX_IRQ_ROUTES)
			goto out;
		if (routing.flags)
			goto out;
		r = -ENOMEM;
		entries = vmalloc(routing.nr * sizeof(*entries));
		if (!entries)
			goto out;
		r = -EFAULT;
		urouting = argp;
		if (copy_from_user(entries, urouting->entries,
				   routing.nr * sizeof(*entries)))
			goto out_free_irq_routing;
		r = kvm_set_irq_routing(kvm, entries, routing.nr,
					routing.flags);
	out_free_irq_routing:
		vfree(entries);
		break;
	}
#endif /* KVM_CAP_IRQ_ROUTING */
#ifdef __KVM_HAVE_MSIX
	case KVM_ASSIGN_SET_MSIX_NR: {
		struct kvm_assigned_msix_nr entry_nr;
		r = -EFAULT;
		if (copy_from_user(&entry_nr, argp, sizeof entry_nr))
			goto out;
		r = kvm_vm_ioctl_set_msix_nr(kvm, &entry_nr);
		if (r)
			goto out;
		break;
	}
	case KVM_ASSIGN_SET_MSIX_ENTRY: {
		struct kvm_assigned_msix_entry entry;
		r = -EFAULT;
		if (copy_from_user(&entry, argp, sizeof entry))
			goto out;
		r = kvm_vm_ioctl_set_msix_entry(kvm, &entry);
		if (r)
			goto out;
		break;
	}
#endif
	case KVM_IRQFD: {
		struct kvm_irqfd data;

		r = -EFAULT;
		if (copy_from_user(&data, argp, sizeof data))
			goto out;
		r = kvm_irqfd(kvm, data.fd, data.gsi, data.flags);
		break;
	}
	case KVM_IOEVENTFD: {
		struct kvm_ioeventfd data;

		r = -EFAULT;
		if (copy_from_user(&data, argp, sizeof data))
			goto out;
		r = kvm_ioeventfd(kvm, &data);
		break;
	}
#ifdef CONFIG_KVM_APIC_ARCHITECTURE
	case KVM_SET_BOOT_CPU_ID:
		r = 0;
		mutex_lock(&kvm->lock);
		if (atomic_read(&kvm->online_vcpus) != 0)
			r = -EBUSY;
		else
			kvm->bsp_vcpu_id = arg;
		mutex_unlock(&kvm->lock);
		break;
#endif
	default:
		r = kvm_arch_vm_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}

static int kvm_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page[1];
	unsigned long addr;
	int npages;
	gfn_t gfn = vmf->pgoff;
	struct kvm *kvm = vma->vm_file->private_data;

	addr = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(addr))
		return VM_FAULT_SIGBUS;

	npages = get_user_pages(current, current->mm, addr, 1, 1, 0, page,
				NULL);
	if (unlikely(npages != 1))
		return VM_FAULT_SIGBUS;

	vmf->page = page[0];
	return 0;
}

static const struct vm_operations_struct kvm_vm_vm_ops = {
	.fault = kvm_vm_fault,
};

static int kvm_vm_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_ops = &kvm_vm_vm_ops;
	return 0;
}

static struct file_operations kvm_vm_fops = {
	.release        = kvm_vm_release,
	.unlocked_ioctl = kvm_vm_ioctl,
	.compat_ioctl   = kvm_vm_ioctl,
	.mmap           = kvm_vm_mmap,
};

static int kvm_dev_ioctl_create_vm(void)
{
	int fd;
	struct kvm *kvm;

	kvm = kvm_create_vm();
	if (IS_ERR(kvm))
		return PTR_ERR(kvm);
	fd = anon_inode_getfd("kvm-vm", &kvm_vm_fops, kvm, 0);
	if (fd < 0)
		kvm_put_kvm(kvm);

	return fd;
}

static long kvm_dev_ioctl_check_extension_generic(long arg)
{
	switch (arg) {
	case KVM_CAP_USER_MEMORY:
	case KVM_CAP_DESTROY_MEMORY_REGION_WORKS:
	case KVM_CAP_JOIN_MEMORY_REGIONS_WORKS:
#ifdef CONFIG_KVM_APIC_ARCHITECTURE
	case KVM_CAP_SET_BOOT_CPU_ID:
#endif
		return 1;
#ifdef CONFIG_HAVE_KVM_IRQCHIP
	case KVM_CAP_IRQ_ROUTING:
		return KVM_MAX_IRQ_ROUTES;
#endif
	default:
		break;
	}
	return kvm_dev_ioctl_check_extension(arg);
}

static long kvm_dev_ioctl(struct file *filp,
			  unsigned int ioctl, unsigned long arg)
{
	long r = -EINVAL;

	switch (ioctl) {
	case KVM_GET_API_VERSION:
		r = -EINVAL;
		if (arg)
			goto out;
		r = KVM_API_VERSION;
		break;
	case KVM_CREATE_VM:
		r = -EINVAL;
		if (arg)
			goto out;
		r = kvm_dev_ioctl_create_vm();
		break;
	case KVM_CHECK_EXTENSION:
		r = kvm_dev_ioctl_check_extension_generic(arg);
		break;
	case KVM_GET_VCPU_MMAP_SIZE:
		r = -EINVAL;
		if (arg)
			goto out;
		r = PAGE_SIZE;     /* struct kvm_run */
#ifdef CONFIG_X86
		r += PAGE_SIZE;    /* pio data page */
#endif
#ifdef KVM_COALESCED_MMIO_PAGE_OFFSET
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
	.compat_ioctl   = kvm_dev_ioctl,
};

static struct miscdevice kvm_dev = {
	KVM_MINOR,
	"kvm",
	&kvm_chardev_ops,
};

static void hardware_enable(void *junk)
{
	int cpu = raw_smp_processor_id();

	if (cpumask_test_cpu(cpu, cpus_hardware_enabled))
		return;
	cpumask_set_cpu(cpu, cpus_hardware_enabled);
	kvm_arch_hardware_enable(NULL);
}

static void hardware_disable(void *junk)
{
	int cpu = raw_smp_processor_id();

	if (!cpumask_test_cpu(cpu, cpus_hardware_enabled))
		return;
	cpumask_clear_cpu(cpu, cpus_hardware_enabled);
	kvm_arch_hardware_disable(NULL);
}

static int kvm_cpu_hotplug(struct notifier_block *notifier, unsigned long val,
			   void *v)
{
	int cpu = (long)v;

	val &= ~CPU_TASKS_FROZEN;
	switch (val) {
	case CPU_DYING:
		printk(KERN_INFO "kvm: disabling virtualization on CPU%d\n",
		       cpu);
		hardware_disable(NULL);
		break;
	case CPU_UP_CANCELED:
		printk(KERN_INFO "kvm: disabling virtualization on CPU%d\n",
		       cpu);
		smp_call_function_single(cpu, hardware_disable, NULL, 1);
		break;
	case CPU_ONLINE:
		printk(KERN_INFO "kvm: enabling virtualization on CPU%d\n",
		       cpu);
		smp_call_function_single(cpu, hardware_enable, NULL, 1);
		break;
	}
	return NOTIFY_OK;
}


asmlinkage void kvm_handle_fault_on_reboot(void)
{
	if (kvm_rebooting)
		/* spin while reset goes on */
		while (true)
			;
	/* Fault while not rebooting.  We want the trace. */
	BUG();
}
EXPORT_SYMBOL_GPL(kvm_handle_fault_on_reboot);

static int kvm_reboot(struct notifier_block *notifier, unsigned long val,
		      void *v)
{
	/*
	 * Some (well, at least mine) BIOSes hang on reboot if
	 * in vmx root mode.
	 *
	 * And Intel TXT required VMX off for all cpu when system shutdown.
	 */
	printk(KERN_INFO "kvm: exiting hardware virtualization\n");
	kvm_rebooting = true;
	on_each_cpu(hardware_disable, NULL, 1);
	return NOTIFY_OK;
}

static struct notifier_block kvm_reboot_notifier = {
	.notifier_call = kvm_reboot,
	.priority = 0,
};

void kvm_io_bus_init(struct kvm_io_bus *bus)
{
	memset(bus, 0, sizeof(*bus));
}

void kvm_io_bus_destroy(struct kvm_io_bus *bus)
{
	int i;

	for (i = 0; i < bus->dev_count; i++) {
		struct kvm_io_device *pos = bus->devs[i];

		kvm_iodevice_destructor(pos);
	}
}

/* kvm_io_bus_write - called under kvm->slots_lock */
int kvm_io_bus_write(struct kvm_io_bus *bus, gpa_t addr,
		     int len, const void *val)
{
	int i;
	for (i = 0; i < bus->dev_count; i++)
		if (!kvm_iodevice_write(bus->devs[i], addr, len, val))
			return 0;
	return -EOPNOTSUPP;
}

/* kvm_io_bus_read - called under kvm->slots_lock */
int kvm_io_bus_read(struct kvm_io_bus *bus, gpa_t addr, int len, void *val)
{
	int i;
	for (i = 0; i < bus->dev_count; i++)
		if (!kvm_iodevice_read(bus->devs[i], addr, len, val))
			return 0;
	return -EOPNOTSUPP;
}

int kvm_io_bus_register_dev(struct kvm *kvm, struct kvm_io_bus *bus,
			     struct kvm_io_device *dev)
{
	int ret;

	down_write(&kvm->slots_lock);
	ret = __kvm_io_bus_register_dev(bus, dev);
	up_write(&kvm->slots_lock);

	return ret;
}

/* An unlocked version. Caller must have write lock on slots_lock. */
int __kvm_io_bus_register_dev(struct kvm_io_bus *bus,
			      struct kvm_io_device *dev)
{
	if (bus->dev_count > NR_IOBUS_DEVS-1)
		return -ENOSPC;

	bus->devs[bus->dev_count++] = dev;

	return 0;
}

void kvm_io_bus_unregister_dev(struct kvm *kvm,
			       struct kvm_io_bus *bus,
			       struct kvm_io_device *dev)
{
	down_write(&kvm->slots_lock);
	__kvm_io_bus_unregister_dev(bus, dev);
	up_write(&kvm->slots_lock);
}

/* An unlocked version. Caller must have write lock on slots_lock. */
void __kvm_io_bus_unregister_dev(struct kvm_io_bus *bus,
				 struct kvm_io_device *dev)
{
	int i;

	for (i = 0; i < bus->dev_count; i++)
		if (bus->devs[i] == dev) {
			bus->devs[i] = bus->devs[--bus->dev_count];
			break;
		}
}

static struct notifier_block kvm_cpu_notifier = {
	.notifier_call = kvm_cpu_hotplug,
	.priority = 20, /* must be > scheduler priority */
};

static int vm_stat_get(void *_offset, u64 *val)
{
	unsigned offset = (long)_offset;
	struct kvm *kvm;

	*val = 0;
	spin_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list)
		*val += *(u32 *)((void *)kvm + offset);
	spin_unlock(&kvm_lock);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vm_stat_fops, vm_stat_get, NULL, "%llu\n");

static int vcpu_stat_get(void *_offset, u64 *val)
{
	unsigned offset = (long)_offset;
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	int i;

	*val = 0;
	spin_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list)
		kvm_for_each_vcpu(i, vcpu, kvm)
			*val += *(u32 *)((void *)vcpu + offset);

	spin_unlock(&kvm_lock);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vcpu_stat_fops, vcpu_stat_get, NULL, "%llu\n");

static const struct file_operations *stat_fops[] = {
	[KVM_STAT_VCPU] = &vcpu_stat_fops,
	[KVM_STAT_VM]   = &vm_stat_fops,
};

static void kvm_init_debug(void)
{
	struct kvm_stats_debugfs_item *p;

	kvm_debugfs_dir = debugfs_create_dir("kvm", NULL);
	for (p = debugfs_entries; p->name; ++p)
		p->dentry = debugfs_create_file(p->name, 0444, kvm_debugfs_dir,
						(void *)(long)p->offset,
						stat_fops[p->kind]);
}

static void kvm_exit_debug(void)
{
	struct kvm_stats_debugfs_item *p;

	for (p = debugfs_entries; p->name; ++p)
		debugfs_remove(p->dentry);
	debugfs_remove(kvm_debugfs_dir);
}

static int kvm_suspend(struct sys_device *dev, pm_message_t state)
{
	hardware_disable(NULL);
	return 0;
}

static int kvm_resume(struct sys_device *dev)
{
	hardware_enable(NULL);
	return 0;
}

static struct sysdev_class kvm_sysdev_class = {
	.name = "kvm",
	.suspend = kvm_suspend,
	.resume = kvm_resume,
};

static struct sys_device kvm_sysdev = {
	.id = 0,
	.cls = &kvm_sysdev_class,
};

struct page *bad_page;
pfn_t bad_pfn;

static inline
struct kvm_vcpu *preempt_notifier_to_vcpu(struct preempt_notifier *pn)
{
	return container_of(pn, struct kvm_vcpu, preempt_notifier);
}

static void kvm_sched_in(struct preempt_notifier *pn, int cpu)
{
	struct kvm_vcpu *vcpu = preempt_notifier_to_vcpu(pn);

	kvm_arch_vcpu_load(vcpu, cpu);
}

static void kvm_sched_out(struct preempt_notifier *pn,
			  struct task_struct *next)
{
	struct kvm_vcpu *vcpu = preempt_notifier_to_vcpu(pn);

	kvm_arch_vcpu_put(vcpu);
}

int kvm_init(void *opaque, unsigned int vcpu_size,
		  struct module *module)
{
	int r;
	int cpu;

	kvm_init_debug();

	r = kvm_arch_init(opaque);
	if (r)
		goto out_fail;

	bad_page = alloc_page(GFP_KERNEL | __GFP_ZERO);

	if (bad_page == NULL) {
		r = -ENOMEM;
		goto out;
	}

	bad_pfn = page_to_pfn(bad_page);

	if (!zalloc_cpumask_var(&cpus_hardware_enabled, GFP_KERNEL)) {
		r = -ENOMEM;
		goto out_free_0;
	}

	r = kvm_arch_hardware_setup();
	if (r < 0)
		goto out_free_0a;

	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu,
				kvm_arch_check_processor_compat,
				&r, 1);
		if (r < 0)
			goto out_free_1;
	}

	on_each_cpu(hardware_enable, NULL, 1);
	r = register_cpu_notifier(&kvm_cpu_notifier);
	if (r)
		goto out_free_2;
	register_reboot_notifier(&kvm_reboot_notifier);

	r = sysdev_class_register(&kvm_sysdev_class);
	if (r)
		goto out_free_3;

	r = sysdev_register(&kvm_sysdev);
	if (r)
		goto out_free_4;

	/* A kmem cache lets us meet the alignment requirements of fx_save. */
	kvm_vcpu_cache = kmem_cache_create("kvm_vcpu", vcpu_size,
					   __alignof__(struct kvm_vcpu),
					   0, NULL);
	if (!kvm_vcpu_cache) {
		r = -ENOMEM;
		goto out_free_5;
	}

	kvm_chardev_ops.owner = module;
	kvm_vm_fops.owner = module;
	kvm_vcpu_fops.owner = module;

	r = misc_register(&kvm_dev);
	if (r) {
		printk(KERN_ERR "kvm: misc device register failed\n");
		goto out_free;
	}

	kvm_preempt_ops.sched_in = kvm_sched_in;
	kvm_preempt_ops.sched_out = kvm_sched_out;

	return 0;

out_free:
	kmem_cache_destroy(kvm_vcpu_cache);
out_free_5:
	sysdev_unregister(&kvm_sysdev);
out_free_4:
	sysdev_class_unregister(&kvm_sysdev_class);
out_free_3:
	unregister_reboot_notifier(&kvm_reboot_notifier);
	unregister_cpu_notifier(&kvm_cpu_notifier);
out_free_2:
	on_each_cpu(hardware_disable, NULL, 1);
out_free_1:
	kvm_arch_hardware_unsetup();
out_free_0a:
	free_cpumask_var(cpus_hardware_enabled);
out_free_0:
	__free_page(bad_page);
out:
	kvm_arch_exit();
out_fail:
	kvm_exit_debug();
	return r;
}
EXPORT_SYMBOL_GPL(kvm_init);

void kvm_exit(void)
{
	tracepoint_synchronize_unregister();
	misc_deregister(&kvm_dev);
	kmem_cache_destroy(kvm_vcpu_cache);
	sysdev_unregister(&kvm_sysdev);
	sysdev_class_unregister(&kvm_sysdev_class);
	unregister_reboot_notifier(&kvm_reboot_notifier);
	unregister_cpu_notifier(&kvm_cpu_notifier);
	on_each_cpu(hardware_disable, NULL, 1);
	kvm_arch_hardware_unsetup();
	kvm_arch_exit();
	kvm_exit_debug();
	free_cpumask_var(cpus_hardware_enabled);
	__free_page(bad_page);
}
EXPORT_SYMBOL_GPL(kvm_exit);

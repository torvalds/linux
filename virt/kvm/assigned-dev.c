/*
 * Kernel-based Virtual Machine - device assignment support
 *
 * Copyright (C) 2010 Red Hat, Inc. and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include "irq.h"

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
	if (index < 0)
		printk(KERN_WARNING "Fail to find correlated MSI-X entry!\n");

	return index;
}

static irqreturn_t kvm_assigned_dev_intx(int irq, void *dev_id)
{
	struct kvm_assigned_dev_kernel *assigned_dev = dev_id;
	int ret;

	spin_lock(&assigned_dev->intx_lock);
	if (pci_check_and_mask_intx(assigned_dev->dev)) {
		assigned_dev->host_irq_disabled = true;
		ret = IRQ_WAKE_THREAD;
	} else
		ret = IRQ_NONE;
	spin_unlock(&assigned_dev->intx_lock);

	return ret;
}

static void
kvm_assigned_dev_raise_guest_irq(struct kvm_assigned_dev_kernel *assigned_dev,
				 int vector)
{
	if (unlikely(assigned_dev->irq_requested_type &
		     KVM_DEV_IRQ_GUEST_INTX)) {
		spin_lock(&assigned_dev->intx_mask_lock);
		if (!(assigned_dev->flags & KVM_DEV_ASSIGN_MASK_INTX))
			kvm_set_irq(assigned_dev->kvm,
				    assigned_dev->irq_source_id, vector, 1);
		spin_unlock(&assigned_dev->intx_mask_lock);
	} else
		kvm_set_irq(assigned_dev->kvm, assigned_dev->irq_source_id,
			    vector, 1);
}

static irqreturn_t kvm_assigned_dev_thread_intx(int irq, void *dev_id)
{
	struct kvm_assigned_dev_kernel *assigned_dev = dev_id;

	if (!(assigned_dev->flags & KVM_DEV_ASSIGN_PCI_2_3)) {
		spin_lock_irq(&assigned_dev->intx_lock);
		disable_irq_nosync(irq);
		assigned_dev->host_irq_disabled = true;
		spin_unlock_irq(&assigned_dev->intx_lock);
	}

	kvm_assigned_dev_raise_guest_irq(assigned_dev,
					 assigned_dev->guest_irq);

	return IRQ_HANDLED;
}

#ifdef __KVM_HAVE_MSI
static irqreturn_t kvm_assigned_dev_thread_msi(int irq, void *dev_id)
{
	struct kvm_assigned_dev_kernel *assigned_dev = dev_id;

	kvm_assigned_dev_raise_guest_irq(assigned_dev,
					 assigned_dev->guest_irq);

	return IRQ_HANDLED;
}
#endif

#ifdef __KVM_HAVE_MSIX
static irqreturn_t kvm_assigned_dev_thread_msix(int irq, void *dev_id)
{
	struct kvm_assigned_dev_kernel *assigned_dev = dev_id;
	int index = find_index_from_host_irq(assigned_dev, irq);
	u32 vector;

	if (index >= 0) {
		vector = assigned_dev->guest_msix_entries[index].vector;
		kvm_assigned_dev_raise_guest_irq(assigned_dev, vector);
	}

	return IRQ_HANDLED;
}
#endif

/* Ack the irq line for an assigned device */
static void kvm_assigned_dev_ack_irq(struct kvm_irq_ack_notifier *kian)
{
	struct kvm_assigned_dev_kernel *dev =
		container_of(kian, struct kvm_assigned_dev_kernel,
			     ack_notifier);

	kvm_set_irq(dev->kvm, dev->irq_source_id, dev->guest_irq, 0);

	spin_lock(&dev->intx_mask_lock);

	if (!(dev->flags & KVM_DEV_ASSIGN_MASK_INTX)) {
		bool reassert = false;

		spin_lock_irq(&dev->intx_lock);
		/*
		 * The guest IRQ may be shared so this ack can come from an
		 * IRQ for another guest device.
		 */
		if (dev->host_irq_disabled) {
			if (!(dev->flags & KVM_DEV_ASSIGN_PCI_2_3))
				enable_irq(dev->host_irq);
			else if (!pci_check_and_unmask_intx(dev->dev))
				reassert = true;
			dev->host_irq_disabled = reassert;
		}
		spin_unlock_irq(&dev->intx_lock);

		if (reassert)
			kvm_set_irq(dev->kvm, dev->irq_source_id,
				    dev->guest_irq, 1);
	}

	spin_unlock(&dev->intx_mask_lock);
}

static void deassign_guest_irq(struct kvm *kvm,
			       struct kvm_assigned_dev_kernel *assigned_dev)
{
	if (assigned_dev->ack_notifier.gsi != -1)
		kvm_unregister_irq_ack_notifier(kvm,
						&assigned_dev->ack_notifier);

	kvm_set_irq(assigned_dev->kvm, assigned_dev->irq_source_id,
		    assigned_dev->guest_irq, 0);

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
	 * We disable irq here to prevent further events.
	 *
	 * Notice this maybe result in nested disable if the interrupt type is
	 * INTx, but it's OK for we are going to free it.
	 *
	 * If this function is a part of VM destroy, please ensure that till
	 * now, the kvm state is still legal for probably we also have to wait
	 * on a currently running IRQ handler.
	 */
	if (assigned_dev->irq_requested_type & KVM_DEV_IRQ_HOST_MSIX) {
		int i;
		for (i = 0; i < assigned_dev->entries_nr; i++)
			disable_irq(assigned_dev->host_msix_entries[i].vector);

		for (i = 0; i < assigned_dev->entries_nr; i++)
			free_irq(assigned_dev->host_msix_entries[i].vector,
				 assigned_dev);

		assigned_dev->entries_nr = 0;
		kfree(assigned_dev->host_msix_entries);
		kfree(assigned_dev->guest_msix_entries);
		pci_disable_msix(assigned_dev->dev);
	} else {
		/* Deal with MSI and INTx */
		if ((assigned_dev->irq_requested_type &
		     KVM_DEV_IRQ_HOST_INTX) &&
		    (assigned_dev->flags & KVM_DEV_ASSIGN_PCI_2_3)) {
			spin_lock_irq(&assigned_dev->intx_lock);
			pci_intx(assigned_dev->dev, false);
			spin_unlock_irq(&assigned_dev->intx_lock);
			synchronize_irq(assigned_dev->host_irq);
		} else
			disable_irq(assigned_dev->host_irq);

		free_irq(assigned_dev->host_irq, assigned_dev);

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
	if (pci_load_and_free_saved_state(assigned_dev->dev,
					  &assigned_dev->pci_saved_state))
		printk(KERN_INFO "%s: Couldn't reload %s saved state\n",
		       __func__, dev_name(&assigned_dev->dev->dev));
	else
		pci_restore_state(assigned_dev->dev);

	assigned_dev->dev->dev_flags &= ~PCI_DEV_FLAGS_ASSIGNED;

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
	irq_handler_t irq_handler;
	unsigned long flags;

	dev->host_irq = dev->dev->irq;

	/*
	 * We can only share the IRQ line with other host devices if we are
	 * able to disable the IRQ source at device-level - independently of
	 * the guest driver. Otherwise host devices may suffer from unbounded
	 * IRQ latencies when the guest keeps the line asserted.
	 */
	if (dev->flags & KVM_DEV_ASSIGN_PCI_2_3) {
		irq_handler = kvm_assigned_dev_intx;
		flags = IRQF_SHARED;
	} else {
		irq_handler = NULL;
		flags = IRQF_ONESHOT;
	}
	if (request_threaded_irq(dev->host_irq, irq_handler,
				 kvm_assigned_dev_thread_intx, flags,
				 dev->irq_name, dev))
		return -EIO;

	if (dev->flags & KVM_DEV_ASSIGN_PCI_2_3) {
		spin_lock_irq(&dev->intx_lock);
		pci_intx(dev->dev, true);
		spin_unlock_irq(&dev->intx_lock);
	}
	return 0;
}

#ifdef __KVM_HAVE_MSI
static irqreturn_t kvm_assigned_dev_msi(int irq, void *dev_id)
{
	return IRQ_WAKE_THREAD;
}

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
	if (request_threaded_irq(dev->host_irq, kvm_assigned_dev_msi,
				 kvm_assigned_dev_thread_msi, 0,
				 dev->irq_name, dev)) {
		pci_disable_msi(dev->dev);
		return -EIO;
	}

	return 0;
}
#endif

#ifdef __KVM_HAVE_MSIX
static irqreturn_t kvm_assigned_dev_msix(int irq, void *dev_id)
{
	return IRQ_WAKE_THREAD;
}

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
		r = request_threaded_irq(dev->host_msix_entries[i].vector,
					 kvm_assigned_dev_msix,
					 kvm_assigned_dev_thread_msix,
					 0, dev->irq_name, dev);
		if (r)
			goto err;
	}

	return 0;
err:
	for (i -= 1; i >= 0; i--)
		free_irq(dev->host_msix_entries[i].vector, dev);
	pci_disable_msix(dev->dev);
	return r;
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

	snprintf(dev->irq_name, sizeof(dev->irq_name), "kvm:%s",
		 pci_name(dev->dev));

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
	dev->host_irq_disabled = false;

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
		if (dev->ack_notifier.gsi != -1)
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
	unsigned long irq_type;

	mutex_lock(&kvm->lock);

	match = kvm_find_assigned_dev(&kvm->arch.assigned_dev_head,
				      assigned_irq->assigned_dev_id);
	if (!match)
		goto out;

	irq_type = assigned_irq->flags & (KVM_DEV_IRQ_HOST_MASK |
					  KVM_DEV_IRQ_GUEST_MASK);
	r = kvm_deassign_irq(kvm, match, irq_type);
out:
	mutex_unlock(&kvm->lock);
	return r;
}

/*
 * We want to test whether the caller has been granted permissions to
 * use this device.  To be able to configure and control the device,
 * the user needs access to PCI configuration space and BAR resources.
 * These are accessed through PCI sysfs.  PCI config space is often
 * passed to the process calling this ioctl via file descriptor, so we
 * can't rely on access to that file.  We can check for permissions
 * on each of the BAR resource files, which is a pretty clear
 * indicator that the user has been granted access to the device.
 */
static int probe_sysfs_permissions(struct pci_dev *dev)
{
#ifdef CONFIG_SYSFS
	int i;
	bool bar_found = false;

	for (i = PCI_STD_RESOURCES; i <= PCI_STD_RESOURCE_END; i++) {
		char *kpath, *syspath;
		struct path path;
		struct inode *inode;
		int r;

		if (!pci_resource_len(dev, i))
			continue;

		kpath = kobject_get_path(&dev->dev.kobj, GFP_KERNEL);
		if (!kpath)
			return -ENOMEM;

		/* Per sysfs-rules, sysfs is always at /sys */
		syspath = kasprintf(GFP_KERNEL, "/sys%s/resource%d", kpath, i);
		kfree(kpath);
		if (!syspath)
			return -ENOMEM;

		r = kern_path(syspath, LOOKUP_FOLLOW, &path);
		kfree(syspath);
		if (r)
			return r;

		inode = path.dentry->d_inode;

		r = inode_permission(inode, MAY_READ | MAY_WRITE | MAY_ACCESS);
		path_put(&path);
		if (r)
			return r;

		bar_found = true;
	}

	/* If no resources, probably something special */
	if (!bar_found)
		return -EPERM;

	return 0;
#else
	return -EINVAL; /* No way to control the device without sysfs */
#endif
}

static int kvm_vm_ioctl_assign_device(struct kvm *kvm,
				      struct kvm_assigned_pci_dev *assigned_dev)
{
	int r = 0, idx;
	struct kvm_assigned_dev_kernel *match;
	struct pci_dev *dev;

	if (!(assigned_dev->flags & KVM_DEV_ASSIGN_ENABLE_IOMMU))
		return -EINVAL;

	mutex_lock(&kvm->lock);
	idx = srcu_read_lock(&kvm->srcu);

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
	dev = pci_get_domain_bus_and_slot(assigned_dev->segnr,
				   assigned_dev->busnr,
				   assigned_dev->devfn);
	if (!dev) {
		printk(KERN_INFO "%s: host device not found\n", __func__);
		r = -EINVAL;
		goto out_free;
	}

	/* Don't allow bridges to be assigned */
	if (dev->hdr_type != PCI_HEADER_TYPE_NORMAL) {
		r = -EPERM;
		goto out_put;
	}

	r = probe_sysfs_permissions(dev);
	if (r)
		goto out_put;

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
	pci_save_state(dev);
	match->pci_saved_state = pci_store_saved_state(dev);
	if (!match->pci_saved_state)
		printk(KERN_DEBUG "%s: Couldn't store %s saved state\n",
		       __func__, dev_name(&dev->dev));

	if (!pci_intx_mask_supported(dev))
		assigned_dev->flags &= ~KVM_DEV_ASSIGN_PCI_2_3;

	match->assigned_dev_id = assigned_dev->assigned_dev_id;
	match->host_segnr = assigned_dev->segnr;
	match->host_busnr = assigned_dev->busnr;
	match->host_devfn = assigned_dev->devfn;
	match->flags = assigned_dev->flags;
	match->dev = dev;
	spin_lock_init(&match->intx_lock);
	spin_lock_init(&match->intx_mask_lock);
	match->irq_source_id = -1;
	match->kvm = kvm;
	match->ack_notifier.irq_acked = kvm_assigned_dev_ack_irq;

	list_add(&match->list, &kvm->arch.assigned_dev_head);

	if (!kvm->arch.iommu_domain) {
		r = kvm_iommu_map_guest(kvm);
		if (r)
			goto out_list_del;
	}
	r = kvm_assign_device(kvm, match);
	if (r)
		goto out_list_del;

out:
	srcu_read_unlock(&kvm->srcu, idx);
	mutex_unlock(&kvm->lock);
	return r;
out_list_del:
	if (pci_load_and_free_saved_state(dev, &match->pci_saved_state))
		printk(KERN_INFO "%s: Couldn't reload %s saved state\n",
		       __func__, dev_name(&dev->dev));
	list_del(&match->list);
	pci_release_regions(dev);
out_disable:
	pci_disable_device(dev);
out_put:
	pci_dev_put(dev);
out_free:
	kfree(match);
	srcu_read_unlock(&kvm->srcu, idx);
	mutex_unlock(&kvm->lock);
	return r;
}

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

	kvm_deassign_device(kvm, match);

	kvm_free_assigned_device(kvm, match);

out:
	mutex_unlock(&kvm->lock);
	return r;
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
		    adev->entries_nr > KVM_MAX_MSIX_PER_DEV) {
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
		adev->guest_msix_entries =
			kzalloc(sizeof(struct msix_entry) * entry_nr->entry_nr,
				GFP_KERNEL);
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

static int kvm_vm_ioctl_set_pci_irq_mask(struct kvm *kvm,
		struct kvm_assigned_pci_dev *assigned_dev)
{
	int r = 0;
	struct kvm_assigned_dev_kernel *match;

	mutex_lock(&kvm->lock);

	match = kvm_find_assigned_dev(&kvm->arch.assigned_dev_head,
				      assigned_dev->assigned_dev_id);
	if (!match) {
		r = -ENODEV;
		goto out;
	}

	spin_lock(&match->intx_mask_lock);

	match->flags &= ~KVM_DEV_ASSIGN_MASK_INTX;
	match->flags |= assigned_dev->flags & KVM_DEV_ASSIGN_MASK_INTX;

	if (match->irq_requested_type & KVM_DEV_IRQ_GUEST_INTX) {
		if (assigned_dev->flags & KVM_DEV_ASSIGN_MASK_INTX) {
			kvm_set_irq(match->kvm, match->irq_source_id,
				    match->guest_irq, 0);
			/*
			 * Masking at hardware-level is performed on demand,
			 * i.e. when an IRQ actually arrives at the host.
			 */
		} else if (!(assigned_dev->flags & KVM_DEV_ASSIGN_PCI_2_3)) {
			/*
			 * Unmask the IRQ line if required. Unmasking at
			 * device level will be performed by user space.
			 */
			spin_lock_irq(&match->intx_lock);
			if (match->host_irq_disabled) {
				enable_irq(match->host_irq);
				match->host_irq_disabled = false;
			}
			spin_unlock_irq(&match->intx_lock);
		}
	}

	spin_unlock(&match->intx_mask_lock);

out:
	mutex_unlock(&kvm->lock);
	return r;
}

long kvm_vm_ioctl_assigned_device(struct kvm *kvm, unsigned ioctl,
				  unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int r;

	switch (ioctl) {
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
	case KVM_ASSIGN_SET_INTX_MASK: {
		struct kvm_assigned_pci_dev assigned_dev;

		r = -EFAULT;
		if (copy_from_user(&assigned_dev, argp, sizeof assigned_dev))
			goto out;
		r = kvm_vm_ioctl_set_pci_irq_mask(kvm, &assigned_dev);
		break;
	}
	default:
		r = -ENOTTY;
		break;
	}
out:
	return r;
}

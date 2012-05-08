/*
 * Copyright (c) 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Copyright (C) 2006-2008 Intel Corporation
 * Copyright IBM Corporation, 2008
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Author: Allen M. Kay <allen.m.kay@intel.com>
 * Author: Weidong Han <weidong.han@intel.com>
 * Author: Ben-Ami Yassour <benami@il.ibm.com>
 */

#include <linux/list.h>
#include <linux/kvm_host.h>
#include <linux/pci.h>
#include <linux/dmar.h>
#include <linux/iommu.h>
#include <linux/intel-iommu.h>

static int kvm_iommu_unmap_memslots(struct kvm *kvm);
static void kvm_iommu_put_pages(struct kvm *kvm,
				gfn_t base_gfn, unsigned long npages);

static pfn_t kvm_pin_pages(struct kvm *kvm, struct kvm_memory_slot *slot,
			   gfn_t gfn, unsigned long size)
{
	gfn_t end_gfn;
	pfn_t pfn;

	pfn     = gfn_to_pfn_memslot(kvm, slot, gfn);
	end_gfn = gfn + (size >> PAGE_SHIFT);
	gfn    += 1;

	if (is_error_pfn(pfn))
		return pfn;

	while (gfn < end_gfn)
		gfn_to_pfn_memslot(kvm, slot, gfn++);

	return pfn;
}

int kvm_iommu_map_pages(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	gfn_t gfn, end_gfn;
	pfn_t pfn;
	int r = 0;
	struct iommu_domain *domain = kvm->arch.iommu_domain;
	int flags;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	gfn     = slot->base_gfn;
	end_gfn = gfn + slot->npages;

	flags = IOMMU_READ | IOMMU_WRITE;
	if (kvm->arch.iommu_flags & KVM_IOMMU_CACHE_COHERENCY)
		flags |= IOMMU_CACHE;


	while (gfn < end_gfn) {
		unsigned long page_size;

		/* Check if already mapped */
		if (iommu_iova_to_phys(domain, gfn_to_gpa(gfn))) {
			gfn += 1;
			continue;
		}

		/* Get the page size we could use to map */
		page_size = kvm_host_page_size(kvm, gfn);

		/* Make sure the page_size does not exceed the memslot */
		while ((gfn + (page_size >> PAGE_SHIFT)) > end_gfn)
			page_size >>= 1;

		/* Make sure gfn is aligned to the page size we want to map */
		while ((gfn << PAGE_SHIFT) & (page_size - 1))
			page_size >>= 1;

		/*
		 * Pin all pages we are about to map in memory. This is
		 * important because we unmap and unpin in 4kb steps later.
		 */
		pfn = kvm_pin_pages(kvm, slot, gfn, page_size);
		if (is_error_pfn(pfn)) {
			gfn += 1;
			continue;
		}

		/* Map into IO address space */
		r = iommu_map(domain, gfn_to_gpa(gfn), pfn_to_hpa(pfn),
			      get_order(page_size), flags);
		if (r) {
			printk(KERN_ERR "kvm_iommu_map_address:"
			       "iommu failed to map pfn=%llx\n", pfn);
			goto unmap_pages;
		}

		gfn += page_size >> PAGE_SHIFT;


	}

	return 0;

unmap_pages:
	kvm_iommu_put_pages(kvm, slot->base_gfn, gfn);
	return r;
}

static int kvm_iommu_map_memslots(struct kvm *kvm)
{
	int i, idx, r = 0;
	struct kvm_memslots *slots;

	idx = srcu_read_lock(&kvm->srcu);
	slots = kvm_memslots(kvm);

	for (i = 0; i < slots->nmemslots; i++) {
		r = kvm_iommu_map_pages(kvm, &slots->memslots[i]);
		if (r)
			break;
	}
	srcu_read_unlock(&kvm->srcu, idx);

	return r;
}

int kvm_assign_device(struct kvm *kvm,
		      struct kvm_assigned_dev_kernel *assigned_dev)
{
	struct pci_dev *pdev = NULL;
	struct iommu_domain *domain = kvm->arch.iommu_domain;
	int r, last_flags;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	pdev = assigned_dev->dev;
	if (pdev == NULL)
		return -ENODEV;

	r = iommu_attach_device(domain, &pdev->dev);
	if (r) {
		printk(KERN_ERR "assign device %x:%x:%x.%x failed",
			pci_domain_nr(pdev->bus),
			pdev->bus->number,
			PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn));
		return r;
	}

	last_flags = kvm->arch.iommu_flags;
	if (iommu_domain_has_cap(kvm->arch.iommu_domain,
				 IOMMU_CAP_CACHE_COHERENCY))
		kvm->arch.iommu_flags |= KVM_IOMMU_CACHE_COHERENCY;

	/* Check if need to update IOMMU page table for guest memory */
	if ((last_flags ^ kvm->arch.iommu_flags) ==
			KVM_IOMMU_CACHE_COHERENCY) {
		kvm_iommu_unmap_memslots(kvm);
		r = kvm_iommu_map_memslots(kvm);
		if (r)
			goto out_unmap;
	}

	printk(KERN_DEBUG "assign device %x:%x:%x.%x\n",
		assigned_dev->host_segnr,
		assigned_dev->host_busnr,
		PCI_SLOT(assigned_dev->host_devfn),
		PCI_FUNC(assigned_dev->host_devfn));

	return 0;
out_unmap:
	kvm_iommu_unmap_memslots(kvm);
	return r;
}

int kvm_deassign_device(struct kvm *kvm,
			struct kvm_assigned_dev_kernel *assigned_dev)
{
	struct iommu_domain *domain = kvm->arch.iommu_domain;
	struct pci_dev *pdev = NULL;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	pdev = assigned_dev->dev;
	if (pdev == NULL)
		return -ENODEV;

	iommu_detach_device(domain, &pdev->dev);

	printk(KERN_DEBUG "deassign device %x:%x:%x.%x\n",
		assigned_dev->host_segnr,
		assigned_dev->host_busnr,
		PCI_SLOT(assigned_dev->host_devfn),
		PCI_FUNC(assigned_dev->host_devfn));

	return 0;
}

int kvm_iommu_map_guest(struct kvm *kvm)
{
	int r;

	if (!iommu_found()) {
		printk(KERN_ERR "%s: iommu not found\n", __func__);
		return -ENODEV;
	}

	kvm->arch.iommu_domain = iommu_domain_alloc();
	if (!kvm->arch.iommu_domain)
		return -ENOMEM;

	r = kvm_iommu_map_memslots(kvm);
	if (r)
		goto out_unmap;

	return 0;

out_unmap:
	kvm_iommu_unmap_memslots(kvm);
	return r;
}

static void kvm_unpin_pages(struct kvm *kvm, pfn_t pfn, unsigned long npages)
{
	unsigned long i;

	for (i = 0; i < npages; ++i)
		kvm_release_pfn_clean(pfn + i);
}

static void kvm_iommu_put_pages(struct kvm *kvm,
				gfn_t base_gfn, unsigned long npages)
{
	struct iommu_domain *domain;
	gfn_t end_gfn, gfn;
	pfn_t pfn;
	u64 phys;

	domain  = kvm->arch.iommu_domain;
	end_gfn = base_gfn + npages;
	gfn     = base_gfn;

	/* check if iommu exists and in use */
	if (!domain)
		return;

	while (gfn < end_gfn) {
		unsigned long unmap_pages;
		int order;

		/* Get physical address */
		phys = iommu_iova_to_phys(domain, gfn_to_gpa(gfn));
		pfn  = phys >> PAGE_SHIFT;

		/* Unmap address from IO address space */
		order       = iommu_unmap(domain, gfn_to_gpa(gfn), 0);
		unmap_pages = 1ULL << order;

		/* Unpin all pages we just unmapped to not leak any memory */
		kvm_unpin_pages(kvm, pfn, unmap_pages);

		gfn += unmap_pages;
	}
}

void kvm_iommu_unmap_pages(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	kvm_iommu_put_pages(kvm, slot->base_gfn, slot->npages);
}

static int kvm_iommu_unmap_memslots(struct kvm *kvm)
{
	int i, idx;
	struct kvm_memslots *slots;

	idx = srcu_read_lock(&kvm->srcu);
	slots = kvm_memslots(kvm);

	for (i = 0; i < slots->nmemslots; i++)
		kvm_iommu_unmap_pages(kvm, &slots->memslots[i]);

	srcu_read_unlock(&kvm->srcu, idx);

	return 0;
}

int kvm_iommu_unmap_guest(struct kvm *kvm)
{
	struct iommu_domain *domain = kvm->arch.iommu_domain;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	kvm_iommu_unmap_memslots(kvm);
	iommu_domain_free(domain);
	return 0;
}

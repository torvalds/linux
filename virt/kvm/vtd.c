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
 * Author: Allen M. Kay <allen.m.kay@intel.com>
 * Author: Weidong Han <weidong.han@intel.com>
 * Author: Ben-Ami Yassour <benami@il.ibm.com>
 */

#include <linux/list.h>
#include <linux/kvm_host.h>
#include <linux/pci.h>
#include <linux/dmar.h>
#include <linux/intel-iommu.h>

static int kvm_iommu_unmap_memslots(struct kvm *kvm);
static void kvm_iommu_put_pages(struct kvm *kvm,
				gfn_t base_gfn, unsigned long npages);

int kvm_iommu_map_pages(struct kvm *kvm,
			gfn_t base_gfn, unsigned long npages)
{
	gfn_t gfn = base_gfn;
	pfn_t pfn;
	int i, r = 0;
	struct dmar_domain *domain = kvm->arch.intel_iommu_domain;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	for (i = 0; i < npages; i++) {
		/* check if already mapped */
		if (intel_iommu_iova_to_phys(domain,
					     gfn_to_gpa(gfn)))
			continue;

		pfn = gfn_to_pfn(kvm, gfn);
		r = intel_iommu_map_address(domain,
					    gfn_to_gpa(gfn),
					    pfn_to_hpa(pfn),
					    PAGE_SIZE,
					    DMA_PTE_READ | DMA_PTE_WRITE);
		if (r) {
			printk(KERN_ERR "kvm_iommu_map_address:"
			       "iommu failed to map pfn=%lx\n", pfn);
			goto unmap_pages;
		}
		gfn++;
	}
	return 0;

unmap_pages:
	kvm_iommu_put_pages(kvm, base_gfn, i);
	return r;
}

static int kvm_iommu_map_memslots(struct kvm *kvm)
{
	int i, r;

	down_read(&kvm->slots_lock);
	for (i = 0; i < kvm->nmemslots; i++) {
		r = kvm_iommu_map_pages(kvm, kvm->memslots[i].base_gfn,
					kvm->memslots[i].npages);
		if (r)
			break;
	}
	up_read(&kvm->slots_lock);
	return r;
}

int kvm_assign_device(struct kvm *kvm,
		      struct kvm_assigned_dev_kernel *assigned_dev)
{
	struct pci_dev *pdev = NULL;
	struct dmar_domain *domain = kvm->arch.intel_iommu_domain;
	int r;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	pdev = assigned_dev->dev;
	if (pdev == NULL)
		return -ENODEV;

	r = intel_iommu_attach_device(domain, pdev);
	if (r) {
		printk(KERN_ERR "assign device %x:%x.%x failed",
			pdev->bus->number,
			PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn));
		return r;
	}

	printk(KERN_DEBUG "assign device: host bdf = %x:%x:%x\n",
		assigned_dev->host_busnr,
		PCI_SLOT(assigned_dev->host_devfn),
		PCI_FUNC(assigned_dev->host_devfn));

	return 0;
}

int kvm_deassign_device(struct kvm *kvm,
			struct kvm_assigned_dev_kernel *assigned_dev)
{
	struct dmar_domain *domain = kvm->arch.intel_iommu_domain;
	struct pci_dev *pdev = NULL;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	pdev = assigned_dev->dev;
	if (pdev == NULL)
		return -ENODEV;

	intel_iommu_detach_device(domain, pdev);

	printk(KERN_DEBUG "deassign device: host bdf = %x:%x:%x\n",
		assigned_dev->host_busnr,
		PCI_SLOT(assigned_dev->host_devfn),
		PCI_FUNC(assigned_dev->host_devfn));

	return 0;
}

int kvm_iommu_map_guest(struct kvm *kvm)
{
	int r;

	if (!intel_iommu_found()) {
		printk(KERN_ERR "%s: intel iommu not found\n", __func__);
		return -ENODEV;
	}

	kvm->arch.intel_iommu_domain = intel_iommu_alloc_domain();
	if (!kvm->arch.intel_iommu_domain)
		return -ENOMEM;

	r = kvm_iommu_map_memslots(kvm);
	if (r)
		goto out_unmap;

	return 0;

out_unmap:
	kvm_iommu_unmap_memslots(kvm);
	return r;
}

static void kvm_iommu_put_pages(struct kvm *kvm,
				gfn_t base_gfn, unsigned long npages)
{
	gfn_t gfn = base_gfn;
	pfn_t pfn;
	struct dmar_domain *domain = kvm->arch.intel_iommu_domain;
	unsigned long i;
	u64 phys;

	/* check if iommu exists and in use */
	if (!domain)
		return;

	for (i = 0; i < npages; i++) {
		phys = intel_iommu_iova_to_phys(domain,
						gfn_to_gpa(gfn));
		pfn = phys >> PAGE_SHIFT;
		kvm_release_pfn_clean(pfn);
		gfn++;
	}

	intel_iommu_unmap_address(domain,
				  gfn_to_gpa(base_gfn),
				  PAGE_SIZE * npages);
}

static int kvm_iommu_unmap_memslots(struct kvm *kvm)
{
	int i;
	down_read(&kvm->slots_lock);
	for (i = 0; i < kvm->nmemslots; i++) {
		kvm_iommu_put_pages(kvm, kvm->memslots[i].base_gfn,
				    kvm->memslots[i].npages);
	}
	up_read(&kvm->slots_lock);

	return 0;
}

int kvm_iommu_unmap_guest(struct kvm *kvm)
{
	struct dmar_domain *domain = kvm->arch.intel_iommu_domain;

	/* check if iommu exists and in use */
	if (!domain)
		return 0;

	kvm_iommu_unmap_memslots(kvm);
	intel_iommu_free_domain(domain);
	return 0;
}

/*
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __LINUX_IOMMU_H
#define __LINUX_IOMMU_H

#include <linux/errno.h>

#define IOMMU_READ	(1)
#define IOMMU_WRITE	(2)
#define IOMMU_CACHE	(4) /* DMA cache coherency */

struct iommu_ops;
struct bus_type;
struct device;
struct iommu_domain;

/* iommu fault flags */
#define IOMMU_FAULT_READ	0x0
#define IOMMU_FAULT_WRITE	0x1

typedef int (*iommu_fault_handler_t)(struct iommu_domain *,
				struct device *, unsigned long, int);

struct iommu_domain {
	struct iommu_ops *ops;
	void *priv;
	iommu_fault_handler_t handler;
};

#define IOMMU_CAP_CACHE_COHERENCY	0x1
#define IOMMU_CAP_INTR_REMAP		0x2	/* isolates device intrs */

#ifdef CONFIG_IOMMU_API

struct iommu_ops {
	int (*domain_init)(struct iommu_domain *domain);
	void (*domain_destroy)(struct iommu_domain *domain);
	int (*attach_dev)(struct iommu_domain *domain, struct device *dev);
	void (*detach_dev)(struct iommu_domain *domain, struct device *dev);
	int (*map)(struct iommu_domain *domain, unsigned long iova,
		   phys_addr_t paddr, int gfp_order, int prot);
	int (*unmap)(struct iommu_domain *domain, unsigned long iova,
		     int gfp_order);
	phys_addr_t (*iova_to_phys)(struct iommu_domain *domain,
				    unsigned long iova);
	int (*domain_has_cap)(struct iommu_domain *domain,
			      unsigned long cap);
};

extern int bus_set_iommu(struct bus_type *bus, struct iommu_ops *ops);
extern bool iommu_present(struct bus_type *bus);
extern struct iommu_domain *iommu_domain_alloc(struct bus_type *bus);
extern void iommu_domain_free(struct iommu_domain *domain);
extern int iommu_attach_device(struct iommu_domain *domain,
			       struct device *dev);
extern void iommu_detach_device(struct iommu_domain *domain,
				struct device *dev);
extern int iommu_map(struct iommu_domain *domain, unsigned long iova,
		     phys_addr_t paddr, int gfp_order, int prot);
extern int iommu_unmap(struct iommu_domain *domain, unsigned long iova,
		       int gfp_order);
extern phys_addr_t iommu_iova_to_phys(struct iommu_domain *domain,
				      unsigned long iova);
extern int iommu_domain_has_cap(struct iommu_domain *domain,
				unsigned long cap);
extern void iommu_set_fault_handler(struct iommu_domain *domain,
					iommu_fault_handler_t handler);

/**
 * report_iommu_fault() - report about an IOMMU fault to the IOMMU framework
 * @domain: the iommu domain where the fault has happened
 * @dev: the device where the fault has happened
 * @iova: the faulting address
 * @flags: mmu fault flags (e.g. IOMMU_FAULT_READ/IOMMU_FAULT_WRITE/...)
 *
 * This function should be called by the low-level IOMMU implementations
 * whenever IOMMU faults happen, to allow high-level users, that are
 * interested in such events, to know about them.
 *
 * This event may be useful for several possible use cases:
 * - mere logging of the event
 * - dynamic TLB/PTE loading
 * - if restarting of the faulting device is required
 *
 * Returns 0 on success and an appropriate error code otherwise (if dynamic
 * PTE/TLB loading will one day be supported, implementations will be able
 * to tell whether it succeeded or not according to this return value).
 *
 * Specifically, -ENOSYS is returned if a fault handler isn't installed
 * (though fault handlers can also return -ENOSYS, in case they want to
 * elicit the default behavior of the IOMMU drivers).
 */
static inline int report_iommu_fault(struct iommu_domain *domain,
		struct device *dev, unsigned long iova, int flags)
{
	int ret = -ENOSYS;

	/*
	 * if upper layers showed interest and installed a fault handler,
	 * invoke it.
	 */
	if (domain->handler)
		ret = domain->handler(domain, dev, iova, flags);

	return ret;
}

#else /* CONFIG_IOMMU_API */

struct iommu_ops {};

static inline bool iommu_present(struct bus_type *bus)
{
	return false;
}

static inline struct iommu_domain *iommu_domain_alloc(struct bus_type *bus)
{
	return NULL;
}

static inline void iommu_domain_free(struct iommu_domain *domain)
{
}

static inline int iommu_attach_device(struct iommu_domain *domain,
				      struct device *dev)
{
	return -ENODEV;
}

static inline void iommu_detach_device(struct iommu_domain *domain,
				       struct device *dev)
{
}

static inline int iommu_map(struct iommu_domain *domain, unsigned long iova,
			    phys_addr_t paddr, int gfp_order, int prot)
{
	return -ENODEV;
}

static inline int iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			      int gfp_order)
{
	return -ENODEV;
}

static inline phys_addr_t iommu_iova_to_phys(struct iommu_domain *domain,
					     unsigned long iova)
{
	return 0;
}

static inline int domain_has_cap(struct iommu_domain *domain,
				 unsigned long cap)
{
	return 0;
}

static inline void iommu_set_fault_handler(struct iommu_domain *domain,
					iommu_fault_handler_t handler)
{
}

#endif /* CONFIG_IOMMU_API */

#endif /* __LINUX_IOMMU_H */

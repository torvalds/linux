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

#define IOMMU_READ	(1)
#define IOMMU_WRITE	(2)
#define IOMMU_CACHE	(4) /* DMA cache coherency */

struct device;

struct iommu_domain {
	void *priv;
};

#define IOMMU_CAP_CACHE_COHERENCY	0x1
#define IOMMU_CAP_INTR_REMAP		0x2	/* isolates device intrs */

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

#ifdef CONFIG_IOMMU_API

extern void register_iommu(struct iommu_ops *ops);
extern bool iommu_found(void);
extern struct iommu_domain *iommu_domain_alloc(void);
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

#else /* CONFIG_IOMMU_API */

static inline void register_iommu(struct iommu_ops *ops)
{
}

static inline bool iommu_found(void)
{
	return false;
}

static inline struct iommu_domain *iommu_domain_alloc(void)
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

#endif /* CONFIG_IOMMU_API */

#endif /* __LINUX_IOMMU_H */

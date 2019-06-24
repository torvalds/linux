/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016, Semihalf
 *	Author: Tomasz Nowicki <tn@semihalf.com>
 */

#ifndef __ACPI_IORT_H__
#define __ACPI_IORT_H__

#include <linux/acpi.h>
#include <linux/fwnode.h>
#include <linux/irqdomain.h>

#define IORT_IRQ_MASK(irq)		(irq & 0xffffffffULL)
#define IORT_IRQ_TRIGGER_MASK(irq)	((irq >> 32) & 0xffffffffULL)

/*
 * PMCG model identifiers for use in smmu pmu driver. Please note
 * that this is purely for the use of software and has nothing to
 * do with hardware or with IORT specification.
 */
#define IORT_SMMU_V3_PMCG_GENERIC        0x00000000 /* Generic SMMUv3 PMCG */
#define IORT_SMMU_V3_PMCG_HISI_HIP08     0x00000001 /* HiSilicon HIP08 PMCG */

int iort_register_domain_token(int trans_id, phys_addr_t base,
			       struct fwnode_handle *fw_node);
void iort_deregister_domain_token(int trans_id);
struct fwnode_handle *iort_find_domain_token(int trans_id);
#ifdef CONFIG_ACPI_IORT
void acpi_iort_init(void);
u32 iort_msi_map_rid(struct device *dev, u32 req_id);
struct irq_domain *iort_get_device_domain(struct device *dev, u32 req_id);
void acpi_configure_pmsi_domain(struct device *dev);
int iort_pmsi_get_dev_id(struct device *dev, u32 *dev_id);
/* IOMMU interface */
void iort_dma_setup(struct device *dev, u64 *dma_addr, u64 *size);
const struct iommu_ops *iort_iommu_configure(struct device *dev);
int iort_iommu_msi_get_resv_regions(struct device *dev, struct list_head *head);
#else
static inline void acpi_iort_init(void) { }
static inline u32 iort_msi_map_rid(struct device *dev, u32 req_id)
{ return req_id; }
static inline struct irq_domain *iort_get_device_domain(struct device *dev,
							u32 req_id)
{ return NULL; }
static inline void acpi_configure_pmsi_domain(struct device *dev) { }
/* IOMMU interface */
static inline void iort_dma_setup(struct device *dev, u64 *dma_addr,
				  u64 *size) { }
static inline const struct iommu_ops *iort_iommu_configure(
				      struct device *dev)
{ return NULL; }
static inline
int iort_iommu_msi_get_resv_regions(struct device *dev, struct list_head *head)
{ return 0; }
#endif

#endif /* __ACPI_IORT_H__ */

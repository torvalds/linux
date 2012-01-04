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
 * Copyright (C) Ashok Raj <ashok.raj@intel.com>
 * Copyright (C) Shaohua Li <shaohua.li@intel.com>
 */

#ifndef __DMAR_H__
#define __DMAR_H__

#include <linux/acpi.h>
#include <linux/types.h>
#include <linux/msi.h>
#include <linux/irqreturn.h>

struct acpi_dmar_header;

/* DMAR Flags */
#define DMAR_INTR_REMAP		0x1
#define DMAR_X2APIC_OPT_OUT	0x2

struct intel_iommu;
#ifdef CONFIG_DMAR_TABLE
extern struct acpi_table_header *dmar_tbl;
struct dmar_drhd_unit {
	struct list_head list;		/* list of drhd units	*/
	struct  acpi_dmar_header *hdr;	/* ACPI header		*/
	u64	reg_base_addr;		/* register base address*/
	struct	pci_dev **devices; 	/* target device array	*/
	int	devices_cnt;		/* target device count	*/
	u16	segment;		/* PCI domain		*/
	u8	ignored:1; 		/* ignore drhd		*/
	u8	include_all:1;
	struct intel_iommu *iommu;
};

extern struct list_head dmar_drhd_units;

#define for_each_drhd_unit(drhd) \
	list_for_each_entry(drhd, &dmar_drhd_units, list)

#define for_each_active_iommu(i, drhd)					\
	list_for_each_entry(drhd, &dmar_drhd_units, list)		\
		if (i=drhd->iommu, drhd->ignored) {} else

#define for_each_iommu(i, drhd)						\
	list_for_each_entry(drhd, &dmar_drhd_units, list)		\
		if (i=drhd->iommu, 0) {} else 

extern int dmar_table_init(void);
extern int dmar_dev_scope_init(void);

/* Intel IOMMU detection */
extern int detect_intel_iommu(void);
extern int enable_drhd_fault_handling(void);

extern int parse_ioapics_under_ir(void);
extern int alloc_iommu(struct dmar_drhd_unit *);
#else
static inline int detect_intel_iommu(void)
{
	return -ENODEV;
}

static inline int dmar_table_init(void)
{
	return -ENODEV;
}
static inline int enable_drhd_fault_handling(void)
{
	return -1;
}
#endif /* !CONFIG_DMAR_TABLE */

struct irte {
	union {
		struct {
			__u64	present 	: 1,
				fpd		: 1,
				dst_mode	: 1,
				redir_hint	: 1,
				trigger_mode	: 1,
				dlvry_mode	: 3,
				avail		: 4,
				__reserved_1	: 4,
				vector		: 8,
				__reserved_2	: 8,
				dest_id		: 32;
		};
		__u64 low;
	};

	union {
		struct {
			__u64	sid		: 16,
				sq		: 2,
				svt		: 2,
				__reserved_3	: 44;
		};
		__u64 high;
	};
};

#ifdef CONFIG_IRQ_REMAP
extern int intr_remapping_enabled;
extern int intr_remapping_supported(void);
extern int enable_intr_remapping(void);
extern void disable_intr_remapping(void);
extern int reenable_intr_remapping(int);

extern int get_irte(int irq, struct irte *entry);
extern int modify_irte(int irq, struct irte *irte_modified);
extern int alloc_irte(struct intel_iommu *iommu, int irq, u16 count);
extern int set_irte_irq(int irq, struct intel_iommu *iommu, u16 index,
   			u16 sub_handle);
extern int map_irq_to_irte_handle(int irq, u16 *sub_handle);
extern int free_irte(int irq);

extern struct intel_iommu *map_dev_to_ir(struct pci_dev *dev);
extern struct intel_iommu *map_ioapic_to_ir(int apic);
extern struct intel_iommu *map_hpet_to_ir(u8 id);
extern int set_ioapic_sid(struct irte *irte, int apic);
extern int set_hpet_sid(struct irte *irte, u8 id);
extern int set_msi_sid(struct irte *irte, struct pci_dev *dev);
#else
static inline int alloc_irte(struct intel_iommu *iommu, int irq, u16 count)
{
	return -1;
}
static inline int modify_irte(int irq, struct irte *irte_modified)
{
	return -1;
}
static inline int free_irte(int irq)
{
	return -1;
}
static inline int map_irq_to_irte_handle(int irq, u16 *sub_handle)
{
	return -1;
}
static inline int set_irte_irq(int irq, struct intel_iommu *iommu, u16 index,
			       u16 sub_handle)
{
	return -1;
}
static inline struct intel_iommu *map_dev_to_ir(struct pci_dev *dev)
{
	return NULL;
}
static inline struct intel_iommu *map_ioapic_to_ir(int apic)
{
	return NULL;
}
static inline struct intel_iommu *map_hpet_to_ir(unsigned int hpet_id)
{
	return NULL;
}
static inline int set_ioapic_sid(struct irte *irte, int apic)
{
	return 0;
}
static inline int set_hpet_sid(struct irte *irte, u8 id)
{
	return -1;
}
static inline int set_msi_sid(struct irte *irte, struct pci_dev *dev)
{
	return 0;
}

#define intr_remapping_enabled		(0)

static inline int enable_intr_remapping(void)
{
	return -1;
}

static inline void disable_intr_remapping(void)
{
}

static inline int reenable_intr_remapping(int eim)
{
	return 0;
}
#endif

enum {
	IRQ_REMAP_XAPIC_MODE,
	IRQ_REMAP_X2APIC_MODE,
};

/* Can't use the common MSI interrupt functions
 * since DMAR is not a pci device
 */
struct irq_data;
extern void dmar_msi_unmask(struct irq_data *data);
extern void dmar_msi_mask(struct irq_data *data);
extern void dmar_msi_read(int irq, struct msi_msg *msg);
extern void dmar_msi_write(int irq, struct msi_msg *msg);
extern int dmar_set_interrupt(struct intel_iommu *iommu);
extern irqreturn_t dmar_fault(int irq, void *dev_id);
extern int arch_setup_dmar_msi(unsigned int irq);

#ifdef CONFIG_INTEL_IOMMU
extern int iommu_detected, no_iommu;
extern struct list_head dmar_rmrr_units;
struct dmar_rmrr_unit {
	struct list_head list;		/* list of rmrr units	*/
	struct acpi_dmar_header *hdr;	/* ACPI header		*/
	u64	base_address;		/* reserved base address*/
	u64	end_address;		/* reserved end address */
	struct pci_dev **devices;	/* target devices */
	int	devices_cnt;		/* target device count */
};

#define for_each_rmrr_units(rmrr) \
	list_for_each_entry(rmrr, &dmar_rmrr_units, list)

struct dmar_atsr_unit {
	struct list_head list;		/* list of ATSR units */
	struct acpi_dmar_header *hdr;	/* ACPI header */
	struct pci_dev **devices;	/* target devices */
	int devices_cnt;		/* target device count */
	u8 include_all:1;		/* include all ports */
};

int dmar_parse_rmrr_atsr_dev(void);
extern int dmar_parse_one_rmrr(struct acpi_dmar_header *header);
extern int dmar_parse_one_atsr(struct acpi_dmar_header *header);
extern int dmar_parse_dev_scope(void *start, void *end, int *cnt,
				struct pci_dev ***devices, u16 segment);
extern int intel_iommu_init(void);
#else /* !CONFIG_INTEL_IOMMU: */
static inline int intel_iommu_init(void) { return -ENODEV; }
static inline int dmar_parse_one_rmrr(struct acpi_dmar_header *header)
{
	return 0;
}
static inline int dmar_parse_one_atsr(struct acpi_dmar_header *header)
{
	return 0;
}
static inline int dmar_parse_rmrr_atsr_dev(void)
{
	return 0;
}
#endif /* CONFIG_INTEL_IOMMU */

#endif /* __DMAR_H__ */

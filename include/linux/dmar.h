/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2006, Intel Corporation.
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
#include <linux/rwsem.h>
#include <linux/rculist.h>

struct acpi_dmar_header;

#define DMAR_UNITS_SUPPORTED	1024

/* DMAR Flags */
#define DMAR_INTR_REMAP		0x1
#define DMAR_X2APIC_OPT_OUT	0x2
#define DMAR_PLATFORM_OPT_IN	0x4

struct intel_iommu;

struct dmar_dev_scope {
	struct device __rcu *dev;
	u8 bus;
	u8 devfn;
};

#ifdef CONFIG_DMAR_TABLE
extern struct acpi_table_header *dmar_tbl;
struct dmar_drhd_unit {
	struct list_head list;		/* list of drhd units	*/
	struct  acpi_dmar_header *hdr;	/* ACPI header		*/
	u64	reg_base_addr;		/* register base address*/
	struct	dmar_dev_scope *devices;/* target device array	*/
	int	devices_cnt;		/* target device count	*/
	u16	segment;		/* PCI domain		*/
	u8	ignored:1; 		/* ignore drhd		*/
	u8	include_all:1;
	u8	gfx_dedicated:1;	/* graphic dedicated	*/
	struct intel_iommu *iommu;
};

struct dmar_pci_path {
	u8 bus;
	u8 device;
	u8 function;
};

struct dmar_pci_notify_info {
	struct pci_dev			*dev;
	unsigned long			event;
	int				bus;
	u16				seg;
	u16				level;
	struct dmar_pci_path		path[];
}  __attribute__((packed));

extern struct rw_semaphore dmar_global_lock;
extern struct list_head dmar_drhd_units;

#define for_each_drhd_unit(drhd)					\
	list_for_each_entry_rcu(drhd, &dmar_drhd_units, list,		\
				dmar_rcu_check())

#define for_each_active_drhd_unit(drhd)					\
	list_for_each_entry_rcu(drhd, &dmar_drhd_units, list,		\
				dmar_rcu_check())			\
		if (drhd->ignored) {} else

#define for_each_active_iommu(i, drhd)					\
	list_for_each_entry_rcu(drhd, &dmar_drhd_units, list,		\
				dmar_rcu_check())			\
		if (i=drhd->iommu, drhd->ignored) {} else

#define for_each_iommu(i, drhd)						\
	list_for_each_entry_rcu(drhd, &dmar_drhd_units, list,		\
				dmar_rcu_check())			\
		if (i=drhd->iommu, 0) {} else 

static inline bool dmar_rcu_check(void)
{
	return rwsem_is_locked(&dmar_global_lock) ||
	       system_state == SYSTEM_BOOTING;
}

#define	dmar_rcu_dereference(p)	rcu_dereference_check((p), dmar_rcu_check())

#define for_each_dev_scope(devs, cnt, i, tmp)				\
	for ((i) = 0; ((tmp) = (i) < (cnt) ?				\
	    dmar_rcu_dereference((devs)[(i)].dev) : NULL, (i) < (cnt)); \
	    (i)++)

#define for_each_active_dev_scope(devs, cnt, i, tmp)			\
	for_each_dev_scope((devs), (cnt), (i), (tmp))			\
		if (!(tmp)) { continue; } else

extern int dmar_table_init(void);
extern int dmar_dev_scope_init(void);
extern void dmar_register_bus_notifier(void);
extern int dmar_parse_dev_scope(void *start, void *end, int *cnt,
				struct dmar_dev_scope **devices, u16 segment);
extern void *dmar_alloc_dev_scope(void *start, void *end, int *cnt);
extern void dmar_free_dev_scope(struct dmar_dev_scope **devices, int *cnt);
extern int dmar_insert_dev_scope(struct dmar_pci_notify_info *info,
				 void *start, void*end, u16 segment,
				 struct dmar_dev_scope *devices,
				 int devices_cnt);
extern int dmar_remove_dev_scope(struct dmar_pci_notify_info *info,
				 u16 segment, struct dmar_dev_scope *devices,
				 int count);
/* Intel IOMMU detection */
void detect_intel_iommu(void);
extern int enable_drhd_fault_handling(void);
extern int dmar_device_add(acpi_handle handle);
extern int dmar_device_remove(acpi_handle handle);

static inline int dmar_res_noop(struct acpi_dmar_header *hdr, void *arg)
{
	return 0;
}

#ifdef CONFIG_DMAR_DEBUG
void dmar_fault_dump_ptes(struct intel_iommu *iommu, u16 source_id,
			  unsigned long long addr, u32 pasid);
#else
static inline void dmar_fault_dump_ptes(struct intel_iommu *iommu, u16 source_id,
					unsigned long long addr, u32 pasid) {}
#endif

#ifdef CONFIG_INTEL_IOMMU
extern int iommu_detected, no_iommu;
extern int intel_iommu_init(void);
extern void intel_iommu_shutdown(void);
extern int dmar_parse_one_rmrr(struct acpi_dmar_header *header, void *arg);
extern int dmar_parse_one_atsr(struct acpi_dmar_header *header, void *arg);
extern int dmar_check_one_atsr(struct acpi_dmar_header *hdr, void *arg);
extern int dmar_parse_one_satc(struct acpi_dmar_header *hdr, void *arg);
extern int dmar_release_one_atsr(struct acpi_dmar_header *hdr, void *arg);
extern int dmar_iommu_hotplug(struct dmar_drhd_unit *dmaru, bool insert);
extern int dmar_iommu_notify_scope_dev(struct dmar_pci_notify_info *info);
#else /* !CONFIG_INTEL_IOMMU: */
static inline int intel_iommu_init(void) { return -ENODEV; }
static inline void intel_iommu_shutdown(void) { }

#define	dmar_parse_one_rmrr		dmar_res_noop
#define	dmar_parse_one_atsr		dmar_res_noop
#define	dmar_check_one_atsr		dmar_res_noop
#define	dmar_release_one_atsr		dmar_res_noop
#define	dmar_parse_one_satc		dmar_res_noop

static inline int dmar_iommu_notify_scope_dev(struct dmar_pci_notify_info *info)
{
	return 0;
}

static inline int dmar_iommu_hotplug(struct dmar_drhd_unit *dmaru, bool insert)
{
	return 0;
}
#endif /* CONFIG_INTEL_IOMMU */

#ifdef CONFIG_IRQ_REMAP
extern int dmar_ir_hotplug(struct dmar_drhd_unit *dmaru, bool insert);
#else  /* CONFIG_IRQ_REMAP */
static inline int dmar_ir_hotplug(struct dmar_drhd_unit *dmaru, bool insert)
{ return 0; }
#endif /* CONFIG_IRQ_REMAP */

extern bool dmar_platform_optin(void);

#else /* CONFIG_DMAR_TABLE */

static inline int dmar_device_add(void *handle)
{
	return 0;
}

static inline int dmar_device_remove(void *handle)
{
	return 0;
}

static inline bool dmar_platform_optin(void)
{
	return false;
}

static inline void detect_intel_iommu(void)
{
}

#endif /* CONFIG_DMAR_TABLE */

struct irte {
	union {
		/* Shared between remapped and posted mode*/
		struct {
			__u64	present		: 1,  /*  0      */
				fpd		: 1,  /*  1      */
				__res0		: 6,  /*  2 -  6 */
				avail		: 4,  /*  8 - 11 */
				__res1		: 3,  /* 12 - 14 */
				pst		: 1,  /* 15      */
				vector		: 8,  /* 16 - 23 */
				__res2		: 40; /* 24 - 63 */
		};

		/* Remapped mode */
		struct {
			__u64	r_present	: 1,  /*  0      */
				r_fpd		: 1,  /*  1      */
				dst_mode	: 1,  /*  2      */
				redir_hint	: 1,  /*  3      */
				trigger_mode	: 1,  /*  4      */
				dlvry_mode	: 3,  /*  5 -  7 */
				r_avail		: 4,  /*  8 - 11 */
				r_res0		: 4,  /* 12 - 15 */
				r_vector	: 8,  /* 16 - 23 */
				r_res1		: 8,  /* 24 - 31 */
				dest_id		: 32; /* 32 - 63 */
		};

		/* Posted mode */
		struct {
			__u64	p_present	: 1,  /*  0      */
				p_fpd		: 1,  /*  1      */
				p_res0		: 6,  /*  2 -  7 */
				p_avail		: 4,  /*  8 - 11 */
				p_res1		: 2,  /* 12 - 13 */
				p_urgent	: 1,  /* 14      */
				p_pst		: 1,  /* 15      */
				p_vector	: 8,  /* 16 - 23 */
				p_res2		: 14, /* 24 - 37 */
				pda_l		: 26; /* 38 - 63 */
		};
		__u64 low;
	};

	union {
		/* Shared between remapped and posted mode*/
		struct {
			__u64	sid		: 16,  /* 64 - 79  */
				sq		: 2,   /* 80 - 81  */
				svt		: 2,   /* 82 - 83  */
				__res3		: 44;  /* 84 - 127 */
		};

		/* Posted mode*/
		struct {
			__u64	p_sid		: 16,  /* 64 - 79  */
				p_sq		: 2,   /* 80 - 81  */
				p_svt		: 2,   /* 82 - 83  */
				p_res3		: 12,  /* 84 - 95  */
				pda_h		: 32;  /* 96 - 127 */
		};
		__u64 high;
	};
};

static inline void dmar_copy_shared_irte(struct irte *dst, struct irte *src)
{
	dst->present	= src->present;
	dst->fpd	= src->fpd;
	dst->avail	= src->avail;
	dst->pst	= src->pst;
	dst->vector	= src->vector;
	dst->sid	= src->sid;
	dst->sq		= src->sq;
	dst->svt	= src->svt;
}

#define PDA_LOW_BIT    26
#define PDA_HIGH_BIT   32

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
extern int dmar_alloc_hwirq(int id, int node, void *arg);
extern void dmar_free_hwirq(int irq);

#endif /* __DMAR_H__ */

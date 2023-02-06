/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015, 2016 ARM Ltd.
 */
#ifndef __KVM_ARM_VGIC_H
#define __KVM_ARM_VGIC_H

#include <linux/bits.h>
#include <linux/kvm.h>
#include <linux/irqreturn.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/static_key.h>
#include <linux/types.h>
#include <kvm/iodev.h>
#include <linux/list.h>
#include <linux/jump_label.h>

#include <linux/irqchip/arm-gic-v4.h>

#define VGIC_V3_MAX_CPUS	512
#define VGIC_V2_MAX_CPUS	8
#define VGIC_NR_IRQS_LEGACY     256
#define VGIC_NR_SGIS		16
#define VGIC_NR_PPIS		16
#define VGIC_NR_PRIVATE_IRQS	(VGIC_NR_SGIS + VGIC_NR_PPIS)
#define VGIC_MAX_PRIVATE	(VGIC_NR_PRIVATE_IRQS - 1)
#define VGIC_MAX_SPI		1019
#define VGIC_MAX_RESERVED	1023
#define VGIC_MIN_LPI		8192
#define KVM_IRQCHIP_NUM_PINS	(1020 - 32)

#define irq_is_ppi(irq) ((irq) >= VGIC_NR_SGIS && (irq) < VGIC_NR_PRIVATE_IRQS)
#define irq_is_spi(irq) ((irq) >= VGIC_NR_PRIVATE_IRQS && \
			 (irq) <= VGIC_MAX_SPI)

enum vgic_type {
	VGIC_V2,		/* Good ol' GICv2 */
	VGIC_V3,		/* New fancy GICv3 */
};

/* same for all guests, as depending only on the _host's_ GIC model */
struct vgic_global {
	/* type of the host GIC */
	enum vgic_type		type;

	/* Physical address of vgic virtual cpu interface */
	phys_addr_t		vcpu_base;

	/* GICV mapping, kernel VA */
	void __iomem		*vcpu_base_va;
	/* GICV mapping, HYP VA */
	void __iomem		*vcpu_hyp_va;

	/* virtual control interface mapping, kernel VA */
	void __iomem		*vctrl_base;
	/* virtual control interface mapping, HYP VA */
	void __iomem		*vctrl_hyp;

	/* Number of implemented list registers */
	int			nr_lr;

	/* Maintenance IRQ number */
	unsigned int		maint_irq;

	/* maximum number of VCPUs allowed (GICv2 limits us to 8) */
	int			max_gic_vcpus;

	/* Only needed for the legacy KVM_CREATE_IRQCHIP */
	bool			can_emulate_gicv2;

	/* Hardware has GICv4? */
	bool			has_gicv4;
	bool			has_gicv4_1;

	/* Pseudo GICv3 from outer space */
	bool			no_hw_deactivation;

	/* GIC system register CPU interface */
	struct static_key_false gicv3_cpuif;

	u32			ich_vtr_el2;
};

extern struct vgic_global kvm_vgic_global_state;

#define VGIC_V2_MAX_LRS		(1 << 6)
#define VGIC_V3_MAX_LRS		16
#define VGIC_V3_LR_INDEX(lr)	(VGIC_V3_MAX_LRS - 1 - lr)

enum vgic_irq_config {
	VGIC_CONFIG_EDGE = 0,
	VGIC_CONFIG_LEVEL
};

/*
 * Per-irq ops overriding some common behavious.
 *
 * Always called in non-preemptible section and the functions can use
 * kvm_arm_get_running_vcpu() to get the vcpu pointer for private IRQs.
 */
struct irq_ops {
	/* Per interrupt flags for special-cased interrupts */
	unsigned long flags;

#define VGIC_IRQ_SW_RESAMPLE	BIT(0)	/* Clear the active state for resampling */

	/*
	 * Callback function pointer to in-kernel devices that can tell us the
	 * state of the input level of mapped level-triggered IRQ faster than
	 * peaking into the physical GIC.
	 */
	bool (*get_input_level)(int vintid);
};

struct vgic_irq {
	raw_spinlock_t irq_lock;	/* Protects the content of the struct */
	struct list_head lpi_list;	/* Used to link all LPIs together */
	struct list_head ap_list;

	struct kvm_vcpu *vcpu;		/* SGIs and PPIs: The VCPU
					 * SPIs and LPIs: The VCPU whose ap_list
					 * this is queued on.
					 */

	struct kvm_vcpu *target_vcpu;	/* The VCPU that this interrupt should
					 * be sent to, as a result of the
					 * targets reg (v2) or the
					 * affinity reg (v3).
					 */

	u32 intid;			/* Guest visible INTID */
	bool line_level;		/* Level only */
	bool pending_latch;		/* The pending latch state used to calculate
					 * the pending state for both level
					 * and edge triggered IRQs. */
	bool active;			/* not used for LPIs */
	bool enabled;
	bool hw;			/* Tied to HW IRQ */
	struct kref refcount;		/* Used for LPIs */
	u32 hwintid;			/* HW INTID number */
	unsigned int host_irq;		/* linux irq corresponding to hwintid */
	union {
		u8 targets;			/* GICv2 target VCPUs mask */
		u32 mpidr;			/* GICv3 target VCPU */
	};
	u8 source;			/* GICv2 SGIs only */
	u8 active_source;		/* GICv2 SGIs only */
	u8 priority;
	u8 group;			/* 0 == group 0, 1 == group 1 */
	enum vgic_irq_config config;	/* Level or edge */

	struct irq_ops *ops;

	void *owner;			/* Opaque pointer to reserve an interrupt
					   for in-kernel devices. */
};

static inline bool vgic_irq_needs_resampling(struct vgic_irq *irq)
{
	return irq->ops && (irq->ops->flags & VGIC_IRQ_SW_RESAMPLE);
}

struct vgic_register_region;
struct vgic_its;

enum iodev_type {
	IODEV_CPUIF,
	IODEV_DIST,
	IODEV_REDIST,
	IODEV_ITS
};

struct vgic_io_device {
	gpa_t base_addr;
	union {
		struct kvm_vcpu *redist_vcpu;
		struct vgic_its *its;
	};
	const struct vgic_register_region *regions;
	enum iodev_type iodev_type;
	int nr_regions;
	struct kvm_io_device dev;
};

struct vgic_its {
	/* The base address of the ITS control register frame */
	gpa_t			vgic_its_base;

	bool			enabled;
	struct vgic_io_device	iodev;
	struct kvm_device	*dev;

	/* These registers correspond to GITS_BASER{0,1} */
	u64			baser_device_table;
	u64			baser_coll_table;

	/* Protects the command queue */
	struct mutex		cmd_lock;
	u64			cbaser;
	u32			creadr;
	u32			cwriter;

	/* migration ABI revision in use */
	u32			abi_rev;

	/* Protects the device and collection lists */
	struct mutex		its_lock;
	struct list_head	device_list;
	struct list_head	collection_list;
};

struct vgic_state_iter;

struct vgic_redist_region {
	u32 index;
	gpa_t base;
	u32 count; /* number of redistributors or 0 if single region */
	u32 free_index; /* index of the next free redistributor */
	struct list_head list;
};

struct vgic_dist {
	bool			in_kernel;
	bool			ready;
	bool			initialized;

	/* vGIC model the kernel emulates for the guest (GICv2 or GICv3) */
	u32			vgic_model;

	/* Implementation revision as reported in the GICD_IIDR */
	u32			implementation_rev;
#define KVM_VGIC_IMP_REV_2	2 /* GICv2 restorable groups */
#define KVM_VGIC_IMP_REV_3	3 /* GICv3 GICR_CTLR.{IW,CES,RWP} */
#define KVM_VGIC_IMP_REV_LATEST	KVM_VGIC_IMP_REV_3

	/* Userspace can write to GICv2 IGROUPR */
	bool			v2_groups_user_writable;

	/* Do injected MSIs require an additional device ID? */
	bool			msis_require_devid;

	int			nr_spis;

	/* base addresses in guest physical address space: */
	gpa_t			vgic_dist_base;		/* distributor */
	union {
		/* either a GICv2 CPU interface */
		gpa_t			vgic_cpu_base;
		/* or a number of GICv3 redistributor regions */
		struct list_head rd_regions;
	};

	/* distributor enabled */
	bool			enabled;

	/* Wants SGIs without active state */
	bool			nassgireq;

	struct vgic_irq		*spis;

	struct vgic_io_device	dist_iodev;

	bool			has_its;
	bool			table_write_in_progress;

	/*
	 * Contains the attributes and gpa of the LPI configuration table.
	 * Since we report GICR_TYPER.CommonLPIAff as 0b00, we can share
	 * one address across all redistributors.
	 * GICv3 spec: IHI 0069E 6.1.1 "LPI Configuration tables"
	 */
	u64			propbaser;

	/* Protects the lpi_list and the count value below. */
	raw_spinlock_t		lpi_list_lock;
	struct list_head	lpi_list_head;
	int			lpi_list_count;

	/* LPI translation cache */
	struct list_head	lpi_translation_cache;

	/* used by vgic-debug */
	struct vgic_state_iter *iter;

	/*
	 * GICv4 ITS per-VM data, containing the IRQ domain, the VPE
	 * array, the property table pointer as well as allocation
	 * data. This essentially ties the Linux IRQ core and ITS
	 * together, and avoids leaking KVM's data structures anywhere
	 * else.
	 */
	struct its_vm		its_vm;
};

struct vgic_v2_cpu_if {
	u32		vgic_hcr;
	u32		vgic_vmcr;
	u32		vgic_apr;
	u32		vgic_lr[VGIC_V2_MAX_LRS];

	unsigned int used_lrs;
};

struct vgic_v3_cpu_if {
	u32		vgic_hcr;
	u32		vgic_vmcr;
	u32		vgic_sre;	/* Restored only, change ignored */
	u32		vgic_ap0r[4];
	u32		vgic_ap1r[4];
	u64		vgic_lr[VGIC_V3_MAX_LRS];

	/*
	 * GICv4 ITS per-VPE data, containing the doorbell IRQ, the
	 * pending table pointer, the its_vm pointer and a few other
	 * HW specific things. As for the its_vm structure, this is
	 * linking the Linux IRQ subsystem and the ITS together.
	 */
	struct its_vpe	its_vpe;

	unsigned int used_lrs;
};

struct vgic_cpu {
	/* CPU vif control registers for world switch */
	union {
		struct vgic_v2_cpu_if	vgic_v2;
		struct vgic_v3_cpu_if	vgic_v3;
	};

	struct vgic_irq private_irqs[VGIC_NR_PRIVATE_IRQS];

	raw_spinlock_t ap_list_lock;	/* Protects the ap_list */

	/*
	 * List of IRQs that this VCPU should consider because they are either
	 * Active or Pending (hence the name; AP list), or because they recently
	 * were one of the two and need to be migrated off this list to another
	 * VCPU.
	 */
	struct list_head ap_list_head;

	/*
	 * Members below are used with GICv3 emulation only and represent
	 * parts of the redistributor.
	 */
	struct vgic_io_device	rd_iodev;
	struct vgic_redist_region *rdreg;
	u32 rdreg_index;
	atomic_t syncr_busy;

	/* Contains the attributes and gpa of the LPI pending tables. */
	u64 pendbaser;
	/* GICR_CTLR.{ENABLE_LPIS,RWP} */
	atomic_t ctlr;

	/* Cache guest priority bits */
	u32 num_pri_bits;

	/* Cache guest interrupt ID bits */
	u32 num_id_bits;
};

extern struct static_key_false vgic_v2_cpuif_trap;
extern struct static_key_false vgic_v3_cpuif_trap;

int kvm_set_legacy_vgic_v2_addr(struct kvm *kvm, struct kvm_arm_device_addr *dev_addr);
void kvm_vgic_early_init(struct kvm *kvm);
int kvm_vgic_vcpu_init(struct kvm_vcpu *vcpu);
int kvm_vgic_create(struct kvm *kvm, u32 type);
void kvm_vgic_destroy(struct kvm *kvm);
void kvm_vgic_vcpu_destroy(struct kvm_vcpu *vcpu);
int kvm_vgic_map_resources(struct kvm *kvm);
int kvm_vgic_hyp_init(void);
void kvm_vgic_init_cpu_hardware(void);

int kvm_vgic_inject_irq(struct kvm *kvm, int cpuid, unsigned int intid,
			bool level, void *owner);
int kvm_vgic_map_phys_irq(struct kvm_vcpu *vcpu, unsigned int host_irq,
			  u32 vintid, struct irq_ops *ops);
int kvm_vgic_unmap_phys_irq(struct kvm_vcpu *vcpu, unsigned int vintid);
bool kvm_vgic_map_is_active(struct kvm_vcpu *vcpu, unsigned int vintid);

int kvm_vgic_vcpu_pending_irq(struct kvm_vcpu *vcpu);

void kvm_vgic_load(struct kvm_vcpu *vcpu);
void kvm_vgic_put(struct kvm_vcpu *vcpu);
void kvm_vgic_vmcr_sync(struct kvm_vcpu *vcpu);

#define irqchip_in_kernel(k)	(!!((k)->arch.vgic.in_kernel))
#define vgic_initialized(k)	((k)->arch.vgic.initialized)
#define vgic_ready(k)		((k)->arch.vgic.ready)
#define vgic_valid_spi(k, i)	(((i) >= VGIC_NR_PRIVATE_IRQS) && \
			((i) < (k)->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS))

bool kvm_vcpu_has_pending_irqs(struct kvm_vcpu *vcpu);
void kvm_vgic_sync_hwstate(struct kvm_vcpu *vcpu);
void kvm_vgic_flush_hwstate(struct kvm_vcpu *vcpu);
void kvm_vgic_reset_mapped_irq(struct kvm_vcpu *vcpu, u32 vintid);

void vgic_v3_dispatch_sgi(struct kvm_vcpu *vcpu, u64 reg, bool allow_group1);

/**
 * kvm_vgic_get_max_vcpus - Get the maximum number of VCPUs allowed by HW
 *
 * The host's GIC naturally limits the maximum amount of VCPUs a guest
 * can use.
 */
static inline int kvm_vgic_get_max_vcpus(void)
{
	return kvm_vgic_global_state.max_gic_vcpus;
}

/**
 * kvm_vgic_setup_default_irq_routing:
 * Setup a default flat gsi routing table mapping all SPIs
 */
int kvm_vgic_setup_default_irq_routing(struct kvm *kvm);

int kvm_vgic_set_owner(struct kvm_vcpu *vcpu, unsigned int intid, void *owner);

struct kvm_kernel_irq_routing_entry;

int kvm_vgic_v4_set_forwarding(struct kvm *kvm, int irq,
			       struct kvm_kernel_irq_routing_entry *irq_entry);

int kvm_vgic_v4_unset_forwarding(struct kvm *kvm, int irq,
				 struct kvm_kernel_irq_routing_entry *irq_entry);

int vgic_v4_load(struct kvm_vcpu *vcpu);
void vgic_v4_commit(struct kvm_vcpu *vcpu);
int vgic_v4_put(struct kvm_vcpu *vcpu, bool need_db);

#endif /* __KVM_ARM_VGIC_H */

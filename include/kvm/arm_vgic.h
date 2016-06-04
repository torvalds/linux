/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __ASM_ARM_KVM_VGIC_H
#define __ASM_ARM_KVM_VGIC_H

#include <linux/kernel.h>
#include <linux/kvm.h>
#include <linux/irqreturn.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <kvm/iodev.h>
#include <linux/irqchip/arm-gic-common.h>

#define VGIC_NR_IRQS_LEGACY	256
#define VGIC_NR_SGIS		16
#define VGIC_NR_PPIS		16
#define VGIC_NR_PRIVATE_IRQS	(VGIC_NR_SGIS + VGIC_NR_PPIS)

#define VGIC_V2_MAX_LRS		(1 << 6)
#define VGIC_V3_MAX_LRS		16
#define VGIC_MAX_IRQS		1024
#define VGIC_V2_MAX_CPUS	8
#define VGIC_V3_MAX_CPUS	255

#if (VGIC_NR_IRQS_LEGACY & 31)
#error "VGIC_NR_IRQS must be a multiple of 32"
#endif

#if (VGIC_NR_IRQS_LEGACY > VGIC_MAX_IRQS)
#error "VGIC_NR_IRQS must be <= 1024"
#endif

/*
 * The GIC distributor registers describing interrupts have two parts:
 * - 32 per-CPU interrupts (SGI + PPI)
 * - a bunch of shared interrupts (SPI)
 */
struct vgic_bitmap {
	/*
	 * - One UL per VCPU for private interrupts (assumes UL is at
	 *   least 32 bits)
	 * - As many UL as necessary for shared interrupts.
	 *
	 * The private interrupts are accessed via the "private"
	 * field, one UL per vcpu (the state for vcpu n is in
	 * private[n]). The shared interrupts are accessed via the
	 * "shared" pointer (IRQn state is at bit n-32 in the bitmap).
	 */
	unsigned long *private;
	unsigned long *shared;
};

struct vgic_bytemap {
	/*
	 * - 8 u32 per VCPU for private interrupts
	 * - As many u32 as necessary for shared interrupts.
	 *
	 * The private interrupts are accessed via the "private"
	 * field, (the state for vcpu n is in private[n*8] to
	 * private[n*8 + 7]). The shared interrupts are accessed via
	 * the "shared" pointer (IRQn state is at byte (n-32)%4 of the
	 * shared[(n-32)/4] word).
	 */
	u32 *private;
	u32 *shared;
};

struct kvm_vcpu;

enum vgic_type {
	VGIC_V2,		/* Good ol' GICv2 */
	VGIC_V3,		/* New fancy GICv3 */
};

#define LR_STATE_PENDING	(1 << 0)
#define LR_STATE_ACTIVE		(1 << 1)
#define LR_STATE_MASK		(3 << 0)
#define LR_EOI_INT		(1 << 2)
#define LR_HW			(1 << 3)

struct vgic_lr {
	unsigned irq:10;
	union {
		unsigned hwirq:10;
		unsigned source:3;
	};
	unsigned state:4;
};

struct vgic_vmcr {
	u32	ctlr;
	u32	abpr;
	u32	bpr;
	u32	pmr;
};

struct vgic_ops {
	struct vgic_lr	(*get_lr)(const struct kvm_vcpu *, int);
	void	(*set_lr)(struct kvm_vcpu *, int, struct vgic_lr);
	u64	(*get_elrsr)(const struct kvm_vcpu *vcpu);
	u64	(*get_eisr)(const struct kvm_vcpu *vcpu);
	void	(*clear_eisr)(struct kvm_vcpu *vcpu);
	u32	(*get_interrupt_status)(const struct kvm_vcpu *vcpu);
	void	(*enable_underflow)(struct kvm_vcpu *vcpu);
	void	(*disable_underflow)(struct kvm_vcpu *vcpu);
	void	(*get_vmcr)(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);
	void	(*set_vmcr)(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);
	void	(*enable)(struct kvm_vcpu *vcpu);
};

struct vgic_params {
	/* vgic type */
	enum vgic_type	type;
	/* Physical address of vgic virtual cpu interface */
	phys_addr_t	vcpu_base;
	/* Number of list registers */
	u32		nr_lr;
	/* Interrupt number */
	unsigned int	maint_irq;
	/* Virtual control interface base address */
	void __iomem	*vctrl_base;
	int		max_gic_vcpus;
	/* Only needed for the legacy KVM_CREATE_IRQCHIP */
	bool		can_emulate_gicv2;
};

struct vgic_vm_ops {
	bool	(*queue_sgi)(struct kvm_vcpu *, int irq);
	void	(*add_sgi_source)(struct kvm_vcpu *, int irq, int source);
	int	(*init_model)(struct kvm *);
	int	(*map_resources)(struct kvm *, const struct vgic_params *);
};

struct vgic_io_device {
	gpa_t addr;
	int len;
	const struct vgic_io_range *reg_ranges;
	struct kvm_vcpu *redist_vcpu;
	struct kvm_io_device dev;
};

struct irq_phys_map {
	u32			virt_irq;
	u32			phys_irq;
};

struct irq_phys_map_entry {
	struct list_head	entry;
	struct rcu_head		rcu;
	struct irq_phys_map	map;
};

struct vgic_dist {
	spinlock_t		lock;
	bool			in_kernel;
	bool			ready;

	/* vGIC model the kernel emulates for the guest (GICv2 or GICv3) */
	u32			vgic_model;

	int			nr_cpus;
	int			nr_irqs;

	/* Virtual control interface mapping */
	void __iomem		*vctrl_base;

	/* Distributor and vcpu interface mapping in the guest */
	phys_addr_t		vgic_dist_base;
	/* GICv2 and GICv3 use different mapped register blocks */
	union {
		phys_addr_t		vgic_cpu_base;
		phys_addr_t		vgic_redist_base;
	};

	/* Distributor enabled */
	u32			enabled;

	/* Interrupt enabled (one bit per IRQ) */
	struct vgic_bitmap	irq_enabled;

	/* Level-triggered interrupt external input is asserted */
	struct vgic_bitmap	irq_level;

	/*
	 * Interrupt state is pending on the distributor
	 */
	struct vgic_bitmap	irq_pending;

	/*
	 * Tracks writes to GICD_ISPENDRn and GICD_ICPENDRn for level-triggered
	 * interrupts.  Essentially holds the state of the flip-flop in
	 * Figure 4-10 on page 4-101 in ARM IHI 0048B.b.
	 * Once set, it is only cleared for level-triggered interrupts on
	 * guest ACKs (when we queue it) or writes to GICD_ICPENDRn.
	 */
	struct vgic_bitmap	irq_soft_pend;

	/* Level-triggered interrupt queued on VCPU interface */
	struct vgic_bitmap	irq_queued;

	/* Interrupt was active when unqueue from VCPU interface */
	struct vgic_bitmap	irq_active;

	/* Interrupt priority. Not used yet. */
	struct vgic_bytemap	irq_priority;

	/* Level/edge triggered */
	struct vgic_bitmap	irq_cfg;

	/*
	 * Source CPU per SGI and target CPU:
	 *
	 * Each byte represent a SGI observable on a VCPU, each bit of
	 * this byte indicating if the corresponding VCPU has
	 * generated this interrupt. This is a GICv2 feature only.
	 *
	 * For VCPUn (n < 8), irq_sgi_sources[n*16] to [n*16 + 15] are
	 * the SGIs observable on VCPUn.
	 */
	u8			*irq_sgi_sources;

	/*
	 * Target CPU for each SPI:
	 *
	 * Array of available SPI, each byte indicating the target
	 * VCPU for SPI. IRQn (n >=32) is at irq_spi_cpu[n-32].
	 */
	u8			*irq_spi_cpu;

	/*
	 * Reverse lookup of irq_spi_cpu for faster compute pending:
	 *
	 * Array of bitmaps, one per VCPU, describing if IRQn is
	 * routed to a particular VCPU.
	 */
	struct vgic_bitmap	*irq_spi_target;

	/* Target MPIDR for each IRQ (needed for GICv3 IROUTERn) only */
	u32			*irq_spi_mpidr;

	/* Bitmap indicating which CPU has something pending */
	unsigned long		*irq_pending_on_cpu;

	/* Bitmap indicating which CPU has active IRQs */
	unsigned long		*irq_active_on_cpu;

	struct vgic_vm_ops	vm_ops;
	struct vgic_io_device	dist_iodev;
	struct vgic_io_device	*redist_iodevs;

	/* Virtual irq to hwirq mapping */
	spinlock_t		irq_phys_map_lock;
	struct list_head	irq_phys_map_list;
};

struct vgic_v2_cpu_if {
	u32		vgic_hcr;
	u32		vgic_vmcr;
	u32		vgic_misr;	/* Saved only */
	u64		vgic_eisr;	/* Saved only */
	u64		vgic_elrsr;	/* Saved only */
	u32		vgic_apr;
	u32		vgic_lr[VGIC_V2_MAX_LRS];
};

struct vgic_v3_cpu_if {
#ifdef CONFIG_KVM_ARM_VGIC_V3
	u32		vgic_hcr;
	u32		vgic_vmcr;
	u32		vgic_sre;	/* Restored only, change ignored */
	u32		vgic_misr;	/* Saved only */
	u32		vgic_eisr;	/* Saved only */
	u32		vgic_elrsr;	/* Saved only */
	u32		vgic_ap0r[4];
	u32		vgic_ap1r[4];
	u64		vgic_lr[VGIC_V3_MAX_LRS];
#endif
};

struct vgic_cpu {
	/* Pending/active/both interrupts on this VCPU */
	DECLARE_BITMAP(pending_percpu, VGIC_NR_PRIVATE_IRQS);
	DECLARE_BITMAP(active_percpu, VGIC_NR_PRIVATE_IRQS);
	DECLARE_BITMAP(pend_act_percpu, VGIC_NR_PRIVATE_IRQS);

	/* Pending/active/both shared interrupts, dynamically sized */
	unsigned long	*pending_shared;
	unsigned long   *active_shared;
	unsigned long   *pend_act_shared;

	/* Number of list registers on this CPU */
	int		nr_lr;

	/* CPU vif control registers for world switch */
	union {
		struct vgic_v2_cpu_if	vgic_v2;
		struct vgic_v3_cpu_if	vgic_v3;
	};

	/* Protected by the distributor's irq_phys_map_lock */
	struct list_head	irq_phys_map_list;

	u64		live_lrs;
};

#define LR_EMPTY	0xff

#define INT_STATUS_EOI		(1 << 0)
#define INT_STATUS_UNDERFLOW	(1 << 1)

struct kvm;
struct kvm_vcpu;

int kvm_vgic_addr(struct kvm *kvm, unsigned long type, u64 *addr, bool write);
int kvm_vgic_hyp_init(void);
int kvm_vgic_map_resources(struct kvm *kvm);
int kvm_vgic_get_max_vcpus(void);
void kvm_vgic_early_init(struct kvm *kvm);
int kvm_vgic_create(struct kvm *kvm, u32 type);
void kvm_vgic_destroy(struct kvm *kvm);
void kvm_vgic_vcpu_early_init(struct kvm_vcpu *vcpu);
void kvm_vgic_vcpu_destroy(struct kvm_vcpu *vcpu);
void kvm_vgic_flush_hwstate(struct kvm_vcpu *vcpu);
void kvm_vgic_sync_hwstate(struct kvm_vcpu *vcpu);
int kvm_vgic_inject_irq(struct kvm *kvm, int cpuid, unsigned int irq_num,
			bool level);
int kvm_vgic_inject_mapped_irq(struct kvm *kvm, int cpuid,
			       unsigned int virt_irq, bool level);
void vgic_v3_dispatch_sgi(struct kvm_vcpu *vcpu, u64 reg);
int kvm_vgic_vcpu_pending_irq(struct kvm_vcpu *vcpu);
struct irq_phys_map *kvm_vgic_map_phys_irq(struct kvm_vcpu *vcpu,
					   int virt_irq, int phys_irq);
int kvm_vgic_unmap_phys_irq(struct kvm_vcpu *vcpu, unsigned int virt_irq);
bool kvm_vgic_map_is_active(struct kvm_vcpu *vcpu, unsigned int virt_irq);

#define irqchip_in_kernel(k)	(!!((k)->arch.vgic.in_kernel))
#define vgic_initialized(k)	(!!((k)->arch.vgic.nr_cpus))
#define vgic_ready(k)		((k)->arch.vgic.ready)

int vgic_v2_probe(const struct gic_kvm_info *gic_kvm_info,
		  const struct vgic_ops **ops,
		  const struct vgic_params **params);
#ifdef CONFIG_KVM_ARM_VGIC_V3
int vgic_v3_probe(const struct gic_kvm_info *gic_kvm_info,
		  const struct vgic_ops **ops,
		  const struct vgic_params **params);
#else
static inline int vgic_v3_probe(const struct gic_kvm_info *gic_kvm_info,
				const struct vgic_ops **ops,
				const struct vgic_params **params)
{
	return -ENODEV;
}
#endif

#endif

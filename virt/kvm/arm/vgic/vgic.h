/*
 * Copyright (C) 2015, 2016 ARM Ltd.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __KVM_ARM_VGIC_NEW_H__
#define __KVM_ARM_VGIC_NEW_H__

#include <linux/irqchip/arm-gic-common.h>

#define PRODUCT_ID_KVM		0x4b	/* ASCII code K */
#define IMPLEMENTER_ARM		0x43b

#define VGIC_ADDR_UNDEF		(-1)
#define IS_VGIC_ADDR_UNDEF(_x)  ((_x) == VGIC_ADDR_UNDEF)

#define INTERRUPT_ID_BITS_SPIS	10
#define INTERRUPT_ID_BITS_ITS	16
#define VGIC_PRI_BITS		5

#define vgic_irq_is_sgi(intid) ((intid) < VGIC_NR_SGIS)

#define VGIC_AFFINITY_0_SHIFT 0
#define VGIC_AFFINITY_0_MASK (0xffUL << VGIC_AFFINITY_0_SHIFT)
#define VGIC_AFFINITY_1_SHIFT 8
#define VGIC_AFFINITY_1_MASK (0xffUL << VGIC_AFFINITY_1_SHIFT)
#define VGIC_AFFINITY_2_SHIFT 16
#define VGIC_AFFINITY_2_MASK (0xffUL << VGIC_AFFINITY_2_SHIFT)
#define VGIC_AFFINITY_3_SHIFT 24
#define VGIC_AFFINITY_3_MASK (0xffUL << VGIC_AFFINITY_3_SHIFT)

#define VGIC_AFFINITY_LEVEL(reg, level) \
	((((reg) & VGIC_AFFINITY_## level ##_MASK) \
	>> VGIC_AFFINITY_## level ##_SHIFT) << MPIDR_LEVEL_SHIFT(level))

/*
 * The Userspace encodes the affinity differently from the MPIDR,
 * Below macro converts vgic userspace format to MPIDR reg format.
 */
#define VGIC_TO_MPIDR(val) (VGIC_AFFINITY_LEVEL(val, 0) | \
			    VGIC_AFFINITY_LEVEL(val, 1) | \
			    VGIC_AFFINITY_LEVEL(val, 2) | \
			    VGIC_AFFINITY_LEVEL(val, 3))

/*
 * As per Documentation/virtual/kvm/devices/arm-vgic-v3.txt,
 * below macros are defined for CPUREG encoding.
 */
#define KVM_REG_ARM_VGIC_SYSREG_OP0_MASK   0x000000000000c000
#define KVM_REG_ARM_VGIC_SYSREG_OP0_SHIFT  14
#define KVM_REG_ARM_VGIC_SYSREG_OP1_MASK   0x0000000000003800
#define KVM_REG_ARM_VGIC_SYSREG_OP1_SHIFT  11
#define KVM_REG_ARM_VGIC_SYSREG_CRN_MASK   0x0000000000000780
#define KVM_REG_ARM_VGIC_SYSREG_CRN_SHIFT  7
#define KVM_REG_ARM_VGIC_SYSREG_CRM_MASK   0x0000000000000078
#define KVM_REG_ARM_VGIC_SYSREG_CRM_SHIFT  3
#define KVM_REG_ARM_VGIC_SYSREG_OP2_MASK   0x0000000000000007
#define KVM_REG_ARM_VGIC_SYSREG_OP2_SHIFT  0

#define KVM_DEV_ARM_VGIC_SYSREG_MASK (KVM_REG_ARM_VGIC_SYSREG_OP0_MASK | \
				      KVM_REG_ARM_VGIC_SYSREG_OP1_MASK | \
				      KVM_REG_ARM_VGIC_SYSREG_CRN_MASK | \
				      KVM_REG_ARM_VGIC_SYSREG_CRM_MASK | \
				      KVM_REG_ARM_VGIC_SYSREG_OP2_MASK)

static inline bool irq_is_pending(struct vgic_irq *irq)
{
	if (irq->config == VGIC_CONFIG_EDGE)
		return irq->pending_latch;
	else
		return irq->pending_latch || irq->line_level;
}

/*
 * This struct provides an intermediate representation of the fields contained
 * in the GICH_VMCR and ICH_VMCR registers, such that code exporting the GIC
 * state to userspace can generate either GICv2 or GICv3 CPU interface
 * registers regardless of the hardware backed GIC used.
 */
struct vgic_vmcr {
	u32	ctlr;
	u32	abpr;
	u32	bpr;
	u32	pmr;  /* Priority mask field in the GICC_PMR and
		       * ICC_PMR_EL1 priority field format */
	/* Below member variable are valid only for GICv3 */
	u32	grpen0;
	u32	grpen1;
};

struct vgic_reg_attr {
	struct kvm_vcpu *vcpu;
	gpa_t addr;
};

int vgic_v3_parse_attr(struct kvm_device *dev, struct kvm_device_attr *attr,
		       struct vgic_reg_attr *reg_attr);
int vgic_v2_parse_attr(struct kvm_device *dev, struct kvm_device_attr *attr,
		       struct vgic_reg_attr *reg_attr);
const struct vgic_register_region *
vgic_get_mmio_region(struct kvm_vcpu *vcpu, struct vgic_io_device *iodev,
		     gpa_t addr, int len);
struct vgic_irq *vgic_get_irq(struct kvm *kvm, struct kvm_vcpu *vcpu,
			      u32 intid);
void vgic_put_irq(struct kvm *kvm, struct vgic_irq *irq);
bool vgic_queue_irq_unlock(struct kvm *kvm, struct vgic_irq *irq);
void vgic_kick_vcpus(struct kvm *kvm);

int vgic_check_ioaddr(struct kvm *kvm, phys_addr_t *ioaddr,
		      phys_addr_t addr, phys_addr_t alignment);

void vgic_v2_process_maintenance(struct kvm_vcpu *vcpu);
void vgic_v2_fold_lr_state(struct kvm_vcpu *vcpu);
void vgic_v2_populate_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq, int lr);
void vgic_v2_clear_lr(struct kvm_vcpu *vcpu, int lr);
void vgic_v2_set_underflow(struct kvm_vcpu *vcpu);
int vgic_v2_has_attr_regs(struct kvm_device *dev, struct kvm_device_attr *attr);
int vgic_v2_dist_uaccess(struct kvm_vcpu *vcpu, bool is_write,
			 int offset, u32 *val);
int vgic_v2_cpuif_uaccess(struct kvm_vcpu *vcpu, bool is_write,
			  int offset, u32 *val);
void vgic_v2_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);
void vgic_v2_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);
void vgic_v2_enable(struct kvm_vcpu *vcpu);
int vgic_v2_probe(const struct gic_kvm_info *info);
int vgic_v2_map_resources(struct kvm *kvm);
int vgic_register_dist_iodev(struct kvm *kvm, gpa_t dist_base_address,
			     enum vgic_type);

void vgic_v2_init_lrs(void);

static inline void vgic_get_irq_kref(struct vgic_irq *irq)
{
	if (irq->intid < VGIC_MIN_LPI)
		return;

	kref_get(&irq->refcount);
}

void vgic_v3_process_maintenance(struct kvm_vcpu *vcpu);
void vgic_v3_fold_lr_state(struct kvm_vcpu *vcpu);
void vgic_v3_populate_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq, int lr);
void vgic_v3_clear_lr(struct kvm_vcpu *vcpu, int lr);
void vgic_v3_set_underflow(struct kvm_vcpu *vcpu);
void vgic_v3_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);
void vgic_v3_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);
void vgic_v3_enable(struct kvm_vcpu *vcpu);
int vgic_v3_probe(const struct gic_kvm_info *info);
int vgic_v3_map_resources(struct kvm *kvm);
int vgic_register_redist_iodevs(struct kvm *kvm, gpa_t dist_base_address);

int vgic_register_its_iodevs(struct kvm *kvm);
bool vgic_has_its(struct kvm *kvm);
int kvm_vgic_register_its_device(void);
void vgic_enable_lpis(struct kvm_vcpu *vcpu);
int vgic_its_inject_msi(struct kvm *kvm, struct kvm_msi *msi);
int vgic_v3_has_attr_regs(struct kvm_device *dev, struct kvm_device_attr *attr);
int vgic_v3_dist_uaccess(struct kvm_vcpu *vcpu, bool is_write,
			 int offset, u32 *val);
int vgic_v3_redist_uaccess(struct kvm_vcpu *vcpu, bool is_write,
			 int offset, u32 *val);
int vgic_v3_cpu_sysregs_uaccess(struct kvm_vcpu *vcpu, bool is_write,
			 u64 id, u64 *val);
int vgic_v3_has_cpu_sysregs_attr(struct kvm_vcpu *vcpu, bool is_write, u64 id,
				u64 *reg);
int vgic_v3_line_level_info_uaccess(struct kvm_vcpu *vcpu, bool is_write,
				    u32 intid, u64 *val);
int kvm_register_vgic_device(unsigned long type);
void vgic_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);
void vgic_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);
int vgic_lazy_init(struct kvm *kvm);
int vgic_init(struct kvm *kvm);

int vgic_debug_init(struct kvm *kvm);
int vgic_debug_destroy(struct kvm *kvm);

#endif

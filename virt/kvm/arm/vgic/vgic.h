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
#define VGIC_PRI_BITS		5

#define vgic_irq_is_sgi(intid) ((intid) < VGIC_NR_SGIS)

struct vgic_vmcr {
	u32	ctlr;
	u32	abpr;
	u32	bpr;
	u32	pmr;
};

struct vgic_irq *vgic_get_irq(struct kvm *kvm, struct kvm_vcpu *vcpu,
			      u32 intid);
bool vgic_queue_irq_unlock(struct kvm *kvm, struct vgic_irq *irq);
void vgic_kick_vcpus(struct kvm *kvm);

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

#ifdef CONFIG_KVM_ARM_VGIC_V3
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
#else
static inline void vgic_v3_process_maintenance(struct kvm_vcpu *vcpu)
{
}

static inline void vgic_v3_fold_lr_state(struct kvm_vcpu *vcpu)
{
}

static inline void vgic_v3_populate_lr(struct kvm_vcpu *vcpu,
				       struct vgic_irq *irq, int lr)
{
}

static inline void vgic_v3_clear_lr(struct kvm_vcpu *vcpu, int lr)
{
}

static inline void vgic_v3_set_underflow(struct kvm_vcpu *vcpu)
{
}

static inline
void vgic_v3_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr)
{
}

static inline
void vgic_v3_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr)
{
}

static inline void vgic_v3_enable(struct kvm_vcpu *vcpu)
{
}

static inline int vgic_v3_probe(const struct gic_kvm_info *info)
{
	return -ENODEV;
}

static inline int vgic_v3_map_resources(struct kvm *kvm)
{
	return -ENODEV;
}

static inline int vgic_register_redist_iodevs(struct kvm *kvm,
					      gpa_t dist_base_address)
{
	return -ENODEV;
}
#endif

int kvm_register_vgic_device(unsigned long type);
int vgic_lazy_init(struct kvm *kvm);
int vgic_init(struct kvm *kvm);

#endif

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

#define PRODUCT_ID_KVM		0x4b	/* ASCII code K */
#define IMPLEMENTER_ARM		0x43b

#define VGIC_PRI_BITS		5

#define vgic_irq_is_sgi(intid) ((intid) < VGIC_NR_SGIS)

struct vgic_irq *vgic_get_irq(struct kvm *kvm, struct kvm_vcpu *vcpu,
			      u32 intid);
bool vgic_queue_irq_unlock(struct kvm *kvm, struct vgic_irq *irq);
void vgic_kick_vcpus(struct kvm *kvm);

void vgic_v2_process_maintenance(struct kvm_vcpu *vcpu);
void vgic_v2_fold_lr_state(struct kvm_vcpu *vcpu);
void vgic_v2_populate_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq, int lr);
void vgic_v2_clear_lr(struct kvm_vcpu *vcpu, int lr);
void vgic_v2_set_underflow(struct kvm_vcpu *vcpu);
int vgic_register_dist_iodev(struct kvm *kvm, gpa_t dist_base_address,
			     enum vgic_type);

#ifdef CONFIG_KVM_ARM_VGIC_V3
void vgic_v3_process_maintenance(struct kvm_vcpu *vcpu);
void vgic_v3_fold_lr_state(struct kvm_vcpu *vcpu);
void vgic_v3_populate_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq, int lr);
void vgic_v3_clear_lr(struct kvm_vcpu *vcpu, int lr);
void vgic_v3_set_underflow(struct kvm_vcpu *vcpu);
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
#endif

#endif

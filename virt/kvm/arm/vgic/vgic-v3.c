/*
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/irqchip/arm-gic-v3.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include "vgic.h"

void vgic_v3_process_maintenance(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpuif = &vcpu->arch.vgic_cpu.vgic_v3;
	u32 model = vcpu->kvm->arch.vgic.vgic_model;

	if (cpuif->vgic_misr & ICH_MISR_EOI) {
		unsigned long eisr_bmap = cpuif->vgic_eisr;
		int lr;

		for_each_set_bit(lr, &eisr_bmap, kvm_vgic_global_state.nr_lr) {
			u32 intid;
			u64 val = cpuif->vgic_lr[lr];

			if (model == KVM_DEV_TYPE_ARM_VGIC_V3)
				intid = val & ICH_LR_VIRTUAL_ID_MASK;
			else
				intid = val & GICH_LR_VIRTUALID;

			WARN_ON(cpuif->vgic_lr[lr] & ICH_LR_STATE);

			kvm_notify_acked_irq(vcpu->kvm, 0,
					     intid - VGIC_NR_PRIVATE_IRQS);
		}

		/*
		 * In the next iterations of the vcpu loop, if we sync
		 * the vgic state after flushing it, but before
		 * entering the guest (this happens for pending
		 * signals and vmid rollovers), then make sure we
		 * don't pick up any old maintenance interrupts here.
		 */
		cpuif->vgic_eisr = 0;
	}

	cpuif->vgic_hcr &= ~ICH_HCR_UIE;
}

void vgic_v3_set_underflow(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpuif = &vcpu->arch.vgic_cpu.vgic_v3;

	cpuif->vgic_hcr |= ICH_HCR_UIE;
}

void vgic_v3_fold_lr_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpuif = &vcpu->arch.vgic_cpu.vgic_v3;
	u32 model = vcpu->kvm->arch.vgic.vgic_model;
	int lr;

	for (lr = 0; lr < vcpu->arch.vgic_cpu.used_lrs; lr++) {
		u64 val = cpuif->vgic_lr[lr];
		u32 intid;
		struct vgic_irq *irq;

		if (model == KVM_DEV_TYPE_ARM_VGIC_V3)
			intid = val & ICH_LR_VIRTUAL_ID_MASK;
		else
			intid = val & GICH_LR_VIRTUALID;
		irq = vgic_get_irq(vcpu->kvm, vcpu, intid);

		spin_lock(&irq->irq_lock);

		/* Always preserve the active bit */
		irq->active = !!(val & ICH_LR_ACTIVE_BIT);

		/* Edge is the only case where we preserve the pending bit */
		if (irq->config == VGIC_CONFIG_EDGE &&
		    (val & ICH_LR_PENDING_BIT)) {
			irq->pending = true;

			if (vgic_irq_is_sgi(intid) &&
			    model == KVM_DEV_TYPE_ARM_VGIC_V2) {
				u32 cpuid = val & GICH_LR_PHYSID_CPUID;

				cpuid >>= GICH_LR_PHYSID_CPUID_SHIFT;
				irq->source |= (1 << cpuid);
			}
		}

		/* Clear soft pending state when level irqs have been acked */
		if (irq->config == VGIC_CONFIG_LEVEL &&
		    !(val & ICH_LR_PENDING_BIT)) {
			irq->soft_pending = false;
			irq->pending = irq->line_level;
		}

		spin_unlock(&irq->irq_lock);
	}
}

/* Requires the irq to be locked already */
void vgic_v3_populate_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq, int lr)
{
	u32 model = vcpu->kvm->arch.vgic.vgic_model;
	u64 val = irq->intid;

	if (irq->pending) {
		val |= ICH_LR_PENDING_BIT;

		if (irq->config == VGIC_CONFIG_EDGE)
			irq->pending = false;

		if (vgic_irq_is_sgi(irq->intid) &&
		    model == KVM_DEV_TYPE_ARM_VGIC_V2) {
			u32 src = ffs(irq->source);

			BUG_ON(!src);
			val |= (src - 1) << GICH_LR_PHYSID_CPUID_SHIFT;
			irq->source &= ~(1 << (src - 1));
			if (irq->source)
				irq->pending = true;
		}
	}

	if (irq->active)
		val |= ICH_LR_ACTIVE_BIT;

	if (irq->hw) {
		val |= ICH_LR_HW;
		val |= ((u64)irq->hwintid) << ICH_LR_PHYS_ID_SHIFT;
	} else {
		if (irq->config == VGIC_CONFIG_LEVEL)
			val |= ICH_LR_EOI;
	}

	/*
	 * We currently only support Group1 interrupts, which is a
	 * known defect. This needs to be addressed at some point.
	 */
	if (model == KVM_DEV_TYPE_ARM_VGIC_V3)
		val |= ICH_LR_GROUP;

	val |= (u64)irq->priority << ICH_LR_PRIORITY_SHIFT;

	vcpu->arch.vgic_cpu.vgic_v3.vgic_lr[lr] = val;
}

void vgic_v3_clear_lr(struct kvm_vcpu *vcpu, int lr)
{
	vcpu->arch.vgic_cpu.vgic_v3.vgic_lr[lr] = 0;
}

void vgic_v3_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	u32 vmcr;

	vmcr  = (vmcrp->ctlr << ICH_VMCR_CTLR_SHIFT) & ICH_VMCR_CTLR_MASK;
	vmcr |= (vmcrp->abpr << ICH_VMCR_BPR1_SHIFT) & ICH_VMCR_BPR1_MASK;
	vmcr |= (vmcrp->bpr << ICH_VMCR_BPR0_SHIFT) & ICH_VMCR_BPR0_MASK;
	vmcr |= (vmcrp->pmr << ICH_VMCR_PMR_SHIFT) & ICH_VMCR_PMR_MASK;

	vcpu->arch.vgic_cpu.vgic_v3.vgic_vmcr = vmcr;
}

void vgic_v3_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	u32 vmcr = vcpu->arch.vgic_cpu.vgic_v3.vgic_vmcr;

	vmcrp->ctlr = (vmcr & ICH_VMCR_CTLR_MASK) >> ICH_VMCR_CTLR_SHIFT;
	vmcrp->abpr = (vmcr & ICH_VMCR_BPR1_MASK) >> ICH_VMCR_BPR1_SHIFT;
	vmcrp->bpr  = (vmcr & ICH_VMCR_BPR0_MASK) >> ICH_VMCR_BPR0_SHIFT;
	vmcrp->pmr  = (vmcr & ICH_VMCR_PMR_MASK) >> ICH_VMCR_PMR_SHIFT;
}

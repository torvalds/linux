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

#include <linux/irqchip/arm-gic.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <kvm/arm_vgic.h>
#include <asm/kvm_mmu.h>

#include "vgic.h"

/*
 * Call this function to convert a u64 value to an unsigned long * bitmask
 * in a way that works on both 32-bit and 64-bit LE and BE platforms.
 *
 * Warning: Calling this function may modify *val.
 */
static unsigned long *u64_to_bitmask(u64 *val)
{
#if defined(CONFIG_CPU_BIG_ENDIAN) && BITS_PER_LONG == 32
	*val = (*val >> 32) | (*val << 32);
#endif
	return (unsigned long *)val;
}

void vgic_v2_process_maintenance(struct kvm_vcpu *vcpu)
{
	struct vgic_v2_cpu_if *cpuif = &vcpu->arch.vgic_cpu.vgic_v2;

	if (cpuif->vgic_misr & GICH_MISR_EOI) {
		u64 eisr = cpuif->vgic_eisr;
		unsigned long *eisr_bmap = u64_to_bitmask(&eisr);
		int lr;

		for_each_set_bit(lr, eisr_bmap, kvm_vgic_global_state.nr_lr) {
			u32 intid = cpuif->vgic_lr[lr] & GICH_LR_VIRTUALID;

			WARN_ON(cpuif->vgic_lr[lr] & GICH_LR_STATE);

			kvm_notify_acked_irq(vcpu->kvm, 0,
					     intid - VGIC_NR_PRIVATE_IRQS);
		}
	}

	/* check and disable underflow maintenance IRQ */
	cpuif->vgic_hcr &= ~GICH_HCR_UIE;

	/*
	 * In the next iterations of the vcpu loop, if we sync the
	 * vgic state after flushing it, but before entering the guest
	 * (this happens for pending signals and vmid rollovers), then
	 * make sure we don't pick up any old maintenance interrupts
	 * here.
	 */
	cpuif->vgic_eisr = 0;
}

void vgic_v2_set_underflow(struct kvm_vcpu *vcpu)
{
	struct vgic_v2_cpu_if *cpuif = &vcpu->arch.vgic_cpu.vgic_v2;

	cpuif->vgic_hcr |= GICH_HCR_UIE;
}

/*
 * transfer the content of the LRs back into the corresponding ap_list:
 * - active bit is transferred as is
 * - pending bit is
 *   - transferred as is in case of edge sensitive IRQs
 *   - set to the line-level (resample time) for level sensitive IRQs
 */
void vgic_v2_fold_lr_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v2_cpu_if *cpuif = &vcpu->arch.vgic_cpu.vgic_v2;
	int lr;

	for (lr = 0; lr < vcpu->arch.vgic_cpu.used_lrs; lr++) {
		u32 val = cpuif->vgic_lr[lr];
		u32 intid = val & GICH_LR_VIRTUALID;
		struct vgic_irq *irq;

		irq = vgic_get_irq(vcpu->kvm, vcpu, intid);

		spin_lock(&irq->irq_lock);

		/* Always preserve the active bit */
		irq->active = !!(val & GICH_LR_ACTIVE_BIT);

		/* Edge is the only case where we preserve the pending bit */
		if (irq->config == VGIC_CONFIG_EDGE &&
		    (val & GICH_LR_PENDING_BIT)) {
			irq->pending = true;

			if (vgic_irq_is_sgi(intid)) {
				u32 cpuid = val & GICH_LR_PHYSID_CPUID;

				cpuid >>= GICH_LR_PHYSID_CPUID_SHIFT;
				irq->source |= (1 << cpuid);
			}
		}

		/* Clear soft pending state when level IRQs have been acked */
		if (irq->config == VGIC_CONFIG_LEVEL &&
		    !(val & GICH_LR_PENDING_BIT)) {
			irq->soft_pending = false;
			irq->pending = irq->line_level;
		}

		spin_unlock(&irq->irq_lock);
	}
}

/*
 * Populates the particular LR with the state of a given IRQ:
 * - for an edge sensitive IRQ the pending state is cleared in struct vgic_irq
 * - for a level sensitive IRQ the pending state value is unchanged;
 *   it is dictated directly by the input level
 *
 * If @irq describes an SGI with multiple sources, we choose the
 * lowest-numbered source VCPU and clear that bit in the source bitmap.
 *
 * The irq_lock must be held by the caller.
 */
void vgic_v2_populate_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq, int lr)
{
	u32 val = irq->intid;

	if (irq->pending) {
		val |= GICH_LR_PENDING_BIT;

		if (irq->config == VGIC_CONFIG_EDGE)
			irq->pending = false;

		if (vgic_irq_is_sgi(irq->intid)) {
			u32 src = ffs(irq->source);

			BUG_ON(!src);
			val |= (src - 1) << GICH_LR_PHYSID_CPUID_SHIFT;
			irq->source &= ~(1 << (src - 1));
			if (irq->source)
				irq->pending = true;
		}
	}

	if (irq->active)
		val |= GICH_LR_ACTIVE_BIT;

	if (irq->hw) {
		val |= GICH_LR_HW;
		val |= irq->hwintid << GICH_LR_PHYSID_CPUID_SHIFT;
	} else {
		if (irq->config == VGIC_CONFIG_LEVEL)
			val |= GICH_LR_EOI;
	}

	/* The GICv2 LR only holds five bits of priority. */
	val |= (irq->priority >> 3) << GICH_LR_PRIORITY_SHIFT;

	vcpu->arch.vgic_cpu.vgic_v2.vgic_lr[lr] = val;
}

void vgic_v2_clear_lr(struct kvm_vcpu *vcpu, int lr)
{
	vcpu->arch.vgic_cpu.vgic_v2.vgic_lr[lr] = 0;
}

void vgic_v2_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	u32 vmcr;

	vmcr  = (vmcrp->ctlr << GICH_VMCR_CTRL_SHIFT) & GICH_VMCR_CTRL_MASK;
	vmcr |= (vmcrp->abpr << GICH_VMCR_ALIAS_BINPOINT_SHIFT) &
		GICH_VMCR_ALIAS_BINPOINT_MASK;
	vmcr |= (vmcrp->bpr << GICH_VMCR_BINPOINT_SHIFT) &
		GICH_VMCR_BINPOINT_MASK;
	vmcr |= (vmcrp->pmr << GICH_VMCR_PRIMASK_SHIFT) &
		GICH_VMCR_PRIMASK_MASK;

	vcpu->arch.vgic_cpu.vgic_v2.vgic_vmcr = vmcr;
}

void vgic_v2_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	u32 vmcr = vcpu->arch.vgic_cpu.vgic_v2.vgic_vmcr;

	vmcrp->ctlr = (vmcr & GICH_VMCR_CTRL_MASK) >>
			GICH_VMCR_CTRL_SHIFT;
	vmcrp->abpr = (vmcr & GICH_VMCR_ALIAS_BINPOINT_MASK) >>
			GICH_VMCR_ALIAS_BINPOINT_SHIFT;
	vmcrp->bpr  = (vmcr & GICH_VMCR_BINPOINT_MASK) >>
			GICH_VMCR_BINPOINT_SHIFT;
	vmcrp->pmr  = (vmcr & GICH_VMCR_PRIMASK_MASK) >>
			GICH_VMCR_PRIMASK_SHIFT;
}

/**
 * vgic_v2_probe - probe for a GICv2 compatible interrupt controller in DT
 * @node:	pointer to the DT node
 *
 * Returns 0 if a GICv2 has been found, returns an error code otherwise
 */
int vgic_v2_probe(const struct gic_kvm_info *info)
{
	int ret;
	u32 vtr;

	if (!info->vctrl.start) {
		kvm_err("GICH not present in the firmware table\n");
		return -ENXIO;
	}

	if (!PAGE_ALIGNED(info->vcpu.start)) {
		kvm_err("GICV physical address 0x%llx not page aligned\n",
			(unsigned long long)info->vcpu.start);
		return -ENXIO;
	}

	if (!PAGE_ALIGNED(resource_size(&info->vcpu))) {
		kvm_err("GICV size 0x%llx not a multiple of page size 0x%lx\n",
			(unsigned long long)resource_size(&info->vcpu),
			PAGE_SIZE);
		return -ENXIO;
	}

	kvm_vgic_global_state.vctrl_base = ioremap(info->vctrl.start,
						   resource_size(&info->vctrl));
	if (!kvm_vgic_global_state.vctrl_base) {
		kvm_err("Cannot ioremap GICH\n");
		return -ENOMEM;
	}

	vtr = readl_relaxed(kvm_vgic_global_state.vctrl_base + GICH_VTR);
	kvm_vgic_global_state.nr_lr = (vtr & 0x3f) + 1;

	ret = create_hyp_io_mappings(kvm_vgic_global_state.vctrl_base,
				     kvm_vgic_global_state.vctrl_base +
					 resource_size(&info->vctrl),
				     info->vctrl.start);

	if (ret) {
		kvm_err("Cannot map VCTRL into hyp\n");
		iounmap(kvm_vgic_global_state.vctrl_base);
		return ret;
	}

	kvm_vgic_global_state.can_emulate_gicv2 = true;
	kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V2);

	kvm_vgic_global_state.vcpu_base = info->vcpu.start;
	kvm_vgic_global_state.type = VGIC_V2;
	kvm_vgic_global_state.max_gic_vcpus = VGIC_V2_MAX_CPUS;

	kvm_info("vgic-v2@%llx\n", info->vctrl.start);

	return 0;
}

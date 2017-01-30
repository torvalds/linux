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

			/* Only SPIs require notification */
			if (vgic_valid_spi(vcpu->kvm, intid))
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

		/*
		 * Clear soft pending state when level irqs have been acked.
		 * Always regenerate the pending state.
		 */
		if (irq->config == VGIC_CONFIG_LEVEL) {
			if (!(val & GICH_LR_PENDING_BIT))
				irq->soft_pending = false;

			irq->pending = irq->line_level || irq->soft_pending;
		}

		spin_unlock(&irq->irq_lock);
		vgic_put_irq(vcpu->kvm, irq);
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

void vgic_v2_enable(struct kvm_vcpu *vcpu)
{
	/*
	 * By forcing VMCR to zero, the GIC will restore the binary
	 * points to their reset values. Anything else resets to zero
	 * anyway.
	 */
	vcpu->arch.vgic_cpu.vgic_v2.vgic_vmcr = 0;
	vcpu->arch.vgic_cpu.vgic_v2.vgic_elrsr = ~0;

	/* Get the show on the road... */
	vcpu->arch.vgic_cpu.vgic_v2.vgic_hcr = GICH_HCR_EN;
}

/* check for overlapping regions and for regions crossing the end of memory */
static bool vgic_v2_check_base(gpa_t dist_base, gpa_t cpu_base)
{
	if (dist_base + KVM_VGIC_V2_DIST_SIZE < dist_base)
		return false;
	if (cpu_base + KVM_VGIC_V2_CPU_SIZE < cpu_base)
		return false;

	if (dist_base + KVM_VGIC_V2_DIST_SIZE <= cpu_base)
		return true;
	if (cpu_base + KVM_VGIC_V2_CPU_SIZE <= dist_base)
		return true;

	return false;
}

int vgic_v2_map_resources(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	int ret = 0;

	if (vgic_ready(kvm))
		goto out;

	if (IS_VGIC_ADDR_UNDEF(dist->vgic_dist_base) ||
	    IS_VGIC_ADDR_UNDEF(dist->vgic_cpu_base)) {
		kvm_err("Need to set vgic cpu and dist addresses first\n");
		ret = -ENXIO;
		goto out;
	}

	if (!vgic_v2_check_base(dist->vgic_dist_base, dist->vgic_cpu_base)) {
		kvm_err("VGIC CPU and dist frames overlap\n");
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Initialize the vgic if this hasn't already been done on demand by
	 * accessing the vgic state from userspace.
	 */
	ret = vgic_init(kvm);
	if (ret) {
		kvm_err("Unable to initialize VGIC dynamic data structures\n");
		goto out;
	}

	ret = vgic_register_dist_iodev(kvm, dist->vgic_dist_base, VGIC_V2);
	if (ret) {
		kvm_err("Unable to register VGIC MMIO regions\n");
		goto out;
	}

	if (!static_branch_unlikely(&vgic_v2_cpuif_trap)) {
		ret = kvm_phys_addr_ioremap(kvm, dist->vgic_cpu_base,
					    kvm_vgic_global_state.vcpu_base,
					    KVM_VGIC_V2_CPU_SIZE, true);
		if (ret) {
			kvm_err("Unable to remap VGIC CPU to VCPU\n");
			goto out;
		}
	}

	dist->ready = true;

out:
	return ret;
}

DEFINE_STATIC_KEY_FALSE(vgic_v2_cpuif_trap);

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

	if (!PAGE_ALIGNED(info->vcpu.start) ||
	    !PAGE_ALIGNED(resource_size(&info->vcpu))) {
		kvm_info("GICV region size/alignment is unsafe, using trapping (reduced performance)\n");
		kvm_vgic_global_state.vcpu_base_va = ioremap(info->vcpu.start,
							     resource_size(&info->vcpu));
		if (!kvm_vgic_global_state.vcpu_base_va) {
			kvm_err("Cannot ioremap GICV\n");
			return -ENOMEM;
		}

		ret = create_hyp_io_mappings(kvm_vgic_global_state.vcpu_base_va,
					     kvm_vgic_global_state.vcpu_base_va + resource_size(&info->vcpu),
					     info->vcpu.start);
		if (ret) {
			kvm_err("Cannot map GICV into hyp\n");
			goto out;
		}

		static_branch_enable(&vgic_v2_cpuif_trap);
	}

	kvm_vgic_global_state.vctrl_base = ioremap(info->vctrl.start,
						   resource_size(&info->vctrl));
	if (!kvm_vgic_global_state.vctrl_base) {
		kvm_err("Cannot ioremap GICH\n");
		ret = -ENOMEM;
		goto out;
	}

	vtr = readl_relaxed(kvm_vgic_global_state.vctrl_base + GICH_VTR);
	kvm_vgic_global_state.nr_lr = (vtr & 0x3f) + 1;

	ret = create_hyp_io_mappings(kvm_vgic_global_state.vctrl_base,
				     kvm_vgic_global_state.vctrl_base +
					 resource_size(&info->vctrl),
				     info->vctrl.start);
	if (ret) {
		kvm_err("Cannot map VCTRL into hyp\n");
		goto out;
	}

	ret = kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V2);
	if (ret) {
		kvm_err("Cannot register GICv2 KVM device\n");
		goto out;
	}

	kvm_vgic_global_state.can_emulate_gicv2 = true;
	kvm_vgic_global_state.vcpu_base = info->vcpu.start;
	kvm_vgic_global_state.type = VGIC_V2;
	kvm_vgic_global_state.max_gic_vcpus = VGIC_V2_MAX_CPUS;

	kvm_info("vgic-v2@%llx\n", info->vctrl.start);

	return 0;
out:
	if (kvm_vgic_global_state.vctrl_base)
		iounmap(kvm_vgic_global_state.vctrl_base);
	if (kvm_vgic_global_state.vcpu_base_va)
		iounmap(kvm_vgic_global_state.vcpu_base_va);

	return ret;
}

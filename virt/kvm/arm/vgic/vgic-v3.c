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
#include <kvm/arm_vgic.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_asm.h>

#include "vgic.h"

void vgic_v3_set_underflow(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpuif = &vcpu->arch.vgic_cpu.vgic_v3;

	cpuif->vgic_hcr |= ICH_HCR_UIE;
}

static bool lr_signals_eoi_mi(u64 lr_val)
{
	return !(lr_val & ICH_LR_STATE) && (lr_val & ICH_LR_EOI) &&
	       !(lr_val & ICH_LR_HW);
}

void vgic_v3_fold_lr_state(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_v3_cpu_if *cpuif = &vgic_cpu->vgic_v3;
	u32 model = vcpu->kvm->arch.vgic.vgic_model;
	int lr;

	cpuif->vgic_hcr &= ~ICH_HCR_UIE;

	for (lr = 0; lr < vgic_cpu->used_lrs; lr++) {
		u64 val = cpuif->vgic_lr[lr];
		u32 intid;
		struct vgic_irq *irq;

		if (model == KVM_DEV_TYPE_ARM_VGIC_V3)
			intid = val & ICH_LR_VIRTUAL_ID_MASK;
		else
			intid = val & GICH_LR_VIRTUALID;

		/* Notify fds when the guest EOI'ed a level-triggered IRQ */
		if (lr_signals_eoi_mi(val) && vgic_valid_spi(vcpu->kvm, intid))
			kvm_notify_acked_irq(vcpu->kvm, 0,
					     intid - VGIC_NR_PRIVATE_IRQS);

		irq = vgic_get_irq(vcpu->kvm, vcpu, intid);
		if (!irq)	/* An LPI could have been unmapped. */
			continue;

		spin_lock(&irq->irq_lock);

		/* Always preserve the active bit */
		irq->active = !!(val & ICH_LR_ACTIVE_BIT);

		/* Edge is the only case where we preserve the pending bit */
		if (irq->config == VGIC_CONFIG_EDGE &&
		    (val & ICH_LR_PENDING_BIT)) {
			irq->pending_latch = true;

			if (vgic_irq_is_sgi(intid) &&
			    model == KVM_DEV_TYPE_ARM_VGIC_V2) {
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
			if (!(val & ICH_LR_PENDING_BIT))
				irq->pending_latch = false;
		}

		spin_unlock(&irq->irq_lock);
		vgic_put_irq(vcpu->kvm, irq);
	}

	vgic_cpu->used_lrs = 0;
}

/* Requires the irq to be locked already */
void vgic_v3_populate_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq, int lr)
{
	u32 model = vcpu->kvm->arch.vgic.vgic_model;
	u64 val = irq->intid;

	if (irq_is_pending(irq)) {
		val |= ICH_LR_PENDING_BIT;

		if (irq->config == VGIC_CONFIG_EDGE)
			irq->pending_latch = false;

		if (vgic_irq_is_sgi(irq->intid) &&
		    model == KVM_DEV_TYPE_ARM_VGIC_V2) {
			u32 src = ffs(irq->source);

			BUG_ON(!src);
			val |= (src - 1) << GICH_LR_PHYSID_CPUID_SHIFT;
			irq->source &= ~(1 << (src - 1));
			if (irq->source)
				irq->pending_latch = true;
		}
	}

	if (irq->active)
		val |= ICH_LR_ACTIVE_BIT;

	if (irq->hw) {
		val |= ICH_LR_HW;
		val |= ((u64)irq->hwintid) << ICH_LR_PHYS_ID_SHIFT;
		/*
		 * Never set pending+active on a HW interrupt, as the
		 * pending state is kept at the physical distributor
		 * level.
		 */
		if (irq->active && irq_is_pending(irq))
			val &= ~ICH_LR_PENDING_BIT;
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
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	u32 vmcr;

	/*
	 * Ignore the FIQen bit, because GIC emulation always implies
	 * SRE=1 which means the vFIQEn bit is also RES1.
	 */
	vmcr = ((vmcrp->ctlr >> ICC_CTLR_EL1_EOImode_SHIFT) <<
		 ICH_VMCR_EOIM_SHIFT) & ICH_VMCR_EOIM_MASK;
	vmcr |= (vmcrp->ctlr << ICH_VMCR_CBPR_SHIFT) & ICH_VMCR_CBPR_MASK;
	vmcr |= (vmcrp->abpr << ICH_VMCR_BPR1_SHIFT) & ICH_VMCR_BPR1_MASK;
	vmcr |= (vmcrp->bpr << ICH_VMCR_BPR0_SHIFT) & ICH_VMCR_BPR0_MASK;
	vmcr |= (vmcrp->pmr << ICH_VMCR_PMR_SHIFT) & ICH_VMCR_PMR_MASK;
	vmcr |= (vmcrp->grpen0 << ICH_VMCR_ENG0_SHIFT) & ICH_VMCR_ENG0_MASK;
	vmcr |= (vmcrp->grpen1 << ICH_VMCR_ENG1_SHIFT) & ICH_VMCR_ENG1_MASK;

	cpu_if->vgic_vmcr = vmcr;
}

void vgic_v3_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	u32 vmcr;

	vmcr = cpu_if->vgic_vmcr;

	/*
	 * Ignore the FIQen bit, because GIC emulation always implies
	 * SRE=1 which means the vFIQEn bit is also RES1.
	 */
	vmcrp->ctlr = ((vmcr >> ICH_VMCR_EOIM_SHIFT) <<
			ICC_CTLR_EL1_EOImode_SHIFT) & ICC_CTLR_EL1_EOImode_MASK;
	vmcrp->ctlr |= (vmcr & ICH_VMCR_CBPR_MASK) >> ICH_VMCR_CBPR_SHIFT;
	vmcrp->abpr = (vmcr & ICH_VMCR_BPR1_MASK) >> ICH_VMCR_BPR1_SHIFT;
	vmcrp->bpr  = (vmcr & ICH_VMCR_BPR0_MASK) >> ICH_VMCR_BPR0_SHIFT;
	vmcrp->pmr  = (vmcr & ICH_VMCR_PMR_MASK) >> ICH_VMCR_PMR_SHIFT;
	vmcrp->grpen0 = (vmcr & ICH_VMCR_ENG0_MASK) >> ICH_VMCR_ENG0_SHIFT;
	vmcrp->grpen1 = (vmcr & ICH_VMCR_ENG1_MASK) >> ICH_VMCR_ENG1_SHIFT;
}

#define INITIAL_PENDBASER_VALUE						  \
	(GIC_BASER_CACHEABILITY(GICR_PENDBASER, INNER, RaWb)		| \
	GIC_BASER_CACHEABILITY(GICR_PENDBASER, OUTER, SameAsInner)	| \
	GIC_BASER_SHAREABILITY(GICR_PENDBASER, InnerShareable))

void vgic_v3_enable(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *vgic_v3 = &vcpu->arch.vgic_cpu.vgic_v3;

	/*
	 * By forcing VMCR to zero, the GIC will restore the binary
	 * points to their reset values. Anything else resets to zero
	 * anyway.
	 */
	vgic_v3->vgic_vmcr = 0;
	vgic_v3->vgic_elrsr = ~0;

	/*
	 * If we are emulating a GICv3, we do it in an non-GICv2-compatible
	 * way, so we force SRE to 1 to demonstrate this to the guest.
	 * Also, we don't support any form of IRQ/FIQ bypass.
	 * This goes with the spec allowing the value to be RAO/WI.
	 */
	if (vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3) {
		vgic_v3->vgic_sre = (ICC_SRE_EL1_DIB |
				     ICC_SRE_EL1_DFB |
				     ICC_SRE_EL1_SRE);
		vcpu->arch.vgic_cpu.pendbaser = INITIAL_PENDBASER_VALUE;
	} else {
		vgic_v3->vgic_sre = 0;
	}

	vcpu->arch.vgic_cpu.num_id_bits = (kvm_vgic_global_state.ich_vtr_el2 &
					   ICH_VTR_ID_BITS_MASK) >>
					   ICH_VTR_ID_BITS_SHIFT;
	vcpu->arch.vgic_cpu.num_pri_bits = ((kvm_vgic_global_state.ich_vtr_el2 &
					    ICH_VTR_PRI_BITS_MASK) >>
					    ICH_VTR_PRI_BITS_SHIFT) + 1;

	/* Get the show on the road... */
	vgic_v3->vgic_hcr = ICH_HCR_EN;
}

int vgic_v3_lpi_sync_pending_status(struct kvm *kvm, struct vgic_irq *irq)
{
	struct kvm_vcpu *vcpu;
	int byte_offset, bit_nr;
	gpa_t pendbase, ptr;
	bool status;
	u8 val;
	int ret;

retry:
	vcpu = irq->target_vcpu;
	if (!vcpu)
		return 0;

	pendbase = GICR_PENDBASER_ADDRESS(vcpu->arch.vgic_cpu.pendbaser);

	byte_offset = irq->intid / BITS_PER_BYTE;
	bit_nr = irq->intid % BITS_PER_BYTE;
	ptr = pendbase + byte_offset;

	ret = kvm_read_guest(kvm, ptr, &val, 1);
	if (ret)
		return ret;

	status = val & (1 << bit_nr);

	spin_lock(&irq->irq_lock);
	if (irq->target_vcpu != vcpu) {
		spin_unlock(&irq->irq_lock);
		goto retry;
	}
	irq->pending_latch = status;
	vgic_queue_irq_unlock(vcpu->kvm, irq);

	if (status) {
		/* clear consumed data */
		val &= ~(1 << bit_nr);
		ret = kvm_write_guest(kvm, ptr, &val, 1);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 * vgic_its_save_pending_tables - Save the pending tables into guest RAM
 * kvm lock and all vcpu lock must be held
 */
int vgic_v3_save_pending_tables(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	int last_byte_offset = -1;
	struct vgic_irq *irq;
	int ret;

	list_for_each_entry(irq, &dist->lpi_list_head, lpi_list) {
		int byte_offset, bit_nr;
		struct kvm_vcpu *vcpu;
		gpa_t pendbase, ptr;
		bool stored;
		u8 val;

		vcpu = irq->target_vcpu;
		if (!vcpu)
			continue;

		pendbase = GICR_PENDBASER_ADDRESS(vcpu->arch.vgic_cpu.pendbaser);

		byte_offset = irq->intid / BITS_PER_BYTE;
		bit_nr = irq->intid % BITS_PER_BYTE;
		ptr = pendbase + byte_offset;

		if (byte_offset != last_byte_offset) {
			ret = kvm_read_guest(kvm, ptr, &val, 1);
			if (ret)
				return ret;
			last_byte_offset = byte_offset;
		}

		stored = val & (1U << bit_nr);
		if (stored == irq->pending_latch)
			continue;

		if (irq->pending_latch)
			val |= 1 << bit_nr;
		else
			val &= ~(1 << bit_nr);

		ret = kvm_write_guest(kvm, ptr, &val, 1);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * Check for overlapping regions and for regions crossing the end of memory
 * for base addresses which have already been set.
 */
bool vgic_v3_check_base(struct kvm *kvm)
{
	struct vgic_dist *d = &kvm->arch.vgic;
	gpa_t redist_size = KVM_VGIC_V3_REDIST_SIZE;

	redist_size *= atomic_read(&kvm->online_vcpus);

	if (!IS_VGIC_ADDR_UNDEF(d->vgic_dist_base) &&
	    d->vgic_dist_base + KVM_VGIC_V3_DIST_SIZE < d->vgic_dist_base)
		return false;

	if (!IS_VGIC_ADDR_UNDEF(d->vgic_redist_base) &&
	    d->vgic_redist_base + redist_size < d->vgic_redist_base)
		return false;

	/* Both base addresses must be set to check if they overlap */
	if (IS_VGIC_ADDR_UNDEF(d->vgic_dist_base) ||
	    IS_VGIC_ADDR_UNDEF(d->vgic_redist_base))
		return true;

	if (d->vgic_dist_base + KVM_VGIC_V3_DIST_SIZE <= d->vgic_redist_base)
		return true;
	if (d->vgic_redist_base + redist_size <= d->vgic_dist_base)
		return true;

	return false;
}

int vgic_v3_map_resources(struct kvm *kvm)
{
	int ret = 0;
	struct vgic_dist *dist = &kvm->arch.vgic;

	if (vgic_ready(kvm))
		goto out;

	if (IS_VGIC_ADDR_UNDEF(dist->vgic_dist_base) ||
	    IS_VGIC_ADDR_UNDEF(dist->vgic_redist_base)) {
		kvm_err("Need to set vgic distributor addresses first\n");
		ret = -ENXIO;
		goto out;
	}

	if (!vgic_v3_check_base(kvm)) {
		kvm_err("VGIC redist and dist frames overlap\n");
		ret = -EINVAL;
		goto out;
	}

	/*
	 * For a VGICv3 we require the userland to explicitly initialize
	 * the VGIC before we need to use it.
	 */
	if (!vgic_initialized(kvm)) {
		ret = -EBUSY;
		goto out;
	}

	ret = vgic_register_dist_iodev(kvm, dist->vgic_dist_base, VGIC_V3);
	if (ret) {
		kvm_err("Unable to register VGICv3 dist MMIO regions\n");
		goto out;
	}

	dist->ready = true;

out:
	return ret;
}

/**
 * vgic_v3_probe - probe for a GICv3 compatible interrupt controller in DT
 * @node:	pointer to the DT node
 *
 * Returns 0 if a GICv3 has been found, returns an error code otherwise
 */
int vgic_v3_probe(const struct gic_kvm_info *info)
{
	u32 ich_vtr_el2 = kvm_call_hyp(__vgic_v3_get_ich_vtr_el2);
	int ret;

	/*
	 * The ListRegs field is 5 bits, but there is a architectural
	 * maximum of 16 list registers. Just ignore bit 4...
	 */
	kvm_vgic_global_state.nr_lr = (ich_vtr_el2 & 0xf) + 1;
	kvm_vgic_global_state.can_emulate_gicv2 = false;
	kvm_vgic_global_state.ich_vtr_el2 = ich_vtr_el2;

	if (!info->vcpu.start) {
		kvm_info("GICv3: no GICV resource entry\n");
		kvm_vgic_global_state.vcpu_base = 0;
	} else if (!PAGE_ALIGNED(info->vcpu.start)) {
		pr_warn("GICV physical address 0x%llx not page aligned\n",
			(unsigned long long)info->vcpu.start);
		kvm_vgic_global_state.vcpu_base = 0;
	} else if (!PAGE_ALIGNED(resource_size(&info->vcpu))) {
		pr_warn("GICV size 0x%llx not a multiple of page size 0x%lx\n",
			(unsigned long long)resource_size(&info->vcpu),
			PAGE_SIZE);
		kvm_vgic_global_state.vcpu_base = 0;
	} else {
		kvm_vgic_global_state.vcpu_base = info->vcpu.start;
		kvm_vgic_global_state.can_emulate_gicv2 = true;
		ret = kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V2);
		if (ret) {
			kvm_err("Cannot register GICv2 KVM device.\n");
			return ret;
		}
		kvm_info("vgic-v2@%llx\n", info->vcpu.start);
	}
	ret = kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V3);
	if (ret) {
		kvm_err("Cannot register GICv3 KVM device.\n");
		kvm_unregister_device_ops(KVM_DEV_TYPE_ARM_VGIC_V2);
		return ret;
	}

	if (kvm_vgic_global_state.vcpu_base == 0)
		kvm_info("disabling GICv2 emulation\n");

	kvm_vgic_global_state.vctrl_base = NULL;
	kvm_vgic_global_state.type = VGIC_V3;
	kvm_vgic_global_state.max_gic_vcpus = VGIC_V3_MAX_CPUS;

	return 0;
}

void vgic_v3_load(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;

	/*
	 * If dealing with a GICv2 emulation on GICv3, VMCR_EL2.VFIQen
	 * is dependent on ICC_SRE_EL1.SRE, and we have to perform the
	 * VMCR_EL2 save/restore in the world switch.
	 */
	if (likely(cpu_if->vgic_sre))
		kvm_call_hyp(__vgic_v3_write_vmcr, cpu_if->vgic_vmcr);
}

void vgic_v3_put(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;

	if (likely(cpu_if->vgic_sre))
		cpu_if->vgic_vmcr = kvm_call_hyp(__vgic_v3_read_vmcr);
}

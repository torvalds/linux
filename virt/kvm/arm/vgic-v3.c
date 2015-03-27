/*
 * Copyright (C) 2013 ARM Limited, All Rights Reserved.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <linux/irqchip/arm-gic-v3.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>

/* These are for GICv2 emulation only */
#define GICH_LR_VIRTUALID		(0x3ffUL << 0)
#define GICH_LR_PHYSID_CPUID_SHIFT	(10)
#define GICH_LR_PHYSID_CPUID		(7UL << GICH_LR_PHYSID_CPUID_SHIFT)
#define ICH_LR_VIRTUALID_MASK		(BIT_ULL(32) - 1)

/*
 * LRs are stored in reverse order in memory. make sure we index them
 * correctly.
 */
#define LR_INDEX(lr)			(VGIC_V3_MAX_LRS - 1 - lr)

static u32 ich_vtr_el2;

static struct vgic_lr vgic_v3_get_lr(const struct kvm_vcpu *vcpu, int lr)
{
	struct vgic_lr lr_desc;
	u64 val = vcpu->arch.vgic_cpu.vgic_v3.vgic_lr[LR_INDEX(lr)];

	if (vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3)
		lr_desc.irq = val & ICH_LR_VIRTUALID_MASK;
	else
		lr_desc.irq = val & GICH_LR_VIRTUALID;

	lr_desc.source = 0;
	if (lr_desc.irq <= 15 &&
	    vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V2)
		lr_desc.source = (val >> GICH_LR_PHYSID_CPUID_SHIFT) & 0x7;

	lr_desc.state = 0;

	if (val & ICH_LR_PENDING_BIT)
		lr_desc.state |= LR_STATE_PENDING;
	if (val & ICH_LR_ACTIVE_BIT)
		lr_desc.state |= LR_STATE_ACTIVE;
	if (val & ICH_LR_EOI)
		lr_desc.state |= LR_EOI_INT;

	return lr_desc;
}

static void vgic_v3_set_lr(struct kvm_vcpu *vcpu, int lr,
			   struct vgic_lr lr_desc)
{
	u64 lr_val;

	lr_val = lr_desc.irq;

	/*
	 * Currently all guest IRQs are Group1, as Group0 would result
	 * in a FIQ in the guest, which it wouldn't expect.
	 * Eventually we want to make this configurable, so we may revisit
	 * this in the future.
	 */
	if (vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3)
		lr_val |= ICH_LR_GROUP;
	else
		lr_val |= (u32)lr_desc.source << GICH_LR_PHYSID_CPUID_SHIFT;

	if (lr_desc.state & LR_STATE_PENDING)
		lr_val |= ICH_LR_PENDING_BIT;
	if (lr_desc.state & LR_STATE_ACTIVE)
		lr_val |= ICH_LR_ACTIVE_BIT;
	if (lr_desc.state & LR_EOI_INT)
		lr_val |= ICH_LR_EOI;

	vcpu->arch.vgic_cpu.vgic_v3.vgic_lr[LR_INDEX(lr)] = lr_val;
}

static void vgic_v3_sync_lr_elrsr(struct kvm_vcpu *vcpu, int lr,
				  struct vgic_lr lr_desc)
{
	if (!(lr_desc.state & LR_STATE_MASK))
		vcpu->arch.vgic_cpu.vgic_v3.vgic_elrsr |= (1U << lr);
	else
		vcpu->arch.vgic_cpu.vgic_v3.vgic_elrsr &= ~(1U << lr);
}

static u64 vgic_v3_get_elrsr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.vgic_cpu.vgic_v3.vgic_elrsr;
}

static u64 vgic_v3_get_eisr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.vgic_cpu.vgic_v3.vgic_eisr;
}

static void vgic_v3_clear_eisr(struct kvm_vcpu *vcpu)
{
	vcpu->arch.vgic_cpu.vgic_v3.vgic_eisr = 0;
}

static u32 vgic_v3_get_interrupt_status(const struct kvm_vcpu *vcpu)
{
	u32 misr = vcpu->arch.vgic_cpu.vgic_v3.vgic_misr;
	u32 ret = 0;

	if (misr & ICH_MISR_EOI)
		ret |= INT_STATUS_EOI;
	if (misr & ICH_MISR_U)
		ret |= INT_STATUS_UNDERFLOW;

	return ret;
}

static void vgic_v3_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	u32 vmcr = vcpu->arch.vgic_cpu.vgic_v3.vgic_vmcr;

	vmcrp->ctlr = (vmcr & ICH_VMCR_CTLR_MASK) >> ICH_VMCR_CTLR_SHIFT;
	vmcrp->abpr = (vmcr & ICH_VMCR_BPR1_MASK) >> ICH_VMCR_BPR1_SHIFT;
	vmcrp->bpr  = (vmcr & ICH_VMCR_BPR0_MASK) >> ICH_VMCR_BPR0_SHIFT;
	vmcrp->pmr  = (vmcr & ICH_VMCR_PMR_MASK) >> ICH_VMCR_PMR_SHIFT;
}

static void vgic_v3_enable_underflow(struct kvm_vcpu *vcpu)
{
	vcpu->arch.vgic_cpu.vgic_v3.vgic_hcr |= ICH_HCR_UIE;
}

static void vgic_v3_disable_underflow(struct kvm_vcpu *vcpu)
{
	vcpu->arch.vgic_cpu.vgic_v3.vgic_hcr &= ~ICH_HCR_UIE;
}

static void vgic_v3_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	u32 vmcr;

	vmcr  = (vmcrp->ctlr << ICH_VMCR_CTLR_SHIFT) & ICH_VMCR_CTLR_MASK;
	vmcr |= (vmcrp->abpr << ICH_VMCR_BPR1_SHIFT) & ICH_VMCR_BPR1_MASK;
	vmcr |= (vmcrp->bpr << ICH_VMCR_BPR0_SHIFT) & ICH_VMCR_BPR0_MASK;
	vmcr |= (vmcrp->pmr << ICH_VMCR_PMR_SHIFT) & ICH_VMCR_PMR_MASK;

	vcpu->arch.vgic_cpu.vgic_v3.vgic_vmcr = vmcr;
}

static void vgic_v3_enable(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *vgic_v3 = &vcpu->arch.vgic_cpu.vgic_v3;

	/*
	 * By forcing VMCR to zero, the GIC will restore the binary
	 * points to their reset values. Anything else resets to zero
	 * anyway.
	 */
	vgic_v3->vgic_vmcr = 0;

	/*
	 * If we are emulating a GICv3, we do it in an non-GICv2-compatible
	 * way, so we force SRE to 1 to demonstrate this to the guest.
	 * This goes with the spec allowing the value to be RAO/WI.
	 */
	if (vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3)
		vgic_v3->vgic_sre = ICC_SRE_EL1_SRE;
	else
		vgic_v3->vgic_sre = 0;

	/* Get the show on the road... */
	vgic_v3->vgic_hcr = ICH_HCR_EN;
}

static const struct vgic_ops vgic_v3_ops = {
	.get_lr			= vgic_v3_get_lr,
	.set_lr			= vgic_v3_set_lr,
	.sync_lr_elrsr		= vgic_v3_sync_lr_elrsr,
	.get_elrsr		= vgic_v3_get_elrsr,
	.get_eisr		= vgic_v3_get_eisr,
	.clear_eisr		= vgic_v3_clear_eisr,
	.get_interrupt_status	= vgic_v3_get_interrupt_status,
	.enable_underflow	= vgic_v3_enable_underflow,
	.disable_underflow	= vgic_v3_disable_underflow,
	.get_vmcr		= vgic_v3_get_vmcr,
	.set_vmcr		= vgic_v3_set_vmcr,
	.enable			= vgic_v3_enable,
};

static struct vgic_params vgic_v3_params;

/**
 * vgic_v3_probe - probe for a GICv3 compatible interrupt controller in DT
 * @node:	pointer to the DT node
 * @ops: 	address of a pointer to the GICv3 operations
 * @params:	address of a pointer to HW-specific parameters
 *
 * Returns 0 if a GICv3 has been found, with the low level operations
 * in *ops and the HW parameters in *params. Returns an error code
 * otherwise.
 */
int vgic_v3_probe(struct device_node *vgic_node,
		  const struct vgic_ops **ops,
		  const struct vgic_params **params)
{
	int ret = 0;
	u32 gicv_idx;
	struct resource vcpu_res;
	struct vgic_params *vgic = &vgic_v3_params;

	vgic->maint_irq = irq_of_parse_and_map(vgic_node, 0);
	if (!vgic->maint_irq) {
		kvm_err("error getting vgic maintenance irq from DT\n");
		ret = -ENXIO;
		goto out;
	}

	ich_vtr_el2 = kvm_call_hyp(__vgic_v3_get_ich_vtr_el2);

	/*
	 * The ListRegs field is 5 bits, but there is a architectural
	 * maximum of 16 list registers. Just ignore bit 4...
	 */
	vgic->nr_lr = (ich_vtr_el2 & 0xf) + 1;
	vgic->can_emulate_gicv2 = false;

	if (of_property_read_u32(vgic_node, "#redistributor-regions", &gicv_idx))
		gicv_idx = 1;

	gicv_idx += 3; /* Also skip GICD, GICC, GICH */
	if (of_address_to_resource(vgic_node, gicv_idx, &vcpu_res)) {
		kvm_info("GICv3: no GICV resource entry\n");
		vgic->vcpu_base = 0;
	} else if (!PAGE_ALIGNED(vcpu_res.start)) {
		pr_warn("GICV physical address 0x%llx not page aligned\n",
			(unsigned long long)vcpu_res.start);
		vgic->vcpu_base = 0;
	} else if (!PAGE_ALIGNED(resource_size(&vcpu_res))) {
		pr_warn("GICV size 0x%llx not a multiple of page size 0x%lx\n",
			(unsigned long long)resource_size(&vcpu_res),
			PAGE_SIZE);
		vgic->vcpu_base = 0;
	} else {
		vgic->vcpu_base = vcpu_res.start;
		vgic->can_emulate_gicv2 = true;
		kvm_register_device_ops(&kvm_arm_vgic_v2_ops,
					KVM_DEV_TYPE_ARM_VGIC_V2);
	}
	if (vgic->vcpu_base == 0)
		kvm_info("disabling GICv2 emulation\n");
	kvm_register_device_ops(&kvm_arm_vgic_v3_ops, KVM_DEV_TYPE_ARM_VGIC_V3);

	vgic->vctrl_base = NULL;
	vgic->type = VGIC_V3;
	vgic->max_gic_vcpus = KVM_MAX_VCPUS;

	kvm_info("%s@%llx IRQ%d\n", vgic_node->name,
		 vcpu_res.start, vgic->maint_irq);

	*ops = &vgic_v3_ops;
	*params = vgic;

out:
	of_node_put(vgic_node);
	return ret;
}

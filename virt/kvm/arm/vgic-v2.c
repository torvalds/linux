/*
 * Copyright (C) 2012,2013 ARM Limited, All Rights Reserved.
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

#include <linux/irqchip/arm-gic.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>

static struct vgic_lr vgic_v2_get_lr(const struct kvm_vcpu *vcpu, int lr)
{
	struct vgic_lr lr_desc;
	u32 val = vcpu->arch.vgic_cpu.vgic_v2.vgic_lr[lr];

	lr_desc.irq	= val & GICH_LR_VIRTUALID;
	if (lr_desc.irq <= 15)
		lr_desc.source	= (val >> GICH_LR_PHYSID_CPUID_SHIFT) & 0x7;
	else
		lr_desc.source = 0;
	lr_desc.state	= 0;

	if (val & GICH_LR_PENDING_BIT)
		lr_desc.state |= LR_STATE_PENDING;
	if (val & GICH_LR_ACTIVE_BIT)
		lr_desc.state |= LR_STATE_ACTIVE;
	if (val & GICH_LR_EOI)
		lr_desc.state |= LR_EOI_INT;
	if (val & GICH_LR_HW) {
		lr_desc.state |= LR_HW;
		lr_desc.hwirq = (val & GICH_LR_PHYSID_CPUID) >> GICH_LR_PHYSID_CPUID_SHIFT;
	}

	return lr_desc;
}

static void vgic_v2_set_lr(struct kvm_vcpu *vcpu, int lr,
			   struct vgic_lr lr_desc)
{
	u32 lr_val;

	lr_val = lr_desc.irq;

	if (lr_desc.state & LR_STATE_PENDING)
		lr_val |= GICH_LR_PENDING_BIT;
	if (lr_desc.state & LR_STATE_ACTIVE)
		lr_val |= GICH_LR_ACTIVE_BIT;
	if (lr_desc.state & LR_EOI_INT)
		lr_val |= GICH_LR_EOI;

	if (lr_desc.state & LR_HW) {
		lr_val |= GICH_LR_HW;
		lr_val |= (u32)lr_desc.hwirq << GICH_LR_PHYSID_CPUID_SHIFT;
	}

	if (lr_desc.irq < VGIC_NR_SGIS)
		lr_val |= (lr_desc.source << GICH_LR_PHYSID_CPUID_SHIFT);

	vcpu->arch.vgic_cpu.vgic_v2.vgic_lr[lr] = lr_val;
}

static void vgic_v2_sync_lr_elrsr(struct kvm_vcpu *vcpu, int lr,
				  struct vgic_lr lr_desc)
{
	if (!(lr_desc.state & LR_STATE_MASK))
		vcpu->arch.vgic_cpu.vgic_v2.vgic_elrsr |= (1ULL << lr);
	else
		vcpu->arch.vgic_cpu.vgic_v2.vgic_elrsr &= ~(1ULL << lr);
}

static u64 vgic_v2_get_elrsr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.vgic_cpu.vgic_v2.vgic_elrsr;
}

static u64 vgic_v2_get_eisr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.vgic_cpu.vgic_v2.vgic_eisr;
}

static void vgic_v2_clear_eisr(struct kvm_vcpu *vcpu)
{
	vcpu->arch.vgic_cpu.vgic_v2.vgic_eisr = 0;
}

static u32 vgic_v2_get_interrupt_status(const struct kvm_vcpu *vcpu)
{
	u32 misr = vcpu->arch.vgic_cpu.vgic_v2.vgic_misr;
	u32 ret = 0;

	if (misr & GICH_MISR_EOI)
		ret |= INT_STATUS_EOI;
	if (misr & GICH_MISR_U)
		ret |= INT_STATUS_UNDERFLOW;

	return ret;
}

static void vgic_v2_enable_underflow(struct kvm_vcpu *vcpu)
{
	vcpu->arch.vgic_cpu.vgic_v2.vgic_hcr |= GICH_HCR_UIE;
}

static void vgic_v2_disable_underflow(struct kvm_vcpu *vcpu)
{
	vcpu->arch.vgic_cpu.vgic_v2.vgic_hcr &= ~GICH_HCR_UIE;
}

static void vgic_v2_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	u32 vmcr = vcpu->arch.vgic_cpu.vgic_v2.vgic_vmcr;

	vmcrp->ctlr = (vmcr & GICH_VMCR_CTRL_MASK) >> GICH_VMCR_CTRL_SHIFT;
	vmcrp->abpr = (vmcr & GICH_VMCR_ALIAS_BINPOINT_MASK) >> GICH_VMCR_ALIAS_BINPOINT_SHIFT;
	vmcrp->bpr  = (vmcr & GICH_VMCR_BINPOINT_MASK) >> GICH_VMCR_BINPOINT_SHIFT;
	vmcrp->pmr  = (vmcr & GICH_VMCR_PRIMASK_MASK) >> GICH_VMCR_PRIMASK_SHIFT;
}

static void vgic_v2_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	u32 vmcr;

	vmcr  = (vmcrp->ctlr << GICH_VMCR_CTRL_SHIFT) & GICH_VMCR_CTRL_MASK;
	vmcr |= (vmcrp->abpr << GICH_VMCR_ALIAS_BINPOINT_SHIFT) & GICH_VMCR_ALIAS_BINPOINT_MASK;
	vmcr |= (vmcrp->bpr << GICH_VMCR_BINPOINT_SHIFT) & GICH_VMCR_BINPOINT_MASK;
	vmcr |= (vmcrp->pmr << GICH_VMCR_PRIMASK_SHIFT) & GICH_VMCR_PRIMASK_MASK;

	vcpu->arch.vgic_cpu.vgic_v2.vgic_vmcr = vmcr;
}

static void vgic_v2_enable(struct kvm_vcpu *vcpu)
{
	/*
	 * By forcing VMCR to zero, the GIC will restore the binary
	 * points to their reset values. Anything else resets to zero
	 * anyway.
	 */
	vcpu->arch.vgic_cpu.vgic_v2.vgic_vmcr = 0;

	/* Get the show on the road... */
	vcpu->arch.vgic_cpu.vgic_v2.vgic_hcr = GICH_HCR_EN;
}

static const struct vgic_ops vgic_v2_ops = {
	.get_lr			= vgic_v2_get_lr,
	.set_lr			= vgic_v2_set_lr,
	.sync_lr_elrsr		= vgic_v2_sync_lr_elrsr,
	.get_elrsr		= vgic_v2_get_elrsr,
	.get_eisr		= vgic_v2_get_eisr,
	.clear_eisr		= vgic_v2_clear_eisr,
	.get_interrupt_status	= vgic_v2_get_interrupt_status,
	.enable_underflow	= vgic_v2_enable_underflow,
	.disable_underflow	= vgic_v2_disable_underflow,
	.get_vmcr		= vgic_v2_get_vmcr,
	.set_vmcr		= vgic_v2_set_vmcr,
	.enable			= vgic_v2_enable,
};

static struct vgic_params vgic_v2_params;

/**
 * vgic_v2_probe - probe for a GICv2 compatible interrupt controller in DT
 * @node:	pointer to the DT node
 * @ops: 	address of a pointer to the GICv2 operations
 * @params:	address of a pointer to HW-specific parameters
 *
 * Returns 0 if a GICv2 has been found, with the low level operations
 * in *ops and the HW parameters in *params. Returns an error code
 * otherwise.
 */
int vgic_v2_probe(struct device_node *vgic_node,
		  const struct vgic_ops **ops,
		  const struct vgic_params **params)
{
	int ret;
	struct resource vctrl_res;
	struct resource vcpu_res;
	struct vgic_params *vgic = &vgic_v2_params;

	vgic->maint_irq = irq_of_parse_and_map(vgic_node, 0);
	if (!vgic->maint_irq) {
		kvm_err("error getting vgic maintenance irq from DT\n");
		ret = -ENXIO;
		goto out;
	}

	ret = of_address_to_resource(vgic_node, 2, &vctrl_res);
	if (ret) {
		kvm_err("Cannot obtain GICH resource\n");
		goto out;
	}

	vgic->vctrl_base = of_iomap(vgic_node, 2);
	if (!vgic->vctrl_base) {
		kvm_err("Cannot ioremap GICH\n");
		ret = -ENOMEM;
		goto out;
	}

	vgic->nr_lr = readl_relaxed(vgic->vctrl_base + GICH_VTR);
	vgic->nr_lr = (vgic->nr_lr & 0x3f) + 1;

	ret = create_hyp_io_mappings(vgic->vctrl_base,
				     vgic->vctrl_base + resource_size(&vctrl_res),
				     vctrl_res.start);
	if (ret) {
		kvm_err("Cannot map VCTRL into hyp\n");
		goto out_unmap;
	}

	if (of_address_to_resource(vgic_node, 3, &vcpu_res)) {
		kvm_err("Cannot obtain GICV resource\n");
		ret = -ENXIO;
		goto out_unmap;
	}

	if (!PAGE_ALIGNED(vcpu_res.start)) {
		kvm_err("GICV physical address 0x%llx not page aligned\n",
			(unsigned long long)vcpu_res.start);
		ret = -ENXIO;
		goto out_unmap;
	}

	if (!PAGE_ALIGNED(resource_size(&vcpu_res))) {
		kvm_err("GICV size 0x%llx not a multiple of page size 0x%lx\n",
			(unsigned long long)resource_size(&vcpu_res),
			PAGE_SIZE);
		ret = -ENXIO;
		goto out_unmap;
	}

	vgic->can_emulate_gicv2 = true;
	kvm_register_device_ops(&kvm_arm_vgic_v2_ops, KVM_DEV_TYPE_ARM_VGIC_V2);

	vgic->vcpu_base = vcpu_res.start;

	kvm_info("%s@%llx IRQ%d\n", vgic_node->name,
		 vctrl_res.start, vgic->maint_irq);

	vgic->type = VGIC_V2;
	vgic->max_gic_vcpus = VGIC_V2_MAX_CPUS;
	*ops = &vgic_v2_ops;
	*params = vgic;
	goto out;

out_unmap:
	iounmap(vgic->vctrl_base);
out:
	of_node_put(vgic_node);
	return ret;
}

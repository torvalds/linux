/*
 * Copyright (C) 2012-2015 - ARM Ltd
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

#include <linux/compiler.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>

static void __hyp_text save_elrsr(struct kvm_vcpu *vcpu, void __iomem *base)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	int nr_lr = (kern_hyp_va(&kvm_vgic_global_state))->nr_lr;
	u32 elrsr0, elrsr1;

	elrsr0 = readl_relaxed(base + GICH_ELRSR0);
	if (unlikely(nr_lr > 32))
		elrsr1 = readl_relaxed(base + GICH_ELRSR1);
	else
		elrsr1 = 0;

	cpu_if->vgic_elrsr = ((u64)elrsr1 << 32) | elrsr0;
}

static void __hyp_text save_lrs(struct kvm_vcpu *vcpu, void __iomem *base)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	int i;
	u64 used_lrs = vcpu->arch.vgic_cpu.used_lrs;

	for (i = 0; i < used_lrs; i++) {
		if (cpu_if->vgic_elrsr & (1UL << i))
			cpu_if->vgic_lr[i] &= ~GICH_LR_STATE;
		else
			cpu_if->vgic_lr[i] = readl_relaxed(base + GICH_LR0 + (i * 4));

		writel_relaxed(0, base + GICH_LR0 + (i * 4));
	}
}

/* vcpu is already in the HYP VA space */
void __hyp_text __vgic_v2_save_state(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = kern_hyp_va(vcpu->kvm);
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	struct vgic_dist *vgic = &kvm->arch.vgic;
	void __iomem *base = kern_hyp_va(vgic->vctrl_base);
	u64 used_lrs = vcpu->arch.vgic_cpu.used_lrs;

	if (!base)
		return;

	if (used_lrs) {
		cpu_if->vgic_apr = readl_relaxed(base + GICH_APR);

		save_elrsr(vcpu, base);
		save_lrs(vcpu, base);

		writel_relaxed(0, base + GICH_HCR);
	} else {
		cpu_if->vgic_elrsr = ~0UL;
		cpu_if->vgic_apr = 0;
	}
}

/* vcpu is already in the HYP VA space */
void __hyp_text __vgic_v2_restore_state(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = kern_hyp_va(vcpu->kvm);
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	struct vgic_dist *vgic = &kvm->arch.vgic;
	void __iomem *base = kern_hyp_va(vgic->vctrl_base);
	int i;
	u64 used_lrs = vcpu->arch.vgic_cpu.used_lrs;

	if (!base)
		return;

	if (used_lrs) {
		writel_relaxed(cpu_if->vgic_hcr, base + GICH_HCR);
		writel_relaxed(cpu_if->vgic_apr, base + GICH_APR);
		for (i = 0; i < used_lrs; i++) {
			writel_relaxed(cpu_if->vgic_lr[i],
				       base + GICH_LR0 + (i * 4));
		}
	}
}

#ifdef CONFIG_ARM64
/*
 * __vgic_v2_perform_cpuif_access -- perform a GICV access on behalf of the
 *				     guest.
 *
 * @vcpu: the offending vcpu
 *
 * Returns:
 *  1: GICV access successfully performed
 *  0: Not a GICV access
 * -1: Illegal GICV access
 */
int __hyp_text __vgic_v2_perform_cpuif_access(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = kern_hyp_va(vcpu->kvm);
	struct vgic_dist *vgic = &kvm->arch.vgic;
	phys_addr_t fault_ipa;
	void __iomem *addr;
	int rd;

	/* Build the full address */
	fault_ipa  = kvm_vcpu_get_fault_ipa(vcpu);
	fault_ipa |= kvm_vcpu_get_hfar(vcpu) & GENMASK(11, 0);

	/* If not for GICV, move on */
	if (fault_ipa <  vgic->vgic_cpu_base ||
	    fault_ipa >= (vgic->vgic_cpu_base + KVM_VGIC_V2_CPU_SIZE))
		return 0;

	/* Reject anything but a 32bit access */
	if (kvm_vcpu_dabt_get_as(vcpu) != sizeof(u32))
		return -1;

	/* Not aligned? Don't bother */
	if (fault_ipa & 3)
		return -1;

	rd = kvm_vcpu_dabt_get_rd(vcpu);
	addr  = kern_hyp_va((kern_hyp_va(&kvm_vgic_global_state))->vcpu_base_va);
	addr += fault_ipa - vgic->vgic_cpu_base;

	if (kvm_vcpu_dabt_iswrite(vcpu)) {
		u32 data = vcpu_data_guest_to_host(vcpu,
						   vcpu_get_reg(vcpu, rd),
						   sizeof(u32));
		writel_relaxed(data, addr);
	} else {
		u32 data = readl_relaxed(addr);
		vcpu_set_reg(vcpu, rd, vcpu_data_host_to_guest(vcpu, data,
							       sizeof(u32)));
	}

	return 1;
}
#endif

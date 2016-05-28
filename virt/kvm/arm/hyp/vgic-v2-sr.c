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

#include <asm/kvm_hyp.h>

static void __hyp_text save_maint_int_state(struct kvm_vcpu *vcpu,
					    void __iomem *base)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	int nr_lr = (kern_hyp_va(&kvm_vgic_global_state))->nr_lr;
	u32 eisr0, eisr1;
	int i;
	bool expect_mi;

	expect_mi = !!(cpu_if->vgic_hcr & GICH_HCR_UIE);

	for (i = 0; i < nr_lr; i++) {
		if (!(vcpu->arch.vgic_cpu.live_lrs & (1UL << i)))
				continue;

		expect_mi |= (!(cpu_if->vgic_lr[i] & GICH_LR_HW) &&
			      (cpu_if->vgic_lr[i] & GICH_LR_EOI));
	}

	if (expect_mi) {
		cpu_if->vgic_misr = readl_relaxed(base + GICH_MISR);

		if (cpu_if->vgic_misr & GICH_MISR_EOI) {
			eisr0  = readl_relaxed(base + GICH_EISR0);
			if (unlikely(nr_lr > 32))
				eisr1  = readl_relaxed(base + GICH_EISR1);
			else
				eisr1 = 0;
		} else {
			eisr0 = eisr1 = 0;
		}
	} else {
		cpu_if->vgic_misr = 0;
		eisr0 = eisr1 = 0;
	}

#ifdef CONFIG_CPU_BIG_ENDIAN
	cpu_if->vgic_eisr = ((u64)eisr0 << 32) | eisr1;
#else
	cpu_if->vgic_eisr = ((u64)eisr1 << 32) | eisr0;
#endif
}

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

#ifdef CONFIG_CPU_BIG_ENDIAN
	cpu_if->vgic_elrsr = ((u64)elrsr0 << 32) | elrsr1;
#else
	cpu_if->vgic_elrsr = ((u64)elrsr1 << 32) | elrsr0;
#endif
}

static void __hyp_text save_lrs(struct kvm_vcpu *vcpu, void __iomem *base)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	int nr_lr = (kern_hyp_va(&kvm_vgic_global_state))->nr_lr;
	int i;

	for (i = 0; i < nr_lr; i++) {
		if (!(vcpu->arch.vgic_cpu.live_lrs & (1UL << i)))
			continue;

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

	if (!base)
		return;

	cpu_if->vgic_vmcr = readl_relaxed(base + GICH_VMCR);

	if (vcpu->arch.vgic_cpu.live_lrs) {
		cpu_if->vgic_apr = readl_relaxed(base + GICH_APR);

		save_maint_int_state(vcpu, base);
		save_elrsr(vcpu, base);
		save_lrs(vcpu, base);

		writel_relaxed(0, base + GICH_HCR);

		vcpu->arch.vgic_cpu.live_lrs = 0;
	} else {
		cpu_if->vgic_eisr = 0;
		cpu_if->vgic_elrsr = ~0UL;
		cpu_if->vgic_misr = 0;
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
	int nr_lr = (kern_hyp_va(&kvm_vgic_global_state))->nr_lr;
	int i;
	u64 live_lrs = 0;

	if (!base)
		return;


	for (i = 0; i < nr_lr; i++)
		if (cpu_if->vgic_lr[i] & GICH_LR_STATE)
			live_lrs |= 1UL << i;

	if (live_lrs) {
		writel_relaxed(cpu_if->vgic_hcr, base + GICH_HCR);
		writel_relaxed(cpu_if->vgic_apr, base + GICH_APR);
		for (i = 0; i < nr_lr; i++) {
			if (!(live_lrs & (1UL << i)))
				continue;

			writel_relaxed(cpu_if->vgic_lr[i],
				       base + GICH_LR0 + (i * 4));
		}
	}

	writel_relaxed(cpu_if->vgic_vmcr, base + GICH_VMCR);
	vcpu->arch.vgic_cpu.live_lrs = live_lrs;
}

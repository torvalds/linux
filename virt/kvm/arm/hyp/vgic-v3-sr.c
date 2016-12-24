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
#include <linux/irqchip/arm-gic-v3.h>
#include <linux/kvm_host.h>

#include <asm/kvm_hyp.h>

#define vtr_to_max_lr_idx(v)		((v) & 0xf)
#define vtr_to_nr_pri_bits(v)		(((u32)(v) >> 29) + 1)

static u64 __hyp_text __gic_v3_get_lr(unsigned int lr)
{
	switch (lr & 0xf) {
	case 0:
		return read_gicreg(ICH_LR0_EL2);
	case 1:
		return read_gicreg(ICH_LR1_EL2);
	case 2:
		return read_gicreg(ICH_LR2_EL2);
	case 3:
		return read_gicreg(ICH_LR3_EL2);
	case 4:
		return read_gicreg(ICH_LR4_EL2);
	case 5:
		return read_gicreg(ICH_LR5_EL2);
	case 6:
		return read_gicreg(ICH_LR6_EL2);
	case 7:
		return read_gicreg(ICH_LR7_EL2);
	case 8:
		return read_gicreg(ICH_LR8_EL2);
	case 9:
		return read_gicreg(ICH_LR9_EL2);
	case 10:
		return read_gicreg(ICH_LR10_EL2);
	case 11:
		return read_gicreg(ICH_LR11_EL2);
	case 12:
		return read_gicreg(ICH_LR12_EL2);
	case 13:
		return read_gicreg(ICH_LR13_EL2);
	case 14:
		return read_gicreg(ICH_LR14_EL2);
	case 15:
		return read_gicreg(ICH_LR15_EL2);
	}

	unreachable();
}

static void __hyp_text __gic_v3_set_lr(u64 val, int lr)
{
	switch (lr & 0xf) {
	case 0:
		write_gicreg(val, ICH_LR0_EL2);
		break;
	case 1:
		write_gicreg(val, ICH_LR1_EL2);
		break;
	case 2:
		write_gicreg(val, ICH_LR2_EL2);
		break;
	case 3:
		write_gicreg(val, ICH_LR3_EL2);
		break;
	case 4:
		write_gicreg(val, ICH_LR4_EL2);
		break;
	case 5:
		write_gicreg(val, ICH_LR5_EL2);
		break;
	case 6:
		write_gicreg(val, ICH_LR6_EL2);
		break;
	case 7:
		write_gicreg(val, ICH_LR7_EL2);
		break;
	case 8:
		write_gicreg(val, ICH_LR8_EL2);
		break;
	case 9:
		write_gicreg(val, ICH_LR9_EL2);
		break;
	case 10:
		write_gicreg(val, ICH_LR10_EL2);
		break;
	case 11:
		write_gicreg(val, ICH_LR11_EL2);
		break;
	case 12:
		write_gicreg(val, ICH_LR12_EL2);
		break;
	case 13:
		write_gicreg(val, ICH_LR13_EL2);
		break;
	case 14:
		write_gicreg(val, ICH_LR14_EL2);
		break;
	case 15:
		write_gicreg(val, ICH_LR15_EL2);
		break;
	}
}

static void __hyp_text save_maint_int_state(struct kvm_vcpu *vcpu, int nr_lr)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	int i;
	bool expect_mi;

	expect_mi = !!(cpu_if->vgic_hcr & ICH_HCR_UIE);

	for (i = 0; i < nr_lr; i++) {
		if (!(vcpu->arch.vgic_cpu.live_lrs & (1UL << i)))
				continue;

		expect_mi |= (!(cpu_if->vgic_lr[i] & ICH_LR_HW) &&
			      (cpu_if->vgic_lr[i] & ICH_LR_EOI));
	}

	if (expect_mi) {
		cpu_if->vgic_misr  = read_gicreg(ICH_MISR_EL2);

		if (cpu_if->vgic_misr & ICH_MISR_EOI)
			cpu_if->vgic_eisr = read_gicreg(ICH_EISR_EL2);
		else
			cpu_if->vgic_eisr = 0;
	} else {
		cpu_if->vgic_misr = 0;
		cpu_if->vgic_eisr = 0;
	}
}

void __hyp_text __vgic_v3_save_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	u64 val;

	/*
	 * Make sure stores to the GIC via the memory mapped interface
	 * are now visible to the system register interface.
	 */
	if (!cpu_if->vgic_sre)
		dsb(st);

	cpu_if->vgic_vmcr  = read_gicreg(ICH_VMCR_EL2);

	if (vcpu->arch.vgic_cpu.live_lrs) {
		int i;
		u32 max_lr_idx, nr_pri_bits;

		cpu_if->vgic_elrsr = read_gicreg(ICH_ELSR_EL2);

		write_gicreg(0, ICH_HCR_EL2);
		val = read_gicreg(ICH_VTR_EL2);
		max_lr_idx = vtr_to_max_lr_idx(val);
		nr_pri_bits = vtr_to_nr_pri_bits(val);

		save_maint_int_state(vcpu, max_lr_idx + 1);

		for (i = 0; i <= max_lr_idx; i++) {
			if (!(vcpu->arch.vgic_cpu.live_lrs & (1UL << i)))
				continue;

			if (cpu_if->vgic_elrsr & (1 << i))
				cpu_if->vgic_lr[i] &= ~ICH_LR_STATE;
			else
				cpu_if->vgic_lr[i] = __gic_v3_get_lr(i);

			__gic_v3_set_lr(0, i);
		}

		switch (nr_pri_bits) {
		case 7:
			cpu_if->vgic_ap0r[3] = read_gicreg(ICH_AP0R3_EL2);
			cpu_if->vgic_ap0r[2] = read_gicreg(ICH_AP0R2_EL2);
		case 6:
			cpu_if->vgic_ap0r[1] = read_gicreg(ICH_AP0R1_EL2);
		default:
			cpu_if->vgic_ap0r[0] = read_gicreg(ICH_AP0R0_EL2);
		}

		switch (nr_pri_bits) {
		case 7:
			cpu_if->vgic_ap1r[3] = read_gicreg(ICH_AP1R3_EL2);
			cpu_if->vgic_ap1r[2] = read_gicreg(ICH_AP1R2_EL2);
		case 6:
			cpu_if->vgic_ap1r[1] = read_gicreg(ICH_AP1R1_EL2);
		default:
			cpu_if->vgic_ap1r[0] = read_gicreg(ICH_AP1R0_EL2);
		}

		vcpu->arch.vgic_cpu.live_lrs = 0;
	} else {
		cpu_if->vgic_misr  = 0;
		cpu_if->vgic_eisr  = 0;
		cpu_if->vgic_elrsr = 0xffff;
		cpu_if->vgic_ap0r[0] = 0;
		cpu_if->vgic_ap0r[1] = 0;
		cpu_if->vgic_ap0r[2] = 0;
		cpu_if->vgic_ap0r[3] = 0;
		cpu_if->vgic_ap1r[0] = 0;
		cpu_if->vgic_ap1r[1] = 0;
		cpu_if->vgic_ap1r[2] = 0;
		cpu_if->vgic_ap1r[3] = 0;
	}

	val = read_gicreg(ICC_SRE_EL2);
	write_gicreg(val | ICC_SRE_EL2_ENABLE, ICC_SRE_EL2);

	if (!cpu_if->vgic_sre) {
		/* Make sure ENABLE is set at EL2 before setting SRE at EL1 */
		isb();
		write_gicreg(1, ICC_SRE_EL1);
	}
}

void __hyp_text __vgic_v3_restore_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	u64 val;
	u32 max_lr_idx, nr_pri_bits;
	u16 live_lrs = 0;
	int i;

	/*
	 * VFIQEn is RES1 if ICC_SRE_EL1.SRE is 1. This causes a
	 * Group0 interrupt (as generated in GICv2 mode) to be
	 * delivered as a FIQ to the guest, with potentially fatal
	 * consequences. So we must make sure that ICC_SRE_EL1 has
	 * been actually programmed with the value we want before
	 * starting to mess with the rest of the GIC.
	 */
	if (!cpu_if->vgic_sre) {
		write_gicreg(0, ICC_SRE_EL1);
		isb();
	}

	val = read_gicreg(ICH_VTR_EL2);
	max_lr_idx = vtr_to_max_lr_idx(val);
	nr_pri_bits = vtr_to_nr_pri_bits(val);

	for (i = 0; i <= max_lr_idx; i++) {
		if (cpu_if->vgic_lr[i] & ICH_LR_STATE)
			live_lrs |= (1 << i);
	}

	write_gicreg(cpu_if->vgic_vmcr, ICH_VMCR_EL2);

	if (live_lrs) {
		write_gicreg(cpu_if->vgic_hcr, ICH_HCR_EL2);

		switch (nr_pri_bits) {
		case 7:
			write_gicreg(cpu_if->vgic_ap0r[3], ICH_AP0R3_EL2);
			write_gicreg(cpu_if->vgic_ap0r[2], ICH_AP0R2_EL2);
		case 6:
			write_gicreg(cpu_if->vgic_ap0r[1], ICH_AP0R1_EL2);
		default:
			write_gicreg(cpu_if->vgic_ap0r[0], ICH_AP0R0_EL2);
		}

		switch (nr_pri_bits) {
		case 7:
			write_gicreg(cpu_if->vgic_ap1r[3], ICH_AP1R3_EL2);
			write_gicreg(cpu_if->vgic_ap1r[2], ICH_AP1R2_EL2);
		case 6:
			write_gicreg(cpu_if->vgic_ap1r[1], ICH_AP1R1_EL2);
		default:
			write_gicreg(cpu_if->vgic_ap1r[0], ICH_AP1R0_EL2);
		}

		for (i = 0; i <= max_lr_idx; i++) {
			if (!(live_lrs & (1 << i)))
				continue;

			__gic_v3_set_lr(cpu_if->vgic_lr[i], i);
		}
	}

	/*
	 * Ensures that the above will have reached the
	 * (re)distributors. This ensure the guest will read the
	 * correct values from the memory-mapped interface.
	 */
	if (!cpu_if->vgic_sre) {
		isb();
		dsb(sy);
	}
	vcpu->arch.vgic_cpu.live_lrs = live_lrs;

	/*
	 * Prevent the guest from touching the GIC system registers if
	 * SRE isn't enabled for GICv3 emulation.
	 */
	write_gicreg(read_gicreg(ICC_SRE_EL2) & ~ICC_SRE_EL2_ENABLE,
		     ICC_SRE_EL2);
}

void __hyp_text __vgic_v3_init_lrs(void)
{
	int max_lr_idx = vtr_to_max_lr_idx(read_gicreg(ICH_VTR_EL2));
	int i;

	for (i = 0; i <= max_lr_idx; i++)
		__gic_v3_set_lr(0, i);
}

u64 __hyp_text __vgic_v3_get_ich_vtr_el2(void)
{
	return read_gicreg(ICH_VTR_EL2);
}

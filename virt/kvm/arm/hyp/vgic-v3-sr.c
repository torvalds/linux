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

#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

#define vtr_to_max_lr_idx(v)		((v) & 0xf)
#define vtr_to_nr_pre_bits(v)		((((u32)(v) >> 26) & 7) + 1)
#define vtr_to_nr_apr_regs(v)		(1 << (vtr_to_nr_pre_bits(v) - 5))

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

static void __hyp_text __vgic_v3_write_ap0rn(u32 val, int n)
{
	switch (n) {
	case 0:
		write_gicreg(val, ICH_AP0R0_EL2);
		break;
	case 1:
		write_gicreg(val, ICH_AP0R1_EL2);
		break;
	case 2:
		write_gicreg(val, ICH_AP0R2_EL2);
		break;
	case 3:
		write_gicreg(val, ICH_AP0R3_EL2);
		break;
	}
}

static void __hyp_text __vgic_v3_write_ap1rn(u32 val, int n)
{
	switch (n) {
	case 0:
		write_gicreg(val, ICH_AP1R0_EL2);
		break;
	case 1:
		write_gicreg(val, ICH_AP1R1_EL2);
		break;
	case 2:
		write_gicreg(val, ICH_AP1R2_EL2);
		break;
	case 3:
		write_gicreg(val, ICH_AP1R3_EL2);
		break;
	}
}

static u32 __hyp_text __vgic_v3_read_ap0rn(int n)
{
	u32 val;

	switch (n) {
	case 0:
		val = read_gicreg(ICH_AP0R0_EL2);
		break;
	case 1:
		val = read_gicreg(ICH_AP0R1_EL2);
		break;
	case 2:
		val = read_gicreg(ICH_AP0R2_EL2);
		break;
	case 3:
		val = read_gicreg(ICH_AP0R3_EL2);
		break;
	default:
		unreachable();
	}

	return val;
}

static u32 __hyp_text __vgic_v3_read_ap1rn(int n)
{
	u32 val;

	switch (n) {
	case 0:
		val = read_gicreg(ICH_AP1R0_EL2);
		break;
	case 1:
		val = read_gicreg(ICH_AP1R1_EL2);
		break;
	case 2:
		val = read_gicreg(ICH_AP1R2_EL2);
		break;
	case 3:
		val = read_gicreg(ICH_AP1R3_EL2);
		break;
	default:
		unreachable();
	}

	return val;
}

void __hyp_text __vgic_v3_save_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	u64 used_lrs = vcpu->arch.vgic_cpu.used_lrs;

	/*
	 * Make sure stores to the GIC via the memory mapped interface
	 * are now visible to the system register interface when reading the
	 * LRs, and when reading back the VMCR on non-VHE systems.
	 */
	if (used_lrs || !has_vhe()) {
		if (!cpu_if->vgic_sre) {
			dsb(sy);
			isb();
		}
	}

	if (used_lrs) {
		int i;
		u32 elrsr;

		elrsr = read_gicreg(ICH_ELSR_EL2);

		write_gicreg(cpu_if->vgic_hcr & ~ICH_HCR_EN, ICH_HCR_EL2);

		for (i = 0; i < used_lrs; i++) {
			if (elrsr & (1 << i))
				cpu_if->vgic_lr[i] &= ~ICH_LR_STATE;
			else
				cpu_if->vgic_lr[i] = __gic_v3_get_lr(i);

			__gic_v3_set_lr(0, i);
		}
	}
}

void __hyp_text __vgic_v3_restore_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	u64 used_lrs = vcpu->arch.vgic_cpu.used_lrs;
	int i;

	if (used_lrs) {
		write_gicreg(cpu_if->vgic_hcr, ICH_HCR_EL2);

		for (i = 0; i < used_lrs; i++)
			__gic_v3_set_lr(cpu_if->vgic_lr[i], i);
	}

	/*
	 * Ensure that writes to the LRs, and on non-VHE systems ensure that
	 * the write to the VMCR in __vgic_v3_activate_traps(), will have
	 * reached the (re)distributors. This ensure the guest will read the
	 * correct values from the memory-mapped interface.
	 */
	if (used_lrs || !has_vhe()) {
		if (!cpu_if->vgic_sre) {
			isb();
			dsb(sy);
		}
	}
}

void __hyp_text __vgic_v3_activate_traps(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;

	/*
	 * VFIQEn is RES1 if ICC_SRE_EL1.SRE is 1. This causes a
	 * Group0 interrupt (as generated in GICv2 mode) to be
	 * delivered as a FIQ to the guest, with potentially fatal
	 * consequences. So we must make sure that ICC_SRE_EL1 has
	 * been actually programmed with the value we want before
	 * starting to mess with the rest of the GIC, and VMCR_EL2 in
	 * particular.  This logic must be called before
	 * __vgic_v3_restore_state().
	 */
	if (!cpu_if->vgic_sre) {
		write_gicreg(0, ICC_SRE_EL1);
		isb();
		write_gicreg(cpu_if->vgic_vmcr, ICH_VMCR_EL2);


		if (has_vhe()) {
			/*
			 * Ensure that the write to the VMCR will have reached
			 * the (re)distributors. This ensure the guest will
			 * read the correct values from the memory-mapped
			 * interface.
			 */
			isb();
			dsb(sy);
		}
	}

	/*
	 * Prevent the guest from touching the GIC system registers if
	 * SRE isn't enabled for GICv3 emulation.
	 */
	write_gicreg(read_gicreg(ICC_SRE_EL2) & ~ICC_SRE_EL2_ENABLE,
		     ICC_SRE_EL2);

	/*
	 * If we need to trap system registers, we must write
	 * ICH_HCR_EL2 anyway, even if no interrupts are being
	 * injected,
	 */
	if (static_branch_unlikely(&vgic_v3_cpuif_trap) ||
	    cpu_if->its_vpe.its_vm)
		write_gicreg(cpu_if->vgic_hcr, ICH_HCR_EL2);
}

void __hyp_text __vgic_v3_deactivate_traps(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	u64 val;

	if (!cpu_if->vgic_sre) {
		cpu_if->vgic_vmcr = read_gicreg(ICH_VMCR_EL2);
	}

	val = read_gicreg(ICC_SRE_EL2);
	write_gicreg(val | ICC_SRE_EL2_ENABLE, ICC_SRE_EL2);

	if (!cpu_if->vgic_sre) {
		/* Make sure ENABLE is set at EL2 before setting SRE at EL1 */
		isb();
		write_gicreg(1, ICC_SRE_EL1);
	}

	/*
	 * If we were trapping system registers, we enabled the VGIC even if
	 * no interrupts were being injected, and we disable it again here.
	 */
	if (static_branch_unlikely(&vgic_v3_cpuif_trap) ||
	    cpu_if->its_vpe.its_vm)
		write_gicreg(0, ICH_HCR_EL2);
}

void __hyp_text __vgic_v3_save_aprs(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if;
	u64 val;
	u32 nr_pre_bits;

	vcpu = kern_hyp_va(vcpu);
	cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;

	val = read_gicreg(ICH_VTR_EL2);
	nr_pre_bits = vtr_to_nr_pre_bits(val);

	switch (nr_pre_bits) {
	case 7:
		cpu_if->vgic_ap0r[3] = __vgic_v3_read_ap0rn(3);
		cpu_if->vgic_ap0r[2] = __vgic_v3_read_ap0rn(2);
	case 6:
		cpu_if->vgic_ap0r[1] = __vgic_v3_read_ap0rn(1);
	default:
		cpu_if->vgic_ap0r[0] = __vgic_v3_read_ap0rn(0);
	}

	switch (nr_pre_bits) {
	case 7:
		cpu_if->vgic_ap1r[3] = __vgic_v3_read_ap1rn(3);
		cpu_if->vgic_ap1r[2] = __vgic_v3_read_ap1rn(2);
	case 6:
		cpu_if->vgic_ap1r[1] = __vgic_v3_read_ap1rn(1);
	default:
		cpu_if->vgic_ap1r[0] = __vgic_v3_read_ap1rn(0);
	}
}

void __hyp_text __vgic_v3_restore_aprs(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if;
	u64 val;
	u32 nr_pre_bits;

	vcpu = kern_hyp_va(vcpu);
	cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;

	val = read_gicreg(ICH_VTR_EL2);
	nr_pre_bits = vtr_to_nr_pre_bits(val);

	switch (nr_pre_bits) {
	case 7:
		__vgic_v3_write_ap0rn(cpu_if->vgic_ap0r[3], 3);
		__vgic_v3_write_ap0rn(cpu_if->vgic_ap0r[2], 2);
	case 6:
		__vgic_v3_write_ap0rn(cpu_if->vgic_ap0r[1], 1);
	default:
		__vgic_v3_write_ap0rn(cpu_if->vgic_ap0r[0], 0);
	}

	switch (nr_pre_bits) {
	case 7:
		__vgic_v3_write_ap1rn(cpu_if->vgic_ap1r[3], 3);
		__vgic_v3_write_ap1rn(cpu_if->vgic_ap1r[2], 2);
	case 6:
		__vgic_v3_write_ap1rn(cpu_if->vgic_ap1r[1], 1);
	default:
		__vgic_v3_write_ap1rn(cpu_if->vgic_ap1r[0], 0);
	}
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

u64 __hyp_text __vgic_v3_read_vmcr(void)
{
	return read_gicreg(ICH_VMCR_EL2);
}

void __hyp_text __vgic_v3_write_vmcr(u32 vmcr)
{
	write_gicreg(vmcr, ICH_VMCR_EL2);
}

#ifdef CONFIG_ARM64

static int __hyp_text __vgic_v3_bpr_min(void)
{
	/* See Pseudocode for VPriorityGroup */
	return 8 - vtr_to_nr_pre_bits(read_gicreg(ICH_VTR_EL2));
}

static int __hyp_text __vgic_v3_get_group(struct kvm_vcpu *vcpu)
{
	u32 esr = kvm_vcpu_get_hsr(vcpu);
	u8 crm = (esr & ESR_ELx_SYS64_ISS_CRM_MASK) >> ESR_ELx_SYS64_ISS_CRM_SHIFT;

	return crm != 8;
}

#define GICv3_IDLE_PRIORITY	0xff

static int __hyp_text __vgic_v3_highest_priority_lr(struct kvm_vcpu *vcpu,
						    u32 vmcr,
						    u64 *lr_val)
{
	unsigned int used_lrs = vcpu->arch.vgic_cpu.used_lrs;
	u8 priority = GICv3_IDLE_PRIORITY;
	int i, lr = -1;

	for (i = 0; i < used_lrs; i++) {
		u64 val = __gic_v3_get_lr(i);
		u8 lr_prio = (val & ICH_LR_PRIORITY_MASK) >> ICH_LR_PRIORITY_SHIFT;

		/* Not pending in the state? */
		if ((val & ICH_LR_STATE) != ICH_LR_PENDING_BIT)
			continue;

		/* Group-0 interrupt, but Group-0 disabled? */
		if (!(val & ICH_LR_GROUP) && !(vmcr & ICH_VMCR_ENG0_MASK))
			continue;

		/* Group-1 interrupt, but Group-1 disabled? */
		if ((val & ICH_LR_GROUP) && !(vmcr & ICH_VMCR_ENG1_MASK))
			continue;

		/* Not the highest priority? */
		if (lr_prio >= priority)
			continue;

		/* This is a candidate */
		priority = lr_prio;
		*lr_val = val;
		lr = i;
	}

	if (lr == -1)
		*lr_val = ICC_IAR1_EL1_SPURIOUS;

	return lr;
}

static int __hyp_text __vgic_v3_find_active_lr(struct kvm_vcpu *vcpu,
					       int intid, u64 *lr_val)
{
	unsigned int used_lrs = vcpu->arch.vgic_cpu.used_lrs;
	int i;

	for (i = 0; i < used_lrs; i++) {
		u64 val = __gic_v3_get_lr(i);

		if ((val & ICH_LR_VIRTUAL_ID_MASK) == intid &&
		    (val & ICH_LR_ACTIVE_BIT)) {
			*lr_val = val;
			return i;
		}
	}

	*lr_val = ICC_IAR1_EL1_SPURIOUS;
	return -1;
}

static int __hyp_text __vgic_v3_get_highest_active_priority(void)
{
	u8 nr_apr_regs = vtr_to_nr_apr_regs(read_gicreg(ICH_VTR_EL2));
	u32 hap = 0;
	int i;

	for (i = 0; i < nr_apr_regs; i++) {
		u32 val;

		/*
		 * The ICH_AP0Rn_EL2 and ICH_AP1Rn_EL2 registers
		 * contain the active priority levels for this VCPU
		 * for the maximum number of supported priority
		 * levels, and we return the full priority level only
		 * if the BPR is programmed to its minimum, otherwise
		 * we return a combination of the priority level and
		 * subpriority, as determined by the setting of the
		 * BPR, but without the full subpriority.
		 */
		val  = __vgic_v3_read_ap0rn(i);
		val |= __vgic_v3_read_ap1rn(i);
		if (!val) {
			hap += 32;
			continue;
		}

		return (hap + __ffs(val)) << __vgic_v3_bpr_min();
	}

	return GICv3_IDLE_PRIORITY;
}

static unsigned int __hyp_text __vgic_v3_get_bpr0(u32 vmcr)
{
	return (vmcr & ICH_VMCR_BPR0_MASK) >> ICH_VMCR_BPR0_SHIFT;
}

static unsigned int __hyp_text __vgic_v3_get_bpr1(u32 vmcr)
{
	unsigned int bpr;

	if (vmcr & ICH_VMCR_CBPR_MASK) {
		bpr = __vgic_v3_get_bpr0(vmcr);
		if (bpr < 7)
			bpr++;
	} else {
		bpr = (vmcr & ICH_VMCR_BPR1_MASK) >> ICH_VMCR_BPR1_SHIFT;
	}

	return bpr;
}

/*
 * Convert a priority to a preemption level, taking the relevant BPR
 * into account by zeroing the sub-priority bits.
 */
static u8 __hyp_text __vgic_v3_pri_to_pre(u8 pri, u32 vmcr, int grp)
{
	unsigned int bpr;

	if (!grp)
		bpr = __vgic_v3_get_bpr0(vmcr) + 1;
	else
		bpr = __vgic_v3_get_bpr1(vmcr);

	return pri & (GENMASK(7, 0) << bpr);
}

/*
 * The priority value is independent of any of the BPR values, so we
 * normalize it using the minumal BPR value. This guarantees that no
 * matter what the guest does with its BPR, we can always set/get the
 * same value of a priority.
 */
static void __hyp_text __vgic_v3_set_active_priority(u8 pri, u32 vmcr, int grp)
{
	u8 pre, ap;
	u32 val;
	int apr;

	pre = __vgic_v3_pri_to_pre(pri, vmcr, grp);
	ap = pre >> __vgic_v3_bpr_min();
	apr = ap / 32;

	if (!grp) {
		val = __vgic_v3_read_ap0rn(apr);
		__vgic_v3_write_ap0rn(val | BIT(ap % 32), apr);
	} else {
		val = __vgic_v3_read_ap1rn(apr);
		__vgic_v3_write_ap1rn(val | BIT(ap % 32), apr);
	}
}

static int __hyp_text __vgic_v3_clear_highest_active_priority(void)
{
	u8 nr_apr_regs = vtr_to_nr_apr_regs(read_gicreg(ICH_VTR_EL2));
	u32 hap = 0;
	int i;

	for (i = 0; i < nr_apr_regs; i++) {
		u32 ap0, ap1;
		int c0, c1;

		ap0 = __vgic_v3_read_ap0rn(i);
		ap1 = __vgic_v3_read_ap1rn(i);
		if (!ap0 && !ap1) {
			hap += 32;
			continue;
		}

		c0 = ap0 ? __ffs(ap0) : 32;
		c1 = ap1 ? __ffs(ap1) : 32;

		/* Always clear the LSB, which is the highest priority */
		if (c0 < c1) {
			ap0 &= ~BIT(c0);
			__vgic_v3_write_ap0rn(ap0, i);
			hap += c0;
		} else {
			ap1 &= ~BIT(c1);
			__vgic_v3_write_ap1rn(ap1, i);
			hap += c1;
		}

		/* Rescale to 8 bits of priority */
		return hap << __vgic_v3_bpr_min();
	}

	return GICv3_IDLE_PRIORITY;
}

static void __hyp_text __vgic_v3_read_iar(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	u64 lr_val;
	u8 lr_prio, pmr;
	int lr, grp;

	grp = __vgic_v3_get_group(vcpu);

	lr = __vgic_v3_highest_priority_lr(vcpu, vmcr, &lr_val);
	if (lr < 0)
		goto spurious;

	if (grp != !!(lr_val & ICH_LR_GROUP))
		goto spurious;

	pmr = (vmcr & ICH_VMCR_PMR_MASK) >> ICH_VMCR_PMR_SHIFT;
	lr_prio = (lr_val & ICH_LR_PRIORITY_MASK) >> ICH_LR_PRIORITY_SHIFT;
	if (pmr <= lr_prio)
		goto spurious;

	if (__vgic_v3_get_highest_active_priority() <= __vgic_v3_pri_to_pre(lr_prio, vmcr, grp))
		goto spurious;

	lr_val &= ~ICH_LR_STATE;
	/* No active state for LPIs */
	if ((lr_val & ICH_LR_VIRTUAL_ID_MASK) <= VGIC_MAX_SPI)
		lr_val |= ICH_LR_ACTIVE_BIT;
	__gic_v3_set_lr(lr_val, lr);
	__vgic_v3_set_active_priority(lr_prio, vmcr, grp);
	vcpu_set_reg(vcpu, rt, lr_val & ICH_LR_VIRTUAL_ID_MASK);
	return;

spurious:
	vcpu_set_reg(vcpu, rt, ICC_IAR1_EL1_SPURIOUS);
}

static void __hyp_text __vgic_v3_clear_active_lr(int lr, u64 lr_val)
{
	lr_val &= ~ICH_LR_ACTIVE_BIT;
	if (lr_val & ICH_LR_HW) {
		u32 pid;

		pid = (lr_val & ICH_LR_PHYS_ID_MASK) >> ICH_LR_PHYS_ID_SHIFT;
		gic_write_dir(pid);
	}

	__gic_v3_set_lr(lr_val, lr);
}

static void __hyp_text __vgic_v3_bump_eoicount(void)
{
	u32 hcr;

	hcr = read_gicreg(ICH_HCR_EL2);
	hcr += 1 << ICH_HCR_EOIcount_SHIFT;
	write_gicreg(hcr, ICH_HCR_EL2);
}

static void __hyp_text __vgic_v3_write_dir(struct kvm_vcpu *vcpu,
					   u32 vmcr, int rt)
{
	u32 vid = vcpu_get_reg(vcpu, rt);
	u64 lr_val;
	int lr;

	/* EOImode == 0, nothing to be done here */
	if (!(vmcr & ICH_VMCR_EOIM_MASK))
		return;

	/* No deactivate to be performed on an LPI */
	if (vid >= VGIC_MIN_LPI)
		return;

	lr = __vgic_v3_find_active_lr(vcpu, vid, &lr_val);
	if (lr == -1) {
		__vgic_v3_bump_eoicount();
		return;
	}

	__vgic_v3_clear_active_lr(lr, lr_val);
}

static void __hyp_text __vgic_v3_write_eoir(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	u32 vid = vcpu_get_reg(vcpu, rt);
	u64 lr_val;
	u8 lr_prio, act_prio;
	int lr, grp;

	grp = __vgic_v3_get_group(vcpu);

	/* Drop priority in any case */
	act_prio = __vgic_v3_clear_highest_active_priority();

	/* If EOIing an LPI, no deactivate to be performed */
	if (vid >= VGIC_MIN_LPI)
		return;

	/* EOImode == 1, nothing to be done here */
	if (vmcr & ICH_VMCR_EOIM_MASK)
		return;

	lr = __vgic_v3_find_active_lr(vcpu, vid, &lr_val);
	if (lr == -1) {
		__vgic_v3_bump_eoicount();
		return;
	}

	lr_prio = (lr_val & ICH_LR_PRIORITY_MASK) >> ICH_LR_PRIORITY_SHIFT;

	/* If priorities or group do not match, the guest has fscked-up. */
	if (grp != !!(lr_val & ICH_LR_GROUP) ||
	    __vgic_v3_pri_to_pre(lr_prio, vmcr, grp) != act_prio)
		return;

	/* Let's now perform the deactivation */
	__vgic_v3_clear_active_lr(lr, lr_val);
}

static void __hyp_text __vgic_v3_read_igrpen0(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	vcpu_set_reg(vcpu, rt, !!(vmcr & ICH_VMCR_ENG0_MASK));
}

static void __hyp_text __vgic_v3_read_igrpen1(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	vcpu_set_reg(vcpu, rt, !!(vmcr & ICH_VMCR_ENG1_MASK));
}

static void __hyp_text __vgic_v3_write_igrpen0(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	u64 val = vcpu_get_reg(vcpu, rt);

	if (val & 1)
		vmcr |= ICH_VMCR_ENG0_MASK;
	else
		vmcr &= ~ICH_VMCR_ENG0_MASK;

	__vgic_v3_write_vmcr(vmcr);
}

static void __hyp_text __vgic_v3_write_igrpen1(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	u64 val = vcpu_get_reg(vcpu, rt);

	if (val & 1)
		vmcr |= ICH_VMCR_ENG1_MASK;
	else
		vmcr &= ~ICH_VMCR_ENG1_MASK;

	__vgic_v3_write_vmcr(vmcr);
}

static void __hyp_text __vgic_v3_read_bpr0(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	vcpu_set_reg(vcpu, rt, __vgic_v3_get_bpr0(vmcr));
}

static void __hyp_text __vgic_v3_read_bpr1(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	vcpu_set_reg(vcpu, rt, __vgic_v3_get_bpr1(vmcr));
}

static void __hyp_text __vgic_v3_write_bpr0(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	u64 val = vcpu_get_reg(vcpu, rt);
	u8 bpr_min = __vgic_v3_bpr_min() - 1;

	/* Enforce BPR limiting */
	if (val < bpr_min)
		val = bpr_min;

	val <<= ICH_VMCR_BPR0_SHIFT;
	val &= ICH_VMCR_BPR0_MASK;
	vmcr &= ~ICH_VMCR_BPR0_MASK;
	vmcr |= val;

	__vgic_v3_write_vmcr(vmcr);
}

static void __hyp_text __vgic_v3_write_bpr1(struct kvm_vcpu *vcpu, u32 vmcr, int rt)
{
	u64 val = vcpu_get_reg(vcpu, rt);
	u8 bpr_min = __vgic_v3_bpr_min();

	if (vmcr & ICH_VMCR_CBPR_MASK)
		return;

	/* Enforce BPR limiting */
	if (val < bpr_min)
		val = bpr_min;

	val <<= ICH_VMCR_BPR1_SHIFT;
	val &= ICH_VMCR_BPR1_MASK;
	vmcr &= ~ICH_VMCR_BPR1_MASK;
	vmcr |= val;

	__vgic_v3_write_vmcr(vmcr);
}

static void __hyp_text __vgic_v3_read_apxrn(struct kvm_vcpu *vcpu, int rt, int n)
{
	u32 val;

	if (!__vgic_v3_get_group(vcpu))
		val = __vgic_v3_read_ap0rn(n);
	else
		val = __vgic_v3_read_ap1rn(n);

	vcpu_set_reg(vcpu, rt, val);
}

static void __hyp_text __vgic_v3_write_apxrn(struct kvm_vcpu *vcpu, int rt, int n)
{
	u32 val = vcpu_get_reg(vcpu, rt);

	if (!__vgic_v3_get_group(vcpu))
		__vgic_v3_write_ap0rn(val, n);
	else
		__vgic_v3_write_ap1rn(val, n);
}

static void __hyp_text __vgic_v3_read_apxr0(struct kvm_vcpu *vcpu,
					    u32 vmcr, int rt)
{
	__vgic_v3_read_apxrn(vcpu, rt, 0);
}

static void __hyp_text __vgic_v3_read_apxr1(struct kvm_vcpu *vcpu,
					    u32 vmcr, int rt)
{
	__vgic_v3_read_apxrn(vcpu, rt, 1);
}

static void __hyp_text __vgic_v3_read_apxr2(struct kvm_vcpu *vcpu,
					    u32 vmcr, int rt)
{
	__vgic_v3_read_apxrn(vcpu, rt, 2);
}

static void __hyp_text __vgic_v3_read_apxr3(struct kvm_vcpu *vcpu,
					    u32 vmcr, int rt)
{
	__vgic_v3_read_apxrn(vcpu, rt, 3);
}

static void __hyp_text __vgic_v3_write_apxr0(struct kvm_vcpu *vcpu,
					     u32 vmcr, int rt)
{
	__vgic_v3_write_apxrn(vcpu, rt, 0);
}

static void __hyp_text __vgic_v3_write_apxr1(struct kvm_vcpu *vcpu,
					     u32 vmcr, int rt)
{
	__vgic_v3_write_apxrn(vcpu, rt, 1);
}

static void __hyp_text __vgic_v3_write_apxr2(struct kvm_vcpu *vcpu,
					     u32 vmcr, int rt)
{
	__vgic_v3_write_apxrn(vcpu, rt, 2);
}

static void __hyp_text __vgic_v3_write_apxr3(struct kvm_vcpu *vcpu,
					     u32 vmcr, int rt)
{
	__vgic_v3_write_apxrn(vcpu, rt, 3);
}

static void __hyp_text __vgic_v3_read_hppir(struct kvm_vcpu *vcpu,
					    u32 vmcr, int rt)
{
	u64 lr_val;
	int lr, lr_grp, grp;

	grp = __vgic_v3_get_group(vcpu);

	lr = __vgic_v3_highest_priority_lr(vcpu, vmcr, &lr_val);
	if (lr == -1)
		goto spurious;

	lr_grp = !!(lr_val & ICH_LR_GROUP);
	if (lr_grp != grp)
		lr_val = ICC_IAR1_EL1_SPURIOUS;

spurious:
	vcpu_set_reg(vcpu, rt, lr_val & ICH_LR_VIRTUAL_ID_MASK);
}

static void __hyp_text __vgic_v3_read_pmr(struct kvm_vcpu *vcpu,
					  u32 vmcr, int rt)
{
	vmcr &= ICH_VMCR_PMR_MASK;
	vmcr >>= ICH_VMCR_PMR_SHIFT;
	vcpu_set_reg(vcpu, rt, vmcr);
}

static void __hyp_text __vgic_v3_write_pmr(struct kvm_vcpu *vcpu,
					   u32 vmcr, int rt)
{
	u32 val = vcpu_get_reg(vcpu, rt);

	val <<= ICH_VMCR_PMR_SHIFT;
	val &= ICH_VMCR_PMR_MASK;
	vmcr &= ~ICH_VMCR_PMR_MASK;
	vmcr |= val;

	write_gicreg(vmcr, ICH_VMCR_EL2);
}

static void __hyp_text __vgic_v3_read_rpr(struct kvm_vcpu *vcpu,
					  u32 vmcr, int rt)
{
	u32 val = __vgic_v3_get_highest_active_priority();
	vcpu_set_reg(vcpu, rt, val);
}

static void __hyp_text __vgic_v3_read_ctlr(struct kvm_vcpu *vcpu,
					   u32 vmcr, int rt)
{
	u32 vtr, val;

	vtr = read_gicreg(ICH_VTR_EL2);
	/* PRIbits */
	val = ((vtr >> 29) & 7) << ICC_CTLR_EL1_PRI_BITS_SHIFT;
	/* IDbits */
	val |= ((vtr >> 23) & 7) << ICC_CTLR_EL1_ID_BITS_SHIFT;
	/* SEIS */
	val |= ((vtr >> 22) & 1) << ICC_CTLR_EL1_SEIS_SHIFT;
	/* A3V */
	val |= ((vtr >> 21) & 1) << ICC_CTLR_EL1_A3V_SHIFT;
	/* EOImode */
	val |= ((vmcr & ICH_VMCR_EOIM_MASK) >> ICH_VMCR_EOIM_SHIFT) << ICC_CTLR_EL1_EOImode_SHIFT;
	/* CBPR */
	val |= (vmcr & ICH_VMCR_CBPR_MASK) >> ICH_VMCR_CBPR_SHIFT;

	vcpu_set_reg(vcpu, rt, val);
}

static void __hyp_text __vgic_v3_write_ctlr(struct kvm_vcpu *vcpu,
					    u32 vmcr, int rt)
{
	u32 val = vcpu_get_reg(vcpu, rt);

	if (val & ICC_CTLR_EL1_CBPR_MASK)
		vmcr |= ICH_VMCR_CBPR_MASK;
	else
		vmcr &= ~ICH_VMCR_CBPR_MASK;

	if (val & ICC_CTLR_EL1_EOImode_MASK)
		vmcr |= ICH_VMCR_EOIM_MASK;
	else
		vmcr &= ~ICH_VMCR_EOIM_MASK;

	write_gicreg(vmcr, ICH_VMCR_EL2);
}

int __hyp_text __vgic_v3_perform_cpuif_access(struct kvm_vcpu *vcpu)
{
	int rt;
	u32 esr;
	u32 vmcr;
	void (*fn)(struct kvm_vcpu *, u32, int);
	bool is_read;
	u32 sysreg;

	esr = kvm_vcpu_get_hsr(vcpu);
	if (vcpu_mode_is_32bit(vcpu)) {
		if (!kvm_condition_valid(vcpu))
			return 1;

		sysreg = esr_cp15_to_sysreg(esr);
	} else {
		sysreg = esr_sys64_to_sysreg(esr);
	}

	is_read = (esr & ESR_ELx_SYS64_ISS_DIR_MASK) == ESR_ELx_SYS64_ISS_DIR_READ;

	switch (sysreg) {
	case SYS_ICC_IAR0_EL1:
	case SYS_ICC_IAR1_EL1:
		if (unlikely(!is_read))
			return 0;
		fn = __vgic_v3_read_iar;
		break;
	case SYS_ICC_EOIR0_EL1:
	case SYS_ICC_EOIR1_EL1:
		if (unlikely(is_read))
			return 0;
		fn = __vgic_v3_write_eoir;
		break;
	case SYS_ICC_IGRPEN1_EL1:
		if (is_read)
			fn = __vgic_v3_read_igrpen1;
		else
			fn = __vgic_v3_write_igrpen1;
		break;
	case SYS_ICC_BPR1_EL1:
		if (is_read)
			fn = __vgic_v3_read_bpr1;
		else
			fn = __vgic_v3_write_bpr1;
		break;
	case SYS_ICC_AP0Rn_EL1(0):
	case SYS_ICC_AP1Rn_EL1(0):
		if (is_read)
			fn = __vgic_v3_read_apxr0;
		else
			fn = __vgic_v3_write_apxr0;
		break;
	case SYS_ICC_AP0Rn_EL1(1):
	case SYS_ICC_AP1Rn_EL1(1):
		if (is_read)
			fn = __vgic_v3_read_apxr1;
		else
			fn = __vgic_v3_write_apxr1;
		break;
	case SYS_ICC_AP0Rn_EL1(2):
	case SYS_ICC_AP1Rn_EL1(2):
		if (is_read)
			fn = __vgic_v3_read_apxr2;
		else
			fn = __vgic_v3_write_apxr2;
		break;
	case SYS_ICC_AP0Rn_EL1(3):
	case SYS_ICC_AP1Rn_EL1(3):
		if (is_read)
			fn = __vgic_v3_read_apxr3;
		else
			fn = __vgic_v3_write_apxr3;
		break;
	case SYS_ICC_HPPIR0_EL1:
	case SYS_ICC_HPPIR1_EL1:
		if (unlikely(!is_read))
			return 0;
		fn = __vgic_v3_read_hppir;
		break;
	case SYS_ICC_IGRPEN0_EL1:
		if (is_read)
			fn = __vgic_v3_read_igrpen0;
		else
			fn = __vgic_v3_write_igrpen0;
		break;
	case SYS_ICC_BPR0_EL1:
		if (is_read)
			fn = __vgic_v3_read_bpr0;
		else
			fn = __vgic_v3_write_bpr0;
		break;
	case SYS_ICC_DIR_EL1:
		if (unlikely(is_read))
			return 0;
		fn = __vgic_v3_write_dir;
		break;
	case SYS_ICC_RPR_EL1:
		if (unlikely(!is_read))
			return 0;
		fn = __vgic_v3_read_rpr;
		break;
	case SYS_ICC_CTLR_EL1:
		if (is_read)
			fn = __vgic_v3_read_ctlr;
		else
			fn = __vgic_v3_write_ctlr;
		break;
	case SYS_ICC_PMR_EL1:
		if (is_read)
			fn = __vgic_v3_read_pmr;
		else
			fn = __vgic_v3_write_pmr;
		break;
	default:
		return 0;
	}

	vmcr = __vgic_v3_read_vmcr();
	rt = kvm_vcpu_sys_get_rt(vcpu);
	fn(vcpu, vmcr, rt);

	return 1;
}

#endif

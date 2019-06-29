// SPDX-License-Identifier: GPL-2.0-only
/*
 * (not much of an) Emulation layer for 32bit guests.
 *
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * based on arch/arm/kvm/emulate.c
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>

/*
 * Table taken from ARMv8 ARM DDI0487B-B, table G1-10.
 */
static const u8 return_offsets[8][2] = {
	[0] = { 0, 0 },		/* Reset, unused */
	[1] = { 4, 2 },		/* Undefined */
	[2] = { 0, 0 },		/* SVC, unused */
	[3] = { 4, 4 },		/* Prefetch abort */
	[4] = { 8, 8 },		/* Data abort */
	[5] = { 0, 0 },		/* HVC, unused */
	[6] = { 4, 4 },		/* IRQ, unused */
	[7] = { 4, 4 },		/* FIQ, unused */
};

static void prepare_fault32(struct kvm_vcpu *vcpu, u32 mode, u32 vect_offset)
{
	unsigned long cpsr;
	unsigned long new_spsr_value = *vcpu_cpsr(vcpu);
	bool is_thumb = (new_spsr_value & PSR_AA32_T_BIT);
	u32 return_offset = return_offsets[vect_offset >> 2][is_thumb];
	u32 sctlr = vcpu_cp15(vcpu, c1_SCTLR);

	cpsr = mode | PSR_AA32_I_BIT;

	if (sctlr & (1 << 30))
		cpsr |= PSR_AA32_T_BIT;
	if (sctlr & (1 << 25))
		cpsr |= PSR_AA32_E_BIT;

	*vcpu_cpsr(vcpu) = cpsr;

	/* Note: These now point to the banked copies */
	vcpu_write_spsr(vcpu, new_spsr_value);
	*vcpu_reg32(vcpu, 14) = *vcpu_pc(vcpu) + return_offset;

	/* Branch to exception vector */
	if (sctlr & (1 << 13))
		vect_offset += 0xffff0000;
	else /* always have security exceptions */
		vect_offset += vcpu_cp15(vcpu, c12_VBAR);

	*vcpu_pc(vcpu) = vect_offset;
}

void kvm_inject_undef32(struct kvm_vcpu *vcpu)
{
	prepare_fault32(vcpu, PSR_AA32_MODE_UND, 4);
}

/*
 * Modelled after TakeDataAbortException() and TakePrefetchAbortException
 * pseudocode.
 */
static void inject_abt32(struct kvm_vcpu *vcpu, bool is_pabt,
			 unsigned long addr)
{
	u32 vect_offset;
	u32 *far, *fsr;
	bool is_lpae;

	if (is_pabt) {
		vect_offset = 12;
		far = &vcpu_cp15(vcpu, c6_IFAR);
		fsr = &vcpu_cp15(vcpu, c5_IFSR);
	} else { /* !iabt */
		vect_offset = 16;
		far = &vcpu_cp15(vcpu, c6_DFAR);
		fsr = &vcpu_cp15(vcpu, c5_DFSR);
	}

	prepare_fault32(vcpu, PSR_AA32_MODE_ABT | PSR_AA32_A_BIT, vect_offset);

	*far = addr;

	/* Give the guest an IMPLEMENTATION DEFINED exception */
	is_lpae = (vcpu_cp15(vcpu, c2_TTBCR) >> 31);
	if (is_lpae)
		*fsr = 1 << 9 | 0x34;
	else
		*fsr = 0x14;
}

void kvm_inject_dabt32(struct kvm_vcpu *vcpu, unsigned long addr)
{
	inject_abt32(vcpu, false, addr);
}

void kvm_inject_pabt32(struct kvm_vcpu *vcpu, unsigned long addr)
{
	inject_abt32(vcpu, true, addr);
}

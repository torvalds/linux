/*
 * (not much of an) Emulation layer for 32bit guests.
 *
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * based on arch/arm/kvm/emulate.c
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software: you can redistribute it and/or modify
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

#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>

/*
 * stolen from arch/arm/kernel/opcodes.c
 *
 * condition code lookup table
 * index into the table is test code: EQ, NE, ... LT, GT, AL, NV
 *
 * bit position in short is condition code: NZCV
 */
static const unsigned short cc_map[16] = {
	0xF0F0,			/* EQ == Z set            */
	0x0F0F,			/* NE                     */
	0xCCCC,			/* CS == C set            */
	0x3333,			/* CC                     */
	0xFF00,			/* MI == N set            */
	0x00FF,			/* PL                     */
	0xAAAA,			/* VS == V set            */
	0x5555,			/* VC                     */
	0x0C0C,			/* HI == C set && Z clear */
	0xF3F3,			/* LS == C clear || Z set */
	0xAA55,			/* GE == (N==V)           */
	0x55AA,			/* LT == (N!=V)           */
	0x0A05,			/* GT == (!Z && (N==V))   */
	0xF5FA,			/* LE == (Z || (N!=V))    */
	0xFFFF,			/* AL always              */
	0			/* NV                     */
};

/*
 * Check if a trapped instruction should have been executed or not.
 */
bool __hyp_text kvm_condition_valid32(const struct kvm_vcpu *vcpu)
{
	unsigned long cpsr;
	u32 cpsr_cond;
	int cond;

	/* Top two bits non-zero?  Unconditional. */
	if (kvm_vcpu_get_hsr(vcpu) >> 30)
		return true;

	/* Is condition field valid? */
	cond = kvm_vcpu_get_condition(vcpu);
	if (cond == 0xE)
		return true;

	cpsr = *vcpu_cpsr(vcpu);

	if (cond < 0) {
		/* This can happen in Thumb mode: examine IT state. */
		unsigned long it;

		it = ((cpsr >> 8) & 0xFC) | ((cpsr >> 25) & 0x3);

		/* it == 0 => unconditional. */
		if (it == 0)
			return true;

		/* The cond for this insn works out as the top 4 bits. */
		cond = (it >> 4);
	}

	cpsr_cond = cpsr >> 28;

	if (!((cc_map[cond] >> cpsr_cond) & 1))
		return false;

	return true;
}

/**
 * adjust_itstate - adjust ITSTATE when emulating instructions in IT-block
 * @vcpu:	The VCPU pointer
 *
 * When exceptions occur while instructions are executed in Thumb IF-THEN
 * blocks, the ITSTATE field of the CPSR is not advanced (updated), so we have
 * to do this little bit of work manually. The fields map like this:
 *
 * IT[7:0] -> CPSR[26:25],CPSR[15:10]
 */
static void __hyp_text kvm_adjust_itstate(struct kvm_vcpu *vcpu)
{
	unsigned long itbits, cond;
	unsigned long cpsr = *vcpu_cpsr(vcpu);
	bool is_arm = !(cpsr & COMPAT_PSR_T_BIT);

	if (is_arm || !(cpsr & COMPAT_PSR_IT_MASK))
		return;

	cond = (cpsr & 0xe000) >> 13;
	itbits = (cpsr & 0x1c00) >> (10 - 2);
	itbits |= (cpsr & (0x3 << 25)) >> 25;

	/* Perform ITAdvance (see page A2-52 in ARM DDI 0406C) */
	if ((itbits & 0x7) == 0)
		itbits = cond = 0;
	else
		itbits = (itbits << 1) & 0x1f;

	cpsr &= ~COMPAT_PSR_IT_MASK;
	cpsr |= cond << 13;
	cpsr |= (itbits & 0x1c) << (10 - 2);
	cpsr |= (itbits & 0x3) << 25;
	*vcpu_cpsr(vcpu) = cpsr;
}

/**
 * kvm_skip_instr - skip a trapped instruction and proceed to the next
 * @vcpu: The vcpu pointer
 */
void __hyp_text kvm_skip_instr32(struct kvm_vcpu *vcpu, bool is_wide_instr)
{
	bool is_thumb;

	is_thumb = !!(*vcpu_cpsr(vcpu) & COMPAT_PSR_T_BIT);
	if (is_thumb && !is_wide_instr)
		*vcpu_pc(vcpu) += 2;
	else
		*vcpu_pc(vcpu) += 4;
	kvm_adjust_itstate(vcpu);
}

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
	bool is_thumb = (new_spsr_value & COMPAT_PSR_T_BIT);
	u32 return_offset = return_offsets[vect_offset >> 2][is_thumb];
	u32 sctlr = vcpu_cp15(vcpu, c1_SCTLR);

	cpsr = mode | COMPAT_PSR_I_BIT;

	if (sctlr & (1 << 30))
		cpsr |= COMPAT_PSR_T_BIT;
	if (sctlr & (1 << 25))
		cpsr |= COMPAT_PSR_E_BIT;

	*vcpu_cpsr(vcpu) = cpsr;

	/* Note: These now point to the banked copies */
	*vcpu_spsr(vcpu) = new_spsr_value;
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
	prepare_fault32(vcpu, COMPAT_PSR_MODE_UND, 4);
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

	prepare_fault32(vcpu, COMPAT_PSR_MODE_ABT | COMPAT_PSR_A_BIT, vect_offset);

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

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

#include <linux/bits.h>
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

/*
 * When an exception is taken, most CPSR fields are left unchanged in the
 * handler. However, some are explicitly overridden (e.g. M[4:0]).
 *
 * The SPSR/SPSR_ELx layouts differ, and the below is intended to work with
 * either format. Note: SPSR.J bit doesn't exist in SPSR_ELx, but this bit was
 * obsoleted by the ARMv7 virtualization extensions and is RES0.
 *
 * For the SPSR layout seen from AArch32, see:
 * - ARM DDI 0406C.d, page B1-1148
 * - ARM DDI 0487E.a, page G8-6264
 *
 * For the SPSR_ELx layout for AArch32 seen from AArch64, see:
 * - ARM DDI 0487E.a, page C5-426
 *
 * Here we manipulate the fields in order of the AArch32 SPSR_ELx layout, from
 * MSB to LSB.
 */
static unsigned long get_except32_cpsr(struct kvm_vcpu *vcpu, u32 mode)
{
	u32 sctlr = vcpu_cp15(vcpu, c1_SCTLR);
	unsigned long old, new;

	old = *vcpu_cpsr(vcpu);
	new = 0;

	new |= (old & PSR_AA32_N_BIT);
	new |= (old & PSR_AA32_Z_BIT);
	new |= (old & PSR_AA32_C_BIT);
	new |= (old & PSR_AA32_V_BIT);
	new |= (old & PSR_AA32_Q_BIT);

	// CPSR.IT[7:0] are set to zero upon any exception
	// See ARM DDI 0487E.a, section G1.12.3
	// See ARM DDI 0406C.d, section B1.8.3

	new |= (old & PSR_AA32_DIT_BIT);

	// CPSR.SSBS is set to SCTLR.DSSBS upon any exception
	// See ARM DDI 0487E.a, page G8-6244
	if (sctlr & BIT(31))
		new |= PSR_AA32_SSBS_BIT;

	// CPSR.PAN is unchanged unless SCTLR.SPAN == 0b0
	// SCTLR.SPAN is RES1 when ARMv8.1-PAN is not implemented
	// See ARM DDI 0487E.a, page G8-6246
	new |= (old & PSR_AA32_PAN_BIT);
	if (!(sctlr & BIT(23)))
		new |= PSR_AA32_PAN_BIT;

	// SS does not exist in AArch32, so ignore

	// CPSR.IL is set to zero upon any exception
	// See ARM DDI 0487E.a, page G1-5527

	new |= (old & PSR_AA32_GE_MASK);

	// CPSR.IT[7:0] are set to zero upon any exception
	// See prior comment above

	// CPSR.E is set to SCTLR.EE upon any exception
	// See ARM DDI 0487E.a, page G8-6245
	// See ARM DDI 0406C.d, page B4-1701
	if (sctlr & BIT(25))
		new |= PSR_AA32_E_BIT;

	// CPSR.A is unchanged upon an exception to Undefined, Supervisor
	// CPSR.A is set upon an exception to other modes
	// See ARM DDI 0487E.a, pages G1-5515 to G1-5516
	// See ARM DDI 0406C.d, page B1-1182
	new |= (old & PSR_AA32_A_BIT);
	if (mode != PSR_AA32_MODE_UND && mode != PSR_AA32_MODE_SVC)
		new |= PSR_AA32_A_BIT;

	// CPSR.I is set upon any exception
	// See ARM DDI 0487E.a, pages G1-5515 to G1-5516
	// See ARM DDI 0406C.d, page B1-1182
	new |= PSR_AA32_I_BIT;

	// CPSR.F is set upon an exception to FIQ
	// CPSR.F is unchanged upon an exception to other modes
	// See ARM DDI 0487E.a, pages G1-5515 to G1-5516
	// See ARM DDI 0406C.d, page B1-1182
	new |= (old & PSR_AA32_F_BIT);
	if (mode == PSR_AA32_MODE_FIQ)
		new |= PSR_AA32_F_BIT;

	// CPSR.T is set to SCTLR.TE upon any exception
	// See ARM DDI 0487E.a, page G8-5514
	// See ARM DDI 0406C.d, page B1-1181
	if (sctlr & BIT(30))
		new |= PSR_AA32_T_BIT;

	new |= mode;

	return new;
}

static void prepare_fault32(struct kvm_vcpu *vcpu, u32 mode, u32 vect_offset)
{
	unsigned long spsr = *vcpu_cpsr(vcpu);
	bool is_thumb = (spsr & PSR_AA32_T_BIT);
	u32 return_offset = return_offsets[vect_offset >> 2][is_thumb];
	u32 sctlr = vcpu_cp15(vcpu, c1_SCTLR);

	*vcpu_cpsr(vcpu) = get_except32_cpsr(vcpu, mode);

	/* Note: These now point to the banked copies */
	vcpu_write_spsr(vcpu, host_spsr_to_spsr32(spsr));
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

	prepare_fault32(vcpu, PSR_AA32_MODE_ABT, vect_offset);

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

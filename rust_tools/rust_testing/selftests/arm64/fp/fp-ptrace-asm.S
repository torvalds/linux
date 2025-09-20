// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2021-3 ARM Limited.
//
// Assembly portion of the FP ptrace test

//
// Load values from memory into registers, break on a breakpoint, then
// break on a further breakpoint
//

#include "fp-ptrace.h"
#include "sme-inst.h"

.arch_extension sve

// Load and save register values with pauses for ptrace
//
// x0 - HAVE_ flags indicating which features are in use

.globl load_and_save
load_and_save:
	stp	x11, x12, [sp, #-0x10]!

	// This should be redundant in the SVE case
	ldr	x7, =v_in
	ldp	q0, q1, [x7]
	ldp	q2, q3, [x7, #16 * 2]
	ldp	q4, q5, [x7, #16 * 4]
	ldp	q6, q7, [x7, #16 * 6]
	ldp	q8, q9, [x7, #16 * 8]
	ldp	q10, q11, [x7, #16 * 10]
	ldp	q12, q13, [x7, #16 * 12]
	ldp	q14, q15, [x7, #16 * 14]
	ldp	q16, q17, [x7, #16 * 16]
	ldp	q18, q19, [x7, #16 * 18]
	ldp	q20, q21, [x7, #16 * 20]
	ldp	q22, q23, [x7, #16 * 22]
	ldp	q24, q25, [x7, #16 * 24]
	ldp	q26, q27, [x7, #16 * 26]
	ldp	q28, q29, [x7, #16 * 28]
	ldp	q30, q31, [x7, #16 * 30]

	// SME?
	tbz	x0, #HAVE_SME_SHIFT, check_sve_in

	adrp	x7, svcr_in
	ldr	x7, [x7, :lo12:svcr_in]
	// SVCR is 0 by default, avoid triggering SME if not in use
	cbz	x7, check_sve_in
	msr	S3_3_C4_C2_2, x7

	// ZA?
	tbz	x7, #SVCR_ZA_SHIFT, check_sm_in
	rdsvl	11, 1
	mov	w12, #0
	ldr	x6, =za_in
1:	_ldr_za 12, 6
	add	x6, x6, x11
	add	x12, x12, #1
	cmp	x11, x12
	bne	1b

	// ZT?
	tbz	x0, #HAVE_SME2_SHIFT, check_sm_in
	adrp	x6, zt_in
	add	x6, x6, :lo12:zt_in
	_ldr_zt 6

	// In streaming mode?
check_sm_in:
	tbz	x7, #SVCR_SM_SHIFT, check_sve_in

	// Load FFR if we have FA64
	ubfx	x4, x0, #HAVE_FA64_SHIFT, #1
	b	load_sve

	// SVE?
check_sve_in:
	tbz	x0, #HAVE_SVE_SHIFT, check_fpmr_in
	mov	x4, #1

load_sve:
	ldr	x7, =z_in
	ldr	z0, [x7, #0, MUL VL]
	ldr	z1, [x7, #1, MUL VL]
	ldr	z2, [x7, #2, MUL VL]
	ldr	z3, [x7, #3, MUL VL]
	ldr	z4, [x7, #4, MUL VL]
	ldr	z5, [x7, #5, MUL VL]
	ldr	z6, [x7, #6, MUL VL]
	ldr	z7, [x7, #7, MUL VL]
	ldr	z8, [x7, #8, MUL VL]
	ldr	z9, [x7, #9, MUL VL]
	ldr	z10, [x7, #10, MUL VL]
	ldr	z11, [x7, #11, MUL VL]
	ldr	z12, [x7, #12, MUL VL]
	ldr	z13, [x7, #13, MUL VL]
	ldr	z14, [x7, #14, MUL VL]
	ldr	z15, [x7, #15, MUL VL]
	ldr	z16, [x7, #16, MUL VL]
	ldr	z17, [x7, #17, MUL VL]
	ldr	z18, [x7, #18, MUL VL]
	ldr	z19, [x7, #19, MUL VL]
	ldr	z20, [x7, #20, MUL VL]
	ldr	z21, [x7, #21, MUL VL]
	ldr	z22, [x7, #22, MUL VL]
	ldr	z23, [x7, #23, MUL VL]
	ldr	z24, [x7, #24, MUL VL]
	ldr	z25, [x7, #25, MUL VL]
	ldr	z26, [x7, #26, MUL VL]
	ldr	z27, [x7, #27, MUL VL]
	ldr	z28, [x7, #28, MUL VL]
	ldr	z29, [x7, #29, MUL VL]
	ldr	z30, [x7, #30, MUL VL]
	ldr	z31, [x7, #31, MUL VL]

	// FFR is not present in base SME
	cbz	x4, 1f
	ldr	x7, =ffr_in
	ldr	p0, [x7]
	ldr	x7, [x7, #0]
	cbz	x7, 1f
	wrffr	p0.b
1:

	ldr	x7, =p_in
	ldr	p0, [x7, #0, MUL VL]
	ldr	p1, [x7, #1, MUL VL]
	ldr	p2, [x7, #2, MUL VL]
	ldr	p3, [x7, #3, MUL VL]
	ldr	p4, [x7, #4, MUL VL]
	ldr	p5, [x7, #5, MUL VL]
	ldr	p6, [x7, #6, MUL VL]
	ldr	p7, [x7, #7, MUL VL]
	ldr	p8, [x7, #8, MUL VL]
	ldr	p9, [x7, #9, MUL VL]
	ldr	p10, [x7, #10, MUL VL]
	ldr	p11, [x7, #11, MUL VL]
	ldr	p12, [x7, #12, MUL VL]
	ldr	p13, [x7, #13, MUL VL]
	ldr	p14, [x7, #14, MUL VL]
	ldr	p15, [x7, #15, MUL VL]

	// This has to come after we set PSTATE.SM
check_fpmr_in:
	tbz	x0, #HAVE_FPMR_SHIFT, wait_for_writes
	adrp	x7, fpmr_in
	ldr	x7, [x7, :lo12:fpmr_in]
	msr	REG_FPMR, x7

wait_for_writes:
	// Wait for the parent
	brk #0

	// Save values
	ldr	x7, =v_out
	stp	q0, q1, [x7]
	stp	q2, q3, [x7, #16 * 2]
	stp	q4, q5, [x7, #16 * 4]
	stp	q6, q7, [x7, #16 * 6]
	stp	q8, q9, [x7, #16 * 8]
	stp	q10, q11, [x7, #16 * 10]
	stp	q12, q13, [x7, #16 * 12]
	stp	q14, q15, [x7, #16 * 14]
	stp	q16, q17, [x7, #16 * 16]
	stp	q18, q19, [x7, #16 * 18]
	stp	q20, q21, [x7, #16 * 20]
	stp	q22, q23, [x7, #16 * 22]
	stp	q24, q25, [x7, #16 * 24]
	stp	q26, q27, [x7, #16 * 26]
	stp	q28, q29, [x7, #16 * 28]
	stp	q30, q31, [x7, #16 * 30]

	tbz	x0, #HAVE_FPMR_SHIFT, check_sme_out
	mrs	x7, REG_FPMR
	adrp	x6, fpmr_out
	str	x7, [x6, :lo12:fpmr_out]

check_sme_out:
	tbz	x0, #HAVE_SME_SHIFT, check_sve_out

	rdsvl	11, 1
	adrp	x6, sme_vl_out
	str	x11, [x6, :lo12:sme_vl_out]

	mrs	x7, S3_3_C4_C2_2
	adrp	x6, svcr_out
	str	x7, [x6, :lo12:svcr_out]

	// ZA?
	tbz	x7, #SVCR_ZA_SHIFT, check_sm_out
	mov	w12, #0
	ldr	x6, =za_out
1:	_str_za 12, 6
	add	x6, x6, x11
	add	x12, x12, #1
	cmp	x11, x12
	bne	1b

	// ZT?
	tbz	x0, #HAVE_SME2_SHIFT, check_sm_out
	adrp	x6, zt_out
	add	x6, x6, :lo12:zt_out
	_str_zt 6

	// In streaming mode?
check_sm_out:
	tbz	x7, #SVCR_SM_SHIFT, check_sve_out

	// Do we have FA64 and FFR?
	ubfx	x4, x0, #HAVE_FA64_SHIFT, #1
	b	read_sve

	// SVE?
check_sve_out:
	tbz	x0, #HAVE_SVE_SHIFT, wait_for_reads
	mov	x4, #1

	rdvl	x7, #1
	adrp	x6, sve_vl_out
	str	x7, [x6, :lo12:sve_vl_out]

read_sve:
	ldr	x7, =z_out
	str	z0, [x7, #0, MUL VL]
	str	z1, [x7, #1, MUL VL]
	str	z2, [x7, #2, MUL VL]
	str	z3, [x7, #3, MUL VL]
	str	z4, [x7, #4, MUL VL]
	str	z5, [x7, #5, MUL VL]
	str	z6, [x7, #6, MUL VL]
	str	z7, [x7, #7, MUL VL]
	str	z8, [x7, #8, MUL VL]
	str	z9, [x7, #9, MUL VL]
	str	z10, [x7, #10, MUL VL]
	str	z11, [x7, #11, MUL VL]
	str	z12, [x7, #12, MUL VL]
	str	z13, [x7, #13, MUL VL]
	str	z14, [x7, #14, MUL VL]
	str	z15, [x7, #15, MUL VL]
	str	z16, [x7, #16, MUL VL]
	str	z17, [x7, #17, MUL VL]
	str	z18, [x7, #18, MUL VL]
	str	z19, [x7, #19, MUL VL]
	str	z20, [x7, #20, MUL VL]
	str	z21, [x7, #21, MUL VL]
	str	z22, [x7, #22, MUL VL]
	str	z23, [x7, #23, MUL VL]
	str	z24, [x7, #24, MUL VL]
	str	z25, [x7, #25, MUL VL]
	str	z26, [x7, #26, MUL VL]
	str	z27, [x7, #27, MUL VL]
	str	z28, [x7, #28, MUL VL]
	str	z29, [x7, #29, MUL VL]
	str	z30, [x7, #30, MUL VL]
	str	z31, [x7, #31, MUL VL]

	ldr	x7, =p_out
	str	p0, [x7, #0, MUL VL]
	str	p1, [x7, #1, MUL VL]
	str	p2, [x7, #2, MUL VL]
	str	p3, [x7, #3, MUL VL]
	str	p4, [x7, #4, MUL VL]
	str	p5, [x7, #5, MUL VL]
	str	p6, [x7, #6, MUL VL]
	str	p7, [x7, #7, MUL VL]
	str	p8, [x7, #8, MUL VL]
	str	p9, [x7, #9, MUL VL]
	str	p10, [x7, #10, MUL VL]
	str	p11, [x7, #11, MUL VL]
	str	p12, [x7, #12, MUL VL]
	str	p13, [x7, #13, MUL VL]
	str	p14, [x7, #14, MUL VL]
	str	p15, [x7, #15, MUL VL]

	// Only save FFR if it exists
	cbz	x4, wait_for_reads
	ldr	x7, =ffr_out
	rdffr	p0.b
	str	p0, [x7]

wait_for_reads:
	// Wait for the parent
	brk #0

	// Ensure we don't leave ourselves in streaming mode
	tbz	x0, #HAVE_SME_SHIFT, out
	msr	S3_3_C4_C2_2, xzr

out:
	ldp	x11, x12, [sp, #-0x10]
	ret

/* SPDX-License-Identifier: GPL-2.0 */

#include "processor.h"

/* address of refill exception should be 4K aligned */
.balign	4096
.global handle_tlb_refill
handle_tlb_refill:
	csrwr	t0, LOONGARCH_CSR_TLBRSAVE
	csrrd	t0, LOONGARCH_CSR_PGD
	lddir	t0, t0, 3
	lddir	t0, t0, 1
	ldpte	t0, 0
	ldpte	t0, 1
	tlbfill
	csrrd	t0, LOONGARCH_CSR_TLBRSAVE
	ertn

	/*
	 * save and restore all gprs except base register,
	 * and default value of base register is sp ($r3).
	 */
.macro save_gprs base
	.irp n,1,2,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	st.d    $r\n, \base, 8 * \n
	.endr
.endm

.macro restore_gprs base
	.irp n,1,2,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	ld.d    $r\n, \base, 8 * \n
	.endr
.endm

/* address of general exception should be 4K aligned */
.balign	4096
.global handle_exception
handle_exception:
	csrwr  sp, LOONGARCH_CSR_KS0
	csrrd  sp, LOONGARCH_CSR_KS1
	addi.d sp, sp, -EXREGS_SIZE

	save_gprs sp
	/* save sp register to stack */
	csrrd  t0, LOONGARCH_CSR_KS0
	st.d   t0, sp, 3 * 8

	csrrd  t0, LOONGARCH_CSR_ERA
	st.d   t0, sp, PC_OFFSET_EXREGS
	csrrd  t0, LOONGARCH_CSR_ESTAT
	st.d   t0, sp, ESTAT_OFFSET_EXREGS
	csrrd  t0, LOONGARCH_CSR_BADV
	st.d   t0, sp, BADV_OFFSET_EXREGS

	or     a0, sp, zero
	bl route_exception
	restore_gprs sp
	csrrd  sp, LOONGARCH_CSR_KS0
	ertn

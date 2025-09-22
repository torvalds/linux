/*	$OpenBSD: prom_swpal.S,v 1.3 1996/10/30 22:40:45 niklas Exp $	*/
/*	$NetBSD: prom_swpal.S,v 1.4 1996/10/17 02:50:41 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Keith Bostic
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include "../../include/asm.h"
#include "../../include/prom.h"
#include "../../include/rpb.h"

/* Offsets from base of HWRPB. */	
#define	RPB_SELFREF	0x00	
#define	RPB_SLOTSIZE 	0x98
#define	RPB_PERCPU_OFF	0xA0

/* Offsets in a boot PCB. */
#define	PCB_KSP		0x00	
#define	PCB_PTBR	0x10
#define	PCB_ASN		0x1c
#define	PCB_FEN		0x28			

/* Pal values. */
#define PAL_RESERVED	0		/* Reserved to Digital. */
#define PAL_VMS		1		/* VMS */
#define PAL_OSF		2		/* OSF */

/*
 * PAL code switch routine.
 */
#define	D_RA			 (7*8)
#define	D_S0			 (8*8)
#define	D_S1			 (9*8)
#define	D_S2			(10*8)
#define	D_S3			(11*8)
#define	D_S4			(12*8)
#define	D_S5			(13*8)
#define	PALSW_FRAME_SIZE	(14*8)
#define	PALSW_REGS		IM_RA|IM_S0|IM_S1|IM_S2|IM_S3|IM_S4|IM_S5

	.comm	ptbr_save,8

	.text
	.align	4

NESTED(switch_palcode, 0, PALSW_FRAME_SIZE, ra, PALSW_REGS, 0)
	LDGP(pv)
/*	ldgp	gp, 0(pv)*/

	lda 	sp, -PALSW_FRAME_SIZE(sp)
	stq 	ra, D_RA(sp)
	stq	s0, D_S0(sp)
	stq	s1, D_S1(sp)
	stq	s2, D_S2(sp)
	stq	s3, D_S3(sp)
	stq	s4, D_S4(sp)
	stq	s5, D_S5(sp)

	stq	pv, 0(sp)
	stq	gp, 8(sp)
	
	ldiq 	s0, HWRPB_ADDR		/* s0 HWRPB_ADDR */
	ldq 	s1, RPB_SLOTSIZE(s0)
	call_pal PAL_VMS_mfpr_whami
	mulq 	s1, v0, s1		/* s1 per_cpu offset from base */
	ldq 	s2, RPB_PERCPU_OFF(s0)
	addq 	s0, s2, s2
	addq	s2, s1, s2		/* s2  PCB (virtual) */
	
	call_pal PAL_VMS_mfpr_ptbr
	stq 	v0,   PCB_PTBR(s2)
	stq	v0,   ptbr_save		/* save PTBR for the kernel */
	stl 	zero, PCB_ASN(s2)
	stq	zero, PCB_FEN(s2)
	stq	sp,   PCB_KSP(s2)
	
	ldq	t0, RPB_SELFREF(s0)	/* HWRBP base (physical) */
	ldq 	t1, RPB_PERCPU_OFF(s0)
	addq 	t0, t1, t0
	addq 	t0, s1, t0		/* PCB base (phys) */
	stq 	t0, 16(sp)

	call_pal PAL_VMS_mfpr_vptb
	mov 	v0, a3
	ldiq	a0, PAL_OSF
	lda	a1, contin
	ldq 	a2, 16(sp)

	call_pal PAL_swppal		/* a0, a1, a2, a3 */

contin:	ldq	pv, 0(sp)
	ldq	gp, 8(sp)

	ldq	ra, D_RA(sp)
	ldq	s0, D_S0(sp)
	ldq	s1, D_S1(sp)
	ldq	s2, D_S2(sp)
	ldq	s3, D_S3(sp)
	ldq	s4, D_S4(sp)
	ldq	s5, D_S5(sp)
	lda	sp, PALSW_FRAME_SIZE(sp)
	RET
END(switch_palcode)

#undef	D_RA
#undef	D_S0
#undef	D_S1
#undef	D_S2
#undef	D_S3
#undef	D_S4
#undef	D_S5
#undef	PALSW_FRAME_SIZE
#undef	PALSW_REGS

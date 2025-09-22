/*	$OpenBSD: profile.h,v 1.5 1997/01/24 19:57:17 niklas Exp $	*/
/*	$NetBSD: profile.h,v 1.7 1996/11/13 22:21:01 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
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

#define	_MCOUNT_DECL	void mcount

#if 0
/*
 * XXX The definition of MCOUNT below is really the following code, run
 * XXX through cpp, since the inline assembly isn't preprocessed.
 */
#define	OFFSET_AT	0
#define OFFSET_V0	8
#define OFFSET_T0	16
#define OFFSET_T1	24
#define OFFSET_T2	32
#define OFFSET_T3	40
#define OFFSET_T4	48
#define OFFSET_T5	56
#define OFFSET_T6	64
#define OFFSET_T7	72
#define OFFSET_S6	80
#define OFFSET_A0	88
#define OFFSET_A1	96
#define OFFSET_A2	104
#define OFFSET_A3	112
#define OFFSET_A4	120
#define OFFSET_A5	128
#define OFFSET_T8	136
#define OFFSET_T9	144
#define OFFSET_T10	152
#define OFFSET_T11	160
#define OFFSET_RA	168
#define OFFSET_T12	176
#define OFFSET_GP	184
#define	FRAME_SIZE	192

LEAF(_mcount,0)			/* XXX */
	.set noat
	.set noreorder

	lda	sp, -FRAME_SIZE(sp)

	stq	at_reg, OFFSET_AT(sp)
	stq	v0, OFFSET_V0(sp)
	stq	t0, OFFSET_T0(sp)
	stq	t1, OFFSET_T1(sp)
	stq	t2, OFFSET_T2(sp)
	stq	t3, OFFSET_T3(sp)
	stq	t4, OFFSET_T4(sp)
	stq	t5, OFFSET_T5(sp)
	stq	t6, OFFSET_T6(sp)
	stq	t7, OFFSET_T7(sp)
	stq	s6, OFFSET_S6(sp)	/* XXX because run _after_ prologue. */
	stq	a0, OFFSET_A0(sp)
	stq	a1, OFFSET_A1(sp)
	stq	a2, OFFSET_A2(sp)
	stq	a3, OFFSET_A3(sp)
	stq	a4, OFFSET_A4(sp)
	stq	a5, OFFSET_A5(sp)
	stq	t8, OFFSET_T8(sp)
	stq	t9, OFFSET_T9(sp)
	stq	t10, OFFSET_T10(sp)
	stq	t11, OFFSET_T11(sp)
	stq	ra, OFFSET_RA(sp)
	stq	t12, OFFSET_T12(sp)
	stq	gp, OFFSET_GP(sp)

	br	pv, LX99
LX99:	SETGP(pv)
	mov	ra, a0
	mov	at_reg, a1
	CALL(mcount)

	ldq	v0, OFFSET_V0(sp)
	ldq	t0, OFFSET_T0(sp)
	ldq	t1, OFFSET_T1(sp)
	ldq	t2, OFFSET_T2(sp)
	ldq	t3, OFFSET_T3(sp)
	ldq	t4, OFFSET_T4(sp)
	ldq	t5, OFFSET_T5(sp)
	ldq	t6, OFFSET_T6(sp)
	ldq	t7, OFFSET_T7(sp)
	ldq	s6, OFFSET_S6(sp)	/* XXX because run _after_ prologue. */
	ldq	a0, OFFSET_A0(sp)
	ldq	a1, OFFSET_A1(sp)
	ldq	a2, OFFSET_A2(sp)
	ldq	a3, OFFSET_A3(sp)
	ldq	a4, OFFSET_A4(sp)
	ldq	a5, OFFSET_A5(sp)
	ldq	t8, OFFSET_T8(sp)
	ldq	t9, OFFSET_T9(sp)
	ldq	t10, OFFSET_T10(sp)
	ldq	t11, OFFSET_T11(sp)
	ldq	ra, OFFSET_RA(sp)
	stq	t12, OFFSET_T12(sp)
	ldq	gp, OFFSET_GP(sp)

	ldq	at_reg, OFFSET_AT(sp)

	lda	sp, FRAME_SIZE(sp)
	ret	zero, (at_reg), 1

	END(_mcount)
#endif /* 0 */

#define MCOUNT asm("		\
	.globl	_mcount;	\
	.ent	_mcount 0;	\
_mcount:;			\
	.frame	$30,0,$26;	\
	.set noat;		\
	.set noreorder;		\
				\
	lda	$30, -192($30);	\
				\
	stq	$28, 0($30);	\
	stq	$0, 8($30);	\
	stq	$1, 16($30);	\
	stq	$2, 24($30);	\
	stq	$3, 32($30);	\
	stq	$4, 40($30);	\
	stq	$5, 48($30);	\
	stq	$6, 56($30);	\
	stq	$7, 64($30);	\
	stq	$8, 72($30);	\
	stq	$15, 80($30);	\
	stq	$16, 88($30);	\
	stq	$17, 96($30);	\
	stq	$18, 104($30);	\
	stq	$19, 112($30);	\
	stq	$20, 120($30);	\
	stq	$21, 128($30);	\
	stq	$22, 136($30);	\
	stq	$23, 144($30);	\
	stq	$24, 152($30);	\
	stq	$25, 160($30);	\
	stq	$26, 168($30);	\
	stq	$27, 176($30);	\
	stq	$29, 184($30);	\
				\
	br	$27, LX98;	\
LX98:	ldgp	$29,0($27);	\
	mov	$26, $16;	\
	mov	$28, $17;	\
	jsr	$26,mcount;	\
	ldgp	$29,0($26);	\
				\
	ldq	$0, 8($30);	\
	ldq	$1, 16($30);	\
	ldq	$2, 24($30);	\
	ldq	$3, 32($30);	\
	ldq	$4, 40($30);	\
	ldq	$5, 48($30);	\
	ldq	$6, 56($30);	\
	ldq	$7, 64($30);	\
	ldq	$8, 72($30);	\
	ldq	$15, 80($30);	\
	ldq	$16, 88($30);	\
	ldq	$17, 96($30);	\
	ldq	$18, 104($30);	\
	ldq	$19, 112($30);	\
	ldq	$20, 120($30);	\
	ldq	$21, 128($30);	\
	ldq	$22, 136($30);	\
	ldq	$23, 144($30);	\
	ldq	$24, 152($30);	\
	ldq	$25, 160($30);	\
	ldq	$25, 160($30);	\
	ldq	$26, 168($30);	\
	ldq	$27, 176($30);	\
	ldq	$29, 184($30);	\
				\
	lda	$30, 192($30);	\
	ret	$31, ($28), 1;	\
				\
	.end	_mcount");

#ifdef _KERNEL
/*
 * The following two macros do splhigh and splx respectively.
 * _alpha_pal_swpipl is a special version of alpha_pal_swpipl which
 * doesn't include profiling support.
 *
 * XXX These macros should probably use inline assembly.
 */
#define MCOUNT_ENTER \
	s = _alpha_pal_swpipl(ALPHA_PSL_IPL_HIGH)
#define MCOUNT_EXIT \
	(void)_alpha_pal_swpipl(s);
#endif

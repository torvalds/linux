/*	$OpenBSD: setjmp.S,v 1.1 2013/10/28 22:13:13 miod Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1993-1992 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * Copyright (c) 1996 Nivas Madhur
 * Copyright (c) 1998 Steve Murphree, Jr.
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
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

#include <machine/asm.h>

/* int setjmp(label_t *); */
ENTRY(setjmp)
	st	%r1,  %r2, 0
	st	%r14, %r2, 4
	st	%r15, %r2, 2*4
	st	%r16, %r2, 3*4
	st	%r17, %r2, 4*4
	st	%r18, %r2, 5*4
	st	%r19, %r2, 6*4
	st	%r20, %r2, 7*4
	st	%r21, %r2, 8*4
	st	%r22, %r2, 9*4
	st	%r23, %r2, 10*4
	st	%r24, %r2, 11*4
	st	%r25, %r2, 12*4
	st	%r26, %r2, 13*4
	st	%r27, %r2, 14*4
	st	%r28, %r2, 15*4
	st	%r29, %r2, 16*4
	st	%r30, %r2, 17*4
	st	%r31, %r2, 18*4
	jmp.n	%r1
	 or	%r2,  %r0, %r0

/* void longjmp(label_t*); */
ENTRY(longjmp)
	ld	%r1,  %r2, 0
	ld	%r14, %r2, 4
	ld	%r15, %r2, 2*4
	ld	%r16, %r2, 3*4
	ld	%r17, %r2, 4*4
	ld	%r18, %r2, 5*4
	ld	%r19, %r2, 6*4
	ld	%r20, %r2, 7*4
	ld	%r21, %r2, 8*4
	ld	%r22, %r2, 9*4
	ld	%r23, %r2, 10*4
	ld	%r24, %r2, 11*4
	ld	%r25, %r2, 12*4
	ld	%r26, %r2, 13*4
	ld	%r27, %r2, 14*4
	ld	%r28, %r2, 15*4
	ld	%r29, %r2, 16*4
	ld	%r30, %r2, 17*4
	ld	%r31, %r2, 18*4
	jmp.n	%r1
	 or	%r2,  %r0, 1

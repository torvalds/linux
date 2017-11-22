/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.	*/
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _IA32_SYS_TRAP_H
#define	_IA32_SYS_TRAP_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Trap type values
 */

#define	T_ZERODIV	0x0	/* #de	divide by 0 error		*/
#define	T_SGLSTP	0x1	/* #db	single step			*/
#define	T_NMIFLT	0x2	/* 	NMI				*/
#define	T_BPTFLT	0x3	/* #bp	breakpoint fault, INT3 insn	*/
#define	T_OVFLW		0x4	/* #of	INTO overflow fault		*/
#define	T_BOUNDFLT	0x5	/* #br	BOUND insn fault		*/
#define	T_ILLINST	0x6	/* #ud	invalid opcode fault		*/
#define	T_NOEXTFLT	0x7	/* #nm	device not available: x87	*/
#define	T_DBLFLT	0x8	/* #df	double fault			*/
#define	T_EXTOVRFLT	0x9	/* 	[not generated: 386 only]	*/
#define	T_TSSFLT	0xa	/* #ts	invalid TSS fault		*/
#define	T_SEGFLT	0xb	/* #np	segment not present fault	*/
#define	T_STKFLT	0xc	/* #ss	stack fault			*/
#define	T_GPFLT		0xd	/* #gp	general protection fault	*/
#define	T_PGFLT		0xe	/* #pf	page fault			*/
#define	T_EXTERRFLT	0x10	/* #mf	x87 FPU error fault		*/
#define	T_ALIGNMENT	0x11	/* #ac	alignment check error		*/
#define	T_MCE		0x12	/* #mc	machine check exception		*/
#define	T_SIMDFPE	0x13	/* #xm	SSE/SSE exception		*/
#define	T_DBGENTR	0x14	/*	debugger entry 			*/
#define	T_ENDPERR	0x21	/*	emulated extension error flt	*/
#define	T_ENOEXTFLT	0x20	/*	emulated ext not present	*/
#define	T_FASTTRAP	0xd2	/*	fast system call		*/
#define	T_SYSCALLINT	0x91	/*	general system call		*/
#define	T_DTRACE_RET	0x7f	/*	DTrace pid return		*/
#define	T_INT80		0x80	/*	int80 handler for linux emulation */
#define	T_SOFTINT	0x50fd	/*	pseudo softint trap type	*/

/*
 * Pseudo traps.
 */
#define	T_INTERRUPT		0x100
#define	T_FAULT			0x200
#define	T_AST			0x400
#define	T_SYSCALL		0x180


/*
 *  Values of error code on stack in case of page fault
 */

#define	PF_ERR_MASK	0x01	/* Mask for error bit */
#define	PF_ERR_PAGE	0x00	/* page not present */
#define	PF_ERR_PROT	0x01	/* protection error */
#define	PF_ERR_WRITE	0x02	/* fault caused by write (else read) */
#define	PF_ERR_USER	0x04	/* processor was in user mode */
				/*	(else supervisor) */
#define	PF_ERR_EXEC	0x10	/* attempt to execute a No eXec page (AMD) */

/*
 *  Definitions for fast system call subfunctions
 */
#define	T_FNULL		0	/* Null trap for testing		*/
#define	T_FGETFP	1	/* Get emulated FP context		*/
#define	T_FSETFP	2	/* Set emulated FP context		*/
#define	T_GETHRTIME	3	/* Get high resolution time		*/
#define	T_GETHRVTIME	4	/* Get high resolution virtual time	*/
#define	T_GETHRESTIME	5	/* Get high resolution time		*/
#define	T_GETLGRP	6	/* Get home lgrpid			*/

#define	T_LASTFAST	6	/* Last valid subfunction		*/

#ifdef	__cplusplus
}
#endif

#endif	/* _IA32_SYS_TRAP_H */

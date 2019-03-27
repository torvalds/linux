/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 *
 * $FreeBSD$ 
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc. */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T		*/
/*	All Rights Reserved	*/

#ifndef	_REGSET_H
#define	_REGSET_H

/*
 * #pragma ident	"@(#)regset.h	1.11	05/06/08 SMI"
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The names and offsets defined here should be specified by the
 * AMD64 ABI suppl.
 *
 * We make fsbase and gsbase part of the lwp context (since they're
 * the only way to access the full 64-bit address range via the segment
 * registers) and thus belong here too.  However we treat them as
 * read-only; if %fs or %gs are updated, the results of the descriptor
 * table lookup that those updates implicitly cause will be reflected
 * in the corresponding fsbase and/or gsbase values the next time the
 * context can be inspected.  However it is NOT possible to override
 * the fsbase/gsbase settings via this interface.
 *
 * Direct modification of the base registers (thus overriding the
 * descriptor table base address) can be achieved with _lwp_setprivate.
 */

#define	REG_GSBASE	27
#define	REG_FSBASE	26
#ifdef illumos
#define	REG_DS		25
#define	REG_ES		24

#define	REG_GS		23
#define	REG_FS		22
#define	REG_SS		21
#define	REG_RSP		20
#define	REG_RFL		19
#define	REG_CS		18
#define	REG_RIP		17
#define	REG_ERR		16
#define	REG_TRAPNO	15
#define	REG_RAX		14
#define	REG_RCX		13
#define	REG_RDX		12
#define	REG_RBX		11
#define	REG_RBP		10
#define	REG_RSI		9
#define	REG_RDI		8
#define	REG_R8		7
#define	REG_R9		6
#define	REG_R10		5
#define	REG_R11		4
#define	REG_R12		3
#define	REG_R13		2
#define	REG_R14		1
#define	REG_R15		0
#else	/* !illumos */
#define	REG_SS		25
#define	REG_RSP		24
#define	REG_RFL		23
#define	REG_CS		22
#define	REG_RIP		21
#define	REG_DS		20
#define	REG_ES		19
#define	REG_ERR		18
#define	REG_GS		17
#define	REG_FS		16
#define	REG_TRAPNO	15
#define	REG_RAX		14
#define	REG_RCX		13
#define	REG_RDX		12
#define	REG_RBX		11
#define	REG_RBP		10
#define	REG_RSI		9
#define	REG_RDI		8
#define	REG_R8		7
#define	REG_R9		6
#define	REG_R10		5
#define	REG_R11		4
#define	REG_R12		3
#define	REG_R13		2
#define	REG_R14		1
#define	REG_R15		0
#endif	/* illumos */

/*
 * The names and offsets defined here are specified by i386 ABI suppl.
 */

#ifdef illumos
#define	SS		18	/* only stored on a privilege transition */
#define	UESP		17	/* only stored on a privilege transition */
#define	EFL		16
#define	CS		15
#define	EIP		14
#define	ERR		13
#define	TRAPNO		12
#define	EAX		11
#define	ECX		10
#define	EDX		9
#define	EBX		8
#define	ESP		7
#define	EBP		6
#define	ESI		5
#define	EDI		4
#define	DS		3
#define	ES		2
#define	FS		1
#define	GS		0
#else	/* !illumos */
#define	GS		18
#define	SS		17	/* only stored on a privilege transition */
#define	UESP		16	/* only stored on a privilege transition */
#define	EFL		15
#define	CS		14
#define	EIP		13
#define	ERR		12
#define	TRAPNO		11
#define	EAX		10
#define	ECX		9
#define	EDX		8
#define	EBX		7
#define	ESP		6
#define	EBP		5
#define	ESI		4
#define	EDI		3
#define	DS		2
#define	ES		1
#define	FS		0
#endif	/* illumos */

#define REG_PC  EIP
#define REG_FP  EBP
#define REG_SP  UESP
#define REG_PS  EFL
#define REG_R0  EAX
#define REG_R1  EDX

#ifdef	__cplusplus
}
#endif

#endif	/* _REGSET_H */

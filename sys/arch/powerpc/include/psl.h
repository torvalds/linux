/*	$OpenBSD: psl.h,v 1.5 2001/11/13 14:31:52 drahn Exp $	*/
/*	$NetBSD: psl.h,v 1.1 1996/09/30 16:34:32 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef	_POWERPC_PSL_H_
#define	_POWERPC_PSL_H_

/*
 * Flags in MSR:
 */
#define PSL_VEC		0x02000000	/* AltiVec vector unit available */
#define	PSL_POW		0x00040000
#define	PSL_ILE		0x00010000
#define	PSL_EE		0x00008000
#define	PSL_PR		0x00004000
#define	PSL_FP		0x00002000
#define	PSL_ME		0x00001000
#define	PSL_FE0		0x00000800
#define	PSL_SE		0x00000400
#define	PSL_BE		0x00000200
#define	PSL_FE1		0x00000100
#define	PSL_IP		0x00000040
#define	PSL_IR		0x00000020
#define	PSL_DR		0x00000010
#define	PSL_RI		0x00000002
#define	PSL_LE		0x00000001

/*
 * Floating-point exception modes:
 */
#define	PSL_FE_DIS	0
#define	PSL_FE_NONREC	PSL_FE1
#define	PSL_FE_REC	PSL_FE0
#define	PSL_FE_PREC	(PSL_FE0 | PSL_FE1)
#define	PSL_FE_DFLT	PSL_FE_DIS

/*
 * Note that PSL_POW and PSL_ILE are not in the saved copy of the MSR
 */
#define	PSL_MBO		0
#define	PSL_MBZ		0

#define	PSL_USERSET	(PSL_EE | PSL_PR | PSL_ME | PSL_IR | PSL_DR | PSL_RI)

#define	PSL_USERSTATIC	(PSL_USERSET | PSL_IP | 0x87c0008c)

#include <machine/intr.h>

#endif	/* _POWERPC_PSL_H_ */

/*	$NetBSD: mcontext.h,v 1.4 2003/10/08 22:43:01 thorpej Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2001, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Klaus Klein and by Jason R. Thorpe of Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_MCONTEXT_H_
#define _MACHINE_MCONTEXT_H_
/*
 * General register state
 */
#define _NGREG		17
typedef unsigned int	__greg_t;
typedef __greg_t	__gregset_t[_NGREG];

#define _REG_R0		0
#define _REG_R1		1
#define _REG_R2		2
#define _REG_R3		3
#define _REG_R4		4
#define _REG_R5		5
#define _REG_R6		6
#define _REG_R7		7
#define _REG_R8		8
#define _REG_R9		9
#define _REG_R10	10
#define _REG_R11	11
#define _REG_R12	12
#define _REG_R13	13
#define _REG_R14	14
#define _REG_R15	15
#define _REG_CPSR	16
/* Convenience synonyms */
#define _REG_FP		_REG_R11
#define _REG_SP		_REG_R13
#define _REG_LR		_REG_R14
#define _REG_PC		_REG_R15

/*
 * Floating point register state
 */
typedef struct {
	__uint64_t	mcv_reg[32];
	__uint32_t	mcv_fpscr;
} mcontext_vfp_t;

typedef struct {
	__gregset_t	__gregs;

	/*
	 * Originally, rest of this structure was named __fpu, 35 * 4 bytes
	 * long, never accessed from kernel. 
	 */
	__size_t	mc_vfp_size;
	void 		*mc_vfp_ptr;
	unsigned int	mc_spare[33];
} mcontext_t;

#define UC_
#endif	/* !_MACHINE_MCONTEXT_H_ */

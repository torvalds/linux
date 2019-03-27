/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 *
 *	$NetBSD: frame.h,v 1.2 1999/01/10 10:13:15 tsubai Exp $
 * $FreeBSD$
 */

#ifndef	_MACHINE_FRAME_H_
#define	_MACHINE_FRAME_H_

#include <sys/types.h>

/*
 * We have to save all registers on every trap, because
 *	1. user could attach this process every time
 *	2. we must be able to restore all user registers in case of fork
 * Actually, we do not save the fp registers on trap, since
 * these are not used by the kernel. They are saved only when switching
 * between processes using the FPU.
 *
 * Change ordering to cluster together these register_t's.		XXX
 */
struct trapframe {
	register_t fixreg[32];
	register_t lr;
	register_t cr;
	register_t xer;
	register_t ctr;
	register_t srr0;
	register_t srr1;
	register_t exc;
	register_t dar;	/* DAR/DEAR filled in on DSI traps */
	union {
		struct {
			/* dsisr only filled on a DSI trap */
			register_t dsisr;
		} aim;
		struct {
			register_t esr;
			register_t dbcr0;
		} booke;
	} cpu;
};

/*
 * FRAMELEN is the size of the stack region used by the low-level trap
 * handler. It is the size of its data (trapframe) plus the callframe
 * header (sizeof(struct callframe) - 3 register widths). It must also
 * be 16-byte aligned.
 */
#define	FRAMELEN	roundup(sizeof(struct trapframe) + \
			    sizeof(struct callframe) - 3*sizeof(register_t), 16)
#define	trapframe(td)	((td)->td_frame)

/*
 * Call frame for PowerPC used during fork.
 */
#ifdef __powerpc64__
struct callframe {
	register_t	cf_dummy_fp;	/* dummy frame pointer */
	register_t	cf_cr;
	register_t	cf_lr;
	register_t	cf_compiler;
	register_t	cf_linkeditor;
	register_t	cf_toc;
	register_t	cf_func;
	register_t	cf_arg0;
	register_t	cf_arg1;
	register_t	_padding;	/* Maintain 16-byte alignment */
};
#else
struct callframe {
	register_t	cf_dummy_fp;	/* dummy frame pointer */
	register_t	cf_lr;		/* space for link register save */
	register_t	cf_func;
	register_t	cf_arg0;
	register_t	cf_arg1;
	register_t	_padding;	/* Maintain 16-byte alignment */
};
#endif

/* Definitions for syscalls */
#define	FIRSTARG	3				/* first arg in reg 3 */
#define	NARGREG		8				/* 8 args in regs */

#endif	/* _MACHINE_FRAME_H_ */

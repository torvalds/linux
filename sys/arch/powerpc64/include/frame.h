/*	$OpenBSD: frame.h,v 1.5 2020/07/13 22:37:37 kettenis Exp $	*/

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

#ifndef _MACHDEP_FRAME_H
#define _MACHDEP_FRAME_H

/*
 * We have to save all registers on every trap, because
 *	1. user could attach this process every time
 *	2. we must be able to restore all user registers in case of fork
 * Actually, we do not save the fp registers on trap, since
 * these are not used by the kernel. They are saved only when switching
 * between processes using the FPU.
 *
 */
struct trapframe {
	__register_t fixreg[32];
	__register_t lr;
	__register_t cr;
	__register_t xer;
	__register_t ctr;
	__register_t srr0;
	__register_t srr1;
	__register_t vrsave;
	__register_t dar;	/* dar & dsisr are only filled on a DSI trap */
	__register_t dsisr;
	__register_t exc;
};

/*
 * This is to ensure alignment of the stackpointer
 */
#define	FRAMELEN	roundup(sizeof(struct trapframe) + 32, 16)

struct callframe {
	register_t	cf_sp;
	register_t	cf_cr;
	register_t	cf_lr;
	register_t	cf_toc;
};

struct sigframe {
	int		sf_signum;
	siginfo_t	*sf_sip;
	struct sigcontext sf_sc;
	siginfo_t	sf_si;
};

struct switchframe {
	register_t	sf_sp;
	register_t	sf_cr;
	register_t	sf_lr;		/* unused */
	register_t	sf_toc;		/* unused */
	register_t	sf_r14;
	register_t	sf_r15;
	register_t	sf_r16;
	register_t	sf_r17;
	register_t	sf_r18;
	register_t	sf_r19;
	register_t	sf_r20;
	register_t	sf_r21;
	register_t	sf_r22;
	register_t	sf_r23;
	register_t	sf_r24;
	register_t	sf_r25;
	register_t	sf_r26;
	register_t	sf_r27;
	register_t	sf_r28;
	register_t	sf_r29;
	register_t	sf_r30;
	register_t	sf_r31;
};

#endif /* _MACHDEP_FRAME_H_ */

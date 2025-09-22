/*	$OpenBSD: frame.h,v 1.11 2024/04/08 20:07:53 miod Exp $	*/
/*	$NetBSD: frame.h,v 1.9 2001/03/04 09:28:35 mrg Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)frame.h	8.1 (Berkeley) 6/11/93
 */

#ifndef _MACHINE_FRAME_H_
#define _MACHINE_FRAME_H_

/*
 * CC64FSZ (C Compiler 64-bit Frame SiZe) is the size of a stack frame used
 * by the compiler in 64-bit mode.  It is (16)*8; space for 8 ins, 8 outs.
 */
#define CC64FSZ		176

/*
 * v9 stacks all have a bias of 2047 added to the %sp and %fp, so you can easily
 * detect it by testing the register for an odd value.  Why 2K-1 I don't know.
 */
#define BIAS	(2048-1)

#if defined(_KERNEL) && !defined(_LOCORE)

/*
 * Sparc v9 stack frame format.
 *
 * Note that the contents of each stack frame may be held only in
 * machine register windows.  In order to get an accurate picture
 * of the frame, you must first force the kernel to write any such
 * windows to the stack.
 *
 * V9 frames have an odd bias, so you can tell a v9 frame from
 * a v8 frame by testing the stack pointer's lsb.
 */
struct frame {
	int64_t	fr_local[8];	/* space to save locals (%l0..%l7) */
	int64_t	fr_arg[6];	/* space to save arguments (%i0..%i5) */
	u_int64_t	fr_fp;		/* space to save frame pointer (%i6) */
	u_int64_t	fr_pc;		/* space to save return pc (%i7) */
	/*
	 * SVR4 reserves a bunch of extra stuff.
	 */
	int64_t fr_argd[6];	/* `register save area' (lunacy) */
	int64_t	fr_argx[0];	/* arg extension (args 7..n; variable size) */
};

#define v9next_frame(f)		((struct frame*)(f->fr_fp+BIAS))

/*
 * The v9 trapframe.
 */
struct trapframe {
	int64_t		tf_tstate;	/* tstate register */
	int64_t		tf_pc;		/* return pc */
	int64_t		tf_npc;		/* return npc */
	int		tf_y;		/* %y register -- 32-bits */
	short		tf_tt;		/* What type of trap this was */
	char		tf_pil;		/* What IRQ we're handling */
	char		tf_oldpil;	/* What our old SPL was */
	int64_t		tf_global[8];	/* global registers in trap's caller */
	int64_t		tf_out[8];	/* output registers in trap's caller */
	int64_t		tf_local[8];	/* local registers in trap's caller */
	int64_t		tf_in[8];	/* in registers in trap's caller (for debug) */
};

#endif	/* _KERNEL && !_LOCORE */

#endif /* _MACHINE_FRAME_H_ */

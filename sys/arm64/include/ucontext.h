/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2014-2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_UCONTEXT_H_
#define	_MACHINE_UCONTEXT_H_

struct gpregs {
	__register_t	gp_x[30];
	__register_t	gp_lr;
	__register_t	gp_sp;
	__register_t	gp_elr;
	__uint32_t	gp_spsr;
	int		gp_pad;
};

struct fpregs {
	__uint128_t	fp_q[32];
	__uint32_t	fp_sr;
	__uint32_t	fp_cr;
	int		fp_flags;
	int		fp_pad;
};

struct __mcontext {
	struct gpregs	mc_gpregs;
	struct fpregs	mc_fpregs;
	int		mc_flags;
#define	_MC_FP_VALID	0x1		/* Set when mc_fpregs has valid data */
	int		mc_pad;		/* Padding */
	__uint64_t	mc_spare[8];	/* Space for expansion, set to zero */
};

typedef struct __mcontext mcontext_t;

#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32_signal.h>
typedef struct __mcontext32 {
	uint32_t		mc_gregset[17];
	uint32_t		mc_vfp_size;
	uint32_t		mc_vfp_ptr;
	uint32_t		mc_spare[33];
} mcontext32_t;

typedef struct __ucontext32 {
	sigset_t		uc_sigmask;
	mcontext32_t		uc_mcontext;
	u_int32_t		uc_link;
	struct sigaltstack32	uc_stack;
	u_int32_t		uc_flags;
	u_int32_t		__spare__[4];
} ucontext32_t;

typedef struct __mcontext32_vfp {
	__uint64_t	mcv_reg[32];
	__uint32_t	mcv_fpscr;
} mcontext32_vfp_t;

#endif /* COMPAT_FREEBSD32 */


#endif	/* !_MACHINE_UCONTEXT_H_ */

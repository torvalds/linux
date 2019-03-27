/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2014 The FreeBSD Foundation
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

#ifndef _MACHINE_FRAME_H_
#define	_MACHINE_FRAME_H_

#ifndef LOCORE

#include <sys/signal.h>
#include <sys/ucontext.h>

/*
 * NOTE: keep this structure in sync with struct reg and struct mcontext.
 */
struct trapframe {
	uint64_t tf_sp;
	uint64_t tf_lr;
	uint64_t tf_elr;
	uint32_t tf_spsr;
	uint32_t tf_esr;
	uint64_t tf_x[30];
};

struct arm64_frame {
	struct arm64_frame	*f_frame;
	u_long			f_retaddr;
};

/*
 * Signal frame, pushed onto the user stack.
 */
struct sigframe {
	siginfo_t       sf_si;          /* actual saved siginfo */
	ucontext_t      sf_uc;          /* actual saved ucontext */
};

/*
 * There is no fixed frame layout, other than to be 16-byte aligned.
 */
struct frame {
	int dummy;
};

#ifdef COMPAT_FREEBSD32
struct sigframe32 {
	struct siginfo32		sf_si;
	ucontext32_t			sf_uc;
	mcontext32_vfp_t		sf_vfp;
};
#endif /* COMPAT_FREEBSD32 */

#endif /* !LOCORE */

#endif /* !_MACHINE_FRAME_H_ */

/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
	uint64_t tf_ra;
	uint64_t tf_sp;
	uint64_t tf_gp;
	uint64_t tf_tp;
	uint64_t tf_t[7];
	uint64_t tf_s[12];
	uint64_t tf_a[8];
	uint64_t tf_sepc;
	uint64_t tf_sstatus;
	uint64_t tf_stval;
	uint64_t tf_scause;
};

struct riscv_frame {
	struct riscv_frame	*f_frame;
	u_long			f_retaddr;
};

/*
 * Signal frame. Pushed onto user stack before calling sigcode.
 */
struct sigframe {
	siginfo_t	sf_si;	/* actual saved siginfo */
	ucontext_t	sf_uc;	/* actual saved ucontext */
};

#endif /* !LOCORE */

/* Definitions for syscalls */
#define	NARGREG		8				/* 8 args in regs */

#endif /* !_MACHINE_FRAME_H_ */

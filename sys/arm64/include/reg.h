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

#ifndef	_MACHINE_REG_H_
#define	_MACHINE_REG_H_

struct reg {
	uint64_t x[30];
	uint64_t lr;
	uint64_t sp;
	uint64_t elr;
	uint32_t spsr;
};

struct reg32 {
	unsigned int r[13];
	unsigned int r_sp;
	unsigned int r_lr;
	unsigned int r_pc;
	unsigned int r_cpsr;
};

struct fpreg {
	__uint128_t	fp_q[32];
	uint32_t	fp_sr;
	uint32_t	fp_cr;
};

struct fpreg32 {
	int dummy;
};

struct dbreg {
	int dummy;
};

struct dbreg32 {
	int dummy;
};

#define	__HAVE_REG32

#ifdef _KERNEL
/*
 * XXX these interfaces are MI, so they should be declared in a MI place.
 */
int	fill_regs(struct thread *, struct reg *);
int	set_regs(struct thread *, struct reg *);
int	fill_fpregs(struct thread *, struct fpreg *);
int	set_fpregs(struct thread *, struct fpreg *);
int	fill_dbregs(struct thread *, struct dbreg *);
int	set_dbregs(struct thread *, struct dbreg *);
#ifdef COMPAT_FREEBSD32
int	fill_regs32(struct thread *, struct reg32 *);
int	set_regs32(struct thread *, struct reg32 *);
int	fill_fpregs32(struct thread *, struct fpreg32 *);
int	set_fpregs32(struct thread *, struct fpreg32 *);
int	fill_dbregs32(struct thread *, struct dbreg32 *);
int	set_dbregs32(struct thread *, struct dbreg32 *);
#endif
#endif

#endif /* !_MACHINE_REG_H_ */

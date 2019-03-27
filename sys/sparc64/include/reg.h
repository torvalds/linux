/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	from: @(#)reg.h	5.5 (Berkeley) 1/18/91
 *	from: FreeBSD: src/sys/i386/include/reg.h,v 1.23 2000/09/21
 * $FreeBSD$
 */

#ifndef	_MACHINE_REG_H_
#define	_MACHINE_REG_H_

/*
 * Register set accessible via /proc/$pid/regs and PT_{SET,GET}REGS.
 *
 * NOTE: DO NOT CHANGE THESE STRUCTURES.  The offsets of the fields are
 * hardcoded in gdb.  Changing them and recompiling doesn't help, the
 * constants in nm-fbsd.h must also be updated.
 */

struct reg32 {
	uint32_t r_global[8];
	uint32_t r_out[8];
	uint32_t r_npc;
	uint32_t r_pc;
	uint32_t r_psr;
	uint32_t r_wim;
	uint32_t r_pad[4];
};

struct reg {
	uint64_t r_global[8];
	uint64_t r_out[8];
	uint64_t r_fprs;
	uint64_t r_fsr;
	uint64_t r_gsr;
	uint64_t r_level;
	uint64_t r_pil;
	uint64_t r_sfar;
	uint64_t r_sfsr;
	uint64_t r_tar;
	uint64_t r_tnpc;
	uint64_t r_tpc;
	uint64_t r_tstate;
	uint64_t r_type;
	uint64_t r_y;
	uint64_t r_wstate;
	uint64_t r_pad[2];
};

/*
 * Register set accessible via /proc/$pid/fpregs.
 */

struct fpreg32 {
	uint32_t fr_regs[32];
	uint32_t fr_fsr;
};

struct fpreg {
	uint32_t fr_regs[64];	/* our view is 64 32-bit registers */
	int64_t	fr_fsr;		/* %fsr */
	int32_t	fr_gsr;		/* %gsr */
	int32_t fr_pad[1];
};

/*
 * Register set accessible via /proc/$pid/dbregs.
 */
struct dbreg {
	int dummy;
};

/*
 * NB: sparcv8 binaries are not supported even though this header
 * defines the relevant structures.
 */
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
#endif

#endif /* !_MACHINE_REG_H_ */

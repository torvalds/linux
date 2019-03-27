/*	$OpenBSD: reg.h,v 1.1 1998/01/28 11:14:53 pefo Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Ralph Campbell.
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
 *	from: Utah Hdr: reg.h 1.1 90/07/09
 *	@(#)reg.h	8.2 (Berkeley) 1/11/94
 *	JNPR: reg.h,v 1.6 2006/09/15 12:52:34 katta
 * $FreeBSD$
 */

#ifndef _MACHINE_REG_H_
#define	_MACHINE_REG_H_

/*
 * Location of the users' stored registers relative to ZERO.
 * must be visible to assembly code.
 */
#include <machine/regnum.h>

/*
 * Register set accessible via /proc/$pid/reg
 */
struct reg {
	register_t r_regs[NUMSAVEREGS];	/* numbered as above */
};

struct fpreg {
	f_register_t r_regs[NUMFPREGS];
};

/*
 * Placeholder.
 */
struct dbreg {
	unsigned long junk;
};

#ifdef __LP64__
/* Must match struct trapframe */
struct reg32 {
	uint32_t r_regs[NUMSAVEREGS];
};

struct fpreg32 {
	int32_t r_regs[NUMFPREGS];
};

struct dbreg32 {
	uint32_t junk;
};

#define __HAVE_REG32
#endif

#ifdef _KERNEL
int	fill_fpregs(struct thread *, struct fpreg *);
int	fill_regs(struct thread *, struct reg *);
int	set_fpregs(struct thread *, struct fpreg *);
int	set_regs(struct thread *, struct reg *);
int	fill_dbregs(struct thread *, struct dbreg *);
int	set_dbregs(struct thread *, struct dbreg *);
#endif

#ifdef COMPAT_FREEBSD32
struct image_params;

int	fill_regs32(struct thread *, struct reg32 *);
int	set_regs32(struct thread *, struct reg32 *);
int	fill_fpregs32(struct thread *, struct fpreg32 *);
int	set_fpregs32(struct thread *, struct fpreg32 *);

#define	fill_dbregs32(td, reg)	0
#define	set_dbregs32(td, reg)	0
#endif

#endif /* !_MACHINE_REG_H_ */

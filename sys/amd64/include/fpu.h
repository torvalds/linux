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
 *	from: @(#)npx.h	5.3 (Berkeley) 1/18/91
 * $FreeBSD$
 */

/*
 * Floating Point Data Structures and Constants
 * W. Jolitz 1/90
 */

#ifndef _MACHINE_FPU_H_
#define	_MACHINE_FPU_H_

#include <x86/fpu.h>

#ifdef _KERNEL

struct fpu_kern_ctx;

#define	PCB_USER_FPU(pcb) (((pcb)->pcb_flags & PCB_KERNFPU) == 0)

#define	XSAVE_AREA_ALIGN	64

void	fpudna(void);
void	fpudrop(void);
void	fpuexit(struct thread *td);
int	fpuformat(void);
int	fpugetregs(struct thread *td);
void	fpuinit(void);
void	fpurestore(void *addr);
void	fpuresume(void *addr);
void	fpusave(void *addr);
int	fpusetregs(struct thread *td, struct savefpu *addr,
	    char *xfpustate, size_t xfpustate_size);
int	fpusetxstate(struct thread *td, char *xfpustate,
	    size_t xfpustate_size);
void	fpususpend(void *addr);
int	fputrap_sse(void);
int	fputrap_x87(void);
void	fpuuserinited(struct thread *td);
struct fpu_kern_ctx *fpu_kern_alloc_ctx(u_int flags);
void	fpu_kern_free_ctx(struct fpu_kern_ctx *ctx);
void	fpu_kern_enter(struct thread *td, struct fpu_kern_ctx *ctx,
	    u_int flags);
int	fpu_kern_leave(struct thread *td, struct fpu_kern_ctx *ctx);
int	fpu_kern_thread(u_int flags);
int	is_fpu_kern_thread(u_int flags);

struct savefpu	*fpu_save_area_alloc(void);
void	fpu_save_area_free(struct savefpu *fsa);
void	fpu_save_area_reset(struct savefpu *fsa);

/*
 * Flags for fpu_kern_alloc_ctx(), fpu_kern_enter() and fpu_kern_thread().
 */
#define	FPU_KERN_NORMAL	0x0000
#define	FPU_KERN_NOWAIT	0x0001
#define	FPU_KERN_KTHR	0x0002
#define	FPU_KERN_NOCTX	0x0004

#endif

#endif /* !_MACHINE_FPU_H_ */

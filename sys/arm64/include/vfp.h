/*-
 * Copyright (c) 2015 The FreeBSD Foundation
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

#ifndef _MACHINE_VFP_H_
#define	_MACHINE_VFP_H_


#ifndef LOCORE
struct vfpstate {
	__uint128_t	vfp_regs[32];
	uint32_t	vfp_fpcr;
	uint32_t	vfp_fpsr;
};

#ifdef _KERNEL
struct pcb;

void	vfp_init(void);
void	vfp_discard(struct thread *);
void	vfp_restore_state(void);
void	vfp_save_state(struct thread *, struct pcb *);

struct fpu_kern_ctx;

/*
 * Flags for fpu_kern_alloc_ctx(), fpu_kern_enter() and fpu_kern_thread().
 */
#define	FPU_KERN_NORMAL	0x0000
#define	FPU_KERN_NOWAIT	0x0001
#define	FPU_KERN_KTHR	0x0002
#define	FPU_KERN_NOCTX	0x0004

struct fpu_kern_ctx *fpu_kern_alloc_ctx(u_int);
void fpu_kern_free_ctx(struct fpu_kern_ctx *);
void fpu_kern_enter(struct thread *, struct fpu_kern_ctx *, u_int);
int fpu_kern_leave(struct thread *, struct fpu_kern_ctx *);
int fpu_kern_thread(u_int);
int is_fpu_kern_thread(u_int);

/* Convert to and from Aarch32 FPSCR to Aarch64 FPCR/FPSR */
#define VFP_FPSCR_FROM_SRCR(vpsr, vpcr) ((vpsr) | ((vpcr) & 0x7c00000))
#define VFP_FPSR_FROM_FPSCR(vpscr) ((vpscr) &~ 0x7c00000)
#define VFP_FPCR_FROM_FPSCR(vpsrc) ((vpsrc) & 0x7c00000)

#endif

#endif

#endif /* !_MACHINE_VFP_H_ */

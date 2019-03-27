/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
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

#ifndef _MACHINE_CPU_H_
#define	_MACHINE_CPU_H_

#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/frame.h>

#define	TRAPF_PC(tfp)		((tfp)->tf_ra)
#define	TRAPF_USERMODE(tfp)	(((tfp)->tf_sstatus & SSTATUS_SPP) == 0)

#define	cpu_getstack(td)	((td)->td_frame->tf_sp)
#define	cpu_setstack(td, sp)	((td)->td_frame->tf_sp = (sp))
#define	cpu_spinwait()		/* nothing */
#define	cpu_lock_delay()	DELAY(1)

#ifdef _KERNEL

/*
 * 0x0000         CPU ID unimplemented
 * 0x0001         UC Berkeley Rocket repo
 * 0x0002­0x7FFE  Reserved for open-source repos
 * 0x7FFF         Reserved for extension
 * 0x8000         Reserved for anonymous source
 * 0x8001­0xFFFE  Reserved for proprietary implementations
 * 0xFFFF         Reserved for extension
 */

#define	CPU_IMPL_SHIFT		0
#define	CPU_IMPL_MASK		(0xffff << CPU_IMPL_SHIFT)
#define	CPU_IMPL(mimpid)	((mimpid & CPU_IMPL_MASK) >> CPU_IMPL_SHIFT)
#define	CPU_IMPL_UNIMPLEMEN	0x0
#define	CPU_IMPL_UCB_ROCKET	0x1

#define	CPU_PART_SHIFT		62
#define	CPU_PART_MASK		(0x3ul << CPU_PART_SHIFT)
#define	CPU_PART(misa)		((misa & CPU_PART_MASK) >> CPU_PART_SHIFT)
#define	CPU_PART_RV32		0x1
#define	CPU_PART_RV64		0x2
#define	CPU_PART_RV128		0x3

extern char btext[];
extern char etext[];

void	cpu_halt(void) __dead2;
void	cpu_reset(void) __dead2;
void	fork_trampoline(void);
void	identify_cpu(void);
void	swi_vm(void *v);

static __inline uint64_t
get_cyclecount(void)
{

	return (rdcycle());
}

#endif

#endif /* !_MACHINE_CPU_H_ */

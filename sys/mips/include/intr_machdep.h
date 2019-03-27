/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Juli Mallett <jmallett@FreeBSD.org>
 * All rights reserved.
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

#ifndef	_MACHINE_INTR_MACHDEP_H_
#define	_MACHINE_INTR_MACHDEP_H_

#include <sys/vmmeter.h>
#include <machine/atomic.h>

#if defined(CPU_RMI) || defined(CPU_NLM)
#define XLR_MAX_INTR 64
#else
#define NHARD_IRQS	6
#define NSOFT_IRQS	2
#endif

struct trapframe;

void cpu_init_interrupts(void);
void cpu_establish_hardintr(const char *, driver_filter_t *, driver_intr_t *,
    void *, int, int, void **);
void cpu_establish_softintr(const char *, driver_filter_t *, void (*)(void*),
    void *, int, int, void **);
void cpu_intr(struct trapframe *);

/*
 * Allow a platform to override the default hard interrupt mask and unmask
 * functions. The 'arg' can be cast safely to an 'int' and holds the mips
 * hard interrupt number to mask or unmask.
 */
typedef void (*cpu_intr_mask_t)(void *arg);
typedef void (*cpu_intr_unmask_t)(void *arg);
void cpu_set_hardintr_mask_func(cpu_intr_mask_t func);
void cpu_set_hardintr_unmask_func(cpu_intr_unmask_t func);

/*
 * Opaque datatype that represents intr counter
 */
typedef unsigned long* mips_intrcnt_t;

mips_intrcnt_t mips_intrcnt_create(const char *);
void mips_intrcnt_setname(mips_intrcnt_t, const char *);

static __inline void
mips_intrcnt_inc(mips_intrcnt_t counter)
{
	if (counter)
		atomic_add_long(counter, 1);
	VM_CNT_INC(v_intr);
}
#endif /* !_MACHINE_INTR_MACHDEP_H_ */

/*-
 * Copyright (c) 2015-2016 Ruslan Bukin <br@bsdpad.com>
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

#ifndef	_MACHINE_INTR_MACHDEP_H_
#define	_MACHINE_INTR_MACHDEP_H_

#define	RISCV_NIRQ		1024

#ifndef	NIRQ
#define	NIRQ			RISCV_NIRQ
#endif

#ifdef INTRNG
#include <sys/intr.h>
#endif

struct trapframe;

int riscv_teardown_intr(void *);
int riscv_setup_intr(const char *, driver_filter_t *, driver_intr_t *,
    void *, int, int, void **);
void riscv_cpu_intr(struct trapframe *);

typedef unsigned long * riscv_intrcnt_t;

riscv_intrcnt_t riscv_intrcnt_create(const char *);
void riscv_intrcnt_setname(riscv_intrcnt_t, const char *);

#ifdef SMP
void riscv_setup_ipihandler(driver_filter_t *);
void riscv_unmask_ipi(void);
#endif

enum {
	IRQ_SOFTWARE_USER,
	IRQ_SOFTWARE_SUPERVISOR,
	IRQ_SOFTWARE_HYPERVISOR,
	IRQ_SOFTWARE_MACHINE,
	IRQ_TIMER_USER,
	IRQ_TIMER_SUPERVISOR,
	IRQ_TIMER_HYPERVISOR,
	IRQ_TIMER_MACHINE,
	IRQ_EXTERNAL_USER,
	IRQ_EXTERNAL_SUPERVISOR,
	IRQ_EXTERNAL_HYPERVISOR,
	IRQ_EXTERNAL_MACHINE,
	INTC_NIRQS
};

#endif /* !_MACHINE_INTR_MACHDEP_H_ */

/*	$OpenBSD: ipifuncs.c,v 1.39 2024/06/07 16:53:35 kettenis Exp $	*/
/*	$NetBSD: ipifuncs.c,v 1.1 2003/04/26 18:39:28 fvdl Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Interprocessor interrupt handlers.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/memrange.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/intr.h>
#include <machine/atomic.h>
#include <machine/cpuvar.h>
#include <machine/i82093var.h>
#include <machine/i82489var.h>
#include <machine/fpu.h>
#include <machine/mplock.h>

#include <machine/db_machdep.h>

#include "vmm.h"
#if NVMM > 0
#include <machine/vmmvar.h>
#endif /* NVMM > 0 */

void x86_64_ipi_nop(struct cpu_info *);
void x86_64_ipi_halt(struct cpu_info *);
void x86_64_ipi_wbinvd(struct cpu_info *);

#if NVMM > 0
void x86_64_ipi_vmclear_vmm(struct cpu_info *);
void x86_64_ipi_start_vmm(struct cpu_info *);
void x86_64_ipi_stop_vmm(struct cpu_info *);
#endif /* NVMM > 0 */

#include "pctr.h"
#if NPCTR > 0
#include <machine/pctr.h>
#define x86_64_ipi_reload_pctr pctr_reload
#else
#define x86_64_ipi_reload_pctr NULL
#endif

#ifdef MTRR
void x86_64_ipi_reload_mtrr(struct cpu_info *);
#else
#define x86_64_ipi_reload_mtrr NULL
#endif

void (*ipifunc[X86_NIPI])(struct cpu_info *) =
{
	x86_64_ipi_halt,
	x86_64_ipi_nop,
#if NVMM > 0
	x86_64_ipi_vmclear_vmm,
#else
	NULL,
#endif
	NULL,
	x86_64_ipi_reload_pctr,
	x86_64_ipi_reload_mtrr,
	x86_setperf_ipi,
#ifdef DDB
	x86_ipi_db,
#else
	NULL,
#endif
#if NVMM > 0
	x86_64_ipi_start_vmm,
	x86_64_ipi_stop_vmm,
#else
	NULL,
	NULL,
#endif
	x86_64_ipi_wbinvd,
};

void
x86_64_ipi_nop(struct cpu_info *ci)
{
}

void
x86_64_ipi_halt(struct cpu_info *ci)
{
	SCHED_ASSERT_UNLOCKED();
	KERNEL_ASSERT_UNLOCKED();

	intr_disable();
	lapic_disable();
	wbinvd();
	atomic_clearbits_int(&ci->ci_flags, CPUF_RUNNING);
	wbinvd();

	for(;;) {
		if (cpu_suspend_cycle_fcn)
			cpu_suspend_cycle_fcn();
		else
			__asm volatile("hlt");
	}
}

#ifdef MTRR
void
x86_64_ipi_reload_mtrr(struct cpu_info *ci)
{
	if (mem_range_softc.mr_op != NULL)
		mem_range_softc.mr_op->reload(&mem_range_softc);
}
#endif

#if NVMM > 0
void
x86_64_ipi_vmclear_vmm(struct cpu_info *ci)
{
	vmclear_on_cpu(ci);
}

void
x86_64_ipi_start_vmm(struct cpu_info *ci)
{
	start_vmm_on_cpu(ci);
}

void
x86_64_ipi_stop_vmm(struct cpu_info *ci)
{
	stop_vmm_on_cpu(ci);
}
#endif /* NVMM > 0 */

void
x86_64_ipi_wbinvd(struct cpu_info *ci)
{
	wbinvd();
}

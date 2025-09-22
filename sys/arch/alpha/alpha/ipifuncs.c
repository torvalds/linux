/* $OpenBSD: ipifuncs.c,v 1.6 2014/02/02 22:49:38 miod Exp $	*/
/* $NetBSD: ipifuncs.c,v 1.9 1999/12/02 01:09:11 thorpej Exp $ */

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
#include <sys/systm.h>
#include <sys/reboot.h>

#include <uvm/uvm_extern.h>

#include <machine/atomic.h>
#include <machine/alpha_cpu.h>
#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/prom.h>
#include <machine/rpb.h>

typedef void (*ipifunc_t)(struct cpu_info *, struct trapframe *);

void	alpha_ipi_halt(struct cpu_info *, struct trapframe *);
void	alpha_ipi_imb(struct cpu_info *, struct trapframe *);
void	alpha_ipi_ast(struct cpu_info *, struct trapframe *);
void	alpha_ipi_synch_fpu(struct cpu_info *, struct trapframe *);
void	alpha_ipi_discard_fpu(struct cpu_info *, struct trapframe *);
void	alpha_ipi_pause(struct cpu_info *, struct trapframe *);

/*
 * NOTE: This table must be kept in order with the bit definitions
 * in <machine/intr.h>.
 */
const ipifunc_t ipifuncs[ALPHA_NIPIS] = {
	alpha_ipi_halt,
	pmap_do_tlb_shootdown,
	alpha_ipi_imb,
	alpha_ipi_ast,
	alpha_ipi_synch_fpu,
	alpha_ipi_discard_fpu,
	alpha_ipi_pause
};

/*
 * Process IPIs for a CPU.
 */
void
alpha_ipi_process(struct cpu_info *ci, struct trapframe *framep)
{
	u_long pending_ipis, bit;

	while ((pending_ipis = atomic_loadlatch_ulong(&ci->ci_ipis, 0)) != 0) {
		for (bit = 0; bit < ALPHA_NIPIS; bit++) {
			if (pending_ipis & (1UL << bit)) {
				(*ipifuncs[bit])(ci, framep);
			}
		}
	}
}

/*
 * Send an interprocessor interrupt.
 */
void
alpha_send_ipi(u_long cpu_id, u_long ipimask)
{

#ifdef DIAGNOSTIC
	if (cpu_id >= hwrpb->rpb_pcs_cnt ||
	    cpu_info[cpu_id] == NULL)
		panic("alpha_send_ipi: bogus cpu_id");
	if (((1UL << cpu_id) & cpus_running) == 0)
		panic("alpha_send_ipi: CPU %ld not running", cpu_id);
#endif

	atomic_setbits_ulong(&cpu_info[cpu_id]->ci_ipis, ipimask);
	alpha_pal_wripir(cpu_id);
}

/*
 * Broadcast an IPI to all but ourselves.
 */
void
alpha_broadcast_ipi(u_long ipimask)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	u_long cpu_id = cpu_number();
	u_long cpumask;

	cpumask = cpus_running & ~(1UL << cpu_id);
	if (cpumask == 0)
		return;

	CPU_INFO_FOREACH(cii, ci) {
		if ((cpumask & (1UL << ci->ci_cpuid)) == 0)
			continue;
		alpha_send_ipi(ci->ci_cpuid, ipimask);
	}
}

/*
 * Send an IPI to all in the list but ourselves.
 */
void
alpha_multicast_ipi(u_long cpumask, u_long ipimask)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	u_long cpu_id = cpu_number();

	cpumask &= cpus_running;
	cpumask &= ~(1UL << cpu_id);
	if (cpumask == 0)
		return;

	CPU_INFO_FOREACH(cii, ci) {
		if ((cpumask & (1UL << ci->ci_cpuid)) == 0)
			continue;
		alpha_send_ipi(ci->ci_cpuid, ipimask);
	}
}

void
alpha_ipi_halt(struct cpu_info *ci, struct trapframe *framep)
{
	SCHED_ASSERT_UNLOCKED();
	fpusave_cpu(ci, 1);
	(void)splhigh();

	cpu_halt();
	/* NOTREACHED */
}

void
alpha_ipi_imb(struct cpu_info *ci, struct trapframe *framep)
{
	alpha_pal_imb();
}

void
alpha_ipi_ast(struct cpu_info *ci, struct trapframe *framep)
{
#if 0 /* useless */
	cpu_unidle(ci);
#endif
}

void
alpha_ipi_synch_fpu(struct cpu_info *ci, struct trapframe *framep)
{
	if (ci->ci_flags & CPUF_FPUSAVE)
		return;
	fpusave_cpu(ci, 1);
}

void
alpha_ipi_discard_fpu(struct cpu_info *ci, struct trapframe *framep)
{
	if (ci->ci_flags & CPUF_FPUSAVE)
		return;
	fpusave_cpu(ci, 0);
}

void
alpha_ipi_pause(struct cpu_info *ci, struct trapframe *framep)
{
	u_long cpumask = (1UL << ci->ci_cpuid);
	int s;

	s = splhigh();

	/* Point debuggers at our trapframe for register state. */
	ci->ci_db_regs = framep;

	atomic_setbits_ulong(&ci->ci_flags, CPUF_PAUSED);

	/* Spin with interrupts disabled until we're resumed. */
	do {
		alpha_mb();
	} while (cpus_paused & cpumask);

	atomic_clearbits_ulong(&ci->ci_flags, CPUF_PAUSED);

	ci->ci_db_regs = NULL;

	splx(s);

	/* Do an IMB on the way out, in case the kernel text was changed. */
	alpha_pal_imb();
}

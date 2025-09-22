/*	$OpenBSD: ipifuncs.c,v 1.22 2024/04/14 19:08:09 miod Exp $	*/
/*	$NetBSD: ipifuncs.c,v 1.8 2006/10/07 18:11:36 rjs Exp $ */

/*-
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/ctlreg.h>
#include <machine/hypervisor.h>
#include <machine/pte.h>
#include <machine/sparc64.h>

#define SPARC64_IPI_RETRIES	10000

void	sun4u_send_ipi(int, void (*)(void), u_int64_t, u_int64_t);
void	sun4u_broadcast_ipi(void (*)(void), u_int64_t, u_int64_t);
void	sun4v_send_ipi(int, void (*)(void), u_int64_t, u_int64_t);
void	sun4v_broadcast_ipi(void (*)(void), u_int64_t, u_int64_t);

/*
 * These are the "function" entry points in locore.s to handle IPI's.
 */
void	sun4u_ipi_tlb_page_demap(void);
void	sun4u_ipi_tlb_context_demap(void);
void	sun4v_ipi_tlb_page_demap(void);
void	sun4v_ipi_tlb_context_demap(void);
void	ipi_softint(void);

/*
 * Send an interprocessor interrupt.
 */
void
sparc64_send_ipi(int itid, void (*func)(void), u_int64_t arg0, u_int64_t arg1)
{
	if (CPU_ISSUN4V)
		sun4v_send_ipi(itid, func, arg0, arg1);
	else
		sun4u_send_ipi(itid, func, arg0, arg1);
}

void
sun4u_send_ipi(int itid, void (*func)(void), u_int64_t arg0, u_int64_t arg1)
{
	int i, j, shift = 0;

	KASSERT((u_int64_t)func > MAXINTNUM);

	/*
	 * UltraSPARC-IIIi CPUs select the BUSY/NACK pair based on the
	 * lower two bits of the ITID.
	 */
	if (((getver() & VER_IMPL) >> VER_IMPL_SHIFT) == IMPL_JALAPENO)
		shift = (itid & 0x3) * 2;

	if (ldxa(0, ASR_IDSR) & (IDSR_BUSY << shift)) {
		__asm volatile("ta 1; nop");
	}

	/* Schedule an interrupt. */
	for (i = 0; i < SPARC64_IPI_RETRIES; i++) {
		u_int64_t s = intr_disable();

		stxa(IDDR_0H, ASI_INTERRUPT_DISPATCH, (u_int64_t)func);
		stxa(IDDR_1H, ASI_INTERRUPT_DISPATCH, arg0);
		stxa(IDDR_2H, ASI_INTERRUPT_DISPATCH, arg1);
		stxa(IDCR(itid), ASI_INTERRUPT_DISPATCH, 0);
		membar_sync();

		for (j = 0; j < 1000000; j++) {
			if (ldxa(0, ASR_IDSR) & (IDSR_BUSY << shift))
				continue;
			else
				break;
		}
		intr_restore(s);

		if (j == 1000000)
			break;

		if ((ldxa(0, ASR_IDSR) & (IDSR_NACK << shift)) == 0)
			return;
	}

#if 1
	if (db_active || panicstr != NULL)
		printf("ipi_send: couldn't send ipi to module %u\n", itid);
	else
		panic("ipi_send: couldn't send ipi");
#else
	__asm volatile("ta 1; nop" : :);
#endif
}

void
sun4v_send_ipi(int itid, void (*func)(void), u_int64_t arg0, u_int64_t arg1)
{
	struct cpu_info *ci = curcpu();
	u_int64_t s;
	int err, i;

	s = intr_disable();

	stha(ci->ci_cpuset, ASI_PHYS_CACHED, itid);
	stxa(ci->ci_mondo, ASI_PHYS_CACHED, (vaddr_t)func);
	stxa(ci->ci_mondo + 8, ASI_PHYS_CACHED, arg0);
	stxa(ci->ci_mondo + 16, ASI_PHYS_CACHED, arg1);

	for (i = 0; i < SPARC64_IPI_RETRIES; i++) {
		err = hv_cpu_mondo_send(1, ci->ci_cpuset, ci->ci_mondo);
		if (err != H_EWOULDBLOCK)
			break;
		delay(10);
	}

	intr_restore(s);

	if (err != H_EOK)
		panic("Unable to send mondo %llx to cpu %d: %d",
		    (u_int64_t)func, itid, err);
}

/*
 * Broadcast an IPI to all but ourselves.
 */
void
sparc64_broadcast_ipi(void (*func)(void), u_int64_t arg0, u_int64_t arg1)
{
	if (CPU_ISSUN4V)
		sun4v_broadcast_ipi(func, arg0, arg1);
	else
		sun4u_broadcast_ipi(func, arg0, arg1);
}

void
sun4u_broadcast_ipi(void (*func)(void), u_int64_t arg0, u_int64_t arg1)
{
	struct cpu_info *ci;

	for (ci = cpus; ci != NULL; ci = ci->ci_next) {
		if (ci->ci_cpuid == cpu_number())
			continue;
		if ((ci->ci_flags & CPUF_RUNNING) == 0)
			continue;
		sun4u_send_ipi(ci->ci_itid, func, arg0, arg1);
	}
}

void
sun4v_broadcast_ipi(void (*func)(void), u_int64_t arg0, u_int64_t arg1)
{
	struct cpu_info *ci = curcpu();
	paddr_t cpuset = ci->ci_cpuset;
	int err, i, ncpus = 0;

	for (ci = cpus; ci != NULL; ci = ci->ci_next) {
		if (ci->ci_cpuid == cpu_number())
			continue;
		if ((ci->ci_flags & CPUF_RUNNING) == 0)
			continue;
		stha(cpuset, ASI_PHYS_CACHED, ci->ci_itid);
		cpuset += sizeof(int16_t);
		ncpus++;
	}

	if (ncpus == 0)
		return;

	ci = curcpu();
	stxa(ci->ci_mondo, ASI_PHYS_CACHED, (vaddr_t)func);
	stxa(ci->ci_mondo + 8, ASI_PHYS_CACHED, arg0);
	stxa(ci->ci_mondo + 16, ASI_PHYS_CACHED, arg1);

	for (i = 0; i < SPARC64_IPI_RETRIES; i++) {
		err = hv_cpu_mondo_send(ncpus, ci->ci_cpuset, ci->ci_mondo);
		if (err != H_EWOULDBLOCK)
			break;
		delay(10);
	}
	if (err != H_EOK)
		panic("Unable to broadcast mondo %llx: %d",
		    (u_int64_t)func, err);
}

void
smp_tlb_flush_pte(vaddr_t va, uint64_t ctx)
{
	(*sp_tlb_flush_pte)(va, ctx);

	if (db_active)
		return;

	if (CPU_ISSUN4V)
		sun4v_broadcast_ipi(sun4v_ipi_tlb_page_demap, va, ctx);
	else
		sun4u_broadcast_ipi(sun4u_ipi_tlb_page_demap, va, ctx);
}

void
smp_tlb_flush_ctx(uint64_t ctx)
{
	(*sp_tlb_flush_ctx)(ctx);

	if (db_active)
		return;

	if (CPU_ISSUN4V)
		sun4v_broadcast_ipi(sun4v_ipi_tlb_context_demap, ctx, 0);
	else
		sun4u_broadcast_ipi(sun4u_ipi_tlb_context_demap, ctx, 0);
}

void
cpu_unidle(struct cpu_info *ci)
{
	if (ci == curcpu() || db_active || ((ci->ci_flags & CPUF_RUNNING) == 0))
		return;

	if (CPU_ISSUN4V)
		sun4v_send_ipi(ci->ci_itid, ipi_softint, 1 << IPL_SOFTINT, 0);
	else
		sun4u_send_ipi(ci->ci_itid, ipi_softint, 1 << IPL_SOFTINT, 0);
}

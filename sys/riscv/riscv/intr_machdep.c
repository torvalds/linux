/*-
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/cpuset.h>
#include <sys/interrupt.h>
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/frame.h>
#include <machine/intr.h>
#include <machine/sbi.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#ifdef SMP
#include <machine/smp.h>
#endif

void intr_irq_handler(struct trapframe *tf);

struct intc_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct intc_irqsrc isrcs[INTC_NIRQS];

static void
riscv_mask_irq(void *source)
{
	uintptr_t irq;

	irq = (uintptr_t)source;

	switch (irq) {
	case IRQ_TIMER_SUPERVISOR:
		csr_clear(sie, SIE_STIE);
		break;
	case IRQ_SOFTWARE_USER:
		csr_clear(sie, SIE_USIE);
		break;
	case IRQ_SOFTWARE_SUPERVISOR:
		csr_clear(sie, SIE_SSIE);
		break;
	default:
		panic("Unknown irq %d\n", irq);
	}
}

static void
riscv_unmask_irq(void *source)
{
	uintptr_t irq;

	irq = (uintptr_t)source;

	switch (irq) {
	case IRQ_TIMER_SUPERVISOR:
		csr_set(sie, SIE_STIE);
		break;
	case IRQ_SOFTWARE_USER:
		csr_set(sie, SIE_USIE);
		break;
	case IRQ_SOFTWARE_SUPERVISOR:
		csr_set(sie, SIE_SSIE);
		break;
	default:
		panic("Unknown irq %d\n", irq);
	}
}

int
riscv_setup_intr(const char *name, driver_filter_t *filt,
    void (*handler)(void*), void *arg, int irq, int flags, void **cookiep)
{
	struct intr_irqsrc *isrc;
	int error;

	if (irq < 0 || irq >= INTC_NIRQS)
		panic("%s: unknown intr %d", __func__, irq);

	isrc = &isrcs[irq].isrc;
	if (isrc->isrc_event == NULL) {
		error = intr_event_create(&isrc->isrc_event, isrc, 0, irq,
		    riscv_mask_irq, riscv_unmask_irq, NULL, NULL, "int%d", irq);
		if (error)
			return (error);
		riscv_unmask_irq((void*)(uintptr_t)irq);
	}

	error = intr_event_add_handler(isrc->isrc_event, name,
	    filt, handler, arg, intr_priority(flags), flags, cookiep);
	if (error) {
		printf("Failed to setup intr: %d\n", irq);
		return (error);
	}

	return (0);
}

int
riscv_teardown_intr(void *ih)
{

	/* TODO */

	return (0);
}

void
riscv_cpu_intr(struct trapframe *frame)
{
	struct intr_irqsrc *isrc;
	int active_irq;

	critical_enter();

	KASSERT(frame->tf_scause & EXCP_INTR,
		("riscv_cpu_intr: wrong frame passed"));

	active_irq = (frame->tf_scause & EXCP_MASK);

	switch (active_irq) {
	case IRQ_SOFTWARE_USER:
	case IRQ_SOFTWARE_SUPERVISOR:
	case IRQ_TIMER_SUPERVISOR:
		isrc = &isrcs[active_irq].isrc;
		if (intr_isrc_dispatch(isrc, frame) != 0)
			printf("stray interrupt %d\n", active_irq);
		break;
	case IRQ_EXTERNAL_SUPERVISOR:
		intr_irq_handler(frame);
		break;
	default:
		break;
	}

	critical_exit();
}

#ifdef SMP
void
riscv_setup_ipihandler(driver_filter_t *filt)
{

	riscv_setup_intr("ipi", filt, NULL, NULL, IRQ_SOFTWARE_SUPERVISOR,
	    INTR_TYPE_MISC, NULL);
}

void
riscv_unmask_ipi(void)
{

	csr_set(sie, SIE_SSIE);
}

/* Sending IPI */
static void
ipi_send(struct pcpu *pc, int ipi)
{
	u_long mask;

	CTR3(KTR_SMP, "%s: cpu=%d, ipi=%x", __func__, pc->pc_cpuid, ipi);

	atomic_set_32(&pc->pc_pending_ipis, ipi);
	mask = (1 << (pc->pc_cpuid));

	sbi_send_ipi(&mask);

	CTR1(KTR_SMP, "%s: sent", __func__);
}

void
ipi_all_but_self(u_int ipi)
{
	cpuset_t other_cpus;

	other_cpus = all_cpus;
	CPU_CLR(PCPU_GET(cpuid), &other_cpus);

	CTR2(KTR_SMP, "%s: ipi: %x", __func__, ipi);
	ipi_selected(other_cpus, ipi);
}

void
ipi_cpu(int cpu, u_int ipi)
{
	cpuset_t cpus;

	CPU_ZERO(&cpus);
	CPU_SET(cpu, &cpus);

	CTR3(KTR_SMP, "%s: cpu: %d, ipi: %x\n", __func__, cpu, ipi);
	ipi_send(cpuid_to_pcpu[cpu], ipi);
}

void
ipi_selected(cpuset_t cpus, u_int ipi)
{
	struct pcpu *pc;
	u_long mask;

	CTR1(KTR_SMP, "ipi_selected: ipi: %x", ipi);

	mask = 0;
	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
		if (CPU_ISSET(pc->pc_cpuid, &cpus)) {
			CTR3(KTR_SMP, "%s: pc: %p, ipi: %x\n", __func__, pc,
			    ipi);
			atomic_set_32(&pc->pc_pending_ipis, ipi);
			mask |= (1 << (pc->pc_cpuid));
		}
	}
	sbi_send_ipi(&mask);
}
#endif

/* Interrupt machdep initialization routine. */
static void
intc_init(void *dummy __unused)
{
	int error;
	int i;

	for (i = 0; i < INTC_NIRQS; i++) {
		isrcs[i].irq = i;
		error = intr_isrc_register(&isrcs[i].isrc, NULL,
		    0, "intc,%u", i);
		if (error != 0)
			printf("Can't register interrupt %d\n", i);
	}
}

SYSINIT(intc_init, SI_SUB_INTR, SI_ORDER_MIDDLE, intc_init, NULL);

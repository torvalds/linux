/*	$OpenBSD: ipi.c,v 1.5 2017/09/08 05:36:51 deraadt Exp $	*/

/*
 * Copyright (c) 2010 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mutex.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/fpu.h>
#include <machine/iomod.h>
#include <machine/intr.h>
#include <machine/mutex.h>
#include <machine/reg.h>

void hppa_ipi_nop(void);
void hppa_ipi_halt(void);
void hppa_ipi_fpu_save(void);
void hppa_ipi_fpu_flush(void);

void (*ipifunc[HPPA_NIPI])(void) =
{
	hppa_ipi_nop,
	hppa_ipi_halt,
	hppa_ipi_fpu_save,
	hppa_ipi_fpu_flush
};

void
hppa_ipi_init(struct cpu_info *ci)
{
	/* Initialise IPIs for given CPU. */
	mtx_init(&ci->ci_ipi_mtx, IPL_IPI);
	ci->ci_mask |= (1 << 30);
}

int
hppa_ipi_intr(void *arg)
{
	struct cpu_info *ci = curcpu();
	u_long ipi_pending;
	int bit = 0;

	/* Handle an IPI. */
	mtx_enter(&ci->ci_ipi_mtx);
	ipi_pending = ci->ci_ipi;
	ci->ci_ipi = 0;
	mtx_leave(&ci->ci_ipi_mtx);

	while (ipi_pending) {
		if (ipi_pending & (1L << bit))
			(*ipifunc[bit])();
		ipi_pending &= ~(1L << bit);
		bit++;
	}

	return 1;
}

int
hppa_ipi_send(struct cpu_info *ci, u_long ipi)
{
	struct iomod *cpu;

	if (!(ci->ci_flags & CPUF_RUNNING))
		return -1;

	mtx_enter(&ci->ci_ipi_mtx);
	ci->ci_ipi |= (1L << ipi);
	asm volatile ("sync" ::: "memory");
	mtx_leave(&ci->ci_ipi_mtx);

	/* Send an IPI to the specified CPU by triggering EIR{1} (irq 30). */
	cpu = (struct iomod *)(ci->ci_hpa);
	cpu->io_eir = 1;
	asm volatile ("sync" ::: "memory");

	return 0;
}

int
hppa_ipi_broadcast(u_long ipi)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	int count = 0;

	CPU_INFO_FOREACH(cii, ci)
		if (ci != curcpu() && (ci->ci_flags & CPUF_RUNNING))
			if (hppa_ipi_send(ci, ipi))
				count++;

	return count;	
}

void
hppa_ipi_nop(void)
{
}

void
hppa_ipi_halt(void)
{
	/* Turn off interrupts and halt CPU. */
	SCHED_ASSERT_UNLOCKED();
	hppa_intr_disable();
	curcpu()->ci_flags &= ~CPUF_RUNNING;

	for (;;) ;
}

void
hppa_ipi_fpu_save(void)
{
	fpu_cpu_save(1);
}

void
hppa_ipi_fpu_flush(void)
{
	fpu_cpu_save(0);
}

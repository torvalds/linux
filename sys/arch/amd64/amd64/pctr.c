/*	$OpenBSD: pctr.c,v 1.10 2024/04/03 02:01:21 guenther Exp $	*/

/*
 * Copyright (c) 2007 Mike Belopuhov
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

/*
 * Pentium performance counter driver for OpenBSD.
 * Copyright 1996 David Mazieres <dm@lcs.mit.edu>.
 *
 * Modification and redistribution in source and binary forms is
 * permitted provided that due credit is given to the author and the
 * OpenBSD project by leaving this copyright notice intact.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <machine/intr.h>
#include <machine/pctr.h>
#include <machine/cpu.h>
#include <machine/specialreg.h>

#define PCTR_AMD_NUM	PCTR_NUM
#define PCTR_INTEL_NUM	2		/* Intel supports only 2 counters */
#define PCTR_INTEL_VERSION_MASK 0xff

#define usetsc		(cpu_feature & CPUID_TSC)
#define usepctr		((pctr_isamd && ((cpu_id >> 8) & 15) >= 6) || \
			    (pctr_isintel && \
			    (pctr_intel_cap & PCTR_INTEL_VERSION_MASK) >= 1))

int			pctr_isamd;
int			pctr_isintel;
uint32_t		pctr_intel_cap;

struct mutex		pctr_conf_lock = MUTEX_INITIALIZER(IPL_HIGH);
uint32_t		pctr_fn[PCTR_NUM];

static void		pctrrd(struct pctrst *);
static int		pctrsel(int, uint32_t, uint32_t);
static void		pctr_enable(struct cpu_info *);

static void
pctrrd(struct pctrst *st)
{
	int i, num, reg;

	num = pctr_isamd ? PCTR_AMD_NUM : PCTR_INTEL_NUM;
	reg = pctr_isamd ? MSR_K7_EVNTSEL0 : MSR_EVNTSEL0;
	for (i = 0; i < num; i++)
		st->pctr_fn[i] = rdmsr(reg + i);
	__asm volatile("cli");
	st->pctr_tsc = rdtsc();
	for (i = 0; i < num; i++)
		st->pctr_hwc[i] = rdpmc(i);
	__asm volatile("sti");
}

void
pctrattach(int num)
{
	struct cpu_info *ci = &cpu_info_primary;
	uint32_t dummy;

	if (num > 1)
		return;

	pctr_isamd = (ci->ci_vendor == CPUV_AMD);
	if (!pctr_isamd) {
		pctr_isintel = (ci->ci_vendor == CPUV_INTEL);
		CPUID(0xa, pctr_intel_cap, dummy, dummy, dummy);
	}
}

void
pctr_enable(struct cpu_info *ci)
{
	if (usepctr) {
		/* Enable RDTSC and RDPMC instructions from user-level. */
		__asm volatile("movq %%cr4,%%rax\n"
				 "\tandq %0,%%rax\n"
				 "\torq %1,%%rax\n"
				 "\tmovq %%rax,%%cr4"
				 :: "i" (~CR4_TSD), "i" (CR4_PCE) : "rax");
	} else if (usetsc) {
		/* Enable RDTSC instruction from user-level. */
		__asm volatile("movq %%cr4,%%rax\n"
				 "\tandq %0,%%rax\n"
				 "\tmovq %%rax,%%cr4"
				 :: "i" (~CR4_TSD) : "rax");
	}
}

int
pctropen(dev_t dev, int oflags, int devtype, struct proc *p)
{

	if (minor(dev))
		return (ENXIO);
	return (0);
}

int
pctrclose(dev_t dev, int oflags, int devtype, struct proc *p)
{

	return (0);
}

static int
pctrsel(int fflag, uint32_t cmd, uint32_t fn)
{
	int msrsel, msrval, changed;

	cmd -= PCIOCS0;
	if (pctr_isamd) {
		if (cmd > PCTR_AMD_NUM-1)
			return (EINVAL);
		msrsel = MSR_K7_EVNTSEL0 + cmd;
		msrval = MSR_K7_PERFCTR0 + cmd;
	} else {
		if (cmd > PCTR_INTEL_NUM-1)
			return (EINVAL);
		msrsel = MSR_EVNTSEL0 + cmd;
		msrval = MSR_PERFCTR0 + cmd;
	}

	if (!(fflag & FWRITE))
		return (EPERM);
	if (fn & 0x380000)
		return (EINVAL);

	if (fn != 0)
		pctr_enable(curcpu());

	mtx_enter(&pctr_conf_lock);
	changed = fn != pctr_fn[cmd];
	if (changed) {
		pctr_fn[cmd] = fn;
		wrmsr(msrval, 0);
		wrmsr(msrsel, fn);
		wrmsr(msrval, 0);
	}
	mtx_leave(&pctr_conf_lock);
#ifdef MULTIPROCESSOR
	if (changed)
		x86_broadcast_ipi(X86_IPI_PCTR);
#endif

	return (0);
}

int
pctrioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, struct proc *p)
{
	switch (cmd) {
	case PCIOCRD:
	{
		struct pctrst *st = (struct pctrst *)data;
		
		if (usepctr)
			pctrrd(st);
		else if (usetsc)
			st->pctr_tsc = rdtsc();
		return (0);
	}
	case PCIOCS0:
	case PCIOCS1:
	case PCIOCS2:
	case PCIOCS3:
		if (usepctr)
			return (pctrsel(fflag, cmd, *(u_int *)data));
		return (ENODEV);
	default:
		return (EINVAL);
	}
}

void
pctr_reload(struct cpu_info *ci)
{
	int num, i, msrsel, msrval, anyset;
	uint32_t fn;

	if (pctr_isamd) {
		num = PCTR_AMD_NUM;
		msrsel = MSR_K7_EVNTSEL0;
		msrval = MSR_K7_PERFCTR0;
	} else {
		num = PCTR_INTEL_NUM;
		msrsel = MSR_EVNTSEL0;
		msrval = MSR_PERFCTR0;
	}

	anyset = 0;
	mtx_enter(&pctr_conf_lock);
	for (i = 0; i < num; i++) {
		/* only update the ones that don't match */
		/* XXX generation numbers for zeroing? */
		fn = rdmsr(msrsel + i);
		if (fn != pctr_fn[i]) {
			wrmsr(msrval + i, 0);
			wrmsr(msrsel + i, pctr_fn[i]);
			wrmsr(msrval + i, 0);
		}
		if (fn)
			anyset = 1;
	}
	mtx_leave(&pctr_conf_lock);

	if (! anyset)
		pctr_enable(curcpu());
}

void
pctr_resume(struct cpu_info *ci)
{
	if (usepctr)
		pctr_reload(ci);
}

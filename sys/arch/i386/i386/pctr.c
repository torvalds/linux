/*	$OpenBSD: pctr.c,v 1.31 2023/01/30 10:49:05 jsg Exp $	*/

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
#include <sys/systm.h>

#include <machine/pctr.h>
#include <machine/cpu.h>
#include <machine/specialreg.h>

#define PCTR_AMD_NUM	PCTR_NUM
#define PCTR_INTEL_NUM	2		/* Intel supports only 2 counters */
#define PCTR_INTEL_VERSION_MASK 0xff

#define usetsc		(cpu_feature & CPUID_TSC)
#define usep5ctr	(pctr_isintel && (((cpu_id >> 8) & 15) == 5) && \
				(((cpu_id >> 4) & 15) > 0))
#define usepctr		((pctr_isamd && ((cpu_id >> 8) & 15) >= 6) || \
			    (pctr_isintel && \
			    (pctr_intel_cap & PCTR_INTEL_VERSION_MASK) >= 1))

int			pctr_isamd;
int			pctr_isintel;
uint32_t		pctr_intel_cap;

static int		p5ctrsel(int fflag, u_int cmd, u_int fn);
static int		pctrsel(int fflag, u_int cmd, u_int fn);
static void		pctrrd(struct pctrst *);

static void
p5ctrrd(struct pctrst *st)
{
	u_int msr11;

	msr11 = rdmsr(P5MSR_CTRSEL);
	st->pctr_fn[0] = msr11 & 0xffff;
	st->pctr_fn[1] = msr11 >> 16;
	__asm volatile("cli");
	st->pctr_tsc = rdtsc();
	st->pctr_hwc[0] = rdmsr(P5MSR_CTR0);
	st->pctr_hwc[1] = rdmsr(P5MSR_CTR1);
	__asm volatile("sti");
}

static void
pctrrd(struct pctrst *st)
{
	int i, num, reg;

	num = pctr_isamd ? PCTR_AMD_NUM : PCTR_INTEL_NUM;
	reg = pctr_isamd ? MSR_K7_EVNTSEL0 : P6MSR_CTRSEL0;
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
	uint32_t dummy;

	if (num > 1)
		return;

	pctr_isamd = (strcmp(cpu_vendor, "AuthenticAMD") == 0);
	if (!pctr_isamd && cpuid_level >= 0xa) {
		pctr_isintel = (strcmp(cpu_vendor, "GenuineIntel") == 0);
		CPUID(0xa, pctr_intel_cap, dummy, dummy, dummy);
	}

	if (usepctr) {
		/* Enable RDTSC and RDPMC instructions from user-level. */
		__asm volatile ("movl %%cr4,%%eax\n"
				  "\tandl %0,%%eax\n"
				  "\torl %1,%%eax\n"
				  "\tmovl %%eax,%%cr4"
				  :: "i" (~CR4_TSD), "i" (CR4_PCE) : "eax");
	} else if (usetsc) {
		/* Enable RDTSC instruction from user-level. */
		__asm volatile ("movl %%cr4,%%eax\n"
				  "\tandl %0,%%eax\n"
				  "\tmovl %%eax,%%cr4"
				  :: "i" (~CR4_TSD) : "eax");
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

int
p5ctrsel(int fflag, u_int cmd, u_int fn)
{
	pctrval msr11;
	int msr, shift;

	cmd -= PCIOCS0;
	if (cmd > 1)
		return (EINVAL);
	msr = P5MSR_CTR0 + cmd;
	shift = cmd ? 0x10 : 0;

	if (!(fflag & FWRITE))
		return (EPERM);
	if (fn >= 0x200)
		return (EINVAL);

	msr11 = rdmsr(P5MSR_CTRSEL);
	msr11 &= ~(0x1ffLL << shift);
	msr11 |= fn << shift;
	wrmsr(P5MSR_CTRSEL, msr11);
	wrmsr(msr, 0);

	return (0);
}

int
pctrsel(int fflag, u_int cmd, u_int fn)
{
	int msrsel, msrval;

	cmd -= PCIOCS0;
	if (pctr_isamd) {
		if (cmd > PCTR_AMD_NUM-1)
			return (EINVAL);
		msrsel = MSR_K7_EVNTSEL0 + cmd;
		msrval = MSR_K7_PERFCTR0 + cmd;
	} else {
		if (cmd > PCTR_INTEL_NUM-1)
			return (EINVAL);
		msrsel = P6MSR_CTRSEL0 + cmd;
		msrval = P6MSR_CTR0 + cmd;
	}

	if (!(fflag & FWRITE))
		return (EPERM);
	if (fn & 0x380000)
		return (EINVAL);

	wrmsr(msrval, 0);
	wrmsr(msrsel, fn);
	wrmsr(msrval, 0);

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
		else if (usep5ctr)
			p5ctrrd(st);
		else {
			bzero(st, sizeof(*st));
			if (usetsc)
				st->pctr_tsc = rdtsc();
		}
		return (0);
	}
	case PCIOCS0:
	case PCIOCS1:
	case PCIOCS2:
	case PCIOCS3:
		if (usepctr)
			return (pctrsel(fflag, cmd, *(u_int *)data));
		if (usep5ctr)
			return (p5ctrsel(fflag, cmd, *(u_int *)data));
		return (ENODEV);
	default:
		return (EINVAL);
	}
}

/*-
 * Copyright (c) 2015 Alexander Kabaev <kan@FreeBSD.org>
 * Copyright (c) 2004-2010 Juli Mallett <jmallett@FreeBSD.org>
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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <machine/cpufunc.h>
#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/smp.h>

#include <mips/ingenic/jz4780_regs.h>
#include <mips/ingenic/jz4780_cpuregs.h>

void jz4780_mpentry(void);

#define JZ4780_MAXCPU	2

void
platform_ipi_send(int cpuid)
{

	if (cpuid == 0)
		mips_wr_xburst_mbox0(1);
	else
		mips_wr_xburst_mbox1(1);
}

void
platform_ipi_clear(void)
{
	int cpuid = PCPU_GET(cpuid);
	uint32_t action;

	action = (cpuid == 0) ? mips_rd_xburst_mbox0() : mips_rd_xburst_mbox1();
	KASSERT(action == 1, ("CPU %d: unexpected IPIs: %#x", cpuid, action));
	mips_wr_xburst_core_sts(~(JZ_CORESTS_MIRQ0P << cpuid));
}

int
platform_processor_id(void)
{

	return (mips_rd_ebase() & 7);
}

int
platform_ipi_hardintr_num(void)
{

	return (1);
}

int
platform_ipi_softintr_num(void)
{

	return (-1);
}

void
platform_init_ap(int cpuid)
{
	unsigned reg;

	/*
	 * Clear any pending IPIs.
	 */
	mips_wr_xburst_core_sts(~(JZ_CORESTS_MIRQ0P << cpuid));

	/* Allow IPI mbox for this core */
	reg = mips_rd_xburst_reim();
	reg |= (JZ_REIM_MIRQ0M << cpuid);
	mips_wr_xburst_reim(reg);

	/*
	 * Unmask the ipi interrupts.
	 */
	reg = hard_int_mask(platform_ipi_hardintr_num());
	set_intr_mask(reg);
}

void
platform_cpu_mask(cpuset_t *mask)
{
	uint32_t i, m;

	CPU_ZERO(mask);
	for (i = 0, m = 1 ; i < JZ4780_MAXCPU; i++, m <<= 1)
		CPU_SET(i, mask);
}

struct cpu_group *
platform_smp_topo(void)
{
	return (smp_topo_none());
}

static void
jz4780_core_powerup(void)
{
	uint32_t reg;

	reg = readreg(JZ_CGU_BASE + JZ_LPCR);
	reg &= ~LPCR_PD_SCPU;
	writereg(JZ_CGU_BASE + JZ_LPCR, reg);
	do {
		reg = readreg(JZ_CGU_BASE + JZ_LPCR);
	} while ((reg & LPCR_SCPUS) != 0);
}

/*
 * Spin up the second code. The code is roughly modeled after
 * similar routine in Linux.
 */
int
platform_start_ap(int cpuid)
{
	uint32_t reg, addr;

	if (cpuid >= JZ4780_MAXCPU)
		return (EINVAL);

	/* Figure out address of mpentry in KSEG1 */
	addr = MIPS_PHYS_TO_KSEG1(MIPS_KSEG0_TO_PHYS(jz4780_mpentry));
	KASSERT((addr & ~JZ_REIM_ENTRY_MASK) == 0,
	    ("Unaligned mpentry"));

	/* Configure core alternative entry point */
	reg = mips_rd_xburst_reim();
	reg &= ~JZ_REIM_ENTRY_MASK;
	reg |= addr & JZ_REIM_ENTRY_MASK;

	/* Allow this core to get IPIs from one being started */
	reg |= JZ_REIM_MIRQ0M;
	mips_wr_xburst_reim(reg);

	/* Force core into reset and enable use of alternate entry point */
	reg = mips_rd_xburst_core_ctl();
	reg |= (JZ_CORECTL_SWRST0 << cpuid) | (JZ_CORECTL_RPC0 << cpuid);
	mips_wr_xburst_core_ctl(reg);

	/* Power the core up */
	jz4780_core_powerup();

	/* Take the core out of reset */
	reg &= ~(JZ_CORECTL_SWRST0 << cpuid);
	mips_wr_xburst_core_ctl(reg);

	return (0);
}

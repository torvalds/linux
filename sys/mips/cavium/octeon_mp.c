/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/smp.h>

#include <mips/cavium/octeon_pcmap_regs.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <mips/cavium/octeon_irq.h>

unsigned octeon_ap_boot = ~0;

void
platform_ipi_send(int cpuid)
{
	cvmx_write_csr(CVMX_CIU_MBOX_SETX(cpuid), 1);
	mips_wbflush();
}

void
platform_ipi_clear(void)
{
	uint64_t action;

	action = cvmx_read_csr(CVMX_CIU_MBOX_CLRX(PCPU_GET(cpuid)));
	KASSERT(action == 1, ("unexpected IPIs: %#jx", (uintmax_t)action));
	cvmx_write_csr(CVMX_CIU_MBOX_CLRX(PCPU_GET(cpuid)), action);
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
	unsigned ciu_int_mask, clock_int_mask, ipi_int_mask;

	/*
	 * Set the exception base.
	 */
	mips_wr_ebase(0x80000000);

	/*
	 * Clear any pending IPIs.
	 */
	cvmx_write_csr(CVMX_CIU_MBOX_CLRX(cpuid), 0xffffffff);

	/*
	 * Set up interrupts.
	 */
	octeon_ciu_reset();

	/*
	 * Unmask the clock, ipi and ciu interrupts.
	 */
	ciu_int_mask = hard_int_mask(0);
	clock_int_mask = hard_int_mask(5);
	ipi_int_mask = hard_int_mask(platform_ipi_hardintr_num());
	set_intr_mask(ciu_int_mask | clock_int_mask | ipi_int_mask);

	mips_wbflush();
}

void
platform_cpu_mask(cpuset_t *mask)
{
	uint64_t core_mask = cvmx_sysinfo_get()->core_mask;
	uint64_t i, m;

	CPU_ZERO(mask);
	for (i = 0, m = 1 ; i < MAXCPU; i++, m <<= 1)
		if (core_mask & m)
			CPU_SET(i, mask);
}

struct cpu_group *
platform_smp_topo(void)
{
	return (smp_topo_none());
}

int
platform_start_ap(int cpuid)
{
	uint64_t cores_in_reset;

	/* 
	 * Release the core if it is in reset, and let it rev up a bit.
	 * The real synchronization happens below via octeon_ap_boot.
	 */
	cores_in_reset = cvmx_read_csr(CVMX_CIU_PP_RST);
	if (cores_in_reset & (1ULL << cpuid)) {
	    if (bootverbose)
		printf ("AP #%d still in reset\n", cpuid);
	    cores_in_reset &= ~(1ULL << cpuid);
	    cvmx_write_csr(CVMX_CIU_PP_RST, (uint64_t)(cores_in_reset));
	    DELAY(2000);    /* Give it a moment to start */
	}

	if (atomic_cmpset_32(&octeon_ap_boot, ~0, cpuid) == 0)
		return (-1);
	for (;;) {
		DELAY(1000);
		if (atomic_cmpset_32(&octeon_ap_boot, 0, ~0) != 0)
			return (0);
		printf("Waiting for cpu%d to start\n", cpuid);
	}
}

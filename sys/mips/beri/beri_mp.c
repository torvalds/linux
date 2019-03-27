/*-
 * Copyright (c) 2017 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2012-2015 Robert N. M. Watson
 * Copyright (c) 2013 SRI International
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/smp.h>

#include <machine/hwfunc.h>
#include <machine/smp.h>

#include <mips/beri/beri_mp.h>

#include <dev/fdt/fdt_common.h>

struct spin_entry {
	uint64_t entry_addr;
	uint64_t a0;
	uint32_t rsvd1;
	uint32_t pir;
	uint64_t rsvd2;
};

static phandle_t cpu_of_nodes[MAXCPU];
static device_t picmap[MAXCPU];

int
platform_processor_id(void)
{
	int cpu;

	cpu = beri_get_cpu();

	return (cpu);
}

void
platform_cpu_mask(cpuset_t *mask)
{
	int ncores, ncpus, nthreads;
	phandle_t cpus, cpu;
	pcell_t reg;
	char prop[16];
	struct spin_entry *se;

	ncores = beri_get_ncores();
	nthreads = beri_get_nthreads();
	KASSERT(ncores <= 0x10000, ("%s: too many cores %d", __func__, ncores));
	KASSERT(nthreads <= 0x10000, ("%s: too many threads %d", __func__,
	    nthreads));
	KASSERT(ncores < 0xffff || nthreads < 0xffff,
	    ("%s: cores x thread (%d x %d) would overflow", __func__, ncores,
	    nthreads));
	ncpus = ncores * nthreads;
	if (MAXCPU > 1 && ncpus > MAXCPU)
		printf("%s: Hardware supports more CPUs (%d) than kernel (%d)\n",
		    __func__, ncpus, MAXCPU);
	printf("%s: hardware has %d cores with %d threads each\n", __func__,
	    ncores, nthreads);

	if ((cpus = OF_finddevice("/cpus")) <= 0) {
		printf("%s: no \"/cpus\" device found in FDT\n", __func__);
		goto error;
	}
	if ((cpu = OF_child(cpus)) <= 0) {
		printf("%s: no children of \"/cpus\" found in FDT\n", __func__);
		goto error;
	}
	CPU_ZERO(mask);
	do {
		if (OF_getprop(cpu, "reg", &reg, sizeof(reg)) <= 0) {
			printf("%s: cpu device with no reg property\n",
			    __func__);
			goto error;
		}
		if (reg > MAXCPU) {
			printf("%s: cpu ID too large (%d > %d)\n", __func__,
			    reg, MAXCPU);
			continue;
		}
		cpu_of_nodes[reg] = cpu;

		if (reg != 0) {
			if (OF_getprop(cpu, "enable-method", &prop,
			    sizeof(prop)) <= 0 && OF_getprop(OF_parent(cpu),
			    "enable-method", &prop, sizeof(prop)) <= 0) {
				printf("%s: CPU %d has no enable-method "
				    "property\n", __func__, reg);
				continue;
			}
			if (strcmp("spin-table", prop) != 0) {
				printf("%s: CPU %d enable-method is '%s' not "
				    "'spin-table'\n", __func__, reg, prop);
				continue;
			}

			if (OF_getprop(cpu, "cpu-release-addr", &se,
			    sizeof(se)) <= 0) {
				printf("%s: CPU %d has missing or invalid "
				    "cpu-release-addr\n", __func__, reg);
				continue;
			}
			if (se->entry_addr != 1) {
				printf("%s: CPU %d has uninitalized spin "
				    "entry\n", __func__, reg);
				continue;
			}
		}

		CPU_SET(reg, mask);
	} while ((cpu = OF_peer(cpu)) > 0);
	return;

error:
	/*
	 * If we run into any problems determining the CPU layout,
	 * fall back to UP.
	 *
	 * XXX: panic instead?
	 */
	CPU_ZERO(mask);
	CPU_SET(0, mask);
}

void
platform_init_secondary(int cpuid)
{
	device_t ic;
	int ipi;

	ipi = platform_ipi_hardintr_num();

	ic = devclass_get_device(devclass_find("beripic"), cpuid);
	picmap[cpuid] = ic;
	beripic_setup_ipi(ic, cpuid, ipi);

	/* Unmask the interrupt */
	if (cpuid != 0) {
		mips_wr_status(mips_rd_status() | (((1 << ipi) << 8) << 2));
	}
}


void
platform_ipi_send(int cpuid)
{

	mips_sync();	/* Ordering, liveness. */

	beripic_send_ipi(picmap[cpuid], cpuid);
}

void
platform_ipi_clear(void)
{
	int cpuid;

	cpuid = platform_processor_id();

	beripic_clear_ipi(picmap[cpuid], cpuid);
}

/*
 * XXXBED: Set via FDT?
 */
int
platform_ipi_hardintr_num(void)
{

	return (4);
}

int
platform_ipi_softintr_num(void)
{

	return (-1);
}

/*
 * XXXBED: Fine for MT, will need something better for multi-core.
 */
struct cpu_group *
platform_smp_topo(void)
{

	return (smp_topo_none());
}

void
platform_init_ap(int cpuid)
{
	uint32_t status;
	u_int clock_int_mask;

	KASSERT(cpuid < MAXCPU, ("%s: invalid CPU id %d", __func__, cpuid));

	/* Make sure coprocessors are enabled. */
	status = mips_rd_status();
	status |= (MIPS_SR_COP_0_BIT | MIPS_SR_COP_1_BIT);
#if defined(CPU_CHERI)
	status |= MIPS_SR_COP_2_BIT;
#endif
	mips_wr_status(status);

#if 0
	register_t hwrena;
	/* Enable HDWRD instruction in userspace. Also enables statcounters. */
	hwrena = mips_rd_hwrena();
	hwrena |= (MIPS_HWRENA_CC | MIPS_HWRENA_CCRES | MIPS_HWRENA_CPUNUM |
	    MIPS_HWRENA_BERI_STATCOUNTERS_MASK);
	mips_wr_hwrena(hwrena);
#endif

	/*
	 * Enable per-thread timer.
	 */
	clock_int_mask = hard_int_mask(5);
	set_intr_mask(clock_int_mask);
}

/*
 * BERI startup conforms to the spin-table start method defined in the
 * ePAPR 1.0 spec.  The initial spin waiting for an address is started
 * by the CPU firmware.
 */
int
platform_start_ap(int cpuid)
{
	phandle_t cpu;
	char prop[16];
	struct spin_entry *se;

	KASSERT(cpuid != 0, ("%s: can't start CPU 0!\n", __func__));
	KASSERT((cpuid > 0 && cpuid < MAXCPU),
	    ("%s: invalid CPU id %d", __func__, cpuid));

	cpu = cpu_of_nodes[cpuid];
	if (OF_getprop(cpu, "status", &prop, sizeof(prop)) <= 0) {
		if (bootverbose)
			printf("%s: CPU %d has no status property, "
			    "trying parent\n", __func__, cpuid);
		if (OF_getprop(OF_parent(cpu), "status", &prop,
		    sizeof(prop)) <= 0)
			panic("%s: CPU %d has no status property", __func__,
			    cpuid);
	}
	if (strcmp("disabled", prop) != 0)
		panic("%s: CPU %d status is '%s' not 'disabled'",
		    __func__, cpuid, prop);

	if (OF_getprop(cpu, "enable-method", &prop, sizeof(prop)) <= 0) {
		if (bootverbose)
			printf("%s: CPU %d has no enable-method, "
			    "trying parent\n", __func__, cpuid);
		if (OF_getprop(OF_parent(cpu), "enable-method", &prop,
		    sizeof(prop)) <= 0)
			panic("%s: CPU %d has no enable-method property",
			    __func__, cpuid);
	}
	if (strcmp("spin-table", prop) != 0)
		panic("%s: CPU %d enable-method is '%s' not "
		    "'spin-table'", __func__, cpuid, prop);

	if (OF_getprop(cpu, "cpu-release-addr", &se, sizeof(se)) <= 0)
		panic("%s: CPU %d has missing or invalid cpu-release-addr",
		    __func__, cpuid);
	se->pir = cpuid;
	if (bootverbose)
		printf("%s: writing %p to %p\n", __func__, mpentry,
		    &se->entry_addr);

	mips_sync();	/* Ordering. */
	se->entry_addr = (intptr_t)mpentry;
	mips_sync();	/* Liveness. */

	return (0);
}

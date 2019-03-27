/*-
 * Copyright (c) 2014 Ganbold Tsagaankhuu <ganbold@freebsd.org>
 * Copyright (c) 2016 Emmanuel Vadot <manu@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/cpu.h>
#include <machine/cpu-v6.h>
#include <machine/smp.h>
#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/platformvar.h>

#include <arm/allwinner/aw_mp.h>
#include <arm/allwinner/aw_machdep.h>

/* Register for all dual-core SoC */
#define	A20_CPUCFG_BASE		0x01c25c00
/* Register for all quad-core SoC */
#define	CPUCFG_BASE		0x01f01c00
#define	CPUCFG_SIZE		0x400
#define	PRCM_BASE		0x01f01400
#define	PRCM_SIZE		0x800
/* Register for multi-cluster SoC */
#define	CPUXCFG_BASE		0x01700000
#define	CPUXCFG_SIZE		0x400

#define	CPU_OFFSET		0x40
#define	CPU_OFFSET_CTL		0x04
#define	CPU_OFFSET_STATUS	0x08
#define	CPU_RST_CTL(cpuid)	((cpuid + 1) * CPU_OFFSET)
#define	CPU_CTL(cpuid)		(((cpuid + 1) * CPU_OFFSET) + CPU_OFFSET_CTL)
#define	CPU_STATUS(cpuid)	(((cpuid + 1) * CPU_OFFSET) + CPU_OFFSET_STATUS)

#define	CPU_RESET		(1 << 0)
#define	CPU_CORE_RESET		(1 << 1)

#define	CPUCFG_GENCTL		0x184
#define	CPUCFG_P_REG0		0x1a4

#define	A20_CPU1_PWR_CLAMP	0x1b0
#define	CPU_PWR_CLAMP_REG	0x140
#define	CPU_PWR_CLAMP(cpu)	((cpu * 4) + CPU_PWR_CLAMP_REG)
#define	CPU_PWR_CLAMP_STEPS	8

#define	A20_CPU1_PWROFF_REG	0x1b4
#define	CPU_PWROFF		0x100

#define	CPUCFG_DBGCTL0		0x1e0
#define	CPUCFG_DBGCTL1		0x1e4

#define	CPUS_CL_RST(cl)		(0x30 + (cl) * 0x4)
#define	CPUX_CL_CTRL0(cl)	(0x0 + (cl) * 0x10)
#define	CPUX_CL_CTRL1(cl)	(0x4 + (cl) * 0x10)
#define	CPUX_CL_CPU_STATUS(cl)	(0x30 + (cl) * 0x4)
#define	CPUX_CL_RST(cl)		(0x80 + (cl) * 0x4)
#define	PRCM_CL_PWROFF(cl)	(0x100 + (cl) * 0x4)
#define	PRCM_CL_PWR_CLAMP(cl, cpu)	(0x140 + (cl) * 0x4 + (cpu) * 0x4)

void
aw_mp_setmaxid(platform_t plat)
{
	int ncpu;
	uint32_t reg;

	if (mp_ncpus != 0)
		return;

	reg = cp15_l2ctlr_get();
	ncpu = CPUV7_L2CTLR_NPROC(reg);

	mp_ncpus = ncpu;
	mp_maxid = ncpu - 1;
}

void
aw_mp_start_ap(platform_t plat)
{
	bus_space_handle_t cpucfg;
	bus_space_handle_t prcm;
	int i, j, soc_family;
	uint32_t val;

	soc_family = allwinner_soc_family();
	if (soc_family == ALLWINNERSOC_SUN7I) {
		if (bus_space_map(fdtbus_bs_tag, A20_CPUCFG_BASE, CPUCFG_SIZE,
		    0, &cpucfg) != 0)
			panic("Couldn't map the CPUCFG\n");
	} else {
		if (bus_space_map(fdtbus_bs_tag, CPUCFG_BASE, CPUCFG_SIZE,
		    0, &cpucfg) != 0)
			panic("Couldn't map the CPUCFG\n");
		if (bus_space_map(fdtbus_bs_tag, PRCM_BASE, PRCM_SIZE, 0,
		    &prcm) != 0)
			panic("Couldn't map the PRCM\n");
	}

	dcache_wbinv_poc_all();

	bus_space_write_4(fdtbus_bs_tag, cpucfg, CPUCFG_P_REG0,
	    pmap_kextract((vm_offset_t)mpentry));

	/*
	 * Assert nCOREPORESET low and set L1RSTDISABLE low.
	 * Ensure DBGPWRDUP is set to LOW to prevent any external
	 * debug access to the processor.
	 */
	for (i = 1; i < mp_ncpus; i++)
		bus_space_write_4(fdtbus_bs_tag, cpucfg, CPU_RST_CTL(i), 0);

	/* Set L1RSTDISABLE low */
	val = bus_space_read_4(fdtbus_bs_tag, cpucfg, CPUCFG_GENCTL);
	for (i = 1; i < mp_ncpus; i++)
		val &= ~(1 << i);
	bus_space_write_4(fdtbus_bs_tag, cpucfg, CPUCFG_GENCTL, val);

	/* Set DBGPWRDUP low */
	val = bus_space_read_4(fdtbus_bs_tag, cpucfg, CPUCFG_DBGCTL1);
	for (i = 1; i < mp_ncpus; i++)
		val &= ~(1 << i);
	bus_space_write_4(fdtbus_bs_tag, cpucfg, CPUCFG_DBGCTL1, val);

	/* Release power clamp */
	for (i = 1; i < mp_ncpus; i++)
		for (j = 0; j <= CPU_PWR_CLAMP_STEPS; j++) {
			if (soc_family != ALLWINNERSOC_SUN7I) {
				bus_space_write_4(fdtbus_bs_tag, prcm,
				    CPU_PWR_CLAMP(i), 0xff >> j);
			} else {
				bus_space_write_4(fdtbus_bs_tag,
				    cpucfg, A20_CPU1_PWR_CLAMP, 0xff >> j);
			}
		}
	DELAY(10000);

	/* Clear power-off gating */
	if (soc_family != ALLWINNERSOC_SUN7I) {
		val = bus_space_read_4(fdtbus_bs_tag, prcm, CPU_PWROFF);
		for (i = 0; i < mp_ncpus; i++)
			val &= ~(1 << i);
		bus_space_write_4(fdtbus_bs_tag, prcm, CPU_PWROFF, val);
	} else {
		val = bus_space_read_4(fdtbus_bs_tag,
		    cpucfg, A20_CPU1_PWROFF_REG);
		val &= ~(1 << 0);
		bus_space_write_4(fdtbus_bs_tag, cpucfg,
		    A20_CPU1_PWROFF_REG, val);
	}
	DELAY(1000);

	/* De-assert cpu core reset */
	for (i = 1; i < mp_ncpus; i++)
		bus_space_write_4(fdtbus_bs_tag, cpucfg, CPU_RST_CTL(i),
		    CPU_RESET | CPU_CORE_RESET);

	/* Assert DBGPWRDUP signal */
	val = bus_space_read_4(fdtbus_bs_tag, cpucfg, CPUCFG_DBGCTL1);
	for (i = 1; i < mp_ncpus; i++)
		val |= (1 << i);
	bus_space_write_4(fdtbus_bs_tag, cpucfg, CPUCFG_DBGCTL1, val);

	dsb();
	sev();
	bus_space_unmap(fdtbus_bs_tag, cpucfg, CPUCFG_SIZE);
	if (soc_family != ALLWINNERSOC_SUN7I)
		bus_space_unmap(fdtbus_bs_tag, prcm, PRCM_SIZE);
}

static void
aw_mc_mp_start_cpu(bus_space_handle_t cpuscfg, bus_space_handle_t cpuxcfg,
    bus_space_handle_t prcm, int cluster, int cpu)
{
	uint32_t val;
	int i;

	/* Assert core reset */
	val = bus_space_read_4(fdtbus_bs_tag, cpuxcfg, CPUX_CL_RST(cluster));
	val &= ~(1 << cpu);
	bus_space_write_4(fdtbus_bs_tag, cpuxcfg, CPUX_CL_RST(cluster), val);

	/* Assert power-on reset */
	val = bus_space_read_4(fdtbus_bs_tag, cpuscfg, CPUS_CL_RST(cluster));
	val &= ~(1 << cpu);
	bus_space_write_4(fdtbus_bs_tag, cpuscfg, CPUS_CL_RST(cluster), val);

	/* Disable automatic L1 cache invalidate at reset */
	val = bus_space_read_4(fdtbus_bs_tag, cpuxcfg, CPUX_CL_CTRL0(cluster));
	val &= ~(1 << cpu);
	bus_space_write_4(fdtbus_bs_tag, cpuxcfg, CPUX_CL_CTRL0(cluster), val);

	/* Release power clamp */
	for (i = 0; i <= CPU_PWR_CLAMP_STEPS; i++)
		bus_space_write_4(fdtbus_bs_tag, prcm,
		    PRCM_CL_PWR_CLAMP(cluster, cpu), 0xff >> i);
	while (bus_space_read_4(fdtbus_bs_tag, prcm,
	    PRCM_CL_PWR_CLAMP(cluster, cpu)) != 0)
		;

	/* Clear power-off gating */
	val = bus_space_read_4(fdtbus_bs_tag, prcm, PRCM_CL_PWROFF(cluster));
	val &= ~(1 << cpu);
	bus_space_write_4(fdtbus_bs_tag, prcm, PRCM_CL_PWROFF(cluster), val);

	/* De-assert power-on reset */
	val = bus_space_read_4(fdtbus_bs_tag, cpuscfg, CPUS_CL_RST(cluster));
	val |= (1 << cpu);
	bus_space_write_4(fdtbus_bs_tag, cpuscfg, CPUS_CL_RST(cluster), val);

	/* De-assert core reset */
	val = bus_space_read_4(fdtbus_bs_tag, cpuxcfg, CPUX_CL_RST(cluster));
	val |= (1 << cpu);
	bus_space_write_4(fdtbus_bs_tag, cpuxcfg, CPUX_CL_RST(cluster), val);
}

static void
aw_mc_mp_start_ap(bus_space_handle_t cpuscfg, bus_space_handle_t cpuxcfg,
    bus_space_handle_t prcm)
{
	int cluster, cpu;

	KASSERT(mp_ncpus <= 4, ("multiple clusters not yet supported"));

	dcache_wbinv_poc_all();

	bus_space_write_4(fdtbus_bs_tag, cpuscfg, CPUCFG_P_REG0,
	    pmap_kextract((vm_offset_t)mpentry));

	cluster = 0;
	for (cpu = 1; cpu < mp_ncpus; cpu++)
		aw_mc_mp_start_cpu(cpuscfg, cpuxcfg, prcm, cluster, cpu);
}

void
a83t_mp_start_ap(platform_t plat)
{
	bus_space_handle_t cpuscfg, cpuxcfg, prcm;

	if (bus_space_map(fdtbus_bs_tag, CPUCFG_BASE, CPUCFG_SIZE,
	    0, &cpuscfg) != 0)
		panic("Couldn't map the CPUCFG\n");
	if (bus_space_map(fdtbus_bs_tag, CPUXCFG_BASE, CPUXCFG_SIZE,
	    0, &cpuxcfg) != 0)
		panic("Couldn't map the CPUXCFG\n");
	if (bus_space_map(fdtbus_bs_tag, PRCM_BASE, PRCM_SIZE, 0,
	    &prcm) != 0)
		panic("Couldn't map the PRCM\n");

	aw_mc_mp_start_ap(cpuscfg, cpuxcfg, prcm);
	dsb();
	sev();
	bus_space_unmap(fdtbus_bs_tag, cpuxcfg, CPUXCFG_SIZE);
	bus_space_unmap(fdtbus_bs_tag, cpuscfg, CPUCFG_SIZE);
	bus_space_unmap(fdtbus_bs_tag, prcm, PRCM_SIZE);
}

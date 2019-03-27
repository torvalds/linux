/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
#include <machine/intr.h>
#include <machine/fdt.h>
#include <machine/smp.h>
#include <machine/platformvar.h>
#include <machine/pmap.h>

#include <arm/nvidia/tegra124/tegra124_mp.h>

#define	PMC_PHYSBASE			0x7000e400
#define	PMC_SIZE			0x400
#define	PMC_CONTROL_REG			0x0
#define	PMC_PWRGATE_TOGGLE		0x30
#define	 PCM_PWRGATE_TOGGLE_START	(1 << 8)
#define	PMC_PWRGATE_STATUS		0x38

#define	TEGRA_EXCEPTION_VECTORS_BASE	0x6000F000 /* exception vectors */
#define	TEGRA_EXCEPTION_VECTORS_SIZE	1024
#define	 TEGRA_EXCEPTION_VECTOR_ENTRY	0x100

void
tegra124_mp_setmaxid(platform_t plat)
{
	int ncpu;

	/* If we've already set the global vars don't bother to do it again. */
	if (mp_ncpus != 0)
		return;

	/* Read current CP15 Cache Size ID Register */
	ncpu = cp15_l2ctlr_get();
	ncpu = CPUV7_L2CTLR_NPROC(ncpu);

	mp_ncpus = ncpu;
	mp_maxid = ncpu - 1;
}

void
tegra124_mp_start_ap(platform_t plat)
{
	bus_space_handle_t pmc;
	bus_space_handle_t exvec;
	int i;
	uint32_t val;
	uint32_t mask;

	if (bus_space_map(fdtbus_bs_tag, PMC_PHYSBASE, PMC_SIZE, 0, &pmc) != 0)
		panic("Couldn't map the PMC\n");
	if (bus_space_map(fdtbus_bs_tag, TEGRA_EXCEPTION_VECTORS_BASE,
	    TEGRA_EXCEPTION_VECTORS_SIZE, 0, &exvec) != 0)
		panic("Couldn't map the exception vectors\n");

	bus_space_write_4(fdtbus_bs_tag, exvec , TEGRA_EXCEPTION_VECTOR_ENTRY,
			  pmap_kextract((vm_offset_t)mpentry));
	bus_space_read_4(fdtbus_bs_tag, exvec , TEGRA_EXCEPTION_VECTOR_ENTRY);


	/* Wait until POWERGATE is ready (max 20 APB cycles). */
	do {
		val = bus_space_read_4(fdtbus_bs_tag, pmc,
		    PMC_PWRGATE_TOGGLE);
	} while ((val & PCM_PWRGATE_TOGGLE_START) != 0);

	for (i = 1; i < mp_ncpus; i++) {
		val = bus_space_read_4(fdtbus_bs_tag, pmc, PMC_PWRGATE_STATUS);
		mask = 1 << (i + 8);	/* cpu mask */
		if ((val & mask) == 0) {
			/* Wait until POWERGATE is ready (max 20 APB cycles). */
			do {
				val = bus_space_read_4(fdtbus_bs_tag, pmc,
				PMC_PWRGATE_TOGGLE);
			} while ((val & PCM_PWRGATE_TOGGLE_START) != 0);
			bus_space_write_4(fdtbus_bs_tag, pmc,
			    PMC_PWRGATE_TOGGLE,
			    PCM_PWRGATE_TOGGLE_START | (8 + i));

			/* Wait until CPU is powered */
			do {
				val = bus_space_read_4(fdtbus_bs_tag, pmc,
				    PMC_PWRGATE_STATUS);
			} while ((val & mask) == 0);
		}

	}
	dsb();
	sev();
	bus_space_unmap(fdtbus_bs_tag, pmc, PMC_SIZE);
	bus_space_unmap(fdtbus_bs_tag, exvec, TEGRA_EXCEPTION_VECTORS_SIZE);
}

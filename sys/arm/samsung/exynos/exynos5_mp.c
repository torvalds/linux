/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Ruslan Bukin <br@bsdpad.com>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/cpu.h>
#include <machine/smp.h>
#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/platformvar.h>

#include <arm/samsung/exynos/exynos5_mp.h>

#define	EXYNOS_CHIPID		0x10000000

#define	EXYNOS5250_SOC_ID	0x43520000
#define	EXYNOS5420_SOC_ID	0xE5420000
#define	EXYNOS5_SOC_ID_MASK	0xFFFFF000

#define	EXYNOS_SYSRAM		0x02020000
#define	EXYNOS5420_SYSRAM_NS	(EXYNOS_SYSRAM + 0x53000 + 0x1c)

#define	EXYNOS_PMU_BASE		0x10040000
#define	CORE_CONFIG(n)		(0x2000 + (0x80 * (n)))
#define	CORE_STATUS(n)		(CORE_CONFIG(n) + 0x4)
#define	CORE_PWR_EN		0x3

static int
exynos_get_soc_id(void)
{
	bus_addr_t chipid;
	int reg;

	if (bus_space_map(fdtbus_bs_tag, EXYNOS_CHIPID,
		0x1000, 0, &chipid) != 0)
		panic("Couldn't map chipid\n");
	reg = bus_space_read_4(fdtbus_bs_tag, chipid, 0x0);
	bus_space_unmap(fdtbus_bs_tag, chipid, 0x1000);

	return (reg & EXYNOS5_SOC_ID_MASK);
}

void
exynos5_mp_setmaxid(platform_t plat)
{

	if (exynos_get_soc_id() == EXYNOS5420_SOC_ID)
		mp_ncpus = 4;
	else
		mp_ncpus = 2;

	mp_maxid = mp_ncpus - 1;
}

void
exynos5_mp_start_ap(platform_t plat)
{
	bus_addr_t sysram, pmu;
	int err, i, j;
	int status;
	int reg;

	err = bus_space_map(fdtbus_bs_tag, EXYNOS_PMU_BASE, 0x20000, 0, &pmu);
	if (err != 0)
		panic("Couldn't map pmu\n");

	if (exynos_get_soc_id() == EXYNOS5420_SOC_ID)
		reg = EXYNOS5420_SYSRAM_NS;
	else
		reg = EXYNOS_SYSRAM;

	err = bus_space_map(fdtbus_bs_tag, reg, 0x100, 0, &sysram);
	if (err != 0)
		panic("Couldn't map sysram\n");

	/* Give power to CPUs */
	for (i = 1; i < mp_ncpus; i++) {
		bus_space_write_4(fdtbus_bs_tag, pmu, CORE_CONFIG(i),
		    CORE_PWR_EN);

		for (j = 10; j >= 0; j--) {
			status = bus_space_read_4(fdtbus_bs_tag, pmu,
			    CORE_STATUS(i));
			if ((status & CORE_PWR_EN) == CORE_PWR_EN)
				break;
			DELAY(10);
			if (j == 0)
				printf("Can't power on CPU%d\n", i);
		}
	}

	bus_space_write_4(fdtbus_bs_tag, sysram, 0x0,
	    pmap_kextract((vm_offset_t)mpentry));

	dcache_wbinv_poc_all();

	dsb();
	sev();
	bus_space_unmap(fdtbus_bs_tag, sysram, 0x100);
	bus_space_unmap(fdtbus_bs_tag, pmu, 0x20000);
}

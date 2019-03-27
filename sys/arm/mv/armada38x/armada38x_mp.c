/*-
 * Copyright (c) 2015 Semihalf.
 * Copyright (c) 2015 Stormshield.
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
#include <sys/smp.h>

#include <machine/smp.h>
#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/platformvar.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/mv/mvreg.h>

#include "pmsu.h"

static int cpu_reset_deassert(void);
void mv_a38x_platform_mp_setmaxid(platform_t plate);
void mv_a38x_platform_mp_start_ap(platform_t plate);

static int
cpu_reset_deassert(void)
{
	bus_space_handle_t vaddr;
	uint32_t reg;
	int rv;

	rv = bus_space_map(fdtbus_bs_tag, (bus_addr_t)MV_CPU_RESET_BASE,
	    MV_CPU_RESET_REGS_LEN, 0, &vaddr);
	if (rv != 0)
		return (rv);

	/* CPU1 is held at reset by default - clear assert bit to release it */
	reg = bus_space_read_4(fdtbus_bs_tag, vaddr, CPU_RESET_OFFSET(1));
	reg &= ~CPU_RESET_ASSERT;

	bus_space_write_4(fdtbus_bs_tag, vaddr, CPU_RESET_OFFSET(1), reg);

	bus_space_unmap(fdtbus_bs_tag, vaddr, MV_CPU_RESET_REGS_LEN);

	return (0);
}

static int
platform_cnt_cpus(void)
{
	bus_space_handle_t vaddr_scu;
	phandle_t cpus_node, child;
	char device_type[16];
	int fdt_cpu_count = 0;
	int reg_cpu_count = 0;
	uint32_t val;
	int rv;

	cpus_node = OF_finddevice("/cpus");
	if (cpus_node == -1) {
		/* Default is one core */
		mp_ncpus = 1;
		return (0);
	}

	/* Get number of 'cpu' nodes from FDT */
	for (child = OF_child(cpus_node); child != 0; child = OF_peer(child)) {
		/* Check if child is a CPU */
		memset(device_type, 0, sizeof(device_type));
		rv = OF_getprop(child, "device_type", device_type,
		    sizeof(device_type) - 1);
		if (rv < 0)
			continue;
		if (strcmp(device_type, "cpu") != 0)
			continue;

		fdt_cpu_count++;
	}

	/* Get number of CPU cores from SCU register to cross-check with FDT */
	rv = bus_space_map(fdtbus_bs_tag, (bus_addr_t)MV_SCU_BASE,
	    MV_SCU_REGS_LEN, 0, &vaddr_scu);
	if (rv != 0) {
		/* Default is one core */
		mp_ncpus = 1;
		return (0);
	}

	val = bus_space_read_4(fdtbus_bs_tag, vaddr_scu, MV_SCU_REG_CONFIG);
	bus_space_unmap(fdtbus_bs_tag, vaddr_scu, MV_SCU_REGS_LEN);
        reg_cpu_count = (val & SCU_CFG_REG_NCPU_MASK) + 1;

	/* Set mp_ncpus to number of cpus in FDT unless SOC contains only one */
	mp_ncpus = min(reg_cpu_count, fdt_cpu_count);
	/* mp_ncpus must be at least 1 */
	mp_ncpus = max(1, mp_ncpus);

	return (mp_ncpus);
}

void
mv_a38x_platform_mp_setmaxid(platform_t plate)
{

	/* Armada38x family supports maximum 2 cores */
	mp_ncpus = platform_cnt_cpus();
	mp_maxid = mp_ncpus - 1;
}

void
mv_a38x_platform_mp_start_ap(platform_t plate)
{
	int rv;

	/* Write secondary entry address to PMSU register */
	rv = pmsu_boot_secondary_cpu();
	if (rv != 0)
		return;

	/* Release CPU1 from reset */
	cpu_reset_deassert();
}

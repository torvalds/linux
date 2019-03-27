/*-
 * Copyright (c) 2014 Ganbold Tsagaankhuu <ganbold@freebsd.org>
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

#include <arm/rockchip/rk30xx_mp.h>

#define	SCU_PHYSBASE			0x1013c000
#define	SCU_SIZE			0x100

#define	SCU_CONTROL_REG			0x00
#define	SCU_CONTROL_ENABLE		(1 << 0)
#define	SCU_STANDBY_EN			(1 << 5)
#define	SCU_CONFIG_REG			0x04
#define	SCU_CONFIG_REG_NCPU_MASK	0x03
#define	SCU_CPUPOWER_REG		0x08
#define	SCU_INV_TAGS_REG		0x0c

#define	SCU_FILTER_START_REG		0x10
#define	SCU_FILTER_END_REG		0x14
#define	SCU_SECURE_ACCESS_REG		0x18
#define	SCU_NONSECURE_ACCESS_REG	0x1c

#define	IMEM_PHYSBASE			0x10080000
#define	IMEM_SIZE			0x20

#define	PMU_PHYSBASE			0x20004000
#define	PMU_SIZE			0x100
#define	PMU_PWRDN_CON			0x08
#define	PMU_PWRDN_SCU			(1 << 4)

extern char 	*mpentry_addr;
static void 	 rk30xx_boot2(void);

static void
rk30xx_boot2(void)
{

	__asm __volatile(
			   "ldr pc, 1f\n"
			   ".globl mpentry_addr\n"
			   "mpentry_addr:\n"
			"1: .space 4\n");
}

void
rk30xx_mp_setmaxid(platform_t plat)
{
	bus_space_handle_t scu;
	int ncpu;
	uint32_t val;

	if (mp_ncpus != 0)
		return;

	if (bus_space_map(fdtbus_bs_tag, SCU_PHYSBASE, SCU_SIZE, 0, &scu) != 0)
		panic("Could not map the SCU");

	val = bus_space_read_4(fdtbus_bs_tag, scu, SCU_CONFIG_REG);
	ncpu = (val & SCU_CONFIG_REG_NCPU_MASK) + 1;
	bus_space_unmap(fdtbus_bs_tag, scu, SCU_SIZE);

	mp_ncpus = ncpu;
	mp_maxid = ncpu - 1;
}

void
rk30xx_mp_start_ap(platform_t plat)
{
	bus_space_handle_t scu;
	bus_space_handle_t imem;
	bus_space_handle_t pmu;
	uint32_t val;
	int i;

	if (bus_space_map(fdtbus_bs_tag, SCU_PHYSBASE, SCU_SIZE, 0, &scu) != 0)
		panic("Could not map the SCU");
	if (bus_space_map(fdtbus_bs_tag, IMEM_PHYSBASE,
	    IMEM_SIZE, 0, &imem) != 0)
		panic("Could not map the IMEM");
	if (bus_space_map(fdtbus_bs_tag, PMU_PHYSBASE, PMU_SIZE, 0, &pmu) != 0)
		panic("Could not map the PMU");

	/*
	 * Invalidate SCU cache tags.  The 0x0000ffff constant invalidates all
	 * ways on all cores 0-3. Per the ARM docs, it's harmless to write to
	 * the bits for cores that are not present.
	 */
	bus_space_write_4(fdtbus_bs_tag, scu, SCU_INV_TAGS_REG, 0x0000ffff);

	/* Make sure all cores except the first are off */
	val = bus_space_read_4(fdtbus_bs_tag, pmu, PMU_PWRDN_CON);
	for (i = 1; i < mp_ncpus; i++)
		val |= 1 << i;
	bus_space_write_4(fdtbus_bs_tag, pmu, PMU_PWRDN_CON, val);

	/* Enable SCU power domain */
	val = bus_space_read_4(fdtbus_bs_tag, pmu, PMU_PWRDN_CON);
	val &= ~PMU_PWRDN_SCU;
	bus_space_write_4(fdtbus_bs_tag, pmu, PMU_PWRDN_CON, val);

	/* Enable SCU */
	val = bus_space_read_4(fdtbus_bs_tag, scu, SCU_CONTROL_REG);
	bus_space_write_4(fdtbus_bs_tag, scu, SCU_CONTROL_REG,
	    val | SCU_CONTROL_ENABLE);

	/*
	 * Cores will execute the code which resides at the start of
	 * the on-chip bootram/sram after power-on. This sram region
	 * should be reserved and the trampoline code that directs
	 * the core to the real startup code in ram should be copied
	 * into this sram region.
	 *
	 * First set boot function for the sram code.
	 */
	mpentry_addr = (char *)pmap_kextract((vm_offset_t)mpentry);

	/* Copy trampoline to sram, that runs during startup of the core */
	bus_space_write_region_4(fdtbus_bs_tag, imem, 0,
	    (uint32_t *)&rk30xx_boot2, 8);

	dcache_wbinv_poc_all();

	/* Start all cores */
	val = bus_space_read_4(fdtbus_bs_tag, pmu, PMU_PWRDN_CON);
	for (i = 1; i < mp_ncpus; i++)
		val &= ~(1 << i);
	bus_space_write_4(fdtbus_bs_tag, pmu, PMU_PWRDN_CON, val);

	dsb();
	sev();

	bus_space_unmap(fdtbus_bs_tag, scu, SCU_SIZE);
	bus_space_unmap(fdtbus_bs_tag, imem, IMEM_SIZE);
	bus_space_unmap(fdtbus_bs_tag, pmu, PMU_SIZE);
}

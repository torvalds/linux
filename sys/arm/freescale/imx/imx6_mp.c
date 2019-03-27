/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Juergen Weiss <weiss@uni-mainz.de>
 * Copyright (c) 2014 Ian Lepore <ian@freebsd.org>
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
#include <machine/smp.h>
#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/platform.h>
#include <machine/platformvar.h>

#include <arm/freescale/imx/imx6_machdep.h>

#define	SCU_PHYSBASE			0x00a00000
#define	SCU_SIZE			0x00001000

#define	SCU_CONTROL_REG			0x00
#define	  SCU_CONTROL_ENABLE		  (1 << 0)
#define	SCU_CONFIG_REG			0x04
#define	  SCU_CONFIG_REG_NCPU_MASK	  0x03
#define	SCU_CPUPOWER_REG		0x08
#define	SCU_INV_TAGS_REG		0x0c
#define	SCU_DIAG_CONTROL		0x30
#define	  SCU_DIAG_DISABLE_MIGBIT	  (1 << 0)
#define	SCU_FILTER_START_REG		0x40
#define	SCU_FILTER_END_REG		0x44
#define	SCU_SECURE_ACCESS_REG		0x50
#define	SCU_NONSECURE_ACCESS_REG	0x54

#define	SRC_PHYSBASE			0x020d8000
#define SRC_SIZE			0x4000
#define	SRC_CONTROL_REG			0x00
#define	SRC_CONTROL_C1ENA_SHIFT		  22	/* Bit for Core 1 enable */
#define	SRC_CONTROL_C1RST_SHIFT		  14	/* Bit for Core 1 reset */
#define	SRC_GPR0_C1FUNC			0x20	/* Register for Core 1 entry func */
#define	SRC_GPR1_C1ARG			0x24	/* Register for Core 1 entry arg */

void
imx6_mp_setmaxid(platform_t plat)
{
	bus_space_handle_t scu;
	int hwcpu, ncpu;
	uint32_t val;

	/* If we've already set the global vars don't bother to do it again. */
	if (mp_ncpus != 0)
		return;

	if (bus_space_map(fdtbus_bs_tag, SCU_PHYSBASE, SCU_SIZE, 0, &scu) != 0)
		panic("Couldn't map the SCU\n");
	val = bus_space_read_4(fdtbus_bs_tag, scu, SCU_CONFIG_REG);
	hwcpu = (val & SCU_CONFIG_REG_NCPU_MASK) + 1;
	bus_space_unmap(fdtbus_bs_tag, scu, SCU_SIZE);

	ncpu = hwcpu;
	TUNABLE_INT_FETCH("hw.ncpu", &ncpu);
	if (ncpu < 1 || ncpu > hwcpu)
		ncpu = hwcpu;

	mp_ncpus = ncpu;
	mp_maxid = ncpu - 1;
}

void
imx6_mp_start_ap(platform_t plat)
{
	bus_space_handle_t scu;
	bus_space_handle_t src;

	uint32_t val;
	int i;

	if (bus_space_map(fdtbus_bs_tag, SCU_PHYSBASE, SCU_SIZE, 0, &scu) != 0)
		panic("Couldn't map the SCU\n");
	if (bus_space_map(fdtbus_bs_tag, SRC_PHYSBASE, SRC_SIZE, 0, &src) != 0)
		panic("Couldn't map the system reset controller (SRC)\n");

	/*
	 * Invalidate SCU cache tags.  The 0x0000ffff constant invalidates all
	 * ways on all cores 0-3.  Per the ARM docs, it's harmless to write to
	 * the bits for cores that are not present.
	 */
	bus_space_write_4(fdtbus_bs_tag, scu, SCU_INV_TAGS_REG, 0x0000ffff);

	/*
	 * Erratum ARM/MP: 764369 (problems with cache maintenance).
	 * Setting the "disable-migratory bit" in the undocumented SCU
	 * Diagnostic Control Register helps work around the problem.
	 */
	val = bus_space_read_4(fdtbus_bs_tag, scu, SCU_DIAG_CONTROL);
	bus_space_write_4(fdtbus_bs_tag, scu, SCU_DIAG_CONTROL, 
	    val | SCU_DIAG_DISABLE_MIGBIT);

	/*
	 * Enable the SCU, then clean the cache on this core.  After these two
	 * operations the cache tag ram in the SCU is coherent with the contents
	 * of the cache on this core.  The other cores aren't running yet so
	 * their caches can't contain valid data yet, but we've initialized
	 * their SCU tag ram above, so they will be coherent from startup.
	 */
	val = bus_space_read_4(fdtbus_bs_tag, scu, SCU_CONTROL_REG);
	bus_space_write_4(fdtbus_bs_tag, scu, SCU_CONTROL_REG, 
	    val | SCU_CONTROL_ENABLE);
	dcache_wbinv_poc_all();

	/*
	 * For each AP core, set the entry point address and argument registers,
	 * and set the core-enable and core-reset bits in the control register.
	 */
	val = bus_space_read_4(fdtbus_bs_tag, src, SRC_CONTROL_REG);
	for (i=1; i < mp_ncpus; i++) {
		bus_space_write_4(fdtbus_bs_tag, src, SRC_GPR0_C1FUNC + 8*i,
		    pmap_kextract((vm_offset_t)mpentry));
		bus_space_write_4(fdtbus_bs_tag, src, SRC_GPR1_C1ARG  + 8*i, 0);

		val |= ((1 << (SRC_CONTROL_C1ENA_SHIFT - 1 + i )) |
		    ( 1 << (SRC_CONTROL_C1RST_SHIFT - 1 + i)));

	}
	bus_space_write_4(fdtbus_bs_tag, src, SRC_CONTROL_REG, val);

	dsb();
	sev();

	bus_space_unmap(fdtbus_bs_tag, scu, SCU_SIZE);
	bus_space_unmap(fdtbus_bs_tag, src, SRC_SIZE);
}

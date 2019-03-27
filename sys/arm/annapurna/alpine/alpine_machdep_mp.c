/*-
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2015 Semihalf
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/cpuset.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/smp.h>
#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/cpu-v6.h>
#include <machine/platformvar.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_cpu.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/annapurna/alpine/alpine_mp.h>

#define AL_CPU_RESUME_WATERMARK_REG		0x00
#define AL_CPU_RESUME_FLAGS_REG			0x04
#define AL_CPU_RESUME_PCPU_RADDR_REG(cpu)	(0x08 + 0x04 + 8*(cpu))
#define AL_CPU_RESUME_PCPU_FLAGS(cpu)		(0x08 + 8*(cpu))

/* Per-CPU flags */
#define AL_CPU_RESUME_FLG_PERCPU_DONT_RESUME	(1 << 2)

/* The expected magic number for validating the resume addresses */
#define AL_CPU_RESUME_MAGIC_NUM			0xf0e1d200
#define AL_CPU_RESUME_MAGIC_NUM_MASK		0xffffff00

/* The expected minimal version number for validating the capabilities */
#define AL_CPU_RESUME_MIN_VER			0x000000c3
#define AL_CPU_RESUME_MIN_VER_MASK		0x000000ff

/* Field controlling the boot-up of companion cores */
#define AL_NB_INIT_CONTROL		(0x8)
#define AL_NB_CONFIG_STATUS_PWR_CTRL(cpu)	(0x2020 + (cpu)*0x100)

extern bus_addr_t al_devmap_pa;
extern bus_addr_t al_devmap_size;

extern void mpentry(void);

static int platform_mp_get_core_cnt(void);
static int alpine_get_cpu_resume_base(u_long *pbase, u_long *psize);
static int alpine_get_nb_base(u_long *pbase, u_long *psize);
static boolean_t alpine_validate_cpu(u_int, phandle_t, u_int, pcell_t *);

static boolean_t
alpine_validate_cpu(u_int id, phandle_t child, u_int addr_cell, pcell_t *reg)
{
	return ofw_bus_node_is_compatible(child, "arm,cortex-a15");
}

static int
platform_mp_get_core_cnt(void)
{
	static int ncores = 0;
	int nchilds;
	uint32_t reg;

	/* Calculate ncores value only once */
	if (ncores)
		return (ncores);

	reg = cp15_l2ctlr_get();
	ncores = CPUV7_L2CTLR_NPROC(reg);

	nchilds = ofw_cpu_early_foreach(alpine_validate_cpu, false);

	/* Limit CPUs if DTS has configured less than available */
	if ((nchilds > 0) && (nchilds < ncores)) {
		printf("SMP: limiting number of active CPUs to %d out of %d\n",
		    nchilds, ncores);
		ncores = nchilds;
	}

	return (ncores);
}

void
alpine_mp_setmaxid(platform_t plat)
{

	mp_ncpus = platform_mp_get_core_cnt();
	mp_maxid = mp_ncpus - 1;
}

static int
alpine_get_cpu_resume_base(u_long *pbase, u_long *psize)
{
	phandle_t node;
	u_long base = 0;
	u_long size = 0;

	if (pbase == NULL || psize == NULL)
		return (EINVAL);

	if ((node = OF_finddevice("/")) == -1)
		return (EFAULT);

	if ((node =
	    ofw_bus_find_compatible(node, "annapurna-labs,al-cpu-resume")) == 0)
		return (EFAULT);

	if (fdt_regsize(node, &base, &size))
		return (EFAULT);

	*pbase = base;
	*psize = size;

	return (0);
}

static int
alpine_get_nb_base(u_long *pbase, u_long *psize)
{
	phandle_t node;
	u_long base = 0;
	u_long size = 0;

	if (pbase == NULL || psize == NULL)
		return (EINVAL);

	if ((node = OF_finddevice("/")) == -1)
		return (EFAULT);

	if ((node =
	    ofw_bus_find_compatible(node, "annapurna-labs,al-nb-service")) == 0)
		return (EFAULT);

	if (fdt_regsize(node, &base, &size))
		return (EFAULT);

	*pbase = base;
	*psize = size;

	return (0);
}

void
alpine_mp_start_ap(platform_t plat)
{
	uint32_t physaddr;
	vm_offset_t vaddr;
	uint32_t val;
	uint32_t start_mask;
	u_long cpu_resume_base;
	u_long nb_base;
	u_long cpu_resume_size;
	u_long nb_size;
	bus_addr_t cpu_resume_baddr;
	bus_addr_t nb_baddr;
	int a;

	if (alpine_get_cpu_resume_base(&cpu_resume_base, &cpu_resume_size))
		panic("Couldn't resolve cpu_resume_base address\n");

	if (alpine_get_nb_base(&nb_base, &nb_size))
		panic("Couldn't resolve_nb_base address\n");

	/* Proceed with start addresses for additional CPUs */
	if (bus_space_map(fdtbus_bs_tag, al_devmap_pa + cpu_resume_base,
	    cpu_resume_size, 0, &cpu_resume_baddr))
		panic("Couldn't map CPU-resume area");
	if (bus_space_map(fdtbus_bs_tag, al_devmap_pa + nb_base,
	    nb_size, 0, &nb_baddr))
		panic("Couldn't map NB-service area");

	/* Proceed with start addresses for additional CPUs */
	val = bus_space_read_4(fdtbus_bs_tag, cpu_resume_baddr,
	    AL_CPU_RESUME_WATERMARK_REG);
	if (((val & AL_CPU_RESUME_MAGIC_NUM_MASK) != AL_CPU_RESUME_MAGIC_NUM) ||
	    ((val & AL_CPU_RESUME_MIN_VER_MASK) < AL_CPU_RESUME_MIN_VER)) {
		panic("CPU-resume device is not compatible");
	}

	vaddr = (vm_offset_t)mpentry;
	physaddr = pmap_kextract(vaddr);

	for (a = 1; a < platform_mp_get_core_cnt(); a++) {
		/* Power up the core */
		bus_space_write_4(fdtbus_bs_tag, nb_baddr,
		    AL_NB_CONFIG_STATUS_PWR_CTRL(a), 0);
		mb();

		/* Enable resume */
		val = bus_space_read_4(fdtbus_bs_tag, cpu_resume_baddr,
		    AL_CPU_RESUME_PCPU_FLAGS(a));
		val &= ~AL_CPU_RESUME_FLG_PERCPU_DONT_RESUME;
		bus_space_write_4(fdtbus_bs_tag, cpu_resume_baddr,
		    AL_CPU_RESUME_PCPU_FLAGS(a), val);
		mb();

		/* Set resume physical address */
		bus_space_write_4(fdtbus_bs_tag, cpu_resume_baddr,
		    AL_CPU_RESUME_PCPU_RADDR_REG(a), physaddr);
		mb();
	}

	/* Release cores from reset */
	if (bus_space_map(fdtbus_bs_tag, al_devmap_pa + nb_base,
	    nb_size, 0, &nb_baddr))
		panic("Couldn't map NB-service area");

	start_mask = (1 << platform_mp_get_core_cnt()) - 1;

	/* Release cores from reset */
	val = bus_space_read_4(fdtbus_bs_tag, nb_baddr, AL_NB_INIT_CONTROL);
	val |= start_mask;
	bus_space_write_4(fdtbus_bs_tag, nb_baddr, AL_NB_INIT_CONTROL, val);
	dsb();

	bus_space_unmap(fdtbus_bs_tag, nb_baddr, nb_size);
	bus_space_unmap(fdtbus_bs_tag, cpu_resume_baddr, cpu_resume_size);
}

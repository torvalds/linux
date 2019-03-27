/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2012 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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

#include "opt_platform.h"
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/hid.h>
#include <machine/_inttypes.h>
#include <machine/machdep.h>
#include <machine/md_var.h>
#include <machine/platform.h>
#include <machine/platformvar.h>
#include <machine/smp.h>
#include <machine/spr.h>
#include <machine/vmparam.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <powerpc/mpc85xx/mpc85xx.h>

#include "platform_if.h"

#ifdef SMP
extern void *ap_pcpu;
extern vm_paddr_t kernload;		/* Kernel physical load address */
extern uint8_t __boot_page[];		/* Boot page body */
extern uint32_t bp_kernload;
extern vm_offset_t __startkernel;

struct cpu_release {
	uint32_t entry_h;
	uint32_t entry_l;
	uint32_t r3_h;
	uint32_t r3_l;
	uint32_t reserved;
	uint32_t pir;
};
#endif

extern uint32_t *bootinfo;
vm_paddr_t ccsrbar_pa;
vm_offset_t ccsrbar_va;
vm_size_t ccsrbar_size;

static int cpu, maxcpu;

static device_t rcpm_dev;
static void dummy_freeze(device_t, bool);

static void (*freeze_timebase)(device_t, bool) = dummy_freeze;

static int mpc85xx_probe(platform_t);
static void mpc85xx_mem_regions(platform_t, struct mem_region *phys,
    int *physsz, struct mem_region *avail, int *availsz);
static u_long mpc85xx_timebase_freq(platform_t, struct cpuref *cpuref);
static int mpc85xx_smp_first_cpu(platform_t, struct cpuref *cpuref);
static int mpc85xx_smp_next_cpu(platform_t, struct cpuref *cpuref);
static int mpc85xx_smp_get_bsp(platform_t, struct cpuref *cpuref);
static int mpc85xx_smp_start_cpu(platform_t, struct pcpu *cpu);
static void mpc85xx_smp_timebase_sync(platform_t, u_long tb, int ap);

static void mpc85xx_reset(platform_t);

static platform_method_t mpc85xx_methods[] = {
	PLATFORMMETHOD(platform_probe,		mpc85xx_probe),
	PLATFORMMETHOD(platform_attach,		mpc85xx_attach),
	PLATFORMMETHOD(platform_mem_regions,	mpc85xx_mem_regions),
	PLATFORMMETHOD(platform_timebase_freq,	mpc85xx_timebase_freq),

	PLATFORMMETHOD(platform_smp_first_cpu,	mpc85xx_smp_first_cpu),
	PLATFORMMETHOD(platform_smp_next_cpu,	mpc85xx_smp_next_cpu),
	PLATFORMMETHOD(platform_smp_get_bsp,	mpc85xx_smp_get_bsp),
	PLATFORMMETHOD(platform_smp_start_cpu,	mpc85xx_smp_start_cpu),
	PLATFORMMETHOD(platform_smp_timebase_sync, mpc85xx_smp_timebase_sync),

	PLATFORMMETHOD(platform_reset,		mpc85xx_reset),

	PLATFORMMETHOD_END
};

DEFINE_CLASS_0(mpc85xx, mpc85xx_platform, mpc85xx_methods, 0);

PLATFORM_DEF(mpc85xx_platform);

static int
mpc85xx_probe(platform_t plat)
{
	u_int pvr = (mfpvr() >> 16) & 0xFFFF;

	switch (pvr) {
		case FSL_E500v1:
		case FSL_E500v2:
		case FSL_E500mc:
		case FSL_E5500:
		case FSL_E6500:
			return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

int
mpc85xx_attach(platform_t plat)
{
	phandle_t cpus, child, ccsr;
	const char *soc_name_guesses[] = {"/soc", "soc", NULL};
	const char **name;
	pcell_t ranges[6], acells, pacells, scells;
	uint64_t ccsrbar, ccsrsize;
	int i;

	if ((cpus = OF_finddevice("/cpus")) != -1) {
		for (maxcpu = 0, child = OF_child(cpus); child != 0;
		    child = OF_peer(child), maxcpu++)
			;
	} else
		maxcpu = 1;

	/*
	 * Locate CCSR region. Irritatingly, there is no way to find it
	 * unless you already know where it is. Try to infer its location
	 * from the device tree.
	 */

	ccsr = -1;
	for (name = soc_name_guesses; *name != NULL && ccsr == -1; name++)
		ccsr = OF_finddevice(*name);
	if (ccsr == -1) {
		char type[64];

	 	/* That didn't work. Search for devices of type "soc" */
		child = OF_child(OF_peer(0));
		for (OF_child(child); child != 0; child = OF_peer(child)) {
			if (OF_getprop(child, "device_type", type, sizeof(type))
			    <= 0)
				continue;

			if (strcmp(type, "soc") == 0) {
				ccsr = child;
				break;
			}
		}
	}

	if (ccsr == -1)
		panic("Could not locate CCSR window!");

	OF_getprop(ccsr, "#size-cells", &scells, sizeof(scells));
	OF_getprop(ccsr, "#address-cells", &acells, sizeof(acells));
	OF_searchprop(OF_parent(ccsr), "#address-cells", &pacells,
	    sizeof(pacells));
	OF_getprop(ccsr, "ranges", ranges, sizeof(ranges));
	ccsrbar = ccsrsize = 0;
	for (i = acells; i < acells + pacells; i++) {
		ccsrbar <<= 32;
		ccsrbar |= ranges[i];
	}
	for (i = acells + pacells; i < acells + pacells + scells; i++) {
		ccsrsize <<= 32;
		ccsrsize |= ranges[i];
	}
	ccsrbar_va = pmap_early_io_map(ccsrbar, ccsrsize);
	ccsrbar_pa = ccsrbar;
	ccsrbar_size = ccsrsize;

	mpc85xx_enable_l3_cache();

	return (0);
}

void
mpc85xx_mem_regions(platform_t plat, struct mem_region *phys, int *physsz,
    struct mem_region *avail, int *availsz)
{

	ofw_mem_regions(phys, physsz, avail, availsz);
}

static u_long
mpc85xx_timebase_freq(platform_t plat, struct cpuref *cpuref)
{
	u_long ticks;
	phandle_t cpus, child;
	pcell_t freq;

	if (bootinfo != NULL) {
		if (bootinfo[0] == 1) {
			/* Backward compatibility. See 8-STABLE. */
			ticks = bootinfo[3] >> 3;
		} else {
			/* Compatibility with Juniper's loader. */
			ticks = bootinfo[5] >> 3;
		}
	} else
		ticks = 0;

	if ((cpus = OF_finddevice("/cpus")) == -1)
		goto out;

	if ((child = OF_child(cpus)) == 0)
		goto out;

	switch (OF_getproplen(child, "timebase-frequency")) {
	case 4:
	{
		uint32_t tbase;
		OF_getprop(child, "timebase-frequency", &tbase, sizeof(tbase));
		ticks = tbase;
		return (ticks);
	}
	case 8:
	{
		uint64_t tbase;
		OF_getprop(child, "timebase-frequency", &tbase, sizeof(tbase));
		ticks = tbase;
		return (ticks);
	}
	default:
		break;
	}

	freq = 0;
	if (OF_getprop(child, "bus-frequency", (void *)&freq,
	    sizeof(freq)) <= 0)
		goto out;

	if (freq == 0)
		goto out;

	/*
	 * Time Base and Decrementer are updated every 8 CCB bus clocks.
	 * HID0[SEL_TBCLK] = 0
	 */
	if (mpc85xx_is_qoriq())
		ticks = freq / 32;
	else
		ticks = freq / 8;

out:
	if (ticks <= 0)
		panic("Unable to determine timebase frequency!");

	return (ticks);
}

static int
mpc85xx_smp_first_cpu(platform_t plat, struct cpuref *cpuref)
{

	cpu = 0;
	cpuref->cr_cpuid = cpu;
	cpuref->cr_hwref = cpuref->cr_cpuid;
	if (bootverbose)
		printf("powerpc_smp_first_cpu: cpuid %d\n", cpuref->cr_cpuid);
	cpu++;

	return (0);
}

static int
mpc85xx_smp_next_cpu(platform_t plat, struct cpuref *cpuref)
{

	if (cpu >= maxcpu)
		return (ENOENT);

	cpuref->cr_cpuid = cpu++;
	cpuref->cr_hwref = cpuref->cr_cpuid;
	if (bootverbose)
		printf("powerpc_smp_next_cpu: cpuid %d\n", cpuref->cr_cpuid);

	return (0);
}

static int
mpc85xx_smp_get_bsp(platform_t plat, struct cpuref *cpuref)
{

	cpuref->cr_cpuid = mfspr(SPR_PIR);
	cpuref->cr_hwref = cpuref->cr_cpuid;

	return (0);
}

#ifdef SMP
static int
mpc85xx_smp_start_cpu_epapr(platform_t plat, struct pcpu *pc)
{
	vm_paddr_t rel_pa, bptr;
	volatile struct cpu_release *rel;
	vm_offset_t rel_va, rel_page;
	phandle_t node;
	int i;

	/* If we're calling this, the node already exists. */
	node = OF_finddevice("/cpus");
	for (i = 0, node = OF_child(node); i < pc->pc_cpuid;
	    i++, node = OF_peer(node))
		;
	if (OF_getencprop(node, "cpu-release-addr", (pcell_t *)&rel_pa,
	    sizeof(rel_pa)) == -1) {
		return (ENOENT);
	}

	rel_page = kva_alloc(PAGE_SIZE);
	if (rel_page == 0)
		return (ENOMEM);

	critical_enter();
	rel_va = rel_page + (rel_pa & PAGE_MASK);
	pmap_kenter(rel_page, rel_pa & ~PAGE_MASK);
	rel = (struct cpu_release *)rel_va;
	bptr = pmap_kextract((uintptr_t)__boot_page);
	cpu_flush_dcache(__DEVOLATILE(struct cpu_release *,rel), sizeof(*rel));
	rel->pir = pc->pc_cpuid; __asm __volatile("sync");
	rel->entry_h = (bptr >> 32);
	rel->entry_l = bptr; __asm __volatile("sync");
	cpu_flush_dcache(__DEVOLATILE(struct cpu_release *,rel), sizeof(*rel));
	if (bootverbose)
		printf("Waking up CPU %d via CPU release page %p\n",
		    pc->pc_cpuid, rel);
	critical_exit();
	pmap_kremove(rel_page);
	kva_free(rel_page, PAGE_SIZE);

	return (0);
}
#endif

static int
mpc85xx_smp_start_cpu(platform_t plat, struct pcpu *pc)
{
#ifdef SMP
	vm_paddr_t bptr;
	uint32_t reg;
	int timeout;
	uintptr_t brr;
	int cpuid;
	int epapr_boot = 0;
	uint32_t tgt;

	if (mpc85xx_is_qoriq()) {
		reg = ccsr_read4(OCP85XX_COREDISR);
		cpuid = pc->pc_cpuid;

		if ((reg & (1 << cpuid)) != 0) {
		    printf("%s: CPU %d is disabled!\n", __func__, pc->pc_cpuid);
		    return (-1);
		}

		brr = OCP85XX_BRR;
	} else {
		brr = OCP85XX_EEBPCR;
		cpuid = pc->pc_cpuid + 24;
	}
	bp_kernload = kernload;
	/*
	 * bp_kernload is in the boot page.  Sync the cache because ePAPR
	 * booting has the other core(s) already running.
	 */
	cpu_flush_dcache(&bp_kernload, sizeof(bp_kernload));

	ap_pcpu = pc;
	__asm __volatile("msync; isync");

	/* First try the ePAPR way. */
	if (mpc85xx_smp_start_cpu_epapr(plat, pc) == 0) {
		epapr_boot = 1;
		goto spin_wait;
	}

	reg = ccsr_read4(brr);
	if ((reg & (1 << cpuid)) != 0) {
		printf("SMP: CPU %d already out of hold-off state!\n",
		    pc->pc_cpuid);
		return (ENXIO);
	}

	/* Flush caches to have our changes hit DRAM. */
	cpu_flush_dcache(__boot_page, 4096);

	bptr = pmap_kextract((uintptr_t)__boot_page);
	KASSERT((bptr & 0xfff) == 0,
	    ("%s: boot page is not aligned (%#jx)", __func__, (uintmax_t)bptr));
	if (mpc85xx_is_qoriq()) {
		/*
		 * Read DDR controller configuration to select proper BPTR target ID.
		 *
		 * On P5020 bit 29 of DDR1_CS0_CONFIG enables DDR controllers
		 * interleaving. If this bit is set, we have to use
		 * OCP85XX_TGTIF_RAM_INTL as BPTR target ID. On other QorIQ DPAA SoCs,
		 * this bit is reserved and always 0.
		 */

		reg = ccsr_read4(OCP85XX_DDR1_CS0_CONFIG);
		if (reg & (1 << 29))
			tgt = OCP85XX_TGTIF_RAM_INTL;
		else
			tgt = OCP85XX_TGTIF_RAM1;

		/*
		 * Set BSTR to the physical address of the boot page
		 */
		ccsr_write4(OCP85XX_BSTRH, bptr >> 32);
		ccsr_write4(OCP85XX_BSTRL, bptr);
		ccsr_write4(OCP85XX_BSTAR, OCP85XX_ENA_MASK |
		    (tgt << OCP85XX_TRGT_SHIFT_QORIQ) | (ffsl(PAGE_SIZE) - 2));

		/* Read back OCP85XX_BSTAR to synchronize write */
		ccsr_read4(OCP85XX_BSTAR);

		/*
		 * Enable and configure time base on new CPU.
		 */

		/* Set TB clock source to platform clock / 32 */
		reg = ccsr_read4(CCSR_CTBCKSELR);
		ccsr_write4(CCSR_CTBCKSELR, reg & ~(1 << pc->pc_cpuid));

		/* Enable TB */
		reg = ccsr_read4(CCSR_CTBENR);
		ccsr_write4(CCSR_CTBENR, reg | (1 << pc->pc_cpuid));
	} else {
		/*
		 * Set BPTR to the physical address of the boot page
		 */
		bptr = (bptr >> 12) | 0x80000000u;
		ccsr_write4(OCP85XX_BPTR, bptr);
		__asm __volatile("isync; msync");
	}

	/*
	 * Release AP from hold-off state
	 */
	reg = ccsr_read4(brr);
	ccsr_write4(brr, reg | (1 << cpuid));
	__asm __volatile("isync; msync");

spin_wait:
	timeout = 500;
	while (!pc->pc_awake && timeout--)
		DELAY(1000);	/* wait 1ms */

	/*
	 * Disable boot page translation so that the 4K page at the default
	 * address (= 0xfffff000) isn't permanently remapped and thus not
	 * usable otherwise.
	 */
	if (!epapr_boot) {
		if (mpc85xx_is_qoriq())
			ccsr_write4(OCP85XX_BSTAR, 0);
		else
			ccsr_write4(OCP85XX_BPTR, 0);
		__asm __volatile("isync; msync");
	}

	if (!pc->pc_awake)
		panic("SMP: CPU %d didn't wake up.\n", pc->pc_cpuid);
	return ((pc->pc_awake) ? 0 : EBUSY);
#else
	/* No SMP support */
	return (ENXIO);
#endif
}

static void
mpc85xx_reset(platform_t plat)
{

	/*
	 * Try the dedicated reset register first.
	 * If the SoC doesn't have one, we'll fall
	 * back to using the debug control register.
	 */
	ccsr_write4(OCP85XX_RSTCR, 2);

	/* Clear DBCR0, disables debug interrupts and events. */
	mtspr(SPR_DBCR0, 0);
	__asm __volatile("isync");

	/* Enable Debug Interrupts in MSR. */
	mtmsr(mfmsr() | PSL_DE);

	/* Enable debug interrupts and issue reset. */
	mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) | DBCR0_IDM | DBCR0_RST_SYSTEM);

	printf("Reset failed...\n");
	while (1)
		;
}

static void
mpc85xx_smp_timebase_sync(platform_t plat, u_long tb, int ap)
{
	static volatile bool tb_ready;
	static volatile int cpu_done;

	if (ap) {
		/* APs.  Hold off until we get a stable timebase. */
		while (!tb_ready)
			atomic_thread_fence_seq_cst();
		mttb(tb);
		atomic_add_int(&cpu_done, 1);
		while (cpu_done < mp_ncpus)
			atomic_thread_fence_seq_cst();
	} else {
		/* BSP */
		freeze_timebase(rcpm_dev, true);
		tb_ready = true;
		mttb(tb);
		atomic_add_int(&cpu_done, 1);
		while (cpu_done < mp_ncpus)
			atomic_thread_fence_seq_cst();
		freeze_timebase(rcpm_dev, false);
	}
}

/* Fallback freeze.  In case no real handler is found in the device tree. */
static void
dummy_freeze(device_t dev, bool freeze)
{
	/* Nothing to do here, move along. */
}


/* QorIQ Run control/power management timebase management. */

#define	RCPM_CTBENR	0x00000084
struct mpc85xx_rcpm_softc {
	struct resource *sc_mem;
};

static void
mpc85xx_rcpm_freeze_timebase(device_t dev, bool freeze)
{
	struct mpc85xx_rcpm_softc *sc;

	sc = device_get_softc(dev);
	
	if (freeze)
		bus_write_4(sc->sc_mem, RCPM_CTBENR, 0);
	else
		bus_write_4(sc->sc_mem, RCPM_CTBENR, (1 << maxcpu) - 1);
}

static int
mpc85xx_rcpm_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "fsl,qoriq-rcpm-1.0"))
		return (ENXIO);

	device_set_desc(dev, "QorIQ Run control and power management");
	return (BUS_PROBE_GENERIC);
}

static int
mpc85xx_rcpm_attach(device_t dev)
{
	struct mpc85xx_rcpm_softc *sc;
	int rid;

	sc = device_get_softc(dev);
	freeze_timebase = mpc85xx_rcpm_freeze_timebase;
	rcpm_dev = dev;

	rid = 0;
	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE | RF_SHAREABLE);

	return (0);
}

static device_method_t mpc85xx_rcpm_methods[] = {
	DEVMETHOD(device_probe,		mpc85xx_rcpm_probe),
	DEVMETHOD(device_attach,	mpc85xx_rcpm_attach),
	DEVMETHOD_END
};

static devclass_t mpc85xx_rcpm_devclass;

static driver_t mpc85xx_rcpm_driver = {
	"rcpm",
	mpc85xx_rcpm_methods,
	sizeof(struct mpc85xx_rcpm_softc)
};

EARLY_DRIVER_MODULE(mpc85xx_rcpm, simplebus, mpc85xx_rcpm_driver,
	mpc85xx_rcpm_devclass, 0, 0, BUS_PASS_BUS);


/* "Global utilities" power management/Timebase management. */

#define	GUTS_DEVDISR	0x00000070
#define	  DEVDISR_TB0	0x00004000
#define	  DEVDISR_TB1	0x00001000

struct mpc85xx_guts_softc {
	struct resource *sc_mem;
};

static void
mpc85xx_guts_freeze_timebase(device_t dev, bool freeze)
{
	struct mpc85xx_guts_softc *sc;
	uint32_t devdisr;

	sc = device_get_softc(dev);
	
	devdisr = bus_read_4(sc->sc_mem, GUTS_DEVDISR);
	if (freeze)
		bus_write_4(sc->sc_mem, GUTS_DEVDISR,
		    devdisr | (DEVDISR_TB0 | DEVDISR_TB1));
	else
		bus_write_4(sc->sc_mem, GUTS_DEVDISR,
		    devdisr & ~(DEVDISR_TB0 | DEVDISR_TB1));
}

static int
mpc85xx_guts_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "fsl,mpc8572-guts") &&
	    !ofw_bus_is_compatible(dev, "fsl,p1020-guts") &&
	    !ofw_bus_is_compatible(dev, "fsl,p1021-guts") &&
	    !ofw_bus_is_compatible(dev, "fsl,p1022-guts") &&
	    !ofw_bus_is_compatible(dev, "fsl,p1023-guts") &&
	    !ofw_bus_is_compatible(dev, "fsl,p2020-guts"))
		return (ENXIO);

	device_set_desc(dev, "MPC85xx Global Utilities");
	return (BUS_PROBE_GENERIC);
}

static int
mpc85xx_guts_attach(device_t dev)
{
	struct mpc85xx_rcpm_softc *sc;
	int rid;

	sc = device_get_softc(dev);
	freeze_timebase = mpc85xx_guts_freeze_timebase;
	rcpm_dev = dev;

	rid = 0;
	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE | RF_SHAREABLE);

	return (0);
}

static device_method_t mpc85xx_guts_methods[] = {
	DEVMETHOD(device_probe,		mpc85xx_guts_probe),
	DEVMETHOD(device_attach,	mpc85xx_guts_attach),
	DEVMETHOD_END
};

static driver_t mpc85xx_guts_driver = {
	"guts",
	mpc85xx_guts_methods,
	sizeof(struct mpc85xx_guts_softc)
};

static devclass_t mpc85xx_guts_devclass;

EARLY_DRIVER_MODULE(mpc85xx_guts, simplebus, mpc85xx_guts_driver,
	mpc85xx_guts_devclass, 0, 0, BUS_PASS_BUS);

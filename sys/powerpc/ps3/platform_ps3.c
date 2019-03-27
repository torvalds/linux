/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Nathan Whitehorn
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/hid.h>
#include <machine/platform.h>
#include <machine/platformvar.h>
#include <machine/smp.h>
#include <machine/spr.h>
#include <machine/vmparam.h>

#include <dev/ofw/openfirm.h>

#include "platform_if.h"
#include "ps3-hvcall.h"

#ifdef SMP
extern void *ap_pcpu;
#endif

static int ps3_probe(platform_t);
static int ps3_attach(platform_t);
static void ps3_mem_regions(platform_t, struct mem_region *phys, int *physsz,
    struct mem_region *avail, int *availsz);
static vm_offset_t ps3_real_maxaddr(platform_t);
static u_long ps3_timebase_freq(platform_t, struct cpuref *cpuref);
#ifdef SMP
static int ps3_smp_first_cpu(platform_t, struct cpuref *cpuref);
static int ps3_smp_next_cpu(platform_t, struct cpuref *cpuref);
static int ps3_smp_get_bsp(platform_t, struct cpuref *cpuref);
static int ps3_smp_start_cpu(platform_t, struct pcpu *cpu);
static void ps3_smp_probe_threads(platform_t);
static struct cpu_group *ps3_smp_topo(platform_t);
#endif
static void ps3_reset(platform_t);
static void ps3_cpu_idle(sbintime_t);

static platform_method_t ps3_methods[] = {
	PLATFORMMETHOD(platform_probe, 		ps3_probe),
	PLATFORMMETHOD(platform_attach,		ps3_attach),
	PLATFORMMETHOD(platform_mem_regions,	ps3_mem_regions),
	PLATFORMMETHOD(platform_real_maxaddr,	ps3_real_maxaddr),
	PLATFORMMETHOD(platform_timebase_freq,	ps3_timebase_freq),

#ifdef SMP
	PLATFORMMETHOD(platform_smp_first_cpu,	ps3_smp_first_cpu),
	PLATFORMMETHOD(platform_smp_next_cpu,	ps3_smp_next_cpu),
	PLATFORMMETHOD(platform_smp_get_bsp,	ps3_smp_get_bsp),
	PLATFORMMETHOD(platform_smp_start_cpu,	ps3_smp_start_cpu),
	PLATFORMMETHOD(platform_smp_probe_threads,	ps3_smp_probe_threads),
	PLATFORMMETHOD(platform_smp_topo,	ps3_smp_topo),
#endif

	PLATFORMMETHOD(platform_reset,		ps3_reset),

	PLATFORMMETHOD_END
};

static platform_def_t ps3_platform = {
	"ps3",
	ps3_methods,
	0
};

PLATFORM_DEF(ps3_platform);

static int ps3_boot_pir = 0;

static int
ps3_probe(platform_t plat)
{
	phandle_t root;
	char compatible[64];

	root = OF_finddevice("/");
	if (OF_getprop(root, "compatible", compatible, sizeof(compatible)) <= 0)
                return (BUS_PROBE_NOWILDCARD);
	
	if (strncmp(compatible, "sony,ps3", sizeof(compatible)) != 0)
		return (BUS_PROBE_NOWILDCARD);

	return (BUS_PROBE_SPECIFIC);
}

static int
ps3_attach(platform_t plat)
{

	pmap_mmu_install("mmu_ps3", BUS_PROBE_SPECIFIC);
	cpu_idle_hook = ps3_cpu_idle;

	/* Record our PIR at boot for later */
	ps3_boot_pir = mfspr(SPR_PIR);

	return (0);
}

void
ps3_mem_regions(platform_t plat, struct mem_region *phys, int *physsz,
    struct mem_region *avail_regions, int *availsz)
{
	uint64_t lpar_id, junk;
	int i;

	/* Prefer device tree information if available */
	if (OF_finddevice("/") != -1) {
		ofw_mem_regions(phys, physsz, avail_regions, availsz);
	} else {
		/* Real mode memory region is first segment */
		phys[0].mr_start = 0;
		phys[0].mr_size = ps3_real_maxaddr(plat);
		*physsz = *availsz = 1;
		avail_regions[0] = phys[0];
	}

	/* Now get extended memory region */
	lv1_get_logical_partition_id(&lpar_id);
	lv1_get_repository_node_value(lpar_id,
	    lv1_repository_string("bi") >> 32,
	    lv1_repository_string("rgntotal"), 0, 0,
	    &phys[*physsz].mr_size, &junk);
	for (i = 0; i < *physsz; i++)
		phys[*physsz].mr_size -= phys[i].mr_size;

	/* Convert to maximum amount we can allocate in 16 MB pages */
	phys[*physsz].mr_size -= phys[*physsz].mr_size % (16*1024*1024);

	/* Allocate extended memory region */
	lv1_allocate_memory(phys[*physsz].mr_size, 24 /* 16 MB pages */,
	    0, 0x04 /* any address */, &phys[*physsz].mr_start, &junk);
	avail_regions[*availsz] = phys[*physsz];
	(*physsz)++;
	(*availsz)++;
}

static u_long
ps3_timebase_freq(platform_t plat, struct cpuref *cpuref)
{
	uint64_t ticks, node_id, junk;

	lv1_get_repository_node_value(PS3_LPAR_ID_PME, 
	    lv1_repository_string("be") >> 32, 0, 0, 0, &node_id, &junk);
	lv1_get_repository_node_value(PS3_LPAR_ID_PME,
	    lv1_repository_string("be") >> 32, node_id,
	    lv1_repository_string("clock"), 0, &ticks, &junk);

	return (ticks);
}

#ifdef SMP
static int
ps3_smp_first_cpu(platform_t plat, struct cpuref *cpuref)
{

	cpuref->cr_cpuid = 0;
	cpuref->cr_hwref = ps3_boot_pir;

	return (0);
}

static int
ps3_smp_next_cpu(platform_t plat, struct cpuref *cpuref)
{

	if (cpuref->cr_cpuid >= 1)
		return (ENOENT);

	cpuref->cr_cpuid++;
	cpuref->cr_hwref = !ps3_boot_pir;

	return (0);
}

static int
ps3_smp_get_bsp(platform_t plat, struct cpuref *cpuref)
{

	cpuref->cr_cpuid = 0;
	cpuref->cr_hwref = ps3_boot_pir;

	return (0);
}

static int
ps3_smp_start_cpu(platform_t plat, struct pcpu *pc)
{
	/* kernel is spinning on 0x40 == -1 right now */
	volatile uint32_t *secondary_spin_sem =
	    (uint32_t *)PHYS_TO_DMAP((uintptr_t)0x40);
	int remote_pir = pc->pc_hwref;
	int timeout;

	ap_pcpu = pc;

	/* Try both PIR values, looping a few times: the HV likes moving us */
	timeout = 10000;
	while (!pc->pc_awake && timeout--) {
		*secondary_spin_sem = remote_pir;
		powerpc_sync();
		DELAY(100);
		remote_pir = !remote_pir;
	}

	return ((pc->pc_awake) ? 0 : EBUSY);
}

static void
ps3_smp_probe_threads(platform_t plat)
{
	mp_ncores = 1;
	smp_threads_per_core = 2;
}

static struct cpu_group *
ps3_smp_topo(platform_t plat)
{
	return (smp_topo_1level(CG_SHARE_L1, 2, CG_FLAG_SMT));
}
#endif

static void
ps3_reset(platform_t plat)
{
	lv1_panic(1);
}

static vm_offset_t
ps3_real_maxaddr(platform_t plat)
{
	uint64_t lpar_id, junk, ppe_id;
	static uint64_t rm_maxaddr = 0;

	if (rm_maxaddr == 0) {
		/* Get real mode memory region */
		lv1_get_logical_partition_id(&lpar_id);
		lv1_get_logical_ppe_id(&ppe_id);

		lv1_get_repository_node_value(lpar_id,
		    lv1_repository_string("bi") >> 32,
		    lv1_repository_string("pu"),
		    ppe_id, lv1_repository_string("rm_size"),
		    &rm_maxaddr, &junk);
	}
	
	return (rm_maxaddr);
}

static void
ps3_cpu_idle(sbintime_t sbt)
{
	lv1_pause(0);
}


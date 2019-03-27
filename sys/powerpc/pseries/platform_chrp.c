/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Marcel Moolenaar
 * Copyright (c) 2009 Nathan Whitehorn
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
#include <sys/sched.h>
#include <sys/smp.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/hid.h>
#include <machine/platformvar.h>
#include <machine/rtas.h>
#include <machine/smp.h>
#include <machine/spr.h>
#include <machine/trap.h>

#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>

#include "platform_if.h"

#ifdef SMP
extern void *ap_pcpu;
#endif

#ifdef __powerpc64__
static uint8_t splpar_vpa[MAXCPU][640] __aligned(128); /* XXX: dpcpu */
#endif

static vm_offset_t realmaxaddr = VM_MAX_ADDRESS;

static int chrp_probe(platform_t);
static int chrp_attach(platform_t);
void chrp_mem_regions(platform_t, struct mem_region *phys, int *physsz,
    struct mem_region *avail, int *availsz);
static vm_offset_t chrp_real_maxaddr(platform_t);
static u_long chrp_timebase_freq(platform_t, struct cpuref *cpuref);
static int chrp_smp_first_cpu(platform_t, struct cpuref *cpuref);
static int chrp_smp_next_cpu(platform_t, struct cpuref *cpuref);
static int chrp_smp_get_bsp(platform_t, struct cpuref *cpuref);
static void chrp_smp_ap_init(platform_t);
static int chrp_cpuref_init(void);
#ifdef SMP
static int chrp_smp_start_cpu(platform_t, struct pcpu *cpu);
static void chrp_smp_probe_threads(platform_t plat);
static struct cpu_group *chrp_smp_topo(platform_t plat);
#endif
static void chrp_reset(platform_t);
#ifdef __powerpc64__
#include "phyp-hvcall.h"
static void phyp_cpu_idle(sbintime_t sbt);
#endif

static struct cpuref platform_cpuref[MAXCPU];
static int platform_cpuref_cnt;
static int platform_cpuref_valid;

static platform_method_t chrp_methods[] = {
	PLATFORMMETHOD(platform_probe, 		chrp_probe),
	PLATFORMMETHOD(platform_attach,		chrp_attach),
	PLATFORMMETHOD(platform_mem_regions,	chrp_mem_regions),
	PLATFORMMETHOD(platform_real_maxaddr,	chrp_real_maxaddr),
	PLATFORMMETHOD(platform_timebase_freq,	chrp_timebase_freq),
	
	PLATFORMMETHOD(platform_smp_ap_init,	chrp_smp_ap_init),
	PLATFORMMETHOD(platform_smp_first_cpu,	chrp_smp_first_cpu),
	PLATFORMMETHOD(platform_smp_next_cpu,	chrp_smp_next_cpu),
	PLATFORMMETHOD(platform_smp_get_bsp,	chrp_smp_get_bsp),
#ifdef SMP
	PLATFORMMETHOD(platform_smp_start_cpu,	chrp_smp_start_cpu),
	PLATFORMMETHOD(platform_smp_probe_threads,	chrp_smp_probe_threads),
	PLATFORMMETHOD(platform_smp_topo,	chrp_smp_topo),
#endif

	PLATFORMMETHOD(platform_reset,		chrp_reset),

	{ 0, 0 }
};

static platform_def_t chrp_platform = {
	"chrp",
	chrp_methods,
	0
};

PLATFORM_DEF(chrp_platform);

static int
chrp_probe(platform_t plat)
{
	if (OF_finddevice("/memory") != -1 || OF_finddevice("/memory@0") != -1)
		return (BUS_PROBE_GENERIC);

	return (ENXIO);
}

static int
chrp_attach(platform_t plat)
{
#ifdef __powerpc64__
	int i;

	/* XXX: check for /rtas/ibm,hypertas-functions? */
	if (!(mfmsr() & PSL_HV)) {
		struct mem_region *phys, *avail;
		int nphys, navail;
		mem_regions(&phys, &nphys, &avail, &navail);
		realmaxaddr = phys[0].mr_size;

		pmap_mmu_install("mmu_phyp", BUS_PROBE_SPECIFIC);
		cpu_idle_hook = phyp_cpu_idle;

		/* Set up important VPA fields */
		for (i = 0; i < MAXCPU; i++) {
			/* First two: VPA size */
			splpar_vpa[i][4] =
			    (uint8_t)((sizeof(splpar_vpa[i]) >> 8) & 0xff);
			splpar_vpa[i][5] =
			    (uint8_t)(sizeof(splpar_vpa[i]) & 0xff);
			splpar_vpa[i][0xba] = 1;	/* Maintain FPRs */
			splpar_vpa[i][0xbb] = 1;	/* Maintain PMCs */
			splpar_vpa[i][0xfc] = 0xff;	/* Maintain full SLB */
			splpar_vpa[i][0xfd] = 0xff;
			splpar_vpa[i][0xff] = 1;	/* Maintain Altivec */
		}
		mb();

		/* Set up hypervisor CPU stuff */
		chrp_smp_ap_init(plat);
	}
#endif
	chrp_cpuref_init();

	/* Some systems (e.g. QEMU) need Open Firmware to stand down */
	ofw_quiesce();

	return (0);
}

static int
parse_drconf_memory(struct mem_region *ofmem, int *msz,
		    struct mem_region *ofavail, int *asz)
{
	phandle_t phandle;
	vm_offset_t base;
	int i, idx, len, lasz, lmsz, res;
	uint32_t flags, lmb_size[2];
	uint32_t *dmem;

	lmsz = *msz;
	lasz = *asz;

	phandle = OF_finddevice("/ibm,dynamic-reconfiguration-memory");
	if (phandle == -1)
		/* No drconf node, return. */
		return (0);

	res = OF_getencprop(phandle, "ibm,lmb-size", lmb_size,
	    sizeof(lmb_size));
	if (res == -1)
		return (0);
	printf("Logical Memory Block size: %d MB\n", lmb_size[1] >> 20);

	/* Parse the /ibm,dynamic-memory.
	   The first position gives the # of entries. The next two words
 	   reflect the address of the memory block. The next four words are
	   the DRC index, reserved, list index and flags.
	   (see PAPR C.6.6.2 ibm,dynamic-reconfiguration-memory)
	   
	    #el  Addr   DRC-idx  res   list-idx  flags
	   -------------------------------------------------
	   | 4 |   8   |   4   |   4   |   4   |   4   |....
	   -------------------------------------------------
	*/

	len = OF_getproplen(phandle, "ibm,dynamic-memory");
	if (len > 0) {

		/* We have to use a variable length array on the stack
		   since we have very limited stack space.
		*/
		cell_t arr[len/sizeof(cell_t)];

		res = OF_getencprop(phandle, "ibm,dynamic-memory", arr,
		    sizeof(arr));
		if (res == -1)
			return (0);

		/* Number of elements */
		idx = arr[0];

		/* First address, in arr[1], arr[2]*/
		dmem = &arr[1];
	
		for (i = 0; i < idx; i++) {
			base = ((uint64_t)dmem[0] << 32) + dmem[1];
			dmem += 4;
			flags = dmem[1];
			/* Use region only if available and not reserved. */
			if ((flags & 0x8) && !(flags & 0x80)) {
				ofmem[lmsz].mr_start = base;
				ofmem[lmsz].mr_size = (vm_size_t)lmb_size[1];
				ofavail[lasz].mr_start = base;
				ofavail[lasz].mr_size = (vm_size_t)lmb_size[1];
				lmsz++;
				lasz++;
			}
			dmem += 2;
		}
	}

	*msz = lmsz;
	*asz = lasz;

	return (1);
}

void
chrp_mem_regions(platform_t plat, struct mem_region *phys, int *physsz,
    struct mem_region *avail, int *availsz)
{
	vm_offset_t maxphysaddr;
	int i;

	ofw_mem_regions(phys, physsz, avail, availsz);
	parse_drconf_memory(phys, physsz, avail, availsz);

	/*
	 * On some firmwares (SLOF), some memory may be marked available that
	 * doesn't actually exist. This manifests as an extension of the last
	 * available segment past the end of physical memory, so truncate that
	 * one.
	 */
	maxphysaddr = 0;
	for (i = 0; i < *physsz; i++)
		if (phys[i].mr_start + phys[i].mr_size > maxphysaddr)
			maxphysaddr = phys[i].mr_start + phys[i].mr_size;

	for (i = 0; i < *availsz; i++)
		if (avail[i].mr_start + avail[i].mr_size > maxphysaddr)
			avail[i].mr_size = maxphysaddr - avail[i].mr_start;
}

static vm_offset_t
chrp_real_maxaddr(platform_t plat)
{
	return (realmaxaddr);
}

static u_long
chrp_timebase_freq(platform_t plat, struct cpuref *cpuref)
{
	phandle_t cpus, cpunode;
	int32_t ticks = -1;
	int res;
	char buf[8];

	cpus = OF_finddevice("/cpus");
	if (cpus == -1)
		panic("CPU tree not found on Open Firmware\n");

	for (cpunode = OF_child(cpus); cpunode != 0; cpunode = OF_peer(cpunode)) {
		res = OF_getprop(cpunode, "device_type", buf, sizeof(buf));
		if (res > 0 && strcmp(buf, "cpu") == 0)
			break;
	}
	if (cpunode <= 0)
		panic("CPU node not found on Open Firmware\n");

	OF_getencprop(cpunode, "timebase-frequency", &ticks, sizeof(ticks));

	if (ticks <= 0)
		panic("Unable to determine timebase frequency!");

	return (ticks);
}

static int
chrp_smp_first_cpu(platform_t plat, struct cpuref *cpuref)
{

	if (platform_cpuref_valid == 0)
		return (EINVAL);

	cpuref->cr_cpuid = 0;
	cpuref->cr_hwref = platform_cpuref[0].cr_hwref;

	return (0);
}

static int
chrp_smp_next_cpu(platform_t plat, struct cpuref *cpuref)
{
	int id;

	if (platform_cpuref_valid == 0)
		return (EINVAL);

	id = cpuref->cr_cpuid + 1;
	if (id >= platform_cpuref_cnt)
		return (ENOENT);

	cpuref->cr_cpuid = platform_cpuref[id].cr_cpuid;
	cpuref->cr_hwref = platform_cpuref[id].cr_hwref;

	return (0);
}

static int
chrp_smp_get_bsp(platform_t plat, struct cpuref *cpuref)
{

	cpuref->cr_cpuid = platform_cpuref[0].cr_cpuid;
	cpuref->cr_hwref = platform_cpuref[0].cr_hwref;
	return (0);
}

static void
get_cpu_reg(phandle_t cpu, cell_t *reg)
{
	int res;

	res = OF_getproplen(cpu, "reg");
	if (res != sizeof(cell_t))
		panic("Unexpected length for CPU property reg on Open Firmware\n");
	OF_getencprop(cpu, "reg", reg, res);
}

static int
chrp_cpuref_init(void)
{
	phandle_t cpu, dev, chosen, pbsp;
	ihandle_t ibsp;
	char buf[32];
	int a, bsp, res, res2, tmp_cpuref_cnt;
	static struct cpuref tmp_cpuref[MAXCPU];
	cell_t interrupt_servers[32], addr_cells, size_cells, reg, bsp_reg;

	if (platform_cpuref_valid)
		return (0);

	dev = OF_peer(0);
	dev = OF_child(dev);
	while (dev != 0) {
		res = OF_getprop(dev, "name", buf, sizeof(buf));
		if (res > 0 && strcmp(buf, "cpus") == 0)
			break;
		dev = OF_peer(dev);
	}

	/* Make sure that cpus reg property have 1 address cell and 0 size cells */
	res = OF_getproplen(dev, "#address-cells");
	res2 = OF_getproplen(dev, "#size-cells");
	if (res != res2 || res != sizeof(cell_t))
		panic("CPU properties #address-cells and #size-cells not found on Open Firmware\n");
	OF_getencprop(dev, "#address-cells", &addr_cells, sizeof(addr_cells));
	OF_getencprop(dev, "#size-cells", &size_cells, sizeof(size_cells));
	if (addr_cells != 1 || size_cells != 0)
		panic("Unexpected values for CPU properties #address-cells and #size-cells on Open Firmware\n");

	/* Look for boot CPU in /chosen/cpu and /chosen/fdtbootcpu */

	chosen = OF_finddevice("/chosen");
	if (chosen == -1)
		panic("Device /chosen not found on Open Firmware\n");

	bsp_reg = -1;

	/* /chosen/cpu */
	if (OF_getproplen(chosen, "cpu") == sizeof(ihandle_t)) {
		OF_getprop(chosen, "cpu", &ibsp, sizeof(ibsp));
		pbsp = OF_instance_to_package(ibsp);
		if (pbsp != -1)
			get_cpu_reg(pbsp, &bsp_reg);
	}

	/* /chosen/fdtbootcpu */
	if (bsp_reg == -1) {
		if (OF_getproplen(chosen, "fdtbootcpu") == sizeof(cell_t))
			OF_getprop(chosen, "fdtbootcpu", &bsp_reg, sizeof(bsp_reg));
	}

	if (bsp_reg == -1)
		panic("Boot CPU not found on Open Firmware\n");

	bsp = -1;
	tmp_cpuref_cnt = 0;
	for (cpu = OF_child(dev); cpu != 0; cpu = OF_peer(cpu)) {
		res = OF_getprop(cpu, "device_type", buf, sizeof(buf));
		if (res > 0 && strcmp(buf, "cpu") == 0) {
			res = OF_getproplen(cpu, "ibm,ppc-interrupt-server#s");
			if (res > 0) {
				OF_getencprop(cpu, "ibm,ppc-interrupt-server#s",
				    interrupt_servers, res);

				get_cpu_reg(cpu, &reg);
				if (reg == bsp_reg)
					bsp = tmp_cpuref_cnt;

				for (a = 0; a < res/sizeof(cell_t); a++) {
					tmp_cpuref[tmp_cpuref_cnt].cr_hwref = interrupt_servers[a];
					tmp_cpuref[tmp_cpuref_cnt].cr_cpuid = tmp_cpuref_cnt;
					tmp_cpuref_cnt++;
				}
			}
		}
	}

	if (bsp == -1)
		panic("Boot CPU not found\n");

	/* Map IDs, so BSP has CPUID 0 regardless of hwref */
	for (a = bsp; a < tmp_cpuref_cnt; a++) {
		platform_cpuref[platform_cpuref_cnt].cr_hwref = tmp_cpuref[a].cr_hwref;
		platform_cpuref[platform_cpuref_cnt].cr_cpuid = platform_cpuref_cnt;
		platform_cpuref_cnt++;
	}
	for (a = 0; a < bsp; a++) {
		platform_cpuref[platform_cpuref_cnt].cr_hwref = tmp_cpuref[a].cr_hwref;
		platform_cpuref[platform_cpuref_cnt].cr_cpuid = platform_cpuref_cnt;
		platform_cpuref_cnt++;
	}

	platform_cpuref_valid = 1;

	return (0);
}


#ifdef SMP
static int
chrp_smp_start_cpu(platform_t plat, struct pcpu *pc)
{
	cell_t start_cpu;
	int result, err, timeout;

	if (!rtas_exists()) {
		printf("RTAS uninitialized: unable to start AP %d\n",
		    pc->pc_cpuid);
		return (ENXIO);
	}

	start_cpu = rtas_token_lookup("start-cpu");
	if (start_cpu == -1) {
		printf("RTAS unknown method: unable to start AP %d\n",
		    pc->pc_cpuid);
		return (ENXIO);
	}

	ap_pcpu = pc;
	powerpc_sync();

	result = rtas_call_method(start_cpu, 3, 1, pc->pc_hwref, EXC_RST, pc,
	    &err);
	if (result < 0 || err != 0) {
		printf("RTAS error (%d/%d): unable to start AP %d\n",
		    result, err, pc->pc_cpuid);
		return (ENXIO);
	}

	timeout = 10000;
	while (!pc->pc_awake && timeout--)
		DELAY(100);

	return ((pc->pc_awake) ? 0 : EBUSY);
}

static void
chrp_smp_probe_threads(platform_t plat)
{
	struct pcpu *pc, *last_pc;
	int i, ncores;

	ncores = 0;
	last_pc = NULL;
	for (i = 0; i <= mp_maxid; i++) {
		pc = pcpu_find(i);
		if (pc == NULL)
			continue;
		if (last_pc == NULL || pc->pc_hwref != last_pc->pc_hwref)
			ncores++;
		last_pc = pc;
	}

	mp_ncores = ncores;
	if (mp_ncpus % ncores == 0)
		smp_threads_per_core = mp_ncpus / ncores;
}

static struct cpu_group *
chrp_smp_topo(platform_t plat)
{

	if (mp_ncpus % mp_ncores != 0) {
		printf("WARNING: Irregular SMP topology. Performance may be "
		     "suboptimal (%d CPUS, %d cores)\n", mp_ncpus, mp_ncores);
		return (smp_topo_none());
	}

	/* Don't do anything fancier for non-threaded SMP */
	if (mp_ncpus == mp_ncores)
		return (smp_topo_none());

	return (smp_topo_1level(CG_SHARE_L1, smp_threads_per_core,
	    CG_FLAG_SMT));
}
#endif

static void
chrp_reset(platform_t platform)
{
	OF_reboot();
}

#ifdef __powerpc64__
static void
phyp_cpu_idle(sbintime_t sbt)
{
	register_t msr;

	msr = mfmsr();

	mtmsr(msr & ~PSL_EE);
	if (sched_runnable()) {
		mtmsr(msr);
		return;
	}

	phyp_hcall(H_CEDE); /* Re-enables interrupts internally */
	mtmsr(msr);
}

static void
chrp_smp_ap_init(platform_t platform)
{
	if (!(mfmsr() & PSL_HV)) {
		/* Register VPA */
		phyp_hcall(H_REGISTER_VPA, 1UL, PCPU_GET(hwref),
		    splpar_vpa[PCPU_GET(hwref)]);

		/* Set interrupt priority */
		phyp_hcall(H_CPPR, 0xff);
	}
}
#else
static void
chrp_smp_ap_init(platform_t platform)
{
}
#endif


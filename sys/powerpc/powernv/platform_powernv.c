/*-
 * Copyright (c) 2015 Nathan Whitehorn
 * Copyright (c) 2017-2018 Semihalf
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
#include <sys/smp.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/hid.h>
#include <machine/platformvar.h>
#include <machine/pmap.h>
#include <machine/rtas.h>
#include <machine/smp.h>
#include <machine/spr.h>
#include <machine/trap.h>

#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>
#include <powerpc/aim/mmu_oea64.h>

#include "platform_if.h"
#include "opal.h"

#ifdef SMP
extern void *ap_pcpu;
#endif

void (*powernv_smp_ap_extra_init)(void);

static int powernv_probe(platform_t);
static int powernv_attach(platform_t);
void powernv_mem_regions(platform_t, struct mem_region *phys, int *physsz,
    struct mem_region *avail, int *availsz);
static u_long powernv_timebase_freq(platform_t, struct cpuref *cpuref);
static int powernv_smp_first_cpu(platform_t, struct cpuref *cpuref);
static int powernv_smp_next_cpu(platform_t, struct cpuref *cpuref);
static int powernv_smp_get_bsp(platform_t, struct cpuref *cpuref);
static void powernv_smp_ap_init(platform_t);
#ifdef SMP
static int powernv_smp_start_cpu(platform_t, struct pcpu *cpu);
static void powernv_smp_probe_threads(platform_t);
static struct cpu_group *powernv_smp_topo(platform_t plat);
#endif
static void powernv_reset(platform_t);
static void powernv_cpu_idle(sbintime_t sbt);
static int powernv_cpuref_init(void);

static platform_method_t powernv_methods[] = {
	PLATFORMMETHOD(platform_probe, 		powernv_probe),
	PLATFORMMETHOD(platform_attach,		powernv_attach),
	PLATFORMMETHOD(platform_mem_regions,	powernv_mem_regions),
	PLATFORMMETHOD(platform_timebase_freq,	powernv_timebase_freq),
	
	PLATFORMMETHOD(platform_smp_ap_init,	powernv_smp_ap_init),
	PLATFORMMETHOD(platform_smp_first_cpu,	powernv_smp_first_cpu),
	PLATFORMMETHOD(platform_smp_next_cpu,	powernv_smp_next_cpu),
	PLATFORMMETHOD(platform_smp_get_bsp,	powernv_smp_get_bsp),
#ifdef SMP
	PLATFORMMETHOD(platform_smp_start_cpu,	powernv_smp_start_cpu),
	PLATFORMMETHOD(platform_smp_probe_threads,	powernv_smp_probe_threads),
	PLATFORMMETHOD(platform_smp_topo,	powernv_smp_topo),
#endif

	PLATFORMMETHOD(platform_reset,		powernv_reset),

	{ 0, 0 }
};

static platform_def_t powernv_platform = {
	"powernv",
	powernv_methods,
	0
};

static struct cpuref platform_cpuref[MAXCPU];
static int platform_cpuref_cnt;
static int platform_cpuref_valid;

PLATFORM_DEF(powernv_platform);

static uint64_t powernv_boot_pir;

static int
powernv_probe(platform_t plat)
{
	if (opal_check() == 0)
		return (BUS_PROBE_SPECIFIC);

	return (ENXIO);
}

static int
powernv_attach(platform_t plat)
{
	uint32_t nptlp, shift = 0, slb_encoding = 0;
	int32_t lp_size, lp_encoding;
	char buf[255];
	pcell_t prop;
	phandle_t cpu;
	int res, len, idx;
	register_t msr;

	/* Ping OPAL again just to make sure */
	opal_check();

#if BYTE_ORDER == LITTLE_ENDIAN
	opal_call(OPAL_REINIT_CPUS, 2 /* Little endian */);
#else
	opal_call(OPAL_REINIT_CPUS, 1 /* Big endian */);
#endif

       if (cpu_idle_hook == NULL)
                cpu_idle_hook = powernv_cpu_idle;

	powernv_boot_pir = mfspr(SPR_PIR);

	/* LPID must not be altered when PSL_DR or PSL_IR is set */
	msr = mfmsr();
	mtmsr(msr & ~(PSL_DR | PSL_IR));

	/* Direct interrupts to SRR instead of HSRR and reset LPCR otherwise */
	mtspr(SPR_LPID, 0);
	isync();

	if (cpu_features2 & PPC_FEATURE2_ARCH_3_00)
		lpcr |= LPCR_HVICE;

	mtspr(SPR_LPCR, lpcr);
	isync();

	mtmsr(msr);

	powernv_cpuref_init();

	/* Set SLB count from device tree */
	cpu = OF_peer(0);
	cpu = OF_child(cpu);
	while (cpu != 0) {
		res = OF_getprop(cpu, "name", buf, sizeof(buf));
		if (res > 0 && strcmp(buf, "cpus") == 0)
			break;
		cpu = OF_peer(cpu);
	}
	if (cpu == 0)
		goto out;

	cpu = OF_child(cpu);
	while (cpu != 0) {
		res = OF_getprop(cpu, "device_type", buf, sizeof(buf));
		if (res > 0 && strcmp(buf, "cpu") == 0)
			break;
		cpu = OF_peer(cpu);
	}
	if (cpu == 0)
		goto out;

	res = OF_getencprop(cpu, "ibm,slb-size", &prop, sizeof(prop));
	if (res > 0)
		n_slbs = prop;

	/*
	 * Scan the large page size property for PAPR compatible machines.
	 * See PAPR D.5 Changes to Section 5.1.4, 'CPU Node Properties'
	 * for the encoding of the property.
	 */

	len = OF_getproplen(cpu, "ibm,segment-page-sizes");
	if (len > 0) {
		/*
		 * We have to use a variable length array on the stack
		 * since we have very limited stack space.
		 */
		pcell_t arr[len/sizeof(cell_t)];
		res = OF_getencprop(cpu, "ibm,segment-page-sizes", arr,
		    sizeof(arr));
		len /= 4;
		idx = 0;
		while (len > 0) {
			shift = arr[idx];
			slb_encoding = arr[idx + 1];
			nptlp = arr[idx + 2];
			idx += 3;
			len -= 3;
			while (len > 0 && nptlp) {
				lp_size = arr[idx];
				lp_encoding = arr[idx+1];
				if (slb_encoding == SLBV_L && lp_encoding == 0)
					break;

				idx += 2;
				len -= 2;
				nptlp--;
			}
			if (nptlp && slb_encoding == SLBV_L && lp_encoding == 0)
				break;
		}

		if (len == 0)
			panic("Standard large pages (SLB[L] = 1, PTE[LP] = 0) "
			    "not supported by this system.");

		moea64_large_page_shift = shift;
		moea64_large_page_size = 1ULL << lp_size;
	}

out:
	return (0);
}


void
powernv_mem_regions(platform_t plat, struct mem_region *phys, int *physsz,
    struct mem_region *avail, int *availsz)
{

	ofw_mem_regions(phys, physsz, avail, availsz);
}

static u_long
powernv_timebase_freq(platform_t plat, struct cpuref *cpuref)
{
	char buf[8];
	phandle_t cpu, dev, root;
	int res;
	int32_t ticks = -1;

	root = OF_peer(0);
	dev = OF_child(root);
	while (dev != 0) {
		res = OF_getprop(dev, "name", buf, sizeof(buf));
		if (res > 0 && strcmp(buf, "cpus") == 0)
			break;
		dev = OF_peer(dev);
	}

	for (cpu = OF_child(dev); cpu != 0; cpu = OF_peer(cpu)) {
		res = OF_getprop(cpu, "device_type", buf, sizeof(buf));
		if (res > 0 && strcmp(buf, "cpu") == 0)
			break;
	}
	if (cpu == 0)
		return (512000000);

	OF_getencprop(cpu, "timebase-frequency", &ticks, sizeof(ticks));

	if (ticks <= 0)
		panic("Unable to determine timebase frequency!");

	return (ticks);

}

static int
powernv_cpuref_init(void)
{
	phandle_t cpu, dev;
	char buf[32];
	int a, res, tmp_cpuref_cnt;
	static struct cpuref tmp_cpuref[MAXCPU];
	cell_t interrupt_servers[32];
	uint64_t bsp;

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

	bsp = 0;
	tmp_cpuref_cnt = 0;
	for (cpu = OF_child(dev); cpu != 0; cpu = OF_peer(cpu)) {
		res = OF_getprop(cpu, "device_type", buf, sizeof(buf));
		if (res > 0 && strcmp(buf, "cpu") == 0) {
			res = OF_getproplen(cpu, "ibm,ppc-interrupt-server#s");
			if (res > 0) {


				OF_getencprop(cpu, "ibm,ppc-interrupt-server#s",
				    interrupt_servers, res);

				for (a = 0; a < res/sizeof(cell_t); a++) {
					tmp_cpuref[tmp_cpuref_cnt].cr_hwref = interrupt_servers[a];
					tmp_cpuref[tmp_cpuref_cnt].cr_cpuid = tmp_cpuref_cnt;

					if (interrupt_servers[a] == (uint32_t)powernv_boot_pir)
						bsp = tmp_cpuref_cnt;

					tmp_cpuref_cnt++;
				}
			}
		}
	}

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

static int
powernv_smp_first_cpu(platform_t plat, struct cpuref *cpuref)
{
	if (platform_cpuref_valid == 0)
		return (EINVAL);

	cpuref->cr_cpuid = 0;
	cpuref->cr_hwref = platform_cpuref[0].cr_hwref;

	return (0);
}

static int
powernv_smp_next_cpu(platform_t plat, struct cpuref *cpuref)
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
powernv_smp_get_bsp(platform_t plat, struct cpuref *cpuref)
{

	cpuref->cr_cpuid = platform_cpuref[0].cr_cpuid;
	cpuref->cr_hwref = platform_cpuref[0].cr_hwref;
	return (0);
}

#ifdef SMP
static int
powernv_smp_start_cpu(platform_t plat, struct pcpu *pc)
{
	int result;

	ap_pcpu = pc;
	powerpc_sync();

	result = opal_call(OPAL_START_CPU, pc->pc_hwref, EXC_RST);
	if (result != OPAL_SUCCESS) {
		printf("OPAL error (%d): unable to start AP %d\n",
		    result, (int)pc->pc_hwref);
		return (ENXIO);
	}

	return (0);
}

static void
powernv_smp_probe_threads(platform_t plat)
{
	char buf[8];
	phandle_t cpu, dev, root;
	int res, nthreads;

	root = OF_peer(0);

	dev = OF_child(root);
	while (dev != 0) {
		res = OF_getprop(dev, "name", buf, sizeof(buf));
		if (res > 0 && strcmp(buf, "cpus") == 0)
			break;
		dev = OF_peer(dev);
	}

	nthreads = 1;
	for (cpu = OF_child(dev); cpu != 0; cpu = OF_peer(cpu)) {
		res = OF_getprop(cpu, "device_type", buf, sizeof(buf));
		if (res <= 0 || strcmp(buf, "cpu") != 0)
			continue;

		res = OF_getproplen(cpu, "ibm,ppc-interrupt-server#s");

		if (res >= 0)
			nthreads = res / sizeof(cell_t);
		else
			nthreads = 1;
		break;
	}

	smp_threads_per_core = nthreads;
	if (mp_ncpus % nthreads == 0)
		mp_ncores = mp_ncpus / nthreads;
}

static struct cpu_group *
powernv_smp_topo(platform_t plat)
{
	if (mp_ncpus % smp_threads_per_core != 0) {
		printf("WARNING: Irregular SMP topology. Performance may be "
		     "suboptimal (%d threads, %d on first core)\n",
		     mp_ncpus, smp_threads_per_core);
		return (smp_topo_none());
	}

	/* Don't do anything fancier for non-threaded SMP */
	if (smp_threads_per_core == 1)
		return (smp_topo_none());

	return (smp_topo_1level(CG_SHARE_L1, smp_threads_per_core,
	    CG_FLAG_SMT));
}

#endif

static void
powernv_reset(platform_t platform)
{

	opal_call(OPAL_CEC_REBOOT);
}

static void
powernv_smp_ap_init(platform_t platform)
{

	if (powernv_smp_ap_extra_init != NULL)
		powernv_smp_ap_extra_init();
}

static void
powernv_cpu_idle(sbintime_t sbt)
{
}

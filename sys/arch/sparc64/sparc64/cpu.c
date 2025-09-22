/*	$OpenBSD: cpu.c,v 1.77 2024/03/29 21:26:38 miod Exp $	*/
/*	$NetBSD: cpu.c,v 1.13 2001/05/26 21:27:15 chs Exp $ */

/*
 * Copyright (c) 1996
 *	The President and Fellows of Harvard College. All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by Harvard University.
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Aaron Brown and
 *	Harvard University.
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)cpu.c	8.5 (Berkeley) 11/23/93
 *
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/reg.h>
#include <machine/hypervisor.h>
#include <machine/openfirm.h>
#include <machine/pmap.h>
#include <machine/sparc64.h>

#include <sparc64/sparc64/cache.h>

#include <sparc64/dev/starfire.h>

struct cacheinfo cacheinfo = {
	.c_dcache_flush_page = us_dcache_flush_page
};

void (*cpu_start_clock)(void);

/* Linked list of all CPUs in system. */
struct cpu_info *cpus = NULL;

struct cpu_info *alloc_cpuinfo(struct mainbus_attach_args *);

/* The following are used externally (sysctl_hw). */
char	machine[] = MACHINE;		/* from <machine/param.h> */
char	cpu_model[100];

/* The CPU configuration driver. */
int cpu_match(struct device *, void *, void *);
void cpu_attach(struct device *, struct device *, void *);

const struct cfattach cpu_ca = {
	sizeof(struct device), cpu_match, cpu_attach
};

void cpu_init(struct cpu_info *ci);
void cpu_hatch(void);

int sparc64_cpuspeed(int *);

int hummingbird_div(uint64_t);
uint64_t hummingbird_estar_mode(int);
void hummingbird_enable_self_refresh(void);
void hummingbird_disable_self_refresh(void);
void hummingbird_set_refresh_count(int, int);
void hummingbird_setperf(int);
void hummingbird_init(struct cpu_info *ci);

#define	IU_IMPL(v)	((((u_int64_t)(v))&VER_IMPL) >> VER_IMPL_SHIFT)
#define	IU_VERS(v)	((((u_int64_t)(v))&VER_MASK) >> VER_MASK_SHIFT)

/* virtual address allocation mode for struct cpu_info */
struct kmem_va_mode kv_cpu_info = {
	.kv_map = &kernel_map,
	.kv_align = 8 * PAGE_SIZE
};

struct cpu_info *
alloc_cpuinfo(struct mainbus_attach_args *ma)
{
	paddr_t pa0, pa;
	vaddr_t va, va0;
	vsize_t sz = 8 * PAGE_SIZE;
	int portid;
	struct cpu_info *cpi, *ci;
	extern paddr_t cpu0paddr;

	portid = getpropint(ma->ma_node, "upa-portid", -1);
	if (portid == -1)
		portid = getpropint(ma->ma_node, "portid", -1);
	if (portid == -1)
		portid = getpropint(ma->ma_node, "cpuid", -1);
	if (portid == -1 && ma->ma_nreg > 0)
		portid = (ma->ma_reg[0].ur_paddr >> 32) & 0x0fffffff;
	if (portid == -1)
		panic("alloc_cpuinfo: portid");

	for (cpi = cpus; cpi != NULL; cpi = cpi->ci_next)
		if (cpi->ci_upaid == portid)
			return cpi;

	va = (vaddr_t)km_alloc(sz, &kv_cpu_info, &kp_none, &kd_nowait);
	if (va == 0)
		panic("alloc_cpuinfo: no virtual space");
	va0 = va;

	pa0 = cpu0paddr;
	cpu0paddr += sz;

	for (pa = pa0; pa < cpu0paddr; pa += PAGE_SIZE, va += PAGE_SIZE)
		pmap_kenter_pa(va, pa, PROT_READ | PROT_WRITE);

	pmap_update(pmap_kernel());

	cpi = (struct cpu_info *)(va0 + CPUINFO_VA - INTSTACK);

	memset((void *)va0, 0, sz);

	/*
	 * Initialize cpuinfo structure.
	 *
	 * Arrange pcb, idle stack and interrupt stack in the same
	 * way as is done for the boot CPU in pmap.c.
	 */
	cpi->ci_next = NULL;
	cpi->ci_curproc = NULL;
	cpi->ci_cpuid = ncpus++;
	cpi->ci_upaid = portid;
	cpi->ci_fpproc = NULL;
#ifdef MULTIPROCESSOR
	cpi->ci_spinup = cpu_hatch;				/* XXX */
#else
	cpi->ci_spinup = NULL;
#endif

	cpi->ci_initstack = cpi;
	cpi->ci_paddr = pa0;
#ifdef SUN4V
	cpi->ci_mmfsa = pa0;
#endif
	cpi->ci_self = cpi;
	cpi->ci_node = ma->ma_node;

	clockqueue_init(&cpi->ci_queue);
	sched_init_cpu(cpi);

	/*
	 * Finally, add itself to the list of active cpus.
	 */
	for (ci = cpus; ci->ci_next != NULL; ci = ci->ci_next)
		;
	ci->ci_next = cpi;
	return (cpi);
}

int
cpu_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;
	char buf[32];
	int portid;

	if (OF_getprop(ma->ma_node, "device_type", buf, sizeof(buf)) <= 0 ||
	    strcmp(buf, "cpu") != 0)
		return (0);

	/*
	 * Make sure we don't match more than the maximum supported
	 * number of CPUs.  But do match the CPU we're running.
	 */
	portid = getpropint(ma->ma_node, "upa-portid", -1);
	if (portid == -1)
		portid = getpropint(ma->ma_node, "portid", -1);
	if (portid == -1)
		portid = getpropint(ma->ma_node, "cpuid", -1);
	if (portid == -1 && ma->ma_nreg > 0)
		portid = (ma->ma_reg[0].ur_paddr >> 32) & 0x0fffffff;
	if (portid == -1)
		return (0);

	if (ncpus < MAXCPUS || portid == cpu_myid())
		return (1);

	return (0);
}

/*
 * Attach the CPU.
 * Discover interesting goop about the virtual address cache
 * (slightly funny place to do it, but this is where it is to be found).
 */
void
cpu_attach(struct device *parent, struct device *dev, void *aux)
{
	int node;
	u_int clk;
	int impl, vers;
	struct mainbus_attach_args *ma = aux;
	struct cpu_info *ci;
	const char *sep;
	register int i, l;
	u_int64_t ver = 0;
	extern u_int64_t cpu_clockrate[];

	if (CPU_ISSUN4U || CPU_ISSUN4US)
		ver = getver();
	impl = IU_IMPL(ver);
	vers = IU_VERS(ver);

	/* tell them what we have */
	if (strcmp(parent->dv_cfdata->cf_driver->cd_name, "core") == 0)
		node = OF_parent(ma->ma_node);
	else
		node = ma->ma_node;

	/*
	 * Allocate cpu_info structure if needed.
	 */
	ci = alloc_cpuinfo(ma);
	ci->ci_node = ma->ma_node;

	clk = getpropint(node, "clock-frequency", 0);
	if (clk == 0) {
		/*
		 * Try to find it in the OpenPROM root...
		 */
		clk = getpropint(findroot(), "clock-frequency", 0);
	}
	if (clk) {
		cpu_clockrate[0] = clk; /* Tell OS what frequency we run on */
		cpu_clockrate[1] = clk/1000000;
	}
	snprintf(cpu_model, sizeof cpu_model, "%s (rev %d.%d) @ %s MHz",
	    ma->ma_name, vers >> 4, vers & 0xf, clockfreq(clk));
	printf(": %s\n", cpu_model);

	cpu_cpuspeed = sparc64_cpuspeed;

	if (ci->ci_upaid == cpu_myid())
		cpu_init(ci);

	l = getpropint(node, "icache-line-size", 0);
	if (l == 0)
		l = getpropint(node, "l1-icache-line-size", 0);
	cacheinfo.ic_linesize = l;
	for (i = 0; (1 << i) < l && l; i++)
		/* void */;
	if ((1 << i) != l && l)
		panic("bad icache line size %d", l);
	cacheinfo.ic_totalsize = getpropint(node, "icache-size", 0);
	if (cacheinfo.ic_totalsize == 0)
		cacheinfo.ic_totalsize = getpropint(node, "l1-icache-size", 0);
	if (cacheinfo.ic_totalsize == 0)
		cacheinfo.ic_totalsize = l *
		    getpropint(node, "icache-nlines", 64) *
		    getpropint(node, "icache-associativity", 1);

	l = getpropint(node, "dcache-line-size", 0);
	if (l == 0)
		l = getpropint(node, "l1-dcache-line-size", 0);
	cacheinfo.dc_linesize = l;
	for (i = 0; (1 << i) < l && l; i++)
		/* void */;
	if ((1 << i) != l && l)
		panic("bad dcache line size %d", l);
	cacheinfo.dc_totalsize = getpropint(node, "dcache-size", 0);
	if (cacheinfo.dc_totalsize == 0)
		cacheinfo.dc_totalsize = getpropint(node, "l1-dcache-size", 0);
	if (cacheinfo.dc_totalsize == 0)
		cacheinfo.dc_totalsize = l *
		    getpropint(node, "dcache-nlines", 128) *
		    getpropint(node, "dcache-associativity", 1);
	
	l = getpropint(node, "ecache-line-size", 0);
	if (l == 0)
		l = getpropint(node, "l2-cache-line-size", 0);
	cacheinfo.ec_linesize = l;
	for (i = 0; (1 << i) < l && l; i++)
		/* void */;
	if ((1 << i) != l && l)
		panic("bad ecache line size %d", l);
	cacheinfo.ec_totalsize = getpropint(node, "ecache-size", 0);
	if (cacheinfo.ec_totalsize == 0)
		cacheinfo.ec_totalsize = getpropint(node, "l2-cache-size", 0);
	if (cacheinfo.ec_totalsize == 0)
		cacheinfo.ec_totalsize = l *
		    getpropint(node, "ecache-nlines", 32768) *
		    getpropint(node, "ecache-associativity", 1);
	
	/*
	 * XXX - The following will have to do until
	 * we have per-cpu cache handling.
	 */
	if (cacheinfo.ic_totalsize + cacheinfo.dc_totalsize == 0)
		return;
	
	sep = " ";
	printf("%s: physical", dev->dv_xname);
	if (cacheinfo.ic_totalsize > 0) {
		printf("%s%ldK instruction (%ld b/l)", sep,
		       (long)cacheinfo.ic_totalsize/1024,
		       (long)cacheinfo.ic_linesize);
		sep = ", ";
	}
	if (cacheinfo.dc_totalsize > 0) {
		printf("%s%ldK data (%ld b/l)", sep,
		       (long)cacheinfo.dc_totalsize/1024,
		       (long)cacheinfo.dc_linesize);
		sep = ", ";
	}
	if (cacheinfo.ec_totalsize > 0) {
		printf("%s%ldK external (%ld b/l)", sep,
		       (long)cacheinfo.ec_totalsize/1024,
		       (long)cacheinfo.ec_linesize);
	}

#ifndef SMALL_KERNEL
	if (impl == IMPL_HUMMINGBIRD)
		hummingbird_init(ci);
#endif

	printf("\n");
}

int
cpu_myid(void)
{
	char buf[32];
	int impl;

#ifdef SUN4V
	if (CPU_ISSUN4V) {
		uint64_t myid;

		hv_cpu_myid(&myid);
		return myid;
	}
#endif

	if (OF_getprop(findroot(), "name", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "SUNW,Ultra-Enterprise-10000") == 0)
		return lduwa(0x1fff40000d0UL, ASI_PHYS_NON_CACHED);

	impl = (getver() & VER_IMPL) >> VER_IMPL_SHIFT;
	switch (impl) {
	case IMPL_OLYMPUS_C:
	case IMPL_JUPITER:
		return CPU_JUPITERID;
	case IMPL_CHEETAH:
	case IMPL_CHEETAH_PLUS:
	case IMPL_JAGUAR:
	case IMPL_PANTHER:
		return CPU_FIREPLANEID;
	default:
		return CPU_UPAID;
	}
}

void
cpu_init(struct cpu_info *ci)
{
#ifdef SUN4V
	paddr_t pa = ci->ci_paddr;
	int err;
#endif

	if (CPU_ISSUN4U || CPU_ISSUN4US)
		return;

#ifdef SUN4V
#define MONDO_QUEUE_SIZE	32
#define QUEUE_ENTRY_SIZE	64

	pa += CPUINFO_VA - INTSTACK;
	pa += PAGE_SIZE;

	ci->ci_cpumq = pa;
	err = hv_cpu_qconf(CPU_MONDO_QUEUE, ci->ci_cpumq, MONDO_QUEUE_SIZE);
	if (err != H_EOK)
		panic("Unable to set cpu mondo queue: %d", err);
	pa += MONDO_QUEUE_SIZE * QUEUE_ENTRY_SIZE;

	ci->ci_devmq = pa;
	err = hv_cpu_qconf(DEVICE_MONDO_QUEUE, ci->ci_devmq, MONDO_QUEUE_SIZE);
	if (err != H_EOK)
		panic("Unable to set device mondo queue: %d", err);
	pa += MONDO_QUEUE_SIZE * QUEUE_ENTRY_SIZE;

	ci->ci_mondo = pa;
	pa += 64;

	ci->ci_cpuset = pa;
	pa += 64;
#endif
}

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

int
sparc64_cpuspeed(int *freq)
{
	extern u_int64_t cpu_clockrate[];

	*freq = cpu_clockrate[1];
	return (0);
}

#ifndef SMALL_KERNEL

/*
 * Hummingbird (UltraSPARC-IIe) has a clock control unit that enables
 * Energy Star mode.  This only works in combination with unbuffered
 * DIMMs so it is not supported on all machines with UltraSPARC-IIe
 * CPUs.
 */

/* Memory_Control_0 (MC0) register. */
#define HB_MC0			0x1fe0000f010ULL
#define  HB_MC0_SELF_REFRESH		0x00010000
#define  HB_MC0_REFRESH_COUNT_MASK	0x00007f00
#define  HB_MC0_REFRESH_COUNT_SHIFT	8
#define  HB_MC0_REFRESH_COUNT(reg) \
  (((reg) & HB_MC0_REFRESH_COUNT_MASK) >> HB_MC0_REFRESH_COUNT_SHIFT)
#define  HB_MC0_REFRESH_CLOCKS_PER_COUNT	64ULL
#define  HB_MC0_REFRESH_INTERVAL	7800ULL

/* Energy Star register. */
#define HB_ESTAR		0x1fe0000f080ULL
#define  HB_ESTAR_MODE_MASK		0x00000007
#define  HB_ESTAR_MODE_DIV_1		0x00000000
#define  HB_ESTAR_MODE_DIV_2		0x00000001
#define  HB_ESTAR_MODE_DIV_4		0x00000003
#define  HB_ESTAR_MODE_DIV_6		0x00000002
#define  HB_ESTAR_MODE_DIV_8		0x00000004
#define  HB_ESTAR_NUM_MODES		5

int hummingbird_divisors[HB_ESTAR_NUM_MODES];

int
hummingbird_div(uint64_t estar_mode)
{
	switch(estar_mode) {
	case HB_ESTAR_MODE_DIV_1:
		return 1;
	case HB_ESTAR_MODE_DIV_2:
		return 2;
	case HB_ESTAR_MODE_DIV_4:
		return 4;
	case HB_ESTAR_MODE_DIV_6:
		return 6;
	case HB_ESTAR_MODE_DIV_8:
		return 8;
	default:
		panic("bad E-Star mode");
	}
}

uint64_t
hummingbird_estar_mode(int div)
{
	switch(div) {
	case 1:
		return HB_ESTAR_MODE_DIV_1;
	case 2:
		return HB_ESTAR_MODE_DIV_2;
	case 4:
		return HB_ESTAR_MODE_DIV_4;
	case 6:
		return HB_ESTAR_MODE_DIV_6;
	case 8:
		return HB_ESTAR_MODE_DIV_8;
	default:
		panic("bad clock divisor");
	}
}

void
hummingbird_enable_self_refresh(void)
{
	uint64_t reg;

	reg = ldxa(HB_MC0, ASI_PHYS_NON_CACHED);
	reg |= HB_MC0_SELF_REFRESH;
	stxa(HB_MC0, ASI_PHYS_NON_CACHED, reg);
	reg = ldxa(HB_MC0, ASI_PHYS_NON_CACHED);
}

void
hummingbird_disable_self_refresh(void)
{
	uint64_t reg;

	reg = ldxa(HB_MC0, ASI_PHYS_NON_CACHED);
	reg &= ~HB_MC0_SELF_REFRESH;
	stxa(HB_MC0, ASI_PHYS_NON_CACHED, reg);
	reg = ldxa(HB_MC0, ASI_PHYS_NON_CACHED);
}

void
hummingbird_set_refresh_count(int div, int new_div)
{
	extern u_int64_t cpu_clockrate[];
	uint64_t count, new_count;
	uint64_t delta;
	uint64_t reg;

	reg = ldxa(HB_MC0, ASI_PHYS_NON_CACHED);
	count = HB_MC0_REFRESH_COUNT(reg);
	new_count = (HB_MC0_REFRESH_INTERVAL * cpu_clockrate[0]) /
		(HB_MC0_REFRESH_CLOCKS_PER_COUNT * new_div * 1000000000);
	reg &= ~HB_MC0_REFRESH_COUNT_MASK;
	reg |= (new_count << HB_MC0_REFRESH_COUNT_SHIFT);
	stxa(HB_MC0, ASI_PHYS_NON_CACHED, reg);
	reg = ldxa(HB_MC0, ASI_PHYS_NON_CACHED);

	if (new_div > div && (reg & HB_MC0_SELF_REFRESH) == 0) {
		delta = HB_MC0_REFRESH_CLOCKS_PER_COUNT * 
		    ((count + new_count) * 1000000UL * div) / cpu_clockrate[0];
		delay(delta + 1);
	}
}

void
hummingbird_setperf(int level)
{
	extern u_int64_t cpu_clockrate[];
	uint64_t estar_mode, new_estar_mode;
	uint64_t reg, s;
	int div, new_div, i;

	new_estar_mode = HB_ESTAR_MODE_DIV_1;
	for (i = 0; i < HB_ESTAR_NUM_MODES && hummingbird_divisors[i]; i++) {
		if (level <= 100 / hummingbird_divisors[i])
			new_estar_mode =
			    hummingbird_estar_mode(hummingbird_divisors[i]);
	}

	reg = ldxa(HB_ESTAR, ASI_PHYS_NON_CACHED);
	estar_mode = reg & HB_ESTAR_MODE_MASK;
	if (estar_mode == new_estar_mode)
		return;

	reg &= ~HB_ESTAR_MODE_MASK;
	div = hummingbird_div(estar_mode);
	new_div = hummingbird_div(new_estar_mode);

	s = intr_disable();
	if (estar_mode == HB_ESTAR_MODE_DIV_1 &&
	    new_estar_mode == HB_ESTAR_MODE_DIV_2) {
		hummingbird_set_refresh_count(1, 2);
		stxa(HB_ESTAR, ASI_PHYS_NON_CACHED, reg | HB_ESTAR_MODE_DIV_2);
		delay(1);
		hummingbird_enable_self_refresh();
	} else if (estar_mode == HB_ESTAR_MODE_DIV_2 &&
	    new_estar_mode == HB_ESTAR_MODE_DIV_1) {
		hummingbird_disable_self_refresh();
		stxa(HB_ESTAR, ASI_PHYS_NON_CACHED, reg | HB_ESTAR_MODE_DIV_1);
		delay(1);
		hummingbird_set_refresh_count(2, 1);
	} else if (estar_mode == HB_ESTAR_MODE_DIV_1) {
		/* 
		 * Transition to 1/2 speed first, then to
		 * lower speed.
		 */
		hummingbird_set_refresh_count(1, 2);
		stxa(HB_ESTAR, ASI_PHYS_NON_CACHED, reg | HB_ESTAR_MODE_DIV_2);
		delay(1);
		hummingbird_enable_self_refresh();

		hummingbird_set_refresh_count(2, new_div);
		stxa(HB_ESTAR, ASI_PHYS_NON_CACHED, reg | new_estar_mode);
		delay(1);
	} else if (new_estar_mode == HB_ESTAR_MODE_DIV_1) {
		/* 
		 * Transition to 1/2 speed first, then to
		 * full speed.
		 */
		stxa(HB_ESTAR, ASI_PHYS_NON_CACHED, reg | HB_ESTAR_MODE_DIV_2);
		delay(1);
		hummingbird_set_refresh_count(div, 2);

		hummingbird_disable_self_refresh();
		stxa(HB_ESTAR, ASI_PHYS_NON_CACHED, reg | HB_ESTAR_MODE_DIV_1);
		delay(1);
		hummingbird_set_refresh_count(2, 1);
	} else if (div < new_div) {
		hummingbird_set_refresh_count(div, new_div);
		stxa(HB_ESTAR, ASI_PHYS_NON_CACHED, reg | new_estar_mode);
		delay(1);
	} else if (div > new_div) {
		stxa(HB_ESTAR, ASI_PHYS_NON_CACHED, reg | new_estar_mode);
		delay(1);
		hummingbird_set_refresh_count(div, new_div);
	}
	cpu_clockrate[1] = cpu_clockrate[0] / (new_div * 1000000);
	intr_restore(s);
}

void
hummingbird_init(struct cpu_info *ci)
{
	/*
	 * The "clock-divisors" property seems to indicate which
	 * frequency scalings are supported on a particular model.
	 */
	if (OF_getprop(ci->ci_node, "clock-divisors",
	    &hummingbird_divisors, sizeof(hummingbird_divisors)) <= 0)
		return;

	cpu_setperf = hummingbird_setperf;
}
#endif

#ifdef MULTIPROCESSOR
void cpu_mp_startup(void);

void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	int cpuid, i;
	char buf[32];

	if (OF_getprop(findroot(), "name", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "SUNW,Ultra-Enterprise-10000") == 0) {
		for (ci = cpus; ci != NULL; ci = ci->ci_next)
			ci->ci_itid = STARFIRE_UPAID2HWMID(ci->ci_upaid);
	} else {
		for (ci = cpus; ci != NULL; ci = ci->ci_next)
			ci->ci_itid = ci->ci_upaid;
	}

	for (ci = cpus; ci != NULL; ci = ci->ci_next) {
		if (ci->ci_upaid == cpu_myid())
			continue;
		ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;

		if (CPU_ISSUN4V)
			cpuid = ci->ci_upaid;
		else
			cpuid = getpropint(ci->ci_node, "cpuid", -1);

		if (OF_test("SUNW,start-cpu-by-cpuid") == 0) {
			prom_start_cpu_by_cpuid(cpuid,
			    (void *)cpu_mp_startup, ci->ci_paddr);
		} else {
			prom_start_cpu(ci->ci_node,
			    (void *)cpu_mp_startup, ci->ci_paddr);
		}

		for (i = 0; i < 2000; i++) {
			membar_sync();
			if (ci->ci_flags & CPUF_RUNNING)
				break;
			delay(10000);
		}
	}
}

void
cpu_hatch(void)
{
	struct cpu_info *ci = curcpu();

	cpu_init(ci);

	ci->ci_flags |= CPUF_RUNNING;
	membar_sync();

	cpu_start_clock();

	sched_toidle();
}
#endif

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;

	/* There's a risk we'll be called before the idle threads start */
	if (ci->ci_curproc) {
		aston(ci->ci_curproc);
		if (ci != curcpu())
			cpu_unidle(ci);
	}
}

/*
 * Idle loop.
 *
 * We disable and reenable the interrupts in every cycle of the idle loop.
 * Since hv_cpu_yield doesn't actually reenable interrupts, it just wakes
 * up if an interrupt would have happened, but it's our responsibility to
 * unblock interrupts.
 */

void
cpu_idle_enter(void)
{
	if (CPU_ISSUN4V) {
		sparc_wrpr(pstate, sparc_rdpr(pstate) & ~PSTATE_IE, 0);
	}
}

void
cpu_idle_cycle(void)
{
#ifdef SUN4V
	if (CPU_ISSUN4V) {
		hv_cpu_yield();
		sparc_wrpr(pstate, sparc_rdpr(pstate) | PSTATE_IE, 0);
		sparc_wrpr(pstate, sparc_rdpr(pstate) & ~PSTATE_IE, 0);
	}
#endif

	/*
	 * On processors with multiple threads we simply force a
	 * thread switch.  Using the sleep instruction seems to work
	 * just as well as using the suspend instruction and makes the
	 * code a little bit less complicated.
	 */
	__asm volatile(
		"999:	nop					\n"
		"	.section .sun4u_mtp_patch, \"ax\"	\n"
		"	.word	999b				\n"
		"	.word	0x81b01060	! sleep		\n"
		"	.previous				\n"
		: : : "memory");
}

void
cpu_idle_leave(void)
{
	if (CPU_ISSUN4V) {
		sparc_wrpr(pstate, sparc_rdpr(pstate) | PSTATE_IE, 0);
	}
}

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/rtprio.h>
#include <sys/systm.h>
#include <sys/interrupt.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/random.h>

#include <sys/cons.h>		/* cinit() */
#include <sys/kdb.h>
#include <sys/boot.h>
#include <sys/reboot.h>
#include <sys/queue.h>
#include <sys/smp.h>
#include <sys/timetc.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/cpuinfo.h>
#include <machine/tlb.h>
#include <machine/cpuregs.h>
#include <machine/frame.h>
#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/asm.h>
#include <machine/pmap.h>
#include <machine/trap.h>
#include <machine/clock.h>
#include <machine/fls64.h>
#include <machine/intr_machdep.h>
#include <machine/smp.h>

#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/sys.h>
#include <mips/nlm/hal/pic.h>
#include <mips/nlm/hal/uart.h>
#include <mips/nlm/hal/mmu.h>
#include <mips/nlm/hal/bridge.h>
#include <mips/nlm/hal/cpucontrol.h>
#include <mips/nlm/hal/cop2.h>

#include <mips/nlm/clock.h>
#include <mips/nlm/interrupt.h>
#include <mips/nlm/board.h>
#include <mips/nlm/xlp.h>
#include <mips/nlm/msgring.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#endif

/* 4KB static data aread to keep a copy of the bootload env until
   the dynamic kenv is setup */
char boot1_env[4096];

uint64_t xlp_cpu_frequency;
uint64_t xlp_io_base = MIPS_PHYS_TO_DIRECT_UNCACHED(XLP_DEFAULT_IO_BASE);

int xlp_ncores;
int xlp_threads_per_core;
uint32_t xlp_hw_thread_mask;
int xlp_cpuid_to_hwtid[MAXCPU];
int xlp_hwtid_to_cpuid[MAXCPU];
uint64_t xlp_pic_base;

static int xlp_mmuval;

extern uint32_t _end;
extern char XLPResetEntry[], XLPResetEntryEnd[];

static void
xlp_setup_core(void)
{
	uint64_t reg;

	reg = nlm_mfcr(LSU_DEFEATURE);
	/* Enable Unaligned and L2HPE */
	reg |= (1 << 30) | (1 << 23);
	/*
	 * Experimental : Enable SUE
	 * Speculative Unmap Enable. Enable speculative L2 cache request for
	 * unmapped access.
	 */
	reg |= (1ull << 31);
	/* Clear S1RCM  - A0 errata */
	reg &= ~0xeull;
	nlm_mtcr(LSU_DEFEATURE, reg);

	reg = nlm_mfcr(SCHED_DEFEATURE);
	/* Experimental: Disable BRU accepting ALU ops - A0 errata */
	reg |= (1 << 24);
	nlm_mtcr(SCHED_DEFEATURE, reg);
}

static void
xlp_setup_mmu(void)
{
	uint32_t pagegrain;

	if (nlm_threadid() == 0) {
		nlm_setup_extended_pagemask(0);
		nlm_large_variable_tlb_en(1);
		nlm_extended_tlb_en(1);
		nlm_mmu_setup(0, 0, 0);
	}

	/* Enable no-read, no-exec, large-physical-address */
	pagegrain = mips_rd_pagegrain();
	pagegrain |= (1U << 31)	|	/* RIE */
	    (1 << 30)		|	/* XIE */
	    (1 << 29);			/* ELPA */
	mips_wr_pagegrain(pagegrain);
}

static void
xlp_enable_blocks(void)
{
	uint64_t sysbase;
	int i;

	for (i = 0; i < XLP_MAX_NODES; i++) {
		if (!nlm_dev_exists(XLP_IO_SYS_OFFSET(i)))
			continue;
		sysbase = nlm_get_sys_regbase(i);
		nlm_sys_enable_block(sysbase, DFS_DEVICE_RSA);
	}
}

static void
xlp_parse_mmu_options(void)
{
	uint64_t sysbase;
	uint32_t cpu_map = xlp_hw_thread_mask;
	uint32_t core0_thr_mask, core_thr_mask, cpu_rst_mask;
	int i, j, k;

#ifdef SMP
	if (cpu_map == 0)
		cpu_map = 0xffffffff;
#else /* Uniprocessor! */
	if (cpu_map == 0)
		cpu_map = 0x1;
	else if (cpu_map != 0x1) {
		printf("WARNING: Starting uniprocessor kernel on cpumask [0x%lx]!\n"
		    "WARNING: Other CPUs will be unused.\n", (u_long)cpu_map);
		cpu_map = 0x1;
	}
#endif

	xlp_ncores = 1;
	core0_thr_mask = cpu_map & 0xf;
	switch (core0_thr_mask) {
	case 1:
		xlp_threads_per_core = 1;
		xlp_mmuval = 0;
		break;
	case 3:
		xlp_threads_per_core = 2;
		xlp_mmuval = 2;
		break;
	case 0xf:
		xlp_threads_per_core = 4;
		xlp_mmuval = 3;
		break;
	default:
		goto unsupp;
	}

	/* Try to find the enabled cores from SYS block */
	sysbase = nlm_get_sys_regbase(0);
	cpu_rst_mask = nlm_read_sys_reg(sysbase, SYS_CPU_RESET) & 0xff;

	/* XLP 416 does not report this correctly, fix */
	if (nlm_processor_id() == CHIP_PROCESSOR_ID_XLP_416)
		cpu_rst_mask = 0xe;

	/* Take out cores which do not exist on chip */
	for (i = 1; i < XLP_MAX_CORES; i++) {
		if ((cpu_rst_mask & (1 << i)) == 0)
			cpu_map &= ~(0xfu << (4 * i));
	}

	/* Verify other cores' CPU masks */
	for (i = 1; i < XLP_MAX_CORES; i++) {
		core_thr_mask = (cpu_map >> (4 * i)) & 0xf;
		if (core_thr_mask == 0)
			continue;
		if (core_thr_mask != core0_thr_mask)
			goto unsupp;
		xlp_ncores++;
	}

	xlp_hw_thread_mask = cpu_map;
	/* setup hardware processor id to cpu id mapping */
	for (i = 0; i< MAXCPU; i++)
		xlp_cpuid_to_hwtid[i] =
		    xlp_hwtid_to_cpuid[i] = -1;
	for (i = 0, k = 0; i < XLP_MAX_CORES; i++) {
		if (((cpu_map >> (i * 4)) & 0xf) == 0)
			continue;
		for (j = 0; j < xlp_threads_per_core; j++) {
			xlp_cpuid_to_hwtid[k] = i * 4 + j;
			xlp_hwtid_to_cpuid[i * 4 + j] = k;
			k++;
		}
	}

	return;

unsupp:
	printf("ERROR : Unsupported CPU mask [use 1,2 or 4 threads per core].\n"
	    "\tcore0 thread mask [%lx], boot cpu mask [%lx].\n",
	    (u_long)core0_thr_mask, (u_long)cpu_map);
	panic("Invalid CPU mask - halting.\n");
	return;
}

#ifdef FDT
static void
xlp_bootargs_init(__register_t arg)
{
	char	buf[2048]; /* early stack is big enough */
	void	*dtbp;
	phandle_t chosen;
	ihandle_t mask;

	dtbp = (void *)(intptr_t)arg;
#if defined(FDT_DTB_STATIC)
	/*
	 * In case the device tree blob was not passed as argument try
	 * to use the statically embedded one.
	 */
	if (dtbp == NULL)
		dtbp = &fdt_static_dtb;
#endif
	if (OF_install(OFW_FDT, 0) == FALSE)
		while (1);
	if (OF_init((void *)dtbp) != 0)
		while (1);
	OF_interpret("perform-fixup", 0);

	chosen = OF_finddevice("/chosen");
	if (OF_getprop(chosen, "cpumask", &mask, sizeof(mask)) != -1) {
		xlp_hw_thread_mask = mask;
	}

	if (OF_getprop(chosen, "bootargs", buf, sizeof(buf)) != -1)
		boothowto |= boot_parse_cmdline(buf);
}
#else
/*
 * arg is a pointer to the environment block, the format of the block is
 * a=xyz\0b=pqr\0\0
 */
static void
xlp_bootargs_init(__register_t arg)
{
	char	buf[2048]; /* early stack is big enough */
	char	*p, *v, *n;
	uint32_t mask;

	/*
	 * provide backward compat for passing cpu mask as arg
	 */
	if (arg & 1) {
		xlp_hw_thread_mask = arg;
		return;
	}

	p = (void *)(intptr_t)arg;
	while (*p != '\0') {
		strlcpy(buf, p, sizeof(buf));
		v = buf;
		n = strsep(&v, "=");
		if (v == NULL)
			kern_setenv(n, "1");
		else
			kern_setenv(n, v);
		p += strlen(p) + 1;
	}

	/* CPU mask can be passed thru env */
	if (getenv_uint("cpumask", &mask) != 0)
		xlp_hw_thread_mask = mask;

	/* command line argument */
	v = kern_getenv("bootargs");
	if (v != NULL) {
		strlcpy(buf, v, sizeof(buf));
		boothowto |= boot_parse_cmdline(buf);
		freeenv(v);
	}
}
#endif

static void
mips_init(void)
{
	init_param1();
	init_param2(physmem);

	mips_cpu_init();
	cpuinfo.cache_coherent_dma = TRUE;
	pmap_bootstrap();
	mips_proc0_init();
	mutex_init();
#ifdef DDB
	kdb_init();
	if (boothowto & RB_KDB) {
		kdb_enter("Boot flags requested debugger", NULL);
	}
#endif
}

unsigned int
platform_get_timecount(struct timecounter *tc __unused)
{
	uint64_t count = nlm_pic_read_timer(xlp_pic_base, PIC_CLOCK_TIMER);

	return (unsigned int)~count;
}

static void
xlp_pic_init(void)
{
	struct timecounter pic_timecounter = {
		platform_get_timecount, /* get_timecount */
		0,                      /* no poll_pps */
		~0U,                    /* counter_mask */
		XLP_IO_CLK,            /* frequency */
		"XLRPIC",               /* name */
		2000,                   /* quality (adjusted in code) */
	};
        int i;
	int maxirt;

	xlp_pic_base = nlm_get_pic_regbase(0);  /* TOOD: Add other nodes */
	maxirt = nlm_read_reg(nlm_get_pic_pcibase(nlm_nodeid()),
	    XLP_PCI_DEVINFO_REG0);
        printf("Initializing PIC...@%jx %d IRTs\n", (uintmax_t)xlp_pic_base,
	    maxirt);
	/* Bind all PIC irqs to cpu 0 */
        for (i = 0; i < maxirt; i++)
	    nlm_pic_write_irt(xlp_pic_base, i, 0, 0, 1, 0,
	    1, 0, 0x1);

	nlm_pic_set_timer(xlp_pic_base, PIC_CLOCK_TIMER, ~0ULL, 0, 0);
	platform_timecounter = &pic_timecounter;
}

#if defined(__mips_n32) || defined(__mips_n64) /* PHYSADDR_64_BIT */
#ifdef XLP_SIM
#define	XLP_MEM_LIM	0x200000000ULL
#else
#define	XLP_MEM_LIM	0x10000000000ULL
#endif
#else
#define	XLP_MEM_LIM	0xfffff000UL
#endif
static vm_paddr_t xlp_mem_excl[] = {
	0,          0,		/* for kernel image region, see xlp_mem_init */
	0x0c000000, 0x14000000,	/* uboot area, cms queue and other stuff */
	0x1fc00000, 0x1fd00000,	/* reset vec */
	0x1e000000, 0x1e200000,	/* poe buffers */
};

static int
mem_exclude_add(vm_paddr_t *avail, vm_paddr_t mstart, vm_paddr_t mend)
{
	int i, pos;

	pos = 0;
	for (i = 0; i < nitems(xlp_mem_excl); i += 2) {
		if (mstart > xlp_mem_excl[i + 1])
			continue;
		if (mstart < xlp_mem_excl[i]) {
			avail[pos++] = mstart;
			if (mend < xlp_mem_excl[i])
				avail[pos++] = mend;
			else
				avail[pos++] = xlp_mem_excl[i];
		}
		mstart = xlp_mem_excl[i + 1];
		if (mend <= mstart)
			break;
	}
	if (mstart < mend) {
		avail[pos++] = mstart;
		avail[pos++] = mend;
	}
	return (pos);
}

static void
xlp_mem_init(void)
{
	vm_paddr_t physsz, tmp;
	uint64_t bridgebase, base, lim, val;
	int i, j, k, n;

	/* update kernel image area in exclude regions */
	tmp = (vm_paddr_t)MIPS_KSEG0_TO_PHYS(&_end);
	tmp = round_page(tmp) + 0x20000; /* round up */
	xlp_mem_excl[1] = tmp;

	printf("Memory (from DRAM BARs):\n");
	bridgebase = nlm_get_bridge_regbase(0); /* TODO: Add other nodes */
	physsz = 0;
        for (i = 0, j = 0; i < 8; i++) {
		val = nlm_read_bridge_reg(bridgebase, BRIDGE_DRAM_BAR(i));
                val = (val >>  12) & 0xfffff;
		base = val << 20;
		val = nlm_read_bridge_reg(bridgebase, BRIDGE_DRAM_LIMIT(i));
                val = (val >>  12) & 0xfffff;
		if (val == 0)	/* BAR not enabled */
			continue;
                lim = (val + 1) << 20;
		printf("  BAR %d: %#jx - %#jx : ", i, (intmax_t)base,
		    (intmax_t)lim);

		if (lim <= base) {
			printf("\tskipped - malformed %#jx -> %#jx\n",
			    (intmax_t)base, (intmax_t)lim);
			continue;
		} else if (base >= XLP_MEM_LIM) {
			printf(" skipped - outside usable limit %#jx.\n",
			    (intmax_t)XLP_MEM_LIM);
			continue;
		} else if (lim >= XLP_MEM_LIM) {
			lim = XLP_MEM_LIM;
			printf(" truncated to %#jx.\n", (intmax_t)XLP_MEM_LIM);
		} else
			printf(" usable\n");

		/* exclude unusable regions from BAR and add rest */
		n = mem_exclude_add(&phys_avail[j], base, lim);
		for (k = j; k < j + n; k += 2) {
			physsz += phys_avail[k + 1] - phys_avail[k];
			printf("\tMem[%d]: %#jx - %#jx\n", k/2,
			    (intmax_t)phys_avail[k], (intmax_t)phys_avail[k+1]);
		}
		j = k;
        }

	/* setup final entry with 0 */
	phys_avail[j] = phys_avail[j + 1] = 0;

	/* copy phys_avail to dump_avail */
	for (i = 0; i <= j + 1; i++)
		dump_avail[i] = phys_avail[i];

	realmem = physmem = btoc(physsz);
}

void
platform_start(__register_t a0 __unused,
    __register_t a1 __unused,
    __register_t a2 __unused,
    __register_t a3 __unused)
{

	/* Initialize pcpu stuff */
	mips_pcpu0_init();

	/* initialize console so that we have printf */
	boothowto |= (RB_SERIAL | RB_MULTIPLE);	/* Use multiple consoles */

	init_static_kenv(boot1_env, sizeof(boot1_env));
	xlp_bootargs_init(a0);

	/* clockrate used by delay, so initialize it here */
	xlp_cpu_frequency = xlp_get_cpu_frequency(0, 0);
	cpu_clock = xlp_cpu_frequency / 1000000;
	mips_timer_early_init(xlp_cpu_frequency);

	/* Init console please */
	cninit();

	/* Early core init and fixes for errata */
	xlp_setup_core();

	xlp_parse_mmu_options();
	xlp_mem_init();

	bcopy(XLPResetEntry, (void *)MIPS_RESET_EXC_VEC,
              XLPResetEntryEnd - XLPResetEntry);
#ifdef SMP
	/*
	 * We will enable the other threads in core 0 here
	 * so that the TLB and cache info is correct when
	 * mips_init runs
	 */
	xlp_enable_threads(xlp_mmuval);
#endif
	/* setup for the startup core */
	xlp_setup_mmu();

	xlp_enable_blocks();

	/* Read/Guess/setup board information */
	nlm_board_info_setup();

	/* MIPS generic init */
	mips_init();

	/*
	 * XLP specific post initialization
	 * initialize other on chip stuff
	 */
	xlp_pic_init();

	mips_timer_init_params(xlp_cpu_frequency, 0);
}

void
platform_cpu_init()
{
}

void
platform_reset(void)
{
	uint64_t sysbase = nlm_get_sys_regbase(0);

	nlm_write_sys_reg(sysbase, SYS_CHIP_RESET, 1);
	for( ; ; )
		__asm __volatile("wait");
}

#ifdef SMP
/*
 * XLP threads are started simultaneously when we enable threads, this will
 * ensure that the threads are blocked in platform_init_ap, until they are
 * ready to proceed to smp_init_secondary()
 */
static volatile int thr_unblock[4];

int
platform_start_ap(int cpuid)
{
	uint32_t coremask, val;
	uint64_t sysbase = nlm_get_sys_regbase(0);
	int hwtid = xlp_cpuid_to_hwtid[cpuid];
	int core, thr;

	core = hwtid / 4;
	thr = hwtid % 4;
	if (thr == 0) {
		/* First thread in core, do core wake up */
		coremask = 1u << core;

		/* Enable core clock */
		val = nlm_read_sys_reg(sysbase, SYS_CORE_DFS_DIS_CTRL);
		val &= ~coremask;
		nlm_write_sys_reg(sysbase, SYS_CORE_DFS_DIS_CTRL, val);

		/* Remove CPU Reset */
		val = nlm_read_sys_reg(sysbase, SYS_CPU_RESET);
		val &=  ~coremask & 0xff;
		nlm_write_sys_reg(sysbase, SYS_CPU_RESET, val);

		if (bootverbose)
			printf("Waking up core %d ...", core);

		/* Poll for CPU to mark itself coherent */
		do {
			val = nlm_read_sys_reg(sysbase, SYS_CPU_NONCOHERENT_MODE);
		} while ((val & coremask) != 0);
		if (bootverbose)
			printf("Done\n");
        } else {
		/* otherwise release the threads stuck in platform_init_ap */
		thr_unblock[thr] = 1;
	}

	return (0);
}

void
platform_init_ap(int cpuid)
{
	uint32_t stat;
	int thr;

	/* The first thread has to setup the MMU and enable other threads */
	thr = nlm_threadid();
	if (thr == 0) {
		xlp_setup_core();
		xlp_enable_threads(xlp_mmuval);
	} else {
		/*
		 * FIXME busy wait here eats too many cycles, especially
		 * in the core 0 while bootup
		 */
		while (thr_unblock[thr] == 0)
			__asm__ __volatile__ ("nop;nop;nop;nop");
		thr_unblock[thr] = 0;
	}

	xlp_setup_mmu();
	stat = mips_rd_status();
	KASSERT((stat & MIPS_SR_INT_IE) == 0,
	    ("Interrupts enabled in %s!", __func__));
	stat |= MIPS_SR_COP_2_BIT | MIPS_SR_COP_0_BIT;
	mips_wr_status(stat);

	nlm_write_c0_eimr(0ull);
	xlp_enable_irq(IRQ_IPI);
	xlp_enable_irq(IRQ_TIMER);
	xlp_enable_irq(IRQ_MSGRING);

	return;
}

int
platform_ipi_hardintr_num(void)
{

	return (IRQ_IPI);
}

int
platform_ipi_softintr_num(void)
{

	return (-1);
}

void
platform_ipi_send(int cpuid)
{

	nlm_pic_send_ipi(xlp_pic_base, xlp_cpuid_to_hwtid[cpuid],
	    platform_ipi_hardintr_num(), 0);
}

void
platform_ipi_clear(void)
{
}

int
platform_processor_id(void)
{

	return (xlp_hwtid_to_cpuid[nlm_cpuid()]);
}

void
platform_cpu_mask(cpuset_t *mask)
{
	int i, s;

	CPU_ZERO(mask);
	s = xlp_ncores * xlp_threads_per_core;
	for (i = 0; i < s; i++)
		CPU_SET(i, mask);
}

struct cpu_group *
platform_smp_topo()
{

	return (smp_topo_2level(CG_SHARE_L2, xlp_ncores, CG_SHARE_L1,
		xlp_threads_per_core, CG_FLAG_THREAD));
}
#endif

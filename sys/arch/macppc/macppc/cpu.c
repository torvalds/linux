/*	$OpenBSD: cpu.c,v 1.90 2024/10/24 17:37:06 gkoehler Exp $ */

/*
 * Copyright (c) 1997 Per Fogelstrom
 * Copyright (c) 1997 RTMX Inc
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed under OpenBSD for RTMX Inc
 *	North Carolina, USA, by Per Fogelstrom, Opsycon AB, Sweden.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/task.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <dev/ofw/openfirm.h>

#include <machine/autoconf.h>
#include <powerpc/bat.h>
#include <machine/cpu.h>
#include <machine/trap.h>
#include <machine/elf.h>
#include <powerpc/hid.h>

/* SCOM addresses (24-bit) */
#define SCOM_PCR	0x0aa001 /* Power Control Register */
#define SCOM_PSR	0x408001 /* Power Tuning Status Register */

/* SCOMC format */
#define SCOMC_ADDR_SHIFT	8
#define SCOMC_ADDR_MASK		0xffff0000
#define SCOMC_READ		0x00008000

/* Power (Tuning) Status Register */
#define PSR_CMD_RECEIVED	0x2000000000000000LL
#define PSR_CMD_COMPLETED	0x1000000000000000LL
#define PSR_FREQ_MASK		0x0300000000000000LL
#define PSR_FREQ_HALF		0x0100000000000000LL

struct cpu_info cpu_info[PPC_MAXPROCS];

char cpu_model[80];
char machine[] = MACHINE;	/* cpu architecture */

/* Definition of the driver for autoconfig. */
int	cpumatch(struct device *, void *, void *);
void	cpuattach(struct device *, struct device *, void *);

const struct cfattach cpu_ca = {
	sizeof(struct device), cpumatch, cpuattach
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

void ppc64_scale_frequency(u_int);
void (*ppc64_slew_voltage)(u_int);
void ppc64_setperf(int);

void config_l2cr(int);

int
cpumatch(struct device *parent, void *cfdata, void *aux)
{
	struct confargs *ca = aux;
	int *reg = ca->ca_reg;

	/* make sure that we're looking for a CPU. */
	if (strcmp(ca->ca_name, cpu_cd.cd_name) != 0)
		return (0);

	if (reg[0] >= PPC_MAXPROCS)
		return (0);

	return (1);
}

u_int32_t ppc_curfreq;
u_int32_t ppc_maxfreq;

int
ppc_cpuspeed(int *freq)
{
	*freq = ppc_curfreq;

	return (0);
}

static u_int32_t ppc_power_mode_data[2];

void
ppc64_scale_frequency(u_int freq_scale)
{
	u_int64_t psr;
	int s;

	s = ppc_intr_disable();

	/* Clear PCRH and PCR. */
	ppc_mtscomd(0x00000000);
	ppc_mtscomc(SCOM_PCR << SCOMC_ADDR_SHIFT);
	ppc_mtscomd(0x80000000);
	ppc_mtscomc(SCOM_PCR << SCOMC_ADDR_SHIFT);

	/* Set PCR. */
	ppc_mtscomd(ppc_power_mode_data[freq_scale] | 0x80000000);
	ppc_mtscomc(SCOM_PCR << SCOMC_ADDR_SHIFT);

	/* Wait until frequency change is completed. */
	do {
		ppc64_mtscomc((SCOM_PSR << SCOMC_ADDR_SHIFT) | SCOMC_READ);
		psr = ppc64_mfscomd();
		ppc64_mfscomc();
		if (psr & PSR_CMD_COMPLETED)
			break;
		DELAY(100);
	} while (psr & PSR_CMD_RECEIVED);

	if ((psr & PSR_FREQ_MASK) == PSR_FREQ_HALF)
		ppc_curfreq = ppc_maxfreq / 2;
	else
		ppc_curfreq = ppc_maxfreq;

	ppc_intr_enable(s);
}

extern int perflevel;

struct task ppc64_setperf_task;
int ppc64_perflevel;

void
ppc64_do_setperf(void *arg)
{
	if (ppc64_perflevel <= 50) {
		if (ppc_curfreq == ppc_maxfreq / 2)
			return;

		ppc64_scale_frequency(FREQ_HALF);
		if (ppc64_slew_voltage)
			ppc64_slew_voltage(FREQ_HALF);
	} else {
		if (ppc_curfreq == ppc_maxfreq)
			return;

		if (ppc64_slew_voltage)
			ppc64_slew_voltage(FREQ_FULL);
		ppc64_scale_frequency(FREQ_FULL);
	}
}

void
ppc64_setperf(int level)
{
	ppc64_perflevel = level;
	task_add(systq, &ppc64_setperf_task);
}

void
cpuattach(struct device *parent, struct device *dev, void *aux)
{
	struct confargs *ca = aux;
	int *reg = ca->ca_reg;
	u_int32_t cpu, pvr, hid0;
	char name[32];
	int qhandle, phandle, len;
	u_int32_t clock_freq = 0, timebase = 0;
	struct cpu_info *ci;

	ci = &cpu_info[reg[0]];
	ci->ci_cpuid = reg[0];
	ci->ci_dev = dev;

	hwcap = PPC_FEATURE_32 | PPC_FEATURE_HAS_FPU | PPC_FEATURE_HAS_MMU;

	pvr = ppc_mfpvr();
	cpu = pvr >> 16;
	switch (cpu) {
	case PPC_CPU_MPC601:
		snprintf(cpu_model, sizeof(cpu_model), "601");
		break;
	case PPC_CPU_MPC603:
		snprintf(cpu_model, sizeof(cpu_model), "603");
		break;
	case PPC_CPU_MPC604:
		snprintf(cpu_model, sizeof(cpu_model), "604");
		break;
	case PPC_CPU_MPC603e:
		snprintf(cpu_model, sizeof(cpu_model), "603e");
		break;
	case PPC_CPU_MPC603ev:
		snprintf(cpu_model, sizeof(cpu_model), "603ev");
		break;
	case PPC_CPU_MPC750:
		snprintf(cpu_model, sizeof(cpu_model), "750");
		break;
	case PPC_CPU_MPC604ev:
		snprintf(cpu_model, sizeof(cpu_model), "604ev");
		break;
	case PPC_CPU_MPC7400:
		ppc_altivec = 1;
		snprintf(cpu_model, sizeof(cpu_model), "7400");
		break;
	case PPC_CPU_MPC7447A:
		ppc_altivec = 1;
		snprintf(cpu_model, sizeof(cpu_model), "7447A");
		break;
	case PPC_CPU_MPC7448:
		ppc_altivec = 1;
		snprintf(cpu_model, sizeof(cpu_model), "7448");
		break;
	case PPC_CPU_IBM970:
		ppc_altivec = 1;
		snprintf(cpu_model, sizeof(cpu_model), "970");
		break;
	case PPC_CPU_IBM970FX:
		ppc_altivec = 1;
		snprintf(cpu_model, sizeof(cpu_model), "970FX");
		break;
	case PPC_CPU_IBM970MP:
		ppc_altivec = 1;
		snprintf(cpu_model, sizeof(cpu_model), "970MP");
		break;
	case PPC_CPU_IBM750FX:
		snprintf(cpu_model, sizeof(cpu_model), "750FX");
		break;
	case PPC_CPU_MPC7410:
		ppc_altivec = 1;
		snprintf(cpu_model, sizeof(cpu_model), "7410");
		break;
	case PPC_CPU_MPC7450:
		ppc_altivec = 1;
		if ((pvr & 0xf) < 3)
			snprintf(cpu_model, sizeof(cpu_model), "7450");
		 else
			snprintf(cpu_model, sizeof(cpu_model), "7451");
		break;
	case PPC_CPU_MPC7455:
		ppc_altivec = 1;
		snprintf(cpu_model, sizeof(cpu_model), "7455");
		break;
	case PPC_CPU_MPC7457:
		ppc_altivec = 1;
		snprintf(cpu_model, sizeof(cpu_model), "7457");
		break;
	default:
		snprintf(cpu_model, sizeof(cpu_model), "Version %x", cpu);
		break;
	}
#ifndef ALTIVEC			/* altivec support absent from kernel */
	ppc_altivec = 0;
#endif
	if (ppc_altivec)
		hwcap |= PPC_FEATURE_HAS_ALTIVEC;

	snprintf(cpu_model + strlen(cpu_model),
	    sizeof(cpu_model) - strlen(cpu_model),
	    " (Revision 0x%x)", pvr & 0xffff);
	printf(": %s", cpu_model);

	for (qhandle = OF_peer(0); qhandle; qhandle = phandle) {
                len = OF_getprop(qhandle, "device_type", name, sizeof(name));
                if (len >= 0 && strcmp(name, "cpu") == 0) {
			OF_getprop(qhandle, "clock-frequency", &clock_freq,
			    sizeof(clock_freq));
			OF_getprop(qhandle, "timebase-frequency", &timebase,
			    sizeof(timebase));
			break;
		}
                if ((phandle = OF_child(qhandle)))
                        continue;
                while (qhandle) {
                        if ((phandle = OF_peer(qhandle)))
                                break;
                        qhandle = OF_parent(qhandle);
                }
	}

	if (timebase != 0) {
		ticks_per_sec = timebase;
		ns_per_tick = 1000000000 / ticks_per_sec;
	}


	if (clock_freq != 0) {
		/* Openfirmware stores clock in Hz, not MHz */
		clock_freq /= 1000000;
		printf(": %d MHz", clock_freq);
		ppc_curfreq = ppc_maxfreq = clock_freq;
		cpu_cpuspeed = ppc_cpuspeed;
	}

	if (cpu == PPC_CPU_IBM970FX) {
		u_int64_t psr;
		int s;

		s = ppc_intr_disable();
		ppc64_mtscomc((SCOM_PSR << SCOMC_ADDR_SHIFT) | SCOMC_READ);
		psr = ppc64_mfscomd();
		ppc64_mfscomc();
		ppc_intr_enable(s);

		if ((psr & PSR_FREQ_MASK) == PSR_FREQ_HALF) {
			ppc_curfreq = ppc_maxfreq / 2;
			perflevel = 50;
		}

		if (OF_getprop(qhandle, "power-mode-data",
		    &ppc_power_mode_data, sizeof ppc_power_mode_data) >= 8) {
			task_set(&ppc64_setperf_task, ppc64_do_setperf, NULL);
			cpu_setperf = ppc64_setperf;
		}
	}

	/* power savings mode */
	hid0 = ppc_mfhid0();

	switch (cpu) {
	case PPC_CPU_MPC603:
	case PPC_CPU_MPC603e:
	case PPC_CPU_MPC750:
	case PPC_CPU_MPC7400:
	case PPC_CPU_IBM750FX:
	case PPC_CPU_MPC7410:
		/* select DOZE mode */
		hid0 &= ~(HID0_NAP | HID0_SLEEP);
		hid0 |= HID0_DOZE | HID0_DPM;
		ppc_cpuidle = 1;
		break;
	case PPC_CPU_MPC7447A:
	case PPC_CPU_MPC7448:
	case PPC_CPU_MPC7450:
	case PPC_CPU_MPC7455:
	case PPC_CPU_MPC7457:
		/* select NAP mode */
		hid0 &= ~(HID0_DOZE | HID0_SLEEP);
		hid0 |= HID0_NAP | HID0_DPM;
		/* try some other flags */
		hid0 |= HID0_SGE | HID0_BTIC;
		hid0 |= HID0_LRSTK | HID0_FOLD | HID0_BHT;
		/* Disable BTIC on 7450 Rev 2.0 or earlier */
		if (cpu == PPC_CPU_MPC7450 && (pvr & 0xffff) < 0x0200)
			hid0 &= ~HID0_BTIC;
		ppc_cpuidle = 1;
		break;
	case PPC_CPU_IBM970:
	case PPC_CPU_IBM970FX:
		/* select NAP mode */
		hid0 &= ~(HID0_DOZE | HID0_DEEPNAP);
		hid0 |= HID0_NAP | HID0_DPM;
		ppc_cpuidle = 1;
		break;
	case PPC_CPU_IBM970MP:
		/* select DEEPNAP mode, which requires NAP */
		hid0 &= ~HID0_DOZE;
		hid0 |= HID0_DEEPNAP | HID0_NAP | HID0_DPM;
		ppc_cpuidle = 1;
		break;
	}
	ppc_mthid0(hid0);

	/* if processor is G3 or G4, configure L2 cache */
	switch (cpu) {
	case PPC_CPU_MPC750:
	case PPC_CPU_MPC7400:
	case PPC_CPU_IBM750FX:
	case PPC_CPU_MPC7410:
	case PPC_CPU_MPC7447A:
	case PPC_CPU_MPC7448:
	case PPC_CPU_MPC7450:
	case PPC_CPU_MPC7455:
	case PPC_CPU_MPC7457:
		config_l2cr(cpu);
		break;
	}
	printf("\n");
}

/* L2CR bit definitions */
#define L2CR_L2E        0x80000000 /* 0: L2 enable */
#define L2CR_L2PE       0x40000000 /* 1: L2 data parity enable */
#define L2CR_L2SIZ      0x30000000 /* 2-3: L2 size */
#define  L2SIZ_RESERVED         0x00000000
#define  L2SIZ_256K             0x10000000
#define  L2SIZ_512K             0x20000000
#define  L2SIZ_1M       0x30000000
#define L2CR_L2CLK      0x0e000000 /* 4-6: L2 clock ratio */
#define  L2CLK_DIS              0x00000000 /* disable L2 clock */
#define  L2CLK_10               0x02000000 /* core clock / 1   */
#define  L2CLK_15               0x04000000 /*            / 1.5 */
#define  L2CLK_20               0x08000000 /*            / 2   */
#define  L2CLK_25               0x0a000000 /*            / 2.5 */
#define  L2CLK_30               0x0c000000 /*            / 3   */
#define L2CR_L2RAM      0x01800000 /* 7-8: L2 RAM type */
#define  L2RAM_FLOWTHRU_BURST   0x00000000
#define  L2RAM_PIPELINE_BURST   0x01000000
#define  L2RAM_PIPELINE_LATE    0x01800000
#define L2CR_L2DO       0x00400000 /* 9: L2 data-only.
                                      Setting this bit disables instruction
                                      caching. */
#define L2CR_L2I        0x00200000 /* 10: L2 global invalidate. */
#define L2CR_L2CTL      0x00100000 /* 11: L2 RAM control (ZZ enable).
                                      Enables automatic operation of the
                                      L2ZZ (low-power mode) signal. */
#define L2CR_L2WT       0x00080000 /* 12: L2 write-through. */
#define L2CR_L2TS       0x00040000 /* 13: L2 test support. */
#define L2CR_L2OH       0x00030000 /* 14-15: L2 output hold. */
#define L2CR_L2SL       0x00008000 /* 16: L2 DLL slow. */
#define L2CR_L2DF       0x00004000 /* 17: L2 differential clock. */
#define L2CR_L2BYP      0x00002000 /* 18: L2 DLL bypass. */
#define L2CR_L2IP       0x00000001 /* 31: L2 global invalidate in progress
				       (read only). */
#ifdef L2CR_CONFIG
u_int l2cr_config = L2CR_CONFIG;
#else
u_int l2cr_config = 0;
#endif

/* L3CR bit definitions */
#define   L3CR_L3E                0x80000000 /*  0: L3 enable */
#define   L3CR_L3SIZ              0x10000000 /*  3: L3 size (0=1MB, 1=2MB) */

void
config_l2cr(int cpu)
{
	u_int l2cr, x;

	l2cr = ppc_mfl2cr();

	/*
	 * Configure L2 cache if not enabled.
	 */
	if ((l2cr & L2CR_L2E) == 0 && l2cr_config != 0) {
		l2cr = l2cr_config;
		ppc_mtl2cr(l2cr);

		/* Wait for L2 clock to be stable (640 L2 clocks). */
		delay(100);

		/* Invalidate all L2 contents. */
		l2cr |= L2CR_L2I;
		ppc_mtl2cr(l2cr);
		do {
			x = ppc_mfl2cr();
		} while (x & L2CR_L2IP);

		/* Enable L2 cache. */
		l2cr &= ~L2CR_L2I;
		l2cr |= L2CR_L2E;
		ppc_mtl2cr(l2cr);
	}

	if (l2cr & L2CR_L2E) {
		if (cpu == PPC_CPU_MPC7450 || cpu == PPC_CPU_MPC7455) {
			u_int l3cr;

			printf(": 256KB L2 cache");

			l3cr = ppc_mfl3cr();
			if (l3cr & L3CR_L3E)
				printf(", %cMB L3 cache",
				    l3cr & L3CR_L3SIZ ? '2' : '1');
		} else if (cpu == PPC_CPU_IBM750FX ||
			   cpu == PPC_CPU_MPC7447A || cpu == PPC_CPU_MPC7457)
			printf(": 512KB L2 cache");
		else if (cpu == PPC_CPU_MPC7448)                                                                                                 
			printf(": 1MB L2 cache");
		else {
			switch (l2cr & L2CR_L2SIZ) {
			case L2SIZ_256K:
				printf(": 256KB");
				break;
			case L2SIZ_512K:
				printf(": 512KB");
				break;
			case L2SIZ_1M:
				printf(": 1MB");
				break;
			default:
				printf(": unknown size");
			}
			printf(" backside cache");
		}
#if 0
		switch (l2cr & L2CR_L2RAM) {
		case L2RAM_FLOWTHRU_BURST:
			printf(" Flow-through synchronous burst SRAM");
			break;
		case L2RAM_PIPELINE_BURST:
			printf(" Pipelined synchronous burst SRAM");
			break;
		case L2RAM_PIPELINE_LATE:
			printf(" Pipelined synchronous late-write SRAM");
			break;
		default:
			printf(" unknown type");
		}

		if (l2cr & L2CR_L2PE)
			printf(" with parity");
#endif
	} else
		printf(": L2 cache not enabled");
}

#ifdef MULTIPROCESSOR

#define	INTSTK	(8*1024)		/* 8K interrupt stack */

int cpu_spinup(struct device *, struct cpu_info *);
void cpu_hatch(void);
void cpu_spinup_trampoline(void);

struct cpu_hatch_data {
	uint64_t tb;
	struct cpu_info *ci;
	uint32_t hid0;
	uint64_t hid1;
	uint64_t hid4;
	uint64_t hid5;
	int l2cr;
	int running;
};

volatile struct cpu_hatch_data *cpu_hatch_data;
volatile void *cpu_hatch_stack;

/*
 * XXX Due to a bug in our OpenFirmware interface/memory mapping,
 * machines with 64bit CPUs hang in the OF_finddevice() call below
 * if this array is stored on the stack.
 */
char cpuname[64];

int
cpu_spinup(struct device *self, struct cpu_info *ci)
{
	volatile struct cpu_hatch_data hatch_data, *h = &hatch_data;
	int i;
	struct pglist mlist;
	struct vm_page *m;
	int error;
	int size = 0;
	char *cp;
	u_char *reset_cpu;
	u_int node;

        /*
         * Allocate some contiguous pages for the interrupt stack
         * from the lowest 256MB (because bat0 always maps it va == pa).
         */
        size += INTSTK;
        size += 8192;   /* SPILLSTK(1k) + DDBSTK(7k) */

	TAILQ_INIT(&mlist);
	error = uvm_pglistalloc(size, 0x0, 0x10000000 - 1, 0, 0,
	    &mlist, 1, UVM_PLA_WAITOK);
	if (error) {
		printf(": unable to allocate idle stack\n");
		return -1;
	}

	m = TAILQ_FIRST(&mlist);
	cp = (char *)VM_PAGE_TO_PHYS(m);
	bzero(cp, size);

	ci->ci_intstk = cp + INTSTK;
	cpu_hatch_stack = ci->ci_intstk - sizeof(struct trapframe);

	h->ci = ci;
	h->running = 0;
	h->hid0 = ppc_mfhid0();
	if (ppc_proc_is_64b) {
		h->hid1 = ppc64_mfhid1();
		h->hid4 = ppc64_mfhid4();
		h->hid5 = ppc64_mfhid5();
	} else {
		h->l2cr = ppc_mfl2cr();
	}
	cpu_hatch_data = h;

	__asm volatile ("sync; isync");

	/* XXX OpenPIC */
	{
		int off;

		*(u_int *)EXC_RST = 0x48000002 | (u_int)cpu_spinup_trampoline;
		syncicache((void *)EXC_RST, 0x100);

		h->running = -1;

		snprintf(cpuname, sizeof(cpuname), "/cpus/@%x", ci->ci_cpuid);
		node = OF_finddevice(cpuname);
		if (node == -1) {
			printf(": unable to locate OF node %s\n", cpuname);
			return  -1;
		}
		if (OF_getprop(node, "soft-reset", &off, 4) == 4) {
			reset_cpu = mapiodev(0x80000000 + off, 1);
			*reset_cpu = 0x4;
			__asm volatile ("eieio" ::: "memory");
			*reset_cpu = 0x0;
			__asm volatile ("eieio" ::: "memory");
		} else {
			/* Start secondary CPU. */
			reset_cpu = mapiodev(0x80000000 + 0x5c, 1);
			*reset_cpu = 0x4;
			__asm volatile ("eieio" ::: "memory");
			*reset_cpu = 0x0;
			__asm volatile ("eieio" ::: "memory");
		}

		/* Sync timebase. */
		h->tb = ppc_mftb() + 100000;	/* 3ms @ 33MHz  */

		while (h->tb > ppc_mftb())
			;
                __asm volatile ("sync; isync");
                h->running = 0;

                delay(500000);
	}


	for (i = 0; i < 0x3fffffff; i++)
		if (h->running) {
			break;
		}

	return 0;
}

volatile static int start_secondary_cpu;

void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	int i;

	for (i = 0; i < PPC_MAXPROCS; i++) {
		ci = &cpu_info[i];
		if (ci->ci_cpuid == 0)
			continue;
		ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;

		clockqueue_init(&ci->ci_queue);
		sched_init_cpu(ci);

		cpu_spinup(NULL, ci);
	}

	start_secondary_cpu = 1;
	__asm volatile ("sync");
}

void cpu_startclock(void);

void
cpu_hatch(void)
{
	volatile struct cpu_hatch_data *h = cpu_hatch_data;
	int intrstate;

        /* Initialize timebase. */
	ppc_mttb(0);

	/* Initialize curcpu(). */
	ppc_mtsprg0((u_int)h->ci);

	ppc_mtibat0u(0);
	ppc_mtibat1u(0);
	ppc_mtibat2u(0);
	ppc_mtibat3u(0);
	ppc_mtdbat0u(0);
	ppc_mtdbat1u(0);
	ppc_mtdbat2u(0);
	ppc_mtdbat3u(0);

	if (ppc_proc_is_64b) {
		/*
		 * The Hardware Interrupt Offset Register should be
		 * cleared after initialization.
		 */
		ppc_mthior(0);
		__asm volatile ("sync");

		ppc_mthid0(h->hid0);
		ppc64_mthid1(h->hid1);
		ppc64_mthid4(h->hid4);
		ppc64_mthid5(h->hid5);
	} else if (h->l2cr != 0) {
		u_int x;

		ppc_mthid0(h->hid0);
		ppc_mtl2cr(h->l2cr & ~L2CR_L2E);

		/* Wait for L2 clock to be stable (640 L2 clocks). */
		delay(100);

		/* Invalidate all L2 contents. */
		ppc_mtl2cr((h->l2cr & ~L2CR_L2E)|L2CR_L2I);
		do {
			x = ppc_mfl2cr();
		} while (x & L2CR_L2IP);

		ppc_mtl2cr(h->l2cr);
	}

	/*
	 * Now enable translation (and machine checks/recoverable interrupts).
	 */
	pmap_enable_mmu();

	/* XXX OpenPIC */
	{
		/* Sync timebase. */
		while (h->running == -1)
			;
                __asm volatile ("sync; isync");
                ppc_mttb(h->tb);
	}

	ncpus++;
	h->running = 1;
	__asm volatile ("eieio" ::: "memory");

	while (start_secondary_cpu == 0)
		;

	__asm volatile ("sync; isync");

	curcpu()->ci_ipending = 0;
	curcpu()->ci_cpl = 0;

	intrstate = ppc_intr_disable();
	cpu_startclock();
	ppc_intr_enable(intrstate);

	/* Enable inter-processor interrupts. */
	openpic_set_priority(curcpu()->ci_cpuid, 14);

	sched_toidle();
}
#endif

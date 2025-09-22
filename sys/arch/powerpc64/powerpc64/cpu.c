/*	$OpenBSD: cpu.c,v 1.30 2024/11/28 18:54:36 gkoehler Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/timeout.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/fdt.h>
#include <machine/opal.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* CPU Identification. */
#define CPU_IBMPOWER8E		0x004b
#define CPU_IBMPOWER8NVL	0x004c
#define CPU_IBMPOWER8		0x004d
#define CPU_IBMPOWER9		0x004e
#define CPU_IBMPOWER9P		0x004f

#define CPU_VERSION(pvr)	((pvr) >> 16)
#define CPU_REV_MAJ(pvr)	(((pvr) >> 8) & 0xf)
#define CPU_REV_MIN(pvr)	(((pvr) >> 0) & 0xf)

struct cpu_version {
	int		version;
	const char	*name;
};

struct cpu_version cpu_version[] = {
	{ CPU_IBMPOWER8, "IBM POWER8" },
	{ CPU_IBMPOWER8E, "IBM POWER8E" },
	{ CPU_IBMPOWER8NVL, "IBM POWER8NVL" },
	{ CPU_IBMPOWER9, "IBM POWER9" },
	{ CPU_IBMPOWER9P, "IBM POWER9P" },
	{ 0, NULL }
};

char cpu_model[64];

uint64_t tb_freq = 512000000;	/* POWER8, POWER9 */

struct cpu_info cpu_info[MAXCPUS];
struct cpu_info *cpu_info_primary = &cpu_info[0];

struct timeout cpu_darn_to;
void	cpu_darn(void *);

int	cpu_match(struct device *, void *, void *);
void	cpu_attach(struct device *, struct device *, void *);

const struct cfattach cpu_ca = {
	sizeof(struct device), cpu_match, cpu_attach
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

void	cpu_hatch(void);
int	cpu_intr(void *);

int
cpu_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;
	char buf[32];

	if (OF_getprop(faa->fa_node, "device_type", buf, sizeof(buf)) <= 0 ||
	    strcmp(buf, "cpu") != 0)
		return 0;

	if (ncpus < MAXCPUS || faa->fa_reg[0].addr == mfpir())
		return 1;

	return 0;
}

void
cpu_attach(struct device *parent, struct device *dev, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct cpu_info *ci;
	const char *name = NULL;
	uint32_t pvr, clock_freq, iline, dline;
	int node, level, i;

	ci = &cpu_info[dev->dv_unit];
	ci->ci_dev = dev;
	ci->ci_cpuid = dev->dv_unit;
	ci->ci_pir = faa->fa_reg[0].addr;
	ci->ci_node = faa->fa_node;

	printf(" pir %x", ci->ci_pir);

	pvr = mfpvr();

	for (i = 0; cpu_version[i].name; i++) {
		if (CPU_VERSION(pvr) == cpu_version[i].version) {
			name = cpu_version[i].name;
			break;
		}
	}

	if (name) {
		printf(": %s %d.%d", name, CPU_REV_MAJ(pvr), CPU_REV_MIN(pvr));
		snprintf(cpu_model, sizeof(cpu_model), "%s %d.%d",
		    name, CPU_REV_MAJ(pvr), CPU_REV_MIN(pvr));
	} else {
		printf(": Unknown, PVR 0x%x", pvr);
		strlcpy(cpu_model, "Unknown", sizeof(cpu_model));
	}

	node = faa->fa_node;
	clock_freq = OF_getpropint(node, "clock-frequency", 0);
	if (clock_freq != 0) {
		clock_freq /= 1000000; /* Hz to MHz */
		printf(", %u MHz", clock_freq);
	}

	iline = OF_getpropint(node, "i-cache-block-size", 128);
	dline = OF_getpropint(node, "d-cache-block-size", 128);
	level = 1;

	while (node) {
		const char *unit = "KB";
		uint32_t isize, iways;
		uint32_t dsize, dways;
		uint32_t cache;

		isize = OF_getpropint(node, "i-cache-size", 0) / 1024;
		iways = OF_getpropint(node, "i-cache-sets", 0);
		dsize = OF_getpropint(node, "d-cache-size", 0) / 1024;
		dways = OF_getpropint(node, "d-cache-sets", 0);

		/* Print large cache sizes in MB. */
		if (isize > 4096 && dsize > 4096) {
			unit = "MB";
			isize /= 1024;
			dsize /= 1024;
		}

		printf("\n%s:", dev->dv_xname);
		
		if (OF_getproplen(node, "cache-unified") == 0) {
			printf(" %d%s %db/line %d-way L%d cache",
			    isize, unit, iline, iways, level);
		} else {
			printf(" %d%s %db/line %d-way L%d I-cache",
			    isize, unit, iline, iways, level);
			printf(", %d%s %db/line %d-way L%d D-cache",
			    dsize, unit, dline, dways, level);
		}

		cache = OF_getpropint(node, "l2-cache", 0);
		node = OF_getnodebyphandle(cache);
		level++;
	}

	if (CPU_IS_PRIMARY(ci) && (hwcap2 & PPC_FEATURE2_DARN)) {
		timeout_set(&cpu_darn_to, cpu_darn, NULL);
		cpu_darn(NULL);
	}

#ifdef MULTIPROCESSOR
	if (dev->dv_unit != 0) {
		int timeout = 10000;

		clockqueue_init(&ci->ci_queue);
		sched_init_cpu(ci);
		ncpus++;

		ci->ci_initstack_end = km_alloc(PAGE_SIZE, &kv_any, &kp_zero,
		    &kd_waitok) + PAGE_SIZE;

		if (opal_start_cpu(ci->ci_pir, (vaddr_t)cpu_hatch) ==
		    OPAL_SUCCESS) {
			atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFY);
			membar_sync();

			while ((ci->ci_flags & CPUF_IDENTIFIED) == 0 &&
			    --timeout)
				delay(1000);
			if (timeout == 0) {
				printf(" failed to identify");
				ci->ci_flags = 0;
			}
		} else {
			printf(" failed to start");
			ci->ci_flags = 0;
		}
	}
#endif

	printf("\n");

	/* Update timebase frequency to reflect reality. */
	tb_freq = OF_getpropint(faa->fa_node, "timebase-frequency", tb_freq);
}

void
cpu_init_features(void)
{
	uint32_t pvr = mfpvr();

	hwcap = PPC_FEATURE_32 | PPC_FEATURE_64 | PPC_FEATURE_HAS_FPU |
	    PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_ALTIVEC |
	    PPC_FEATURE_HAS_VSX;

	switch (CPU_VERSION(pvr)) {
	case CPU_IBMPOWER9:
	case CPU_IBMPOWER9P:
		hwcap2 |= PPC_FEATURE2_ARCH_3_00;
		hwcap2 |= PPC_FEATURE2_DARN;
		break;
	}
}

void
cpu_init(void)
{
	uint64_t lpcr = LPCR_LPES;

	if (hwcap2 & PPC_FEATURE2_ARCH_3_00)
		lpcr |= LPCR_PECE | LPCR_HVICE;

	mtlpcr(lpcr);
	isync();

	mtfscr(0);
	isync();

	/*
	 * Set AMR to inhibit loads and stores for all virtual page
	 * class keys, except for Key0 which is used for normal kernel
	 * access.  This means we can pick any other key to implement
	 * execute-only mappings.  But we pick Key1 since that allows
	 * us to use the same bit in the PTE as was used to enable the
	 * Data Access Compare mechanism on CPUs based on older
	 * versions of the architecture (such as the PowerPC 970).
	 *
	 * Set UAMOR (and AMOR just to be safe) to zero to prevent
	 * userland from modifying any bits in AMR.
	 */
	mtamr(0x3fffffffffffffff);
	mtuamor(0);
	mtamor(0);
	isync();
}

void
cpu_darn(void *arg)
{
	uint64_t value;

	__asm volatile ("darn %0, 1" : "=r"(value));
	if (value != UINT64_MAX) {
		enqueue_randomness(value);
		enqueue_randomness(value >> 32);
	}

	timeout_add_msec(&cpu_darn_to, 10);
}

uint64_t cpu_idle_state_psscr;
void	cpu_idle_spin(void);
void	(*cpu_idle_cycle_fcn)(void) = &cpu_idle_spin;

void
cpu_idle_cycle(void)
{
	intr_disable();

	if (!cpu_is_idle(curcpu())) {
		intr_enable();
		return;
	}

	(*cpu_idle_cycle_fcn)();

	intr_enable();
}

#ifdef MULTIPROCESSOR

volatile int mp_perflevel;
void (*ul_setperf)(int);

void
cpu_bootstrap(void)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	uint32_t pir = mfpir();
	uint64_t msr;

	CPU_INFO_FOREACH(cii, ci) {
		if (pir == ci->ci_pir)
			break;
	}

	/* Store pointer to our struct cpu_info. */
	__asm volatile ("mtsprg0 %0" :: "r"(ci));

	/* We're now ready to take traps. */
	msr = mfmsr();
	mtmsr(msr | (PSL_ME|PSL_RI));

	cpu_init();

	pmap_bootstrap_cpu();

	/* Enable translation. */
	msr = mfmsr();
	mtmsr(msr | (PSL_DR|PSL_IR));
	isync();
}

void
cpu_start_secondary(void)
{
	struct cpu_info *ci = curcpu();
	int s;

	atomic_setbits_int(&ci->ci_flags, CPUF_PRESENT);

	while ((ci->ci_flags & CPUF_IDENTIFY) == 0)
		CPU_BUSY_CYCLE();

	atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFIED);
	membar_sync();

	while ((ci->ci_flags & CPUF_GO) == 0)
		CPU_BUSY_CYCLE();

	s = splhigh();
	cpu_startclock();

	atomic_setbits_int(&ci->ci_flags, CPUF_RUNNING);
	membar_sync();

	spllower(IPL_NONE);

	sched_toidle();
}

void
cpu_boot_secondary(struct cpu_info *ci)
{
	atomic_setbits_int(&ci->ci_flags, CPUF_GO);
	membar_sync();

	while ((ci->ci_flags & CPUF_RUNNING) == 0)
		CPU_BUSY_CYCLE();
}

void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		/* Set up IPI handler. */
		ci->ci_ipi = fdt_intr_establish_idx_cpu(ci->ci_node, 0,
		    IPL_IPI, ci, cpu_intr, ci, ci->ci_dev->dv_xname);

		if (CPU_IS_PRIMARY(ci))
			continue;
		if ((ci->ci_flags & CPUF_PRESENT) == 0)
			continue;

		ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;
		cpu_boot_secondary(ci);
	}
}

int
cpu_intr(void *arg)
{
	struct cpu_info *ci = curcpu();
	int pending;

	pending = atomic_swap_uint(&ci->ci_ipi_reason, IPI_NOP);

	if (pending & IPI_DDB)
		db_enter();

	if (pending & IPI_SETPERF)
		ul_setperf(mp_perflevel);

	return 1;
}

void
cpu_kick(struct cpu_info *ci)
{
	if (ci != curcpu())
		intr_send_ipi(ci, IPI_NOP);
}

void
cpu_unidle(struct cpu_info *ci)
{
	if (ci != curcpu())
		intr_send_ipi(ci, IPI_NOP);
}

/*
 * Run ul_setperf(level) on every core.
 */
void
mp_setperf(int level)
{
	int i;

	mp_perflevel = level;
	ul_setperf(level);
	for (i = 0; i < ncpus; i++) {
		if (i != cpu_number())
			intr_send_ipi(&cpu_info[i], IPI_SETPERF);
	}
}

#endif

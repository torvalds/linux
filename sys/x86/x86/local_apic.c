/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Local APIC support on Pentium and later processors.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_atpic.h"
#include "opt_hwpmc_hooks.h"

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/timeet.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <x86/apicreg.h>
#include <machine/clock.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#include <x86/mca.h>
#include <machine/md_var.h>
#include <machine/smp.h>
#include <machine/specialreg.h>
#include <x86/init.h>

#ifdef DDB
#include <sys/interrupt.h>
#include <ddb/ddb.h>
#endif

#ifdef __amd64__
#define	SDT_APIC	SDT_SYSIGT
#define	GSEL_APIC	0
#else
#define	SDT_APIC	SDT_SYS386IGT
#define	GSEL_APIC	GSEL(GCODE_SEL, SEL_KPL)
#endif

static MALLOC_DEFINE(M_LAPIC, "local_apic", "Local APIC items");

/* Sanity checks on IDT vectors. */
CTASSERT(APIC_IO_INTS + APIC_NUM_IOINTS == APIC_TIMER_INT);
CTASSERT(APIC_TIMER_INT < APIC_LOCAL_INTS);
CTASSERT(APIC_LOCAL_INTS == 240);
CTASSERT(IPI_STOP < APIC_SPURIOUS_INT);

/*
 * I/O interrupts use non-negative IRQ values.  These values are used
 * to mark unused IDT entries or IDT entries reserved for a non-I/O
 * interrupt.
 */
#define	IRQ_FREE	-1
#define	IRQ_TIMER	-2
#define	IRQ_SYSCALL	-3
#define	IRQ_DTRACE_RET	-4
#define	IRQ_EVTCHN	-5

enum lat_timer_mode {
	LAT_MODE_UNDEF =	0,
	LAT_MODE_PERIODIC =	1,
	LAT_MODE_ONESHOT =	2,
	LAT_MODE_DEADLINE =	3,
};

/*
 * Support for local APICs.  Local APICs manage interrupts on each
 * individual processor as opposed to I/O APICs which receive interrupts
 * from I/O devices and then forward them on to the local APICs.
 *
 * Local APICs can also send interrupts to each other thus providing the
 * mechanism for IPIs.
 */

struct lvt {
	u_int lvt_edgetrigger:1;
	u_int lvt_activehi:1;
	u_int lvt_masked:1;
	u_int lvt_active:1;
	u_int lvt_mode:16;
	u_int lvt_vector:8;
};

struct lapic {
	struct lvt la_lvts[APIC_LVT_MAX + 1];
	struct lvt la_elvts[APIC_ELVT_MAX + 1];;
	u_int la_id:8;
	u_int la_cluster:4;
	u_int la_cluster_id:2;
	u_int la_present:1;
	u_long *la_timer_count;
	uint64_t la_timer_period;
	enum lat_timer_mode la_timer_mode;
	uint32_t lvt_timer_base;
	uint32_t lvt_timer_last;
	/* Include IDT_SYSCALL to make indexing easier. */
	int la_ioint_irqs[APIC_NUM_IOINTS + 1];
} static *lapics;

/* Global defaults for local APIC LVT entries. */
static struct lvt lvts[APIC_LVT_MAX + 1] = {
	{ 1, 1, 1, 1, APIC_LVT_DM_EXTINT, 0 },	/* LINT0: masked ExtINT */
	{ 1, 1, 0, 1, APIC_LVT_DM_NMI, 0 },	/* LINT1: NMI */
	{ 1, 1, 1, 1, APIC_LVT_DM_FIXED, APIC_TIMER_INT },	/* Timer */
	{ 1, 1, 0, 1, APIC_LVT_DM_FIXED, APIC_ERROR_INT },	/* Error */
	{ 1, 1, 1, 1, APIC_LVT_DM_NMI, 0 },	/* PMC */
	{ 1, 1, 1, 1, APIC_LVT_DM_FIXED, APIC_THERMAL_INT },	/* Thermal */
	{ 1, 1, 1, 1, APIC_LVT_DM_FIXED, APIC_CMC_INT },	/* CMCI */
};

/* Global defaults for AMD local APIC ELVT entries. */
static struct lvt elvts[APIC_ELVT_MAX + 1] = {
	{ 1, 1, 1, 0, APIC_LVT_DM_FIXED, 0 },
	{ 1, 1, 1, 0, APIC_LVT_DM_FIXED, APIC_CMC_INT },
	{ 1, 1, 1, 0, APIC_LVT_DM_FIXED, 0 },
	{ 1, 1, 1, 0, APIC_LVT_DM_FIXED, 0 },
};

static inthand_t *ioint_handlers[] = {
	NULL,			/* 0 - 31 */
	IDTVEC(apic_isr1),	/* 32 - 63 */
	IDTVEC(apic_isr2),	/* 64 - 95 */
	IDTVEC(apic_isr3),	/* 96 - 127 */
	IDTVEC(apic_isr4),	/* 128 - 159 */
	IDTVEC(apic_isr5),	/* 160 - 191 */
	IDTVEC(apic_isr6),	/* 192 - 223 */
	IDTVEC(apic_isr7),	/* 224 - 255 */
};

static inthand_t *ioint_pti_handlers[] = {
	NULL,			/* 0 - 31 */
	IDTVEC(apic_isr1_pti),	/* 32 - 63 */
	IDTVEC(apic_isr2_pti),	/* 64 - 95 */
	IDTVEC(apic_isr3_pti),	/* 96 - 127 */
	IDTVEC(apic_isr4_pti),	/* 128 - 159 */
	IDTVEC(apic_isr5_pti),	/* 160 - 191 */
	IDTVEC(apic_isr6_pti),	/* 192 - 223 */
	IDTVEC(apic_isr7_pti),	/* 224 - 255 */
};

static u_int32_t lapic_timer_divisors[] = {
	APIC_TDCR_1, APIC_TDCR_2, APIC_TDCR_4, APIC_TDCR_8, APIC_TDCR_16,
	APIC_TDCR_32, APIC_TDCR_64, APIC_TDCR_128
};

extern inthand_t IDTVEC(rsvd_pti), IDTVEC(rsvd);

volatile char *lapic_map;
vm_paddr_t lapic_paddr;
int x2apic_mode;
int lapic_eoi_suppression;
static int lapic_timer_tsc_deadline;
static u_long lapic_timer_divisor, count_freq;
static struct eventtimer lapic_et;
#ifdef SMP
static uint64_t lapic_ipi_wait_mult;
#endif
unsigned int max_apic_id;

SYSCTL_NODE(_hw, OID_AUTO, apic, CTLFLAG_RD, 0, "APIC options");
SYSCTL_INT(_hw_apic, OID_AUTO, x2apic_mode, CTLFLAG_RD, &x2apic_mode, 0, "");
SYSCTL_INT(_hw_apic, OID_AUTO, eoi_suppression, CTLFLAG_RD,
    &lapic_eoi_suppression, 0, "");
SYSCTL_INT(_hw_apic, OID_AUTO, timer_tsc_deadline, CTLFLAG_RD,
    &lapic_timer_tsc_deadline, 0, "");

static void lapic_calibrate_initcount(struct lapic *la);
static void lapic_calibrate_deadline(struct lapic *la);

static uint32_t
lapic_read32(enum LAPIC_REGISTERS reg)
{
	uint32_t res;

	if (x2apic_mode) {
		res = rdmsr32(MSR_APIC_000 + reg);
	} else {
		res = *(volatile uint32_t *)(lapic_map + reg * LAPIC_MEM_MUL);
	}
	return (res);
}

static void
lapic_write32(enum LAPIC_REGISTERS reg, uint32_t val)
{

	if (x2apic_mode) {
		mfence();
		lfence();
		wrmsr(MSR_APIC_000 + reg, val);
	} else {
		*(volatile uint32_t *)(lapic_map + reg * LAPIC_MEM_MUL) = val;
	}
}

static void
lapic_write32_nofence(enum LAPIC_REGISTERS reg, uint32_t val)
{

	if (x2apic_mode) {
		wrmsr(MSR_APIC_000 + reg, val);
	} else {
		*(volatile uint32_t *)(lapic_map + reg * LAPIC_MEM_MUL) = val;
	}
}

#ifdef SMP
static uint64_t
lapic_read_icr(void)
{
	uint64_t v;
	uint32_t vhi, vlo;

	if (x2apic_mode) {
		v = rdmsr(MSR_APIC_000 + LAPIC_ICR_LO);
	} else {
		vhi = lapic_read32(LAPIC_ICR_HI);
		vlo = lapic_read32(LAPIC_ICR_LO);
		v = ((uint64_t)vhi << 32) | vlo;
	}
	return (v);
}

static uint64_t
lapic_read_icr_lo(void)
{

	return (lapic_read32(LAPIC_ICR_LO));
}

static void
lapic_write_icr(uint32_t vhi, uint32_t vlo)
{
	uint64_t v;

	if (x2apic_mode) {
		v = ((uint64_t)vhi << 32) | vlo;
		mfence();
		wrmsr(MSR_APIC_000 + LAPIC_ICR_LO, v);
	} else {
		lapic_write32(LAPIC_ICR_HI, vhi);
		lapic_write32(LAPIC_ICR_LO, vlo);
	}
}
#endif /* SMP */

static void
native_lapic_enable_x2apic(void)
{
	uint64_t apic_base;

	apic_base = rdmsr(MSR_APICBASE);
	apic_base |= APICBASE_X2APIC | APICBASE_ENABLED;
	wrmsr(MSR_APICBASE, apic_base);
}

static bool
native_lapic_is_x2apic(void)
{
	uint64_t apic_base;

	apic_base = rdmsr(MSR_APICBASE);
	return ((apic_base & (APICBASE_X2APIC | APICBASE_ENABLED)) ==
	    (APICBASE_X2APIC | APICBASE_ENABLED));
}

static void	lapic_enable(void);
static void	lapic_resume(struct pic *pic, bool suspend_cancelled);
static void	lapic_timer_oneshot(struct lapic *);
static void	lapic_timer_oneshot_nointr(struct lapic *, uint32_t);
static void	lapic_timer_periodic(struct lapic *);
static void	lapic_timer_deadline(struct lapic *);
static void	lapic_timer_stop(struct lapic *);
static void	lapic_timer_set_divisor(u_int divisor);
static uint32_t	lvt_mode(struct lapic *la, u_int pin, uint32_t value);
static int	lapic_et_start(struct eventtimer *et,
		    sbintime_t first, sbintime_t period);
static int	lapic_et_stop(struct eventtimer *et);
static u_int	apic_idt_to_irq(u_int apic_id, u_int vector);
static void	lapic_set_tpr(u_int vector);

struct pic lapic_pic = { .pic_resume = lapic_resume };

/* Forward declarations for apic_ops */
static void	native_lapic_create(u_int apic_id, int boot_cpu);
static void	native_lapic_init(vm_paddr_t addr);
static void	native_lapic_xapic_mode(void);
static void	native_lapic_setup(int boot);
static void	native_lapic_dump(const char *str);
static void	native_lapic_disable(void);
static void	native_lapic_eoi(void);
static int	native_lapic_id(void);
static int	native_lapic_intr_pending(u_int vector);
static u_int	native_apic_cpuid(u_int apic_id);
static u_int	native_apic_alloc_vector(u_int apic_id, u_int irq);
static u_int	native_apic_alloc_vectors(u_int apic_id, u_int *irqs,
		    u_int count, u_int align);
static void 	native_apic_disable_vector(u_int apic_id, u_int vector);
static void 	native_apic_enable_vector(u_int apic_id, u_int vector);
static void 	native_apic_free_vector(u_int apic_id, u_int vector, u_int irq);
static void 	native_lapic_set_logical_id(u_int apic_id, u_int cluster,
		    u_int cluster_id);
static int 	native_lapic_enable_pmc(void);
static void 	native_lapic_disable_pmc(void);
static void 	native_lapic_reenable_pmc(void);
static void 	native_lapic_enable_cmc(void);
static int 	native_lapic_enable_mca_elvt(void);
static int 	native_lapic_set_lvt_mask(u_int apic_id, u_int lvt,
		    u_char masked);
static int 	native_lapic_set_lvt_mode(u_int apic_id, u_int lvt,
		    uint32_t mode);
static int 	native_lapic_set_lvt_polarity(u_int apic_id, u_int lvt,
		    enum intr_polarity pol);
static int 	native_lapic_set_lvt_triggermode(u_int apic_id, u_int lvt,
		    enum intr_trigger trigger);
#ifdef SMP
static void 	native_lapic_ipi_raw(register_t icrlo, u_int dest);
static void 	native_lapic_ipi_vectored(u_int vector, int dest);
static int 	native_lapic_ipi_wait(int delay);
#endif /* SMP */
static int	native_lapic_ipi_alloc(inthand_t *ipifunc);
static void	native_lapic_ipi_free(int vector);

struct apic_ops apic_ops = {
	.create			= native_lapic_create,
	.init			= native_lapic_init,
	.xapic_mode		= native_lapic_xapic_mode,
	.is_x2apic		= native_lapic_is_x2apic,
	.setup			= native_lapic_setup,
	.dump			= native_lapic_dump,
	.disable		= native_lapic_disable,
	.eoi			= native_lapic_eoi,
	.id			= native_lapic_id,
	.intr_pending		= native_lapic_intr_pending,
	.set_logical_id		= native_lapic_set_logical_id,
	.cpuid			= native_apic_cpuid,
	.alloc_vector		= native_apic_alloc_vector,
	.alloc_vectors		= native_apic_alloc_vectors,
	.enable_vector		= native_apic_enable_vector,
	.disable_vector		= native_apic_disable_vector,
	.free_vector		= native_apic_free_vector,
	.enable_pmc		= native_lapic_enable_pmc,
	.disable_pmc		= native_lapic_disable_pmc,
	.reenable_pmc		= native_lapic_reenable_pmc,
	.enable_cmc		= native_lapic_enable_cmc,
	.enable_mca_elvt	= native_lapic_enable_mca_elvt,
#ifdef SMP
	.ipi_raw		= native_lapic_ipi_raw,
	.ipi_vectored		= native_lapic_ipi_vectored,
	.ipi_wait		= native_lapic_ipi_wait,
#endif
	.ipi_alloc		= native_lapic_ipi_alloc,
	.ipi_free		= native_lapic_ipi_free,
	.set_lvt_mask		= native_lapic_set_lvt_mask,
	.set_lvt_mode		= native_lapic_set_lvt_mode,
	.set_lvt_polarity	= native_lapic_set_lvt_polarity,
	.set_lvt_triggermode	= native_lapic_set_lvt_triggermode,
};

static uint32_t
lvt_mode_impl(struct lapic *la, struct lvt *lvt, u_int pin, uint32_t value)
{

	value &= ~(APIC_LVT_M | APIC_LVT_TM | APIC_LVT_IIPP | APIC_LVT_DM |
	    APIC_LVT_VECTOR);
	if (lvt->lvt_edgetrigger == 0)
		value |= APIC_LVT_TM;
	if (lvt->lvt_activehi == 0)
		value |= APIC_LVT_IIPP_INTALO;
	if (lvt->lvt_masked)
		value |= APIC_LVT_M;
	value |= lvt->lvt_mode;
	switch (lvt->lvt_mode) {
	case APIC_LVT_DM_NMI:
	case APIC_LVT_DM_SMI:
	case APIC_LVT_DM_INIT:
	case APIC_LVT_DM_EXTINT:
		if (!lvt->lvt_edgetrigger && bootverbose) {
			printf("lapic%u: Forcing LINT%u to edge trigger\n",
			    la->la_id, pin);
			value &= ~APIC_LVT_TM;
		}
		/* Use a vector of 0. */
		break;
	case APIC_LVT_DM_FIXED:
		value |= lvt->lvt_vector;
		break;
	default:
		panic("bad APIC LVT delivery mode: %#x\n", value);
	}
	return (value);
}

static uint32_t
lvt_mode(struct lapic *la, u_int pin, uint32_t value)
{
	struct lvt *lvt;

	KASSERT(pin <= APIC_LVT_MAX,
	    ("%s: pin %u out of range", __func__, pin));
	if (la->la_lvts[pin].lvt_active)
		lvt = &la->la_lvts[pin];
	else
		lvt = &lvts[pin];

	return (lvt_mode_impl(la, lvt, pin, value));
}

static uint32_t
elvt_mode(struct lapic *la, u_int idx, uint32_t value)
{
	struct lvt *elvt;

	KASSERT(idx <= APIC_ELVT_MAX,
	    ("%s: idx %u out of range", __func__, idx));

	elvt = &la->la_elvts[idx];
	KASSERT(elvt->lvt_active, ("%s: ELVT%u is not active", __func__, idx));
	KASSERT(elvt->lvt_edgetrigger,
	    ("%s: ELVT%u is not edge triggered", __func__, idx));
	KASSERT(elvt->lvt_activehi,
	    ("%s: ELVT%u is not active high", __func__, idx));
	return (lvt_mode_impl(la, elvt, idx, value));
}

/*
 * Map the local APIC and setup necessary interrupt vectors.
 */
static void
native_lapic_init(vm_paddr_t addr)
{
#ifdef SMP
	uint64_t r, r1, r2, rx;
#endif
	uint32_t ver;
	u_int regs[4];
	int i, arat;

	/*
	 * Enable x2APIC mode if possible. Map the local APIC
	 * registers page.
	 *
	 * Keep the LAPIC registers page mapped uncached for x2APIC
	 * mode too, to have direct map page attribute set to
	 * uncached.  This is needed to work around CPU errata present
	 * on all Intel processors.
	 */
	KASSERT(trunc_page(addr) == addr,
	    ("local APIC not aligned on a page boundary"));
	lapic_paddr = addr;
	lapic_map = pmap_mapdev(addr, PAGE_SIZE);
	if (x2apic_mode) {
		native_lapic_enable_x2apic();
		lapic_map = NULL;
	}

	/* Setup the spurious interrupt handler. */
	setidt(APIC_SPURIOUS_INT, IDTVEC(spuriousint), SDT_APIC, SEL_KPL,
	    GSEL_APIC);

	/* Perform basic initialization of the BSP's local APIC. */
	lapic_enable();

	/* Set BSP's per-CPU local APIC ID. */
	PCPU_SET(apic_id, lapic_id());

	/* Local APIC timer interrupt. */
	setidt(APIC_TIMER_INT, pti ? IDTVEC(timerint_pti) : IDTVEC(timerint),
	    SDT_APIC, SEL_KPL, GSEL_APIC);

	/* Local APIC error interrupt. */
	setidt(APIC_ERROR_INT, pti ? IDTVEC(errorint_pti) : IDTVEC(errorint),
	    SDT_APIC, SEL_KPL, GSEL_APIC);

	/* XXX: Thermal interrupt */

	/* Local APIC CMCI. */
	setidt(APIC_CMC_INT, pti ? IDTVEC(cmcint_pti) : IDTVEC(cmcint),
	    SDT_APIC, SEL_KPL, GSEL_APIC);

	if ((resource_int_value("apic", 0, "clock", &i) != 0 || i != 0)) {
		arat = 0;
		/* Intel CPUID 0x06 EAX[2] set if APIC timer runs in C3. */
		if (cpu_vendor_id == CPU_VENDOR_INTEL && cpu_high >= 6) {
			do_cpuid(0x06, regs);
			if ((regs[0] & CPUTPM1_ARAT) != 0)
				arat = 1;
		} else if (cpu_vendor_id == CPU_VENDOR_AMD &&
		    CPUID_TO_FAMILY(cpu_id) >= 0x12) {
			arat = 1;
		}
		bzero(&lapic_et, sizeof(lapic_et));
		lapic_et.et_name = "LAPIC";
		lapic_et.et_flags = ET_FLAGS_PERIODIC | ET_FLAGS_ONESHOT |
		    ET_FLAGS_PERCPU;
		lapic_et.et_quality = 600;
		if (!arat) {
			lapic_et.et_flags |= ET_FLAGS_C3STOP;
			lapic_et.et_quality = 100;
		}
		if ((cpu_feature & CPUID_TSC) != 0 &&
		    (cpu_feature2 & CPUID2_TSCDLT) != 0 &&
		    tsc_is_invariant && tsc_freq != 0) {
			lapic_timer_tsc_deadline = 1;
			TUNABLE_INT_FETCH("hw.lapic_tsc_deadline",
			    &lapic_timer_tsc_deadline);
		}

		lapic_et.et_frequency = 0;
		/* We don't know frequency yet, so trying to guess. */
		lapic_et.et_min_period = 0x00001000LL;
		lapic_et.et_max_period = SBT_1S;
		lapic_et.et_start = lapic_et_start;
		lapic_et.et_stop = lapic_et_stop;
		lapic_et.et_priv = NULL;
		et_register(&lapic_et);
	}

	/*
	 * Set lapic_eoi_suppression after lapic_enable(), to not
	 * enable suppression in the hardware prematurely.  Note that
	 * we by default enable suppression even when system only has
	 * one IO-APIC, since EOI is broadcasted to all APIC agents,
	 * including CPUs, otherwise.
	 *
	 * It seems that at least some KVM versions report
	 * EOI_SUPPRESSION bit, but auto-EOI does not work.
	 */
	ver = lapic_read32(LAPIC_VERSION);
	if ((ver & APIC_VER_EOI_SUPPRESSION) != 0) {
		lapic_eoi_suppression = 1;
		if (vm_guest == VM_GUEST_KVM) {
			if (bootverbose)
				printf(
		       "KVM -- disabling lapic eoi suppression\n");
			lapic_eoi_suppression = 0;
		}
		TUNABLE_INT_FETCH("hw.lapic_eoi_suppression",
		    &lapic_eoi_suppression);
	}

#ifdef SMP
#define	LOOPS	100000
	/*
	 * Calibrate the busy loop waiting for IPI ack in xAPIC mode.
	 * lapic_ipi_wait_mult contains the number of iterations which
	 * approximately delay execution for 1 microsecond (the
	 * argument to native_lapic_ipi_wait() is in microseconds).
	 *
	 * We assume that TSC is present and already measured.
	 * Possible TSC frequency jumps are irrelevant to the
	 * calibration loop below, the CPU clock management code is
	 * not yet started, and we do not enter sleep states.
	 */
	KASSERT((cpu_feature & CPUID_TSC) != 0 && tsc_freq != 0,
	    ("TSC not initialized"));
	if (!x2apic_mode) {
		r = rdtsc();
		for (rx = 0; rx < LOOPS; rx++) {
			(void)lapic_read_icr_lo();
			ia32_pause();
		}
		r = rdtsc() - r;
		r1 = tsc_freq * LOOPS;
		r2 = r * 1000000;
		lapic_ipi_wait_mult = r1 >= r2 ? r1 / r2 : 1;
		if (bootverbose) {
			printf("LAPIC: ipi_wait() us multiplier %ju (r %ju "
			    "tsc %ju)\n", (uintmax_t)lapic_ipi_wait_mult,
			    (uintmax_t)r, (uintmax_t)tsc_freq);
		}
	}
#undef LOOPS
#endif /* SMP */
}

/*
 * Create a local APIC instance.
 */
static void
native_lapic_create(u_int apic_id, int boot_cpu)
{
	int i;

	if (apic_id > max_apic_id) {
		printf("APIC: Ignoring local APIC with ID %d\n", apic_id);
		if (boot_cpu)
			panic("Can't ignore BSP");
		return;
	}
	KASSERT(!lapics[apic_id].la_present, ("duplicate local APIC %u",
	    apic_id));

	/*
	 * Assume no local LVT overrides and a cluster of 0 and
	 * intra-cluster ID of 0.
	 */
	lapics[apic_id].la_present = 1;
	lapics[apic_id].la_id = apic_id;
	for (i = 0; i <= APIC_LVT_MAX; i++) {
		lapics[apic_id].la_lvts[i] = lvts[i];
		lapics[apic_id].la_lvts[i].lvt_active = 0;
	}
	for (i = 0; i <= APIC_ELVT_MAX; i++) {
		lapics[apic_id].la_elvts[i] = elvts[i];
		lapics[apic_id].la_elvts[i].lvt_active = 0;
	}
	for (i = 0; i <= APIC_NUM_IOINTS; i++)
	    lapics[apic_id].la_ioint_irqs[i] = IRQ_FREE;
	lapics[apic_id].la_ioint_irqs[IDT_SYSCALL - APIC_IO_INTS] = IRQ_SYSCALL;
	lapics[apic_id].la_ioint_irqs[APIC_TIMER_INT - APIC_IO_INTS] =
	    IRQ_TIMER;
#ifdef KDTRACE_HOOKS
	lapics[apic_id].la_ioint_irqs[IDT_DTRACE_RET - APIC_IO_INTS] =
	    IRQ_DTRACE_RET;
#endif
#ifdef XENHVM
	lapics[apic_id].la_ioint_irqs[IDT_EVTCHN - APIC_IO_INTS] = IRQ_EVTCHN;
#endif


#ifdef SMP
	cpu_add(apic_id, boot_cpu);
#endif
}

static inline uint32_t
amd_read_ext_features(void)
{
	uint32_t version;

	if (cpu_vendor_id != CPU_VENDOR_AMD)
		return (0);
	version = lapic_read32(LAPIC_VERSION);
	if ((version & APIC_VER_AMD_EXT_SPACE) != 0)
		return (lapic_read32(LAPIC_EXT_FEATURES));
	else
		return (0);
}

static inline uint32_t
amd_read_elvt_count(void)
{
	uint32_t extf;
	uint32_t count;

	extf = amd_read_ext_features();
	count = (extf & APIC_EXTF_ELVT_MASK) >> APIC_EXTF_ELVT_SHIFT;
	count = min(count, APIC_ELVT_MAX + 1);
	return (count);
}

/*
 * Dump contents of local APIC registers
 */
static void
native_lapic_dump(const char* str)
{
	uint32_t version;
	uint32_t maxlvt;
	uint32_t extf;
	int elvt_count;
	int i;

	version = lapic_read32(LAPIC_VERSION);
	maxlvt = (version & APIC_VER_MAXLVT) >> MAXLVTSHIFT;
	printf("cpu%d %s:\n", PCPU_GET(cpuid), str);
	printf("     ID: 0x%08x   VER: 0x%08x LDR: 0x%08x DFR: 0x%08x",
	    lapic_read32(LAPIC_ID), version,
	    lapic_read32(LAPIC_LDR), x2apic_mode ? 0 : lapic_read32(LAPIC_DFR));
	if ((cpu_feature2 & CPUID2_X2APIC) != 0)
		printf(" x2APIC: %d", x2apic_mode);
	printf("\n  lint0: 0x%08x lint1: 0x%08x TPR: 0x%08x SVR: 0x%08x\n",
	    lapic_read32(LAPIC_LVT_LINT0), lapic_read32(LAPIC_LVT_LINT1),
	    lapic_read32(LAPIC_TPR), lapic_read32(LAPIC_SVR));
	printf("  timer: 0x%08x therm: 0x%08x err: 0x%08x",
	    lapic_read32(LAPIC_LVT_TIMER), lapic_read32(LAPIC_LVT_THERMAL),
	    lapic_read32(LAPIC_LVT_ERROR));
	if (maxlvt >= APIC_LVT_PMC)
		printf(" pmc: 0x%08x", lapic_read32(LAPIC_LVT_PCINT));
	printf("\n");
	if (maxlvt >= APIC_LVT_CMCI)
		printf("   cmci: 0x%08x\n", lapic_read32(LAPIC_LVT_CMCI));
	extf = amd_read_ext_features();
	if (extf != 0) {
		printf("   AMD ext features: 0x%08x\n", extf);
		elvt_count = amd_read_elvt_count();
		for (i = 0; i < elvt_count; i++)
			printf("   AMD elvt%d: 0x%08x\n", i,
			    lapic_read32(LAPIC_EXT_LVT0 + i));
	}
}

static void
native_lapic_xapic_mode(void)
{
	register_t saveintr;

	saveintr = intr_disable();
	if (x2apic_mode)
		native_lapic_enable_x2apic();
	intr_restore(saveintr);
}

static void
native_lapic_setup(int boot)
{
	struct lapic *la;
	uint32_t version;
	uint32_t maxlvt;
	register_t saveintr;
	int elvt_count;
	int i;

	saveintr = intr_disable();

	la = &lapics[lapic_id()];
	KASSERT(la->la_present, ("missing APIC structure"));
	version = lapic_read32(LAPIC_VERSION);
	maxlvt = (version & APIC_VER_MAXLVT) >> MAXLVTSHIFT;

	/* Initialize the TPR to allow all interrupts. */
	lapic_set_tpr(0);

	/* Setup spurious vector and enable the local APIC. */
	lapic_enable();

	/* Program LINT[01] LVT entries. */
	lapic_write32(LAPIC_LVT_LINT0, lvt_mode(la, APIC_LVT_LINT0,
	    lapic_read32(LAPIC_LVT_LINT0)));
	lapic_write32(LAPIC_LVT_LINT1, lvt_mode(la, APIC_LVT_LINT1,
	    lapic_read32(LAPIC_LVT_LINT1)));

	/* Program the PMC LVT entry if present. */
	if (maxlvt >= APIC_LVT_PMC) {
		lapic_write32(LAPIC_LVT_PCINT, lvt_mode(la, APIC_LVT_PMC,
		    LAPIC_LVT_PCINT));
	}

	/* Program timer LVT. */
	la->lvt_timer_base = lvt_mode(la, APIC_LVT_TIMER,
	    lapic_read32(LAPIC_LVT_TIMER));
	la->lvt_timer_last = la->lvt_timer_base;
	lapic_write32(LAPIC_LVT_TIMER, la->lvt_timer_base);

	/* Calibrate the timer parameters using BSP. */
	if (boot && IS_BSP()) {
		lapic_calibrate_initcount(la);
		if (lapic_timer_tsc_deadline)
			lapic_calibrate_deadline(la);
	}

	/* Setup the timer if configured. */
	if (la->la_timer_mode != LAT_MODE_UNDEF) {
		KASSERT(la->la_timer_period != 0, ("lapic%u: zero divisor",
		    lapic_id()));
		switch (la->la_timer_mode) {
		case LAT_MODE_PERIODIC:
			lapic_timer_set_divisor(lapic_timer_divisor);
			lapic_timer_periodic(la);
			break;
		case LAT_MODE_ONESHOT:
			lapic_timer_set_divisor(lapic_timer_divisor);
			lapic_timer_oneshot(la);
			break;
		case LAT_MODE_DEADLINE:
			lapic_timer_deadline(la);
			break;
		default:
			panic("corrupted la_timer_mode %p %d", la,
			    la->la_timer_mode);
		}
	}

	/* Program error LVT and clear any existing errors. */
	lapic_write32(LAPIC_LVT_ERROR, lvt_mode(la, APIC_LVT_ERROR,
	    lapic_read32(LAPIC_LVT_ERROR)));
	lapic_write32(LAPIC_ESR, 0);

	/* XXX: Thermal LVT */

	/* Program the CMCI LVT entry if present. */
	if (maxlvt >= APIC_LVT_CMCI) {
		lapic_write32(LAPIC_LVT_CMCI, lvt_mode(la, APIC_LVT_CMCI,
		    lapic_read32(LAPIC_LVT_CMCI)));
	}

	elvt_count = amd_read_elvt_count();
	for (i = 0; i < elvt_count; i++) {
		if (la->la_elvts[i].lvt_active)
			lapic_write32(LAPIC_EXT_LVT0 + i,
			    elvt_mode(la, i, lapic_read32(LAPIC_EXT_LVT0 + i)));
	}

	intr_restore(saveintr);
}

static void
native_lapic_intrcnt(void *dummy __unused)
{
	struct pcpu *pc;
	struct lapic *la;
	char buf[MAXCOMLEN + 1];

	/* If there are no APICs, skip this function. */
	if (lapics == NULL)
		return;

	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
		la = &lapics[pc->pc_apic_id];
		if (!la->la_present)
		    continue;

		snprintf(buf, sizeof(buf), "cpu%d:timer", pc->pc_cpuid);
		intrcnt_add(buf, &la->la_timer_count);
	}
}
SYSINIT(native_lapic_intrcnt, SI_SUB_INTR, SI_ORDER_MIDDLE, native_lapic_intrcnt,
    NULL);

static void
native_lapic_reenable_pmc(void)
{
#ifdef HWPMC_HOOKS
	uint32_t value;

	value = lapic_read32(LAPIC_LVT_PCINT);
	value &= ~APIC_LVT_M;
	lapic_write32(LAPIC_LVT_PCINT, value);
#endif
}

#ifdef HWPMC_HOOKS
static void
lapic_update_pmc(void *dummy)
{
	struct lapic *la;

	la = &lapics[lapic_id()];
	lapic_write32(LAPIC_LVT_PCINT, lvt_mode(la, APIC_LVT_PMC,
	    lapic_read32(LAPIC_LVT_PCINT)));
}
#endif

static int
native_lapic_enable_pmc(void)
{
#ifdef HWPMC_HOOKS
	u_int32_t maxlvt;

	/* Fail if the local APIC is not present. */
	if (!x2apic_mode && lapic_map == NULL)
		return (0);

	/* Fail if the PMC LVT is not present. */
	maxlvt = (lapic_read32(LAPIC_VERSION) & APIC_VER_MAXLVT) >> MAXLVTSHIFT;
	if (maxlvt < APIC_LVT_PMC)
		return (0);

	lvts[APIC_LVT_PMC].lvt_masked = 0;

#ifdef EARLY_AP_STARTUP
	MPASS(mp_ncpus == 1 || smp_started);
	smp_rendezvous(NULL, lapic_update_pmc, NULL, NULL);
#else
#ifdef SMP
	/*
	 * If hwpmc was loaded at boot time then the APs may not be
	 * started yet.  In that case, don't forward the request to
	 * them as they will program the lvt when they start.
	 */
	if (smp_started)
		smp_rendezvous(NULL, lapic_update_pmc, NULL, NULL);
	else
#endif
		lapic_update_pmc(NULL);
#endif
	return (1);
#else
	return (0);
#endif
}

static void
native_lapic_disable_pmc(void)
{
#ifdef HWPMC_HOOKS
	u_int32_t maxlvt;

	/* Fail if the local APIC is not present. */
	if (!x2apic_mode && lapic_map == NULL)
		return;

	/* Fail if the PMC LVT is not present. */
	maxlvt = (lapic_read32(LAPIC_VERSION) & APIC_VER_MAXLVT) >> MAXLVTSHIFT;
	if (maxlvt < APIC_LVT_PMC)
		return;

	lvts[APIC_LVT_PMC].lvt_masked = 1;

#ifdef SMP
	/* The APs should always be started when hwpmc is unloaded. */
	KASSERT(mp_ncpus == 1 || smp_started, ("hwpmc unloaded too early"));
#endif
	smp_rendezvous(NULL, lapic_update_pmc, NULL, NULL);
#endif
}

static void
lapic_calibrate_initcount(struct lapic *la)
{
	u_long value;

	/* Start off with a divisor of 2 (power on reset default). */
	lapic_timer_divisor = 2;
	/* Try to calibrate the local APIC timer. */
	do {
		lapic_timer_set_divisor(lapic_timer_divisor);
		lapic_timer_oneshot_nointr(la, APIC_TIMER_MAX_COUNT);
		DELAY(1000000);
		value = APIC_TIMER_MAX_COUNT - lapic_read32(LAPIC_CCR_TIMER);
		if (value != APIC_TIMER_MAX_COUNT)
			break;
		lapic_timer_divisor <<= 1;
	} while (lapic_timer_divisor <= 128);
	if (lapic_timer_divisor > 128)
		panic("lapic: Divisor too big");
	if (bootverbose) {
		printf("lapic: Divisor %lu, Frequency %lu Hz\n",
		    lapic_timer_divisor, value);
	}
	count_freq = value;
}

static void
lapic_calibrate_deadline(struct lapic *la __unused)
{

	if (bootverbose) {
		printf("lapic: deadline tsc mode, Frequency %ju Hz\n",
		    (uintmax_t)tsc_freq);
	}
}

static void
lapic_change_mode(struct eventtimer *et, struct lapic *la,
    enum lat_timer_mode newmode)
{

	if (la->la_timer_mode == newmode)
		return;
	switch (newmode) {
	case LAT_MODE_PERIODIC:
		lapic_timer_set_divisor(lapic_timer_divisor);
		et->et_frequency = count_freq;
		break;
	case LAT_MODE_DEADLINE:
		et->et_frequency = tsc_freq;
		break;
	case LAT_MODE_ONESHOT:
		lapic_timer_set_divisor(lapic_timer_divisor);
		et->et_frequency = count_freq;
		break;
	default:
		panic("lapic_change_mode %d", newmode);
	}
	la->la_timer_mode = newmode;
	et->et_min_period = (0x00000002LLU << 32) / et->et_frequency;
	et->et_max_period = (0xfffffffeLLU << 32) / et->et_frequency;
}

static int
lapic_et_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct lapic *la;

	la = &lapics[PCPU_GET(apic_id)];
	if (period != 0) {
		lapic_change_mode(et, la, LAT_MODE_PERIODIC);
		la->la_timer_period = ((uint32_t)et->et_frequency * period) >>
		    32;
		lapic_timer_periodic(la);
	} else if (lapic_timer_tsc_deadline) {
		lapic_change_mode(et, la, LAT_MODE_DEADLINE);
		la->la_timer_period = (et->et_frequency * first) >> 32;
		lapic_timer_deadline(la);
	} else {
		lapic_change_mode(et, la, LAT_MODE_ONESHOT);
		la->la_timer_period = ((uint32_t)et->et_frequency * first) >>
		    32;
		lapic_timer_oneshot(la);
	}
	return (0);
}

static int
lapic_et_stop(struct eventtimer *et)
{
	struct lapic *la;

	la = &lapics[PCPU_GET(apic_id)];
	lapic_timer_stop(la);
	la->la_timer_mode = LAT_MODE_UNDEF;
	return (0);
}

static void
native_lapic_disable(void)
{
	uint32_t value;

	/* Software disable the local APIC. */
	value = lapic_read32(LAPIC_SVR);
	value &= ~APIC_SVR_SWEN;
	lapic_write32(LAPIC_SVR, value);
}

static void
lapic_enable(void)
{
	uint32_t value;

	/* Program the spurious vector to enable the local APIC. */
	value = lapic_read32(LAPIC_SVR);
	value &= ~(APIC_SVR_VECTOR | APIC_SVR_FOCUS);
	value |= APIC_SVR_FEN | APIC_SVR_SWEN | APIC_SPURIOUS_INT;
	if (lapic_eoi_suppression)
		value |= APIC_SVR_EOI_SUPPRESSION;
	lapic_write32(LAPIC_SVR, value);
}

/* Reset the local APIC on the BSP during resume. */
static void
lapic_resume(struct pic *pic, bool suspend_cancelled)
{

	lapic_setup(0);
}

static int
native_lapic_id(void)
{
	uint32_t v;

	KASSERT(x2apic_mode || lapic_map != NULL, ("local APIC is not mapped"));
	v = lapic_read32(LAPIC_ID);
	if (!x2apic_mode)
		v >>= APIC_ID_SHIFT;
	return (v);
}

static int
native_lapic_intr_pending(u_int vector)
{
	uint32_t irr;

	/*
	 * The IRR registers are an array of registers each of which
	 * only describes 32 interrupts in the low 32 bits.  Thus, we
	 * divide the vector by 32 to get the register index.
	 * Finally, we modulus the vector by 32 to determine the
	 * individual bit to test.
	 */
	irr = lapic_read32(LAPIC_IRR0 + vector / 32);
	return (irr & 1 << (vector % 32));
}

static void
native_lapic_set_logical_id(u_int apic_id, u_int cluster, u_int cluster_id)
{
	struct lapic *la;

	KASSERT(lapics[apic_id].la_present, ("%s: APIC %u doesn't exist",
	    __func__, apic_id));
	KASSERT(cluster <= APIC_MAX_CLUSTER, ("%s: cluster %u too big",
	    __func__, cluster));
	KASSERT(cluster_id <= APIC_MAX_INTRACLUSTER_ID,
	    ("%s: intra cluster id %u too big", __func__, cluster_id));
	la = &lapics[apic_id];
	la->la_cluster = cluster;
	la->la_cluster_id = cluster_id;
}

static int
native_lapic_set_lvt_mask(u_int apic_id, u_int pin, u_char masked)
{

	if (pin > APIC_LVT_MAX)
		return (EINVAL);
	if (apic_id == APIC_ID_ALL) {
		lvts[pin].lvt_masked = masked;
		if (bootverbose)
			printf("lapic:");
	} else {
		KASSERT(lapics[apic_id].la_present,
		    ("%s: missing APIC %u", __func__, apic_id));
		lapics[apic_id].la_lvts[pin].lvt_masked = masked;
		lapics[apic_id].la_lvts[pin].lvt_active = 1;
		if (bootverbose)
			printf("lapic%u:", apic_id);
	}
	if (bootverbose)
		printf(" LINT%u %s\n", pin, masked ? "masked" : "unmasked");
	return (0);
}

static int
native_lapic_set_lvt_mode(u_int apic_id, u_int pin, u_int32_t mode)
{
	struct lvt *lvt;

	if (pin > APIC_LVT_MAX)
		return (EINVAL);
	if (apic_id == APIC_ID_ALL) {
		lvt = &lvts[pin];
		if (bootverbose)
			printf("lapic:");
	} else {
		KASSERT(lapics[apic_id].la_present,
		    ("%s: missing APIC %u", __func__, apic_id));
		lvt = &lapics[apic_id].la_lvts[pin];
		lvt->lvt_active = 1;
		if (bootverbose)
			printf("lapic%u:", apic_id);
	}
	lvt->lvt_mode = mode;
	switch (mode) {
	case APIC_LVT_DM_NMI:
	case APIC_LVT_DM_SMI:
	case APIC_LVT_DM_INIT:
	case APIC_LVT_DM_EXTINT:
		lvt->lvt_edgetrigger = 1;
		lvt->lvt_activehi = 1;
		if (mode == APIC_LVT_DM_EXTINT)
			lvt->lvt_masked = 1;
		else
			lvt->lvt_masked = 0;
		break;
	default:
		panic("Unsupported delivery mode: 0x%x\n", mode);
	}
	if (bootverbose) {
		printf(" Routing ");
		switch (mode) {
		case APIC_LVT_DM_NMI:
			printf("NMI");
			break;
		case APIC_LVT_DM_SMI:
			printf("SMI");
			break;
		case APIC_LVT_DM_INIT:
			printf("INIT");
			break;
		case APIC_LVT_DM_EXTINT:
			printf("ExtINT");
			break;
		}
		printf(" -> LINT%u\n", pin);
	}
	return (0);
}

static int
native_lapic_set_lvt_polarity(u_int apic_id, u_int pin, enum intr_polarity pol)
{

	if (pin > APIC_LVT_MAX || pol == INTR_POLARITY_CONFORM)
		return (EINVAL);
	if (apic_id == APIC_ID_ALL) {
		lvts[pin].lvt_activehi = (pol == INTR_POLARITY_HIGH);
		if (bootverbose)
			printf("lapic:");
	} else {
		KASSERT(lapics[apic_id].la_present,
		    ("%s: missing APIC %u", __func__, apic_id));
		lapics[apic_id].la_lvts[pin].lvt_active = 1;
		lapics[apic_id].la_lvts[pin].lvt_activehi =
		    (pol == INTR_POLARITY_HIGH);
		if (bootverbose)
			printf("lapic%u:", apic_id);
	}
	if (bootverbose)
		printf(" LINT%u polarity: %s\n", pin,
		    pol == INTR_POLARITY_HIGH ? "high" : "low");
	return (0);
}

static int
native_lapic_set_lvt_triggermode(u_int apic_id, u_int pin,
     enum intr_trigger trigger)
{

	if (pin > APIC_LVT_MAX || trigger == INTR_TRIGGER_CONFORM)
		return (EINVAL);
	if (apic_id == APIC_ID_ALL) {
		lvts[pin].lvt_edgetrigger = (trigger == INTR_TRIGGER_EDGE);
		if (bootverbose)
			printf("lapic:");
	} else {
		KASSERT(lapics[apic_id].la_present,
		    ("%s: missing APIC %u", __func__, apic_id));
		lapics[apic_id].la_lvts[pin].lvt_edgetrigger =
		    (trigger == INTR_TRIGGER_EDGE);
		lapics[apic_id].la_lvts[pin].lvt_active = 1;
		if (bootverbose)
			printf("lapic%u:", apic_id);
	}
	if (bootverbose)
		printf(" LINT%u trigger: %s\n", pin,
		    trigger == INTR_TRIGGER_EDGE ? "edge" : "level");
	return (0);
}

/*
 * Adjust the TPR of the current CPU so that it blocks all interrupts below
 * the passed in vector.
 */
static void
lapic_set_tpr(u_int vector)
{
#ifdef CHEAP_TPR
	lapic_write32(LAPIC_TPR, vector);
#else
	uint32_t tpr;

	tpr = lapic_read32(LAPIC_TPR) & ~APIC_TPR_PRIO;
	tpr |= vector;
	lapic_write32(LAPIC_TPR, tpr);
#endif
}

static void
native_lapic_eoi(void)
{

	lapic_write32_nofence(LAPIC_EOI, 0);
}

void
lapic_handle_intr(int vector, struct trapframe *frame)
{
	struct intsrc *isrc;

	isrc = intr_lookup_source(apic_idt_to_irq(PCPU_GET(apic_id),
	    vector));
	intr_execute_handlers(isrc, frame);
}

void
lapic_handle_timer(struct trapframe *frame)
{
	struct lapic *la;
	struct trapframe *oldframe;
	struct thread *td;

	/* Send EOI first thing. */
	lapic_eoi();

#if defined(SMP) && !defined(SCHED_ULE)
	/*
	 * Don't do any accounting for the disabled HTT cores, since it
	 * will provide misleading numbers for the userland.
	 *
	 * No locking is necessary here, since even if we lose the race
	 * when hlt_cpus_mask changes it is not a big deal, really.
	 *
	 * Don't do that for ULE, since ULE doesn't consider hlt_cpus_mask
	 * and unlike other schedulers it actually schedules threads to
	 * those CPUs.
	 */
	if (CPU_ISSET(PCPU_GET(cpuid), &hlt_cpus_mask))
		return;
#endif

	/* Look up our local APIC structure for the tick counters. */
	la = &lapics[PCPU_GET(apic_id)];
	(*la->la_timer_count)++;
	critical_enter();
	if (lapic_et.et_active) {
		td = curthread;
		td->td_intr_nesting_level++;
		oldframe = td->td_intr_frame;
		td->td_intr_frame = frame;
		lapic_et.et_event_cb(&lapic_et, lapic_et.et_arg);
		td->td_intr_frame = oldframe;
		td->td_intr_nesting_level--;
	}
	critical_exit();
}

static void
lapic_timer_set_divisor(u_int divisor)
{

	KASSERT(powerof2(divisor), ("lapic: invalid divisor %u", divisor));
	KASSERT(ffs(divisor) <= nitems(lapic_timer_divisors),
		("lapic: invalid divisor %u", divisor));
	lapic_write32(LAPIC_DCR_TIMER, lapic_timer_divisors[ffs(divisor) - 1]);
}

static void
lapic_timer_oneshot(struct lapic *la)
{
	uint32_t value;

	value = la->lvt_timer_base;
	value &= ~(APIC_LVTT_TM | APIC_LVT_M);
	value |= APIC_LVTT_TM_ONE_SHOT;
	la->lvt_timer_last = value;
	lapic_write32(LAPIC_LVT_TIMER, value);
	lapic_write32(LAPIC_ICR_TIMER, la->la_timer_period);
}

static void
lapic_timer_oneshot_nointr(struct lapic *la, uint32_t count)
{
	uint32_t value;

	value = la->lvt_timer_base;
	value &= ~APIC_LVTT_TM;
	value |= APIC_LVTT_TM_ONE_SHOT | APIC_LVT_M;
	la->lvt_timer_last = value;
	lapic_write32(LAPIC_LVT_TIMER, value);
	lapic_write32(LAPIC_ICR_TIMER, count);
}

static void
lapic_timer_periodic(struct lapic *la)
{
	uint32_t value;

	value = la->lvt_timer_base;
	value &= ~(APIC_LVTT_TM | APIC_LVT_M);
	value |= APIC_LVTT_TM_PERIODIC;
	la->lvt_timer_last = value;
	lapic_write32(LAPIC_LVT_TIMER, value);
	lapic_write32(LAPIC_ICR_TIMER, la->la_timer_period);
}

static void
lapic_timer_deadline(struct lapic *la)
{
	uint32_t value;

	value = la->lvt_timer_base;
	value &= ~(APIC_LVTT_TM | APIC_LVT_M);
	value |= APIC_LVTT_TM_TSCDLT;
	if (value != la->lvt_timer_last) {
		la->lvt_timer_last = value;
		lapic_write32_nofence(LAPIC_LVT_TIMER, value);
		if (!x2apic_mode)
			mfence();
	}
	wrmsr(MSR_TSC_DEADLINE, la->la_timer_period + rdtsc());
}

static void
lapic_timer_stop(struct lapic *la)
{
	uint32_t value;

	if (la->la_timer_mode == LAT_MODE_DEADLINE) {
		wrmsr(MSR_TSC_DEADLINE, 0);
		mfence();
	} else {
		value = la->lvt_timer_base;
		value &= ~APIC_LVTT_TM;
		value |= APIC_LVT_M;
		la->lvt_timer_last = value;
		lapic_write32(LAPIC_LVT_TIMER, value);
	}
}

void
lapic_handle_cmc(void)
{

	lapic_eoi();
	cmc_intr();
}

/*
 * Called from the mca_init() to activate the CMC interrupt if this CPU is
 * responsible for monitoring any MC banks for CMC events.  Since mca_init()
 * is called prior to lapic_setup() during boot, this just needs to unmask
 * this CPU's LVT_CMCI entry.
 */
static void
native_lapic_enable_cmc(void)
{
	u_int apic_id;

#ifdef DEV_ATPIC
	if (!x2apic_mode && lapic_map == NULL)
		return;
#endif
	apic_id = PCPU_GET(apic_id);
	KASSERT(lapics[apic_id].la_present,
	    ("%s: missing APIC %u", __func__, apic_id));
	lapics[apic_id].la_lvts[APIC_LVT_CMCI].lvt_masked = 0;
	lapics[apic_id].la_lvts[APIC_LVT_CMCI].lvt_active = 1;
	if (bootverbose)
		printf("lapic%u: CMCI unmasked\n", apic_id);
}

static int
native_lapic_enable_mca_elvt(void)
{
	u_int apic_id;
	uint32_t value;
	int elvt_count;

#ifdef DEV_ATPIC
	if (lapic_map == NULL)
		return (-1);
#endif

	apic_id = PCPU_GET(apic_id);
	KASSERT(lapics[apic_id].la_present,
	    ("%s: missing APIC %u", __func__, apic_id));
	elvt_count = amd_read_elvt_count();
	if (elvt_count <= APIC_ELVT_MCA)
		return (-1);

	value = lapic_read32(LAPIC_EXT_LVT0 + APIC_ELVT_MCA);
	if ((value & APIC_LVT_M) == 0) {
		if (bootverbose)
			printf("AMD MCE Thresholding Extended LVT is already active\n");
		return (APIC_ELVT_MCA);
	}
	lapics[apic_id].la_elvts[APIC_ELVT_MCA].lvt_masked = 0;
	lapics[apic_id].la_elvts[APIC_ELVT_MCA].lvt_active = 1;
	if (bootverbose)
		printf("lapic%u: MCE Thresholding ELVT unmasked\n", apic_id);
	return (APIC_ELVT_MCA);
}

void
lapic_handle_error(void)
{
	uint32_t esr;

	/*
	 * Read the contents of the error status register.  Write to
	 * the register first before reading from it to force the APIC
	 * to update its value to indicate any errors that have
	 * occurred since the previous write to the register.
	 */
	lapic_write32(LAPIC_ESR, 0);
	esr = lapic_read32(LAPIC_ESR);

	printf("CPU%d: local APIC error 0x%x\n", PCPU_GET(cpuid), esr);
	lapic_eoi();
}

static u_int
native_apic_cpuid(u_int apic_id)
{
#ifdef SMP
	return apic_cpuids[apic_id];
#else
	return 0;
#endif
}

/* Request a free IDT vector to be used by the specified IRQ. */
static u_int
native_apic_alloc_vector(u_int apic_id, u_int irq)
{
	u_int vector;

	KASSERT(irq < num_io_irqs, ("Invalid IRQ %u", irq));

	/*
	 * Search for a free vector.  Currently we just use a very simple
	 * algorithm to find the first free vector.
	 */
	mtx_lock_spin(&icu_lock);
	for (vector = 0; vector < APIC_NUM_IOINTS; vector++) {
		if (lapics[apic_id].la_ioint_irqs[vector] != IRQ_FREE)
			continue;
		lapics[apic_id].la_ioint_irqs[vector] = irq;
		mtx_unlock_spin(&icu_lock);
		return (vector + APIC_IO_INTS);
	}
	mtx_unlock_spin(&icu_lock);
	return (0);
}

/*
 * Request 'count' free contiguous IDT vectors to be used by 'count'
 * IRQs.  'count' must be a power of two and the vectors will be
 * aligned on a boundary of 'align'.  If the request cannot be
 * satisfied, 0 is returned.
 */
static u_int
native_apic_alloc_vectors(u_int apic_id, u_int *irqs, u_int count, u_int align)
{
	u_int first, run, vector;

	KASSERT(powerof2(count), ("bad count"));
	KASSERT(powerof2(align), ("bad align"));
	KASSERT(align >= count, ("align < count"));
#ifdef INVARIANTS
	for (run = 0; run < count; run++)
		KASSERT(irqs[run] < num_io_irqs, ("Invalid IRQ %u at index %u",
		    irqs[run], run));
#endif

	/*
	 * Search for 'count' free vectors.  As with apic_alloc_vector(),
	 * this just uses a simple first fit algorithm.
	 */
	run = 0;
	first = 0;
	mtx_lock_spin(&icu_lock);
	for (vector = 0; vector < APIC_NUM_IOINTS; vector++) {

		/* Vector is in use, end run. */
		if (lapics[apic_id].la_ioint_irqs[vector] != IRQ_FREE) {
			run = 0;
			first = 0;
			continue;
		}

		/* Start a new run if run == 0 and vector is aligned. */
		if (run == 0) {
			if ((vector & (align - 1)) != 0)
				continue;
			first = vector;
		}
		run++;

		/* Keep looping if the run isn't long enough yet. */
		if (run < count)
			continue;

		/* Found a run, assign IRQs and return the first vector. */
		for (vector = 0; vector < count; vector++)
			lapics[apic_id].la_ioint_irqs[first + vector] =
			    irqs[vector];
		mtx_unlock_spin(&icu_lock);
		return (first + APIC_IO_INTS);
	}
	mtx_unlock_spin(&icu_lock);
	printf("APIC: Couldn't find APIC vectors for %u IRQs\n", count);
	return (0);
}

/*
 * Enable a vector for a particular apic_id.  Since all lapics share idt
 * entries and ioint_handlers this enables the vector on all lapics.  lapics
 * which do not have the vector configured would report spurious interrupts
 * should it fire.
 */
static void
native_apic_enable_vector(u_int apic_id, u_int vector)
{

	KASSERT(vector != IDT_SYSCALL, ("Attempt to overwrite syscall entry"));
	KASSERT(ioint_handlers[vector / 32] != NULL,
	    ("No ISR handler for vector %u", vector));
#ifdef KDTRACE_HOOKS
	KASSERT(vector != IDT_DTRACE_RET,
	    ("Attempt to overwrite DTrace entry"));
#endif
	setidt(vector, (pti ? ioint_pti_handlers : ioint_handlers)[vector / 32],
	    SDT_APIC, SEL_KPL, GSEL_APIC);
}

static void
native_apic_disable_vector(u_int apic_id, u_int vector)
{

	KASSERT(vector != IDT_SYSCALL, ("Attempt to overwrite syscall entry"));
#ifdef KDTRACE_HOOKS
	KASSERT(vector != IDT_DTRACE_RET,
	    ("Attempt to overwrite DTrace entry"));
#endif
	KASSERT(ioint_handlers[vector / 32] != NULL,
	    ("No ISR handler for vector %u", vector));
#ifdef notyet
	/*
	 * We can not currently clear the idt entry because other cpus
	 * may have a valid vector at this offset.
	 */
	setidt(vector, pti ? &IDTVEC(rsvd_pti) : &IDTVEC(rsvd), SDT_APIC,
	    SEL_KPL, GSEL_APIC);
#endif
}

/* Release an APIC vector when it's no longer in use. */
static void
native_apic_free_vector(u_int apic_id, u_int vector, u_int irq)
{
	struct thread *td;

	KASSERT(vector >= APIC_IO_INTS && vector != IDT_SYSCALL &&
	    vector <= APIC_IO_INTS + APIC_NUM_IOINTS,
	    ("Vector %u does not map to an IRQ line", vector));
	KASSERT(irq < num_io_irqs, ("Invalid IRQ %u", irq));
	KASSERT(lapics[apic_id].la_ioint_irqs[vector - APIC_IO_INTS] ==
	    irq, ("IRQ mismatch"));
#ifdef KDTRACE_HOOKS
	KASSERT(vector != IDT_DTRACE_RET,
	    ("Attempt to overwrite DTrace entry"));
#endif

	/*
	 * Bind us to the cpu that owned the vector before freeing it so
	 * we don't lose an interrupt delivery race.
	 */
	td = curthread;
	if (!rebooting) {
		thread_lock(td);
		if (sched_is_bound(td))
			panic("apic_free_vector: Thread already bound.\n");
		sched_bind(td, apic_cpuid(apic_id));
		thread_unlock(td);
	}
	mtx_lock_spin(&icu_lock);
	lapics[apic_id].la_ioint_irqs[vector - APIC_IO_INTS] = IRQ_FREE;
	mtx_unlock_spin(&icu_lock);
	if (!rebooting) {
		thread_lock(td);
		sched_unbind(td);
		thread_unlock(td);
	}
}

/* Map an IDT vector (APIC) to an IRQ (interrupt source). */
static u_int
apic_idt_to_irq(u_int apic_id, u_int vector)
{
	int irq;

	KASSERT(vector >= APIC_IO_INTS && vector != IDT_SYSCALL &&
	    vector <= APIC_IO_INTS + APIC_NUM_IOINTS,
	    ("Vector %u does not map to an IRQ line", vector));
#ifdef KDTRACE_HOOKS
	KASSERT(vector != IDT_DTRACE_RET,
	    ("Attempt to overwrite DTrace entry"));
#endif
	irq = lapics[apic_id].la_ioint_irqs[vector - APIC_IO_INTS];
	if (irq < 0)
		irq = 0;
	return (irq);
}

#ifdef DDB
/*
 * Dump data about APIC IDT vector mappings.
 */
DB_SHOW_COMMAND(apic, db_show_apic)
{
	struct intsrc *isrc;
	int i, verbose;
	u_int apic_id;
	u_int irq;

	if (strcmp(modif, "vv") == 0)
		verbose = 2;
	else if (strcmp(modif, "v") == 0)
		verbose = 1;
	else
		verbose = 0;
	for (apic_id = 0; apic_id <= max_apic_id; apic_id++) {
		if (lapics[apic_id].la_present == 0)
			continue;
		db_printf("Interrupts bound to lapic %u\n", apic_id);
		for (i = 0; i < APIC_NUM_IOINTS + 1 && !db_pager_quit; i++) {
			irq = lapics[apic_id].la_ioint_irqs[i];
			if (irq == IRQ_FREE || irq == IRQ_SYSCALL)
				continue;
#ifdef KDTRACE_HOOKS
			if (irq == IRQ_DTRACE_RET)
				continue;
#endif
#ifdef XENHVM
			if (irq == IRQ_EVTCHN)
				continue;
#endif
			db_printf("vec 0x%2x -> ", i + APIC_IO_INTS);
			if (irq == IRQ_TIMER)
				db_printf("lapic timer\n");
			else if (irq < num_io_irqs) {
				isrc = intr_lookup_source(irq);
				if (isrc == NULL || verbose == 0)
					db_printf("IRQ %u\n", irq);
				else
					db_dump_intr_event(isrc->is_event,
					    verbose == 2);
			} else
				db_printf("IRQ %u ???\n", irq);
		}
	}
}

static void
dump_mask(const char *prefix, uint32_t v, int base)
{
	int i, first;

	first = 1;
	for (i = 0; i < 32; i++)
		if (v & (1 << i)) {
			if (first) {
				db_printf("%s:", prefix);
				first = 0;
			}
			db_printf(" %02x", base + i);
		}
	if (!first)
		db_printf("\n");
}

/* Show info from the lapic regs for this CPU. */
DB_SHOW_COMMAND(lapic, db_show_lapic)
{
	uint32_t v;

	db_printf("lapic ID = %d\n", lapic_id());
	v = lapic_read32(LAPIC_VERSION);
	db_printf("version  = %d.%d\n", (v & APIC_VER_VERSION) >> 4,
	    v & 0xf);
	db_printf("max LVT  = %d\n", (v & APIC_VER_MAXLVT) >> MAXLVTSHIFT);
	v = lapic_read32(LAPIC_SVR);
	db_printf("SVR      = %02x (%s)\n", v & APIC_SVR_VECTOR,
	    v & APIC_SVR_ENABLE ? "enabled" : "disabled");
	db_printf("TPR      = %02x\n", lapic_read32(LAPIC_TPR));

#define dump_field(prefix, regn, index)					\
	dump_mask(__XSTRING(prefix ## index), 				\
	    lapic_read32(LAPIC_ ## regn ## index),			\
	    index * 32)

	db_printf("In-service Interrupts:\n");
	dump_field(isr, ISR, 0);
	dump_field(isr, ISR, 1);
	dump_field(isr, ISR, 2);
	dump_field(isr, ISR, 3);
	dump_field(isr, ISR, 4);
	dump_field(isr, ISR, 5);
	dump_field(isr, ISR, 6);
	dump_field(isr, ISR, 7);

	db_printf("TMR Interrupts:\n");
	dump_field(tmr, TMR, 0);
	dump_field(tmr, TMR, 1);
	dump_field(tmr, TMR, 2);
	dump_field(tmr, TMR, 3);
	dump_field(tmr, TMR, 4);
	dump_field(tmr, TMR, 5);
	dump_field(tmr, TMR, 6);
	dump_field(tmr, TMR, 7);

	db_printf("IRR Interrupts:\n");
	dump_field(irr, IRR, 0);
	dump_field(irr, IRR, 1);
	dump_field(irr, IRR, 2);
	dump_field(irr, IRR, 3);
	dump_field(irr, IRR, 4);
	dump_field(irr, IRR, 5);
	dump_field(irr, IRR, 6);
	dump_field(irr, IRR, 7);

#undef dump_field
}
#endif

/*
 * APIC probing support code.  This includes code to manage enumerators.
 */

static SLIST_HEAD(, apic_enumerator) enumerators =
	SLIST_HEAD_INITIALIZER(enumerators);
static struct apic_enumerator *best_enum;

void
apic_register_enumerator(struct apic_enumerator *enumerator)
{
#ifdef INVARIANTS
	struct apic_enumerator *apic_enum;

	SLIST_FOREACH(apic_enum, &enumerators, apic_next) {
		if (apic_enum == enumerator)
			panic("%s: Duplicate register of %s", __func__,
			    enumerator->apic_name);
	}
#endif
	SLIST_INSERT_HEAD(&enumerators, enumerator, apic_next);
}

/*
 * We have to look for CPU's very, very early because certain subsystems
 * want to know how many CPU's we have extremely early on in the boot
 * process.
 */
static void
apic_init(void *dummy __unused)
{
	struct apic_enumerator *enumerator;
	int retval, best;

	/* We only support built in local APICs. */
	if (!(cpu_feature & CPUID_APIC))
		return;

	/* Don't probe if APIC mode is disabled. */
	if (resource_disabled("apic", 0))
		return;

	/* Probe all the enumerators to find the best match. */
	best_enum = NULL;
	best = 0;
	SLIST_FOREACH(enumerator, &enumerators, apic_next) {
		retval = enumerator->apic_probe();
		if (retval > 0)
			continue;
		if (best_enum == NULL || best < retval) {
			best_enum = enumerator;
			best = retval;
		}
	}
	if (best_enum == NULL) {
		if (bootverbose)
			printf("APIC: Could not find any APICs.\n");
#ifndef DEV_ATPIC
		panic("running without device atpic requires a local APIC");
#endif
		return;
	}

	if (bootverbose)
		printf("APIC: Using the %s enumerator.\n",
		    best_enum->apic_name);

#ifdef I686_CPU
	/*
	 * To work around an errata, we disable the local APIC on some
	 * CPUs during early startup.  We need to turn the local APIC back
	 * on on such CPUs now.
	 */
	ppro_reenable_apic();
#endif

	/* Probe the CPU's in the system. */
	retval = best_enum->apic_probe_cpus();
	if (retval != 0)
		printf("%s: Failed to probe CPUs: returned %d\n",
		    best_enum->apic_name, retval);

}
SYSINIT(apic_init, SI_SUB_TUNABLES - 1, SI_ORDER_SECOND, apic_init, NULL);

/*
 * Setup the local APIC.  We have to do this prior to starting up the APs
 * in the SMP case.
 */
static void
apic_setup_local(void *dummy __unused)
{
	int retval;

	if (best_enum == NULL)
		return;

	lapics = malloc(sizeof(*lapics) * (max_apic_id + 1), M_LAPIC,
	    M_WAITOK | M_ZERO);

	/* Initialize the local APIC. */
	retval = best_enum->apic_setup_local();
	if (retval != 0)
		printf("%s: Failed to setup the local APIC: returned %d\n",
		    best_enum->apic_name, retval);
}
SYSINIT(apic_setup_local, SI_SUB_CPU, SI_ORDER_SECOND, apic_setup_local, NULL);

/*
 * Setup the I/O APICs.
 */
static void
apic_setup_io(void *dummy __unused)
{
	int retval;

	if (best_enum == NULL)
		return;

	/*
	 * Local APIC must be registered before other PICs and pseudo PICs
	 * for proper suspend/resume order.
	 */
	intr_register_pic(&lapic_pic);

	retval = best_enum->apic_setup_io();
	if (retval != 0)
		printf("%s: Failed to setup I/O APICs: returned %d\n",
		    best_enum->apic_name, retval);

	/*
	 * Finish setting up the local APIC on the BSP once we know
	 * how to properly program the LINT pins.  In particular, this
	 * enables the EOI suppression mode, if LAPIC supports it and
	 * user did not disable the mode.
	 */
	lapic_setup(1);
	if (bootverbose)
		lapic_dump("BSP");

	/* Enable the MSI "pic". */
	init_ops.msi_init();

#ifdef XENHVM
	xen_intr_alloc_irqs();
#endif
}
SYSINIT(apic_setup_io, SI_SUB_INTR, SI_ORDER_THIRD, apic_setup_io, NULL);

#ifdef SMP
/*
 * Inter Processor Interrupt functions.  The lapic_ipi_*() functions are
 * private to the MD code.  The public interface for the rest of the
 * kernel is defined in mp_machdep.c.
 */

/*
 * Wait delay microseconds for IPI to be sent.  If delay is -1, we
 * wait forever.
 */
static int
native_lapic_ipi_wait(int delay)
{
	uint64_t rx;

	/* LAPIC_ICR.APIC_DELSTAT_MASK is undefined in x2APIC mode */
	if (x2apic_mode)
		return (1);

	for (rx = 0; delay == -1 || rx < lapic_ipi_wait_mult * delay; rx++) {
		if ((lapic_read_icr_lo() & APIC_DELSTAT_MASK) ==
		    APIC_DELSTAT_IDLE)
			return (1);
		ia32_pause();
	}
	return (0);
}

static void
native_lapic_ipi_raw(register_t icrlo, u_int dest)
{
	uint64_t icr;
	uint32_t vhi, vlo;
	register_t saveintr;

	/* XXX: Need more sanity checking of icrlo? */
	KASSERT(x2apic_mode || lapic_map != NULL,
	    ("%s called too early", __func__));
	KASSERT(x2apic_mode ||
	    (dest & ~(APIC_ID_MASK >> APIC_ID_SHIFT)) == 0,
	    ("%s: invalid dest field", __func__));
	KASSERT((icrlo & APIC_ICRLO_RESV_MASK) == 0,
	    ("%s: reserved bits set in ICR LO register", __func__));

	/* Set destination in ICR HI register if it is being used. */
	if (!x2apic_mode) {
		saveintr = intr_disable();
		icr = lapic_read_icr();
	}

	if ((icrlo & APIC_DEST_MASK) == APIC_DEST_DESTFLD) {
		if (x2apic_mode) {
			vhi = dest;
		} else {
			vhi = icr >> 32;
			vhi &= ~APIC_ID_MASK;
			vhi |= dest << APIC_ID_SHIFT;
		}
	} else {
		vhi = 0;
	}

	/* Program the contents of the IPI and dispatch it. */
	if (x2apic_mode) {
		vlo = icrlo;
	} else {
		vlo = icr;
		vlo &= APIC_ICRLO_RESV_MASK;
		vlo |= icrlo;
	}
	lapic_write_icr(vhi, vlo);
	if (!x2apic_mode)
		intr_restore(saveintr);
}

#define	BEFORE_SPIN	50000
#ifdef DETECT_DEADLOCK
#define	AFTER_SPIN	50
#endif

static void
native_lapic_ipi_vectored(u_int vector, int dest)
{
	register_t icrlo, destfield;

	KASSERT((vector & ~APIC_VECTOR_MASK) == 0,
	    ("%s: invalid vector %d", __func__, vector));

	icrlo = APIC_DESTMODE_PHY | APIC_TRIGMOD_EDGE | APIC_LEVEL_ASSERT;

	/*
	 * NMI IPIs are just fake vectors used to send a NMI.  Use special rules
	 * regarding NMIs if passed, otherwise specify the vector.
	 */
	if (vector >= IPI_NMI_FIRST)
		icrlo |= APIC_DELMODE_NMI;
	else
		icrlo |= vector | APIC_DELMODE_FIXED;
	destfield = 0;
	switch (dest) {
	case APIC_IPI_DEST_SELF:
		icrlo |= APIC_DEST_SELF;
		break;
	case APIC_IPI_DEST_ALL:
		icrlo |= APIC_DEST_ALLISELF;
		break;
	case APIC_IPI_DEST_OTHERS:
		icrlo |= APIC_DEST_ALLESELF;
		break;
	default:
		KASSERT(x2apic_mode ||
		    (dest & ~(APIC_ID_MASK >> APIC_ID_SHIFT)) == 0,
		    ("%s: invalid destination 0x%x", __func__, dest));
		destfield = dest;
	}

	/* Wait for an earlier IPI to finish. */
	if (!lapic_ipi_wait(BEFORE_SPIN)) {
		if (panicstr != NULL)
			return;
		else
			panic("APIC: Previous IPI is stuck");
	}

	lapic_ipi_raw(icrlo, destfield);

#ifdef DETECT_DEADLOCK
	/* Wait for IPI to be delivered. */
	if (!lapic_ipi_wait(AFTER_SPIN)) {
#ifdef needsattention
		/*
		 * XXX FIXME:
		 *
		 * The above function waits for the message to actually be
		 * delivered.  It breaks out after an arbitrary timeout
		 * since the message should eventually be delivered (at
		 * least in theory) and that if it wasn't we would catch
		 * the failure with the check above when the next IPI is
		 * sent.
		 *
		 * We could skip this wait entirely, EXCEPT it probably
		 * protects us from other routines that assume that the
		 * message was delivered and acted upon when this function
		 * returns.
		 */
		printf("APIC: IPI might be stuck\n");
#else /* !needsattention */
		/* Wait until mesage is sent without a timeout. */
		while (lapic_read_icr_lo() & APIC_DELSTAT_PEND)
			ia32_pause();
#endif /* needsattention */
	}
#endif /* DETECT_DEADLOCK */
}

#endif /* SMP */

/*
 * Since the IDT is shared by all CPUs the IPI slot update needs to be globally
 * visible.
 *
 * Consider the case where an IPI is generated immediately after allocation:
 *     vector = lapic_ipi_alloc(ipifunc);
 *     ipi_selected(other_cpus, vector);
 *
 * In xAPIC mode a write to ICR_LO has serializing semantics because the
 * APIC page is mapped as an uncached region. In x2APIC mode there is an
 * explicit 'mfence' before the ICR MSR is written. Therefore in both cases
 * the IDT slot update is globally visible before the IPI is delivered.
 */
static int
native_lapic_ipi_alloc(inthand_t *ipifunc)
{
	struct gate_descriptor *ip;
	long func;
	int idx, vector;

	KASSERT(ipifunc != &IDTVEC(rsvd) && ipifunc != &IDTVEC(rsvd_pti),
	    ("invalid ipifunc %p", ipifunc));

	vector = -1;
	mtx_lock_spin(&icu_lock);
	for (idx = IPI_DYN_FIRST; idx <= IPI_DYN_LAST; idx++) {
		ip = &idt[idx];
		func = (ip->gd_hioffset << 16) | ip->gd_looffset;
		if ((!pti && func == (uintptr_t)&IDTVEC(rsvd)) ||
		    (pti && func == (uintptr_t)&IDTVEC(rsvd_pti))) {
			vector = idx;
			setidt(vector, ipifunc, SDT_APIC, SEL_KPL, GSEL_APIC);
			break;
		}
	}
	mtx_unlock_spin(&icu_lock);
	return (vector);
}

static void
native_lapic_ipi_free(int vector)
{
	struct gate_descriptor *ip;
	long func;

	KASSERT(vector >= IPI_DYN_FIRST && vector <= IPI_DYN_LAST,
	    ("%s: invalid vector %d", __func__, vector));

	mtx_lock_spin(&icu_lock);
	ip = &idt[vector];
	func = (ip->gd_hioffset << 16) | ip->gd_looffset;
	KASSERT(func != (uintptr_t)&IDTVEC(rsvd) &&
	    func != (uintptr_t)&IDTVEC(rsvd_pti),
	    ("invalid idtfunc %#lx", func));
	setidt(vector, pti ? &IDTVEC(rsvd_pti) : &IDTVEC(rsvd), SDT_APIC,
	    SEL_KPL, GSEL_APIC);
	mtx_unlock_spin(&icu_lock);
}

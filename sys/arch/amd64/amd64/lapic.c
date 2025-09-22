/*	$OpenBSD: lapic.c,v 1.75 2025/08/02 07:33:28 sf Exp $	*/
/* $NetBSD: lapic.c,v 1.2 2003/05/08 01:04:35 fvdl Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/clockintr.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/codepatch.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/pmap.h>
#include <machine/mpbiosvar.h>
#include <machine/specialreg.h>
#include <machine/segments.h>

#include <machine/i82489reg.h>
#include <machine/i82489var.h>

#include <dev/ic/i8253reg.h>

#include "ioapic.h"
#include "xen.h"
#include "hyperv.h"
#include "vmm.h"

#if NIOAPIC > 0
#include <machine/i82093var.h>
#endif

/* #define LAPIC_DEBUG */

#ifdef LAPIC_DEBUG
#define DPRINTF(x...)	do { printf(x); } while(0)
#else
#define DPRINTF(x...)
#endif /* LAPIC_DEBUG */

struct evcount clk_count;
#ifdef MULTIPROCESSOR
struct evcount ipi_count;
#endif

static u_int32_t lapic_gettick(void);
void	lapic_clockintr(void *, struct intrframe);
void	lapic_initclocks(void);
void	lapic_map(paddr_t);

void lapic_hwmask(struct pic *, int);
void lapic_hwunmask(struct pic *, int);
void lapic_setup(struct pic *, struct cpu_info *, int, int, int);

extern char idt_allocmap[];

struct pic local_pic = {
	{0, {NULL}, NULL, 0, "lapic", NULL, 0, 0},
	PIC_LAPIC,
#ifdef MULTIPROCESSOR
	{},
#endif
	lapic_hwmask,
	lapic_hwunmask,
	lapic_setup,
	lapic_setup,
};

extern int x2apic_eoi;
int x2apic_enabled = 0;

u_int32_t x2apic_readreg(int reg);
u_int32_t x2apic_cpu_number(void);
void x2apic_writereg(int reg, u_int32_t val);
void x2apic_ipi(int vec, int target, int dl);

u_int32_t i82489_readreg(int reg);
u_int32_t i82489_cpu_number(void);
void i82489_writereg(int reg, u_int32_t val);
void i82489_ipi(int vec, int target, int dl);

u_int32_t (*lapic_readreg)(int)			= i82489_readreg;
void (*lapic_writereg)(int, u_int32_t)		= i82489_writereg;
#ifdef MULTIPROCESSOR
void (*x86_ipi)(int vec, int target, int dl)	= i82489_ipi;
#endif

u_int32_t
i82489_readreg(int reg)
{
	return *((volatile u_int32_t *)(((volatile u_int8_t *)local_apic)
	    + reg));
}

u_int32_t
i82489_cpu_number(void)
{
	return i82489_readreg(LAPIC_ID) >> LAPIC_ID_SHIFT;
}

void
i82489_writereg(int reg, u_int32_t val)
{
	*((volatile u_int32_t *)(((volatile u_int8_t *)local_apic) + reg)) =
	    val;
}

u_int32_t
x2apic_readreg(int reg)
{
	return rdmsr(MSR_X2APIC_BASE + (reg >> 4));
}

u_int32_t
x2apic_cpu_number(void)
{
	return x2apic_readreg(LAPIC_ID) & X2APIC_ID_MASK;
}

void
x2apic_writereg(int reg, u_int32_t val)
{
	wrmsr(MSR_X2APIC_BASE + (reg >> 4), val);
}

#ifdef MULTIPROCESSOR
static inline void
x2apic_writeicr(u_int32_t hi, u_int32_t lo)
{
	u_int32_t msr = MSR_X2APIC_BASE + (LAPIC_ICRLO >> 4);
	__asm volatile("wrmsr" : : "a" (lo), "d" (hi), "c" (msr));
}
#endif

u_int32_t
lapic_cpu_number(void)
{
	if (x2apic_enabled)
		return x2apic_cpu_number();
	return i82489_cpu_number();
}

void
lapic_map(paddr_t lapic_base)
{
	pt_entry_t *pte;
	vaddr_t va;
	u_int64_t msr;
	u_long s;

	s = intr_disable();

	msr = rdmsr(MSR_APICBASE);

	if (ISSET(msr, APICBASE_ENABLE_X2APIC) ||
	    (ISSET(cpu_ecxfeature, CPUIDECX_HV) &&
	    ISSET(cpu_ecxfeature, CPUIDECX_X2APIC))) {
		 /*
		  * On real hardware, x2apic must only be enabled if interrupt
		  * remapping is also enabled. See 10.12.7 of the SDM vol 3.
		  * On hypervisors, this is not necessary. Hypervisors can
		  * implement x2apic support even if the host CPU does not
		  * support it.  Until we support interrupt remapping, use
		  * x2apic only if the hypervisor flag is also set or it is
		  * enabled by BIOS.
		  */
		if (!ISSET(msr, APICBASE_ENABLE_X2APIC)) {
			msr |= APICBASE_ENABLE_X2APIC;
			wrmsr(MSR_APICBASE, msr);
		}
		lapic_readreg = x2apic_readreg;
		lapic_writereg = x2apic_writereg;
#ifdef MULTIPROCESSOR
		x86_ipi = x2apic_ipi;
#endif
		x2apic_enabled = 1;
		codepatch_call(CPTAG_EOI, &x2apic_eoi);

		va = (vaddr_t)&local_apic;
	} else {
		/*
		 * Map local apic.
		 *
		 * Whap the PTE "by hand" rather than calling pmap_kenter_pa
		 * because the latter will attempt to invoke TLB shootdown
		 * code just as we might have changed the value of
		 * cpu_number()..
		 */
		va = (vaddr_t)&local_apic;
		pte = kvtopte(va);
		*pte = lapic_base | PG_RW | PG_V | PG_N | PG_G | pg_nx;
		invlpg(va);
	}

	/*
	 * Enter the LAPIC MMIO page in the U-K page table for handling
	 * Meltdown (needed in the interrupt stub to acknowledge the
	 * incoming interrupt). On CPUs unaffected by Meltdown,
	 * pmap_enter_special is a no-op.
	 */
	pmap_enter_special(va, lapic_base, PROT_READ | PROT_WRITE);
	DPRINTF("%s: entered lapic page va 0x%llx pa 0x%llx\n", __func__,
	    (uint64_t)va, (uint64_t)lapic_base);

	intr_restore(s);
}

/*
 * enable local apic
 */
void
lapic_enable(void)
{
	lapic_writereg(LAPIC_SVR, LAPIC_SVR_ENABLE | LAPIC_SPURIOUS_VECTOR);
}

void
lapic_disable(void)
{
	lapic_writereg(LAPIC_SVR, 0);
}

void
lapic_set_lvt(void)
{
	struct cpu_info *ci = curcpu();
	int i;
	struct mp_intr_map *mpi;
	uint32_t lint0;

#ifdef MULTIPROCESSOR
	if (mp_verbose) {
		apic_format_redir(ci->ci_dev->dv_xname, "prelint", 0, 0,
		    lapic_readreg(LAPIC_LVINT0));
		apic_format_redir(ci->ci_dev->dv_xname, "prelint", 1, 0,
		    lapic_readreg(LAPIC_LVINT1));
	}
#endif

#if NIOAPIC > 0
	/*
	 * Disable ExtINT by default when using I/O APICs.
	 */
	if (nioapics > 0) {
		lint0 = lapic_readreg(LAPIC_LVINT0);
		lint0 |= LAPIC_LVT_MASKED;
		lapic_writereg(LAPIC_LVINT0, lint0);
	}
#endif

	if (ci->ci_vendor == CPUV_AMD) {
		/*
		 * Detect the presence of C1E capability mostly on latest
		 * dual-cores (or future) k8 family. This mis-feature renders
		 * the local APIC timer dead, so we disable it by reading
		 * the Interrupt Pending Message register and clearing both
		 * C1eOnCmpHalt (bit 28) and SmiOnCmpHalt (bit 27).
		 *
		 * Reference:
		 *   "BIOS and Kernel Developer's Guide for AMD NPT
		 *    Family 0Fh Processors"
		 *   #32559 revision 3.00
		 */
		if (ci->ci_family == 0xf || ci->ci_family == 0x10) {
			uint64_t msr;

			msr = rdmsr(MSR_INT_PEN_MSG);
			if (msr & (IPM_C1E_CMP_HLT|IPM_SMI_CMP_HLT)) {
				msr &= ~(IPM_C1E_CMP_HLT|IPM_SMI_CMP_HLT);
				wrmsr(MSR_INT_PEN_MSG, msr);
			}
		}
	}

	for (i = 0; i < mp_nintrs; i++) {
		mpi = &mp_intrs[i];
		if (mpi->ioapic == NULL && (mpi->cpu_id == MPS_ALL_APICS
					    || mpi->cpu_id == ci->ci_apicid)) {
#ifdef DIAGNOSTIC
			if (mpi->ioapic_pin > 1)
				panic("lapic_set_lvt: bad pin value %d",
				    mpi->ioapic_pin);
#endif
			if (mpi->ioapic_pin == 0)
				lapic_writereg(LAPIC_LVINT0, mpi->redir);
			else
				lapic_writereg(LAPIC_LVINT1, mpi->redir);
		}
	}

#ifdef MULTIPROCESSOR
	if (mp_verbose) {
		apic_format_redir(ci->ci_dev->dv_xname, "timer", 0, 0,
		    lapic_readreg(LAPIC_LVTT));
		apic_format_redir(ci->ci_dev->dv_xname, "pcint", 0, 0,
		    lapic_readreg(LAPIC_PCINT));
		apic_format_redir(ci->ci_dev->dv_xname, "lint", 0, 0,
		    lapic_readreg(LAPIC_LVINT0));
		apic_format_redir(ci->ci_dev->dv_xname, "lint", 1, 0,
		    lapic_readreg(LAPIC_LVINT1));
		apic_format_redir(ci->ci_dev->dv_xname, "err", 0, 0,
		    lapic_readreg(LAPIC_LVERR));
	}
#endif
}

/*
 * Initialize fixed idt vectors for use by local apic.
 */
void
lapic_boot_init(paddr_t lapic_base)
{
	static u_int64_t clk_irq = 0;
#ifdef MULTIPROCESSOR
	static u_int64_t ipi_irq = 0;
#endif

	lapic_map(lapic_base);

#ifdef MULTIPROCESSOR
	idt_allocmap[LAPIC_IPI_VECTOR] = 1;
	idt_vec_set(LAPIC_IPI_VECTOR, Xintr_lapic_ipi);
	idt_allocmap[LAPIC_IPI_INVLTLB] = 1;
	idt_allocmap[LAPIC_IPI_INVLPG] = 1;
	idt_allocmap[LAPIC_IPI_INVLRANGE] = 1;
	if (!pmap_use_pcid) {
		idt_vec_set(LAPIC_IPI_INVLTLB, Xipi_invltlb);
		idt_vec_set(LAPIC_IPI_INVLPG, Xipi_invlpg);
		idt_vec_set(LAPIC_IPI_INVLRANGE, Xipi_invlrange);
	} else {
		idt_vec_set(LAPIC_IPI_INVLTLB, Xipi_invltlb_pcid);
		idt_vec_set(LAPIC_IPI_INVLPG, Xipi_invlpg_pcid);
		idt_vec_set(LAPIC_IPI_INVLRANGE, Xipi_invlrange_pcid);
	}
	idt_allocmap[LAPIC_IPI_WBINVD] = 1;
	idt_vec_set(LAPIC_IPI_WBINVD, Xipi_wbinvd);
#if NVMM > 0
	idt_allocmap[LAPIC_IPI_INVEPT] = 1;
	idt_vec_set(LAPIC_IPI_INVEPT, Xipi_invept);
#endif /* NVMM > 0 */
#endif /* MULTIPROCESSOR */
	idt_allocmap[LAPIC_SPURIOUS_VECTOR] = 1;
	idt_vec_set(LAPIC_SPURIOUS_VECTOR, Xintrspurious);

	idt_allocmap[LAPIC_TIMER_VECTOR] = 1;
	idt_vec_set(LAPIC_TIMER_VECTOR, Xintr_lapic_ltimer);

#if NXEN > 0
	/* Xen HVM Event Channel Interrupt Vector */
	idt_allocmap[LAPIC_XEN_VECTOR] = 1;
	idt_vec_set(LAPIC_XEN_VECTOR, Xintr_xen_upcall);
#endif
#if NHYPERV > 0
	/* Hyper-V Interrupt Vector */
	idt_allocmap[LAPIC_HYPERV_VECTOR] = 1;
	idt_vec_set(LAPIC_HYPERV_VECTOR, Xintr_hyperv_upcall);
#endif

	evcount_attach(&clk_count, "clock", &clk_irq);
	evcount_percpu(&clk_count);
#ifdef MULTIPROCESSOR
	evcount_attach(&ipi_count, "ipi", &ipi_irq);
	evcount_percpu(&ipi_count);
#endif
}

static __inline u_int32_t
lapic_gettick(void)
{
	return lapic_readreg(LAPIC_CCR_TIMER);
}

#include <sys/kernel.h>		/* for hz */

/*
 * this gets us up to a 4GHz busclock....
 */
u_int32_t lapic_per_second = 0;
uint64_t lapic_timer_nsec_cycle_ratio;
uint64_t lapic_timer_nsec_max;

void lapic_timer_rearm(void *, uint64_t);
void lapic_timer_trigger(void *);

const struct intrclock lapic_timer_intrclock = {
	.ic_rearm = lapic_timer_rearm,
	.ic_trigger = lapic_timer_trigger
};

void lapic_timer_oneshot(uint32_t, uint32_t);
void lapic_timer_periodic(uint32_t, uint32_t);

void
lapic_timer_rearm(void *unused, uint64_t nsecs)
{
	uint32_t cycles;

	if (nsecs > lapic_timer_nsec_max)
		nsecs = lapic_timer_nsec_max;
	cycles = (nsecs * lapic_timer_nsec_cycle_ratio) >> 32;
	if (cycles == 0)
		cycles = 1;
	lapic_writereg(LAPIC_ICR_TIMER, cycles);
}

void
lapic_timer_trigger(void *unused)
{
	u_long s;

	s = intr_disable();
	lapic_timer_oneshot(0, 1);
	intr_restore(s);
}

/*
 * Start the local apic countdown timer.
 *
 * First set the mode, mask, and vector.  Then set the
 * divisor.  Last, set the cycle count: this restarts
 * the countdown.
 */
static inline void
lapic_timer_start(uint32_t mode, uint32_t mask, uint32_t cycles)
{
	lapic_writereg(LAPIC_LVTT, mode | mask | LAPIC_TIMER_VECTOR);
	lapic_writereg(LAPIC_DCR_TIMER, LAPIC_DCRT_DIV1);
	lapic_writereg(LAPIC_ICR_TIMER, cycles);
}

void
lapic_timer_oneshot(uint32_t mask, uint32_t cycles)
{
	lapic_timer_start(LAPIC_LVTT_TM_ONESHOT, mask, cycles);
}

void
lapic_timer_periodic(uint32_t mask, uint32_t cycles)
{
	lapic_timer_start(LAPIC_LVTT_TM_PERIODIC, mask, cycles);
}

void
lapic_clockintr(void *arg, struct intrframe frame)
{
	struct cpu_info *ci = curcpu();
	int floor;

	floor = ci->ci_handled_intr_level;
	ci->ci_handled_intr_level = ci->ci_ilevel;
	clockintr_dispatch(&frame);
	ci->ci_handled_intr_level = floor;

	evcount_inc(&clk_count);
}

void
lapic_startclock(void)
{
	clockintr_cpu_init(&lapic_timer_intrclock);
	clockintr_trigger();
}

void
lapic_initclocks(void)
{
	i8254_inittimecounter_simple();

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;
}


extern int gettick(void);	/* XXX put in header file */
extern u_long rtclock_tval; /* XXX put in header file */

static __inline void
wait_next_cycle(void)
{
	unsigned int tick, tlast;

	tlast = (1 << 16);	/* i8254 counter has 16 bits at most */
	for (;;) {
		tick = gettick();
		if (tick > tlast)
			return;
		tlast = tick;
	}
}

/*
 * Calibrate the local apic count-down timer (which is running at
 * bus-clock speed) vs. the i8254 counter/timer (which is running at
 * a fixed rate).
 *
 * The Intel MP spec says: "An MP operating system may use the IRQ8
 * real-time clock as a reference to determine the actual APIC timer clock
 * speed."
 *
 * We're actually using the IRQ0 timer.  Hmm.
 */
void
lapic_calibrate_timer(struct cpu_info *ci)
{
	unsigned int startapic, endapic;
	u_int64_t dtick, dapic, tmp;
	u_long s;
	int i;

	if (lapic_per_second)
		goto skip_calibration;

	if (mp_verbose)
		printf("%s: calibrating local timer\n", ci->ci_dev->dv_xname);

	/*
	 * Configure timer to one-shot, interrupt masked,
	 * large positive number.
	 */
	lapic_timer_oneshot(LAPIC_LVTT_M, 0x80000000);

	if (delay_func == i8254_delay) {
		s = intr_disable();

		/* wait for current cycle to finish */
		wait_next_cycle();

		startapic = lapic_gettick();

		/* wait the next hz cycles */
		for (i = 0; i < hz; i++)
			wait_next_cycle();

		endapic = lapic_gettick();

		intr_restore(s);

		dtick = hz * rtclock_tval;
		dapic = startapic-endapic;

		/*
		 * there are TIMER_FREQ ticks per second.
		 * in dtick ticks, there are dapic bus clocks.
		 */
		tmp = (TIMER_FREQ * dapic) / dtick;

		lapic_per_second = tmp;
	} else {
		s = intr_disable();
		startapic = lapic_gettick();
		delay(1 * 1000 * 1000);
		endapic = lapic_gettick();
		intr_restore(s);
		lapic_per_second = startapic - endapic;
	}

skip_calibration:
	printf("%s: apic clock running at %dMHz\n",
	    ci->ci_dev->dv_xname, lapic_per_second / (1000 * 1000));

	/* XXX What do we do if this is zero? */
	if (lapic_per_second == 0)
		return;

	lapic_timer_nsec_cycle_ratio =
	    lapic_per_second * (1ULL << 32) / 1000000000;
	lapic_timer_nsec_max = UINT64_MAX / lapic_timer_nsec_cycle_ratio;
	initclock_func = lapic_initclocks;
	startclock_func = lapic_startclock;
}

/*
 * XXX the following belong mostly or partly elsewhere..
 */

#ifdef MULTIPROCESSOR
static __inline void i82489_icr_wait(void);

static __inline void
i82489_icr_wait(void)
{
#ifdef DIAGNOSTIC
	unsigned j = 100000;
#endif /* DIAGNOSTIC */

	while ((i82489_readreg(LAPIC_ICRLO) & LAPIC_DLSTAT_BUSY) != 0) {
		__asm volatile("pause": : :"memory");
#ifdef DIAGNOSTIC
		j--;
		if (j == 0)
			panic("i82489_icr_wait: busy");
#endif /* DIAGNOSTIC */
	}
}

void
i82489_ipi_init(int target)
{

	if ((target & LAPIC_DEST_MASK) == 0)
		i82489_writereg(LAPIC_ICRHI, target << LAPIC_ID_SHIFT);

	i82489_writereg(LAPIC_ICRLO, (target & LAPIC_DEST_MASK) |
	    LAPIC_DLMODE_INIT | LAPIC_LVL_ASSERT );

	i82489_icr_wait();

	delay(10000);

	i82489_writereg(LAPIC_ICRLO, (target & LAPIC_DEST_MASK) |
	     LAPIC_DLMODE_INIT | LAPIC_LVL_TRIG | LAPIC_LVL_DEASSERT);

	i82489_icr_wait();
}

void
i82489_ipi(int vec, int target, int dl)
{
	int s;

	s = splhigh();

	i82489_icr_wait();

	if ((target & LAPIC_DEST_MASK) == 0)
		i82489_writereg(LAPIC_ICRHI, target << LAPIC_ID_SHIFT);

	i82489_writereg(LAPIC_ICRLO,
	    (target & LAPIC_DEST_MASK) | vec | dl | LAPIC_LVL_ASSERT);

	i82489_icr_wait();

	splx(s);
}

void
x2apic_ipi_init(int target)
{
	u_int64_t hi = 0;

	if ((target & LAPIC_DEST_MASK) == 0)
		hi = target & 0xff;

	x2apic_writeicr(hi, (target & LAPIC_DEST_MASK) | LAPIC_DLMODE_INIT |
	    LAPIC_LVL_ASSERT );

	delay(10000);

	x2apic_writeicr(0, (target & LAPIC_DEST_MASK) | LAPIC_DLMODE_INIT |
	    LAPIC_LVL_TRIG | LAPIC_LVL_DEASSERT);
}

void
x2apic_ipi(int vec, int target, int dl)
{
	u_int64_t hi = 0, lo;

	if ((target & LAPIC_DEST_MASK) == 0)
		hi = target & 0xff;

	lo = (target & LAPIC_DEST_MASK) | vec | dl | LAPIC_LVL_ASSERT;

	x2apic_writeicr(hi, lo);
}

void
x86_ipi_init(int target)
{
	if (x2apic_enabled)
		x2apic_ipi_init(target);
	else
		i82489_ipi_init(target);
}
#endif /* MULTIPROCESSOR */


/*
 * Using 'pin numbers' as:
 * 0 - timer
 * 1 - unused
 * 2 - PCINT
 * 3 - LVINT0
 * 4 - LVINT1
 * 5 - LVERR
 */

void
lapic_hwmask(struct pic *pic, int pin)
{
	int reg;
	u_int32_t val;

	reg = LAPIC_LVTT + (pin << 4);
	val = lapic_readreg(reg);
	val |= LAPIC_LVT_MASKED;
	lapic_writereg(reg, val);
}

void
lapic_hwunmask(struct pic *pic, int pin)
{
	int reg;
	u_int32_t val;

	reg = LAPIC_LVTT + (pin << 4);
	val = lapic_readreg(reg);
	val &= ~LAPIC_LVT_MASKED;
	lapic_writereg(reg, val);
}

void
lapic_setup(struct pic *pic, struct cpu_info *ci, int pin, int idtvec, int type)
{
}

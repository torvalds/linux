/*	$OpenBSD: lapic.c,v 1.58 2023/09/17 14:50:51 cheloha Exp $	*/
/* $NetBSD: lapic.c,v 1.1.2.8 2000/02/23 06:10:50 sommerfeld Exp $ */

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
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/stdint.h>

#include <uvm/uvm_extern.h>

#include <machine/cpufunc.h>
#include <machine/cpuvar.h>
#include <machine/pmap.h>
#include <machine/mpbiosvar.h>
#include <machine/specialreg.h>
#include <machine/segments.h>

#include <machine/apicvar.h>
#include <machine/i82489reg.h>
#include <machine/i82489var.h>
#include <machine/pctr.h>

#include <dev/ic/i8253reg.h>

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
void	lapic_clockintr(void *);
void	lapic_initclocks(void);
void	lapic_map(paddr_t);

void
lapic_map(paddr_t lapic_base)
{
	vaddr_t va = (vaddr_t)&local_apic;
	u_long s;
	int tpr;

	s = intr_disable();
	tpr = lapic_tpr;

	/*
	 * Map local apic.
	 *
	 * Whap the PTE "by hand" rather than calling pmap_kenter_pa because
	 * the latter will attempt to invoke TLB shootdown code just as we
	 * might have changed the value of cpu_number()..
	 */

	pmap_pte_set(va, lapic_base, PG_RW | PG_V | PG_N);
	invlpg(va);

	pmap_enter_special(va, lapic_base, PROT_READ | PROT_WRITE, PG_N);
	DPRINTF("%s: entered lapic page va 0x%08lx pa 0x%08lx\n", __func__,
	    va, lapic_base);

#ifdef MULTIPROCESSOR
	cpu_init_first();
#endif

	lapic_tpr = tpr;
	intr_restore(s);
}

/*
 * enable local apic
 */
void
lapic_enable(void)
{
	i82489_writereg(LAPIC_SVR, LAPIC_SVR_ENABLE | LAPIC_SPURIOUS_VECTOR);
}

void
lapic_disable(void)
{
	i82489_writereg(LAPIC_SVR, 0);
}

void
lapic_set_softvectors(void)
{
	idt_vec_set(LAPIC_SOFTCLOCK_VECTOR, Xintrsoftclock);
	idt_vec_set(LAPIC_SOFTNET_VECTOR, Xintrsoftnet);
	idt_vec_set(LAPIC_SOFTTTY_VECTOR, Xintrsofttty);
}

void
lapic_set_lvt(void)
{
	struct cpu_info *ci = curcpu();
	int i;
	struct mp_intr_map *mpi;

#ifdef MULTIPROCESSOR
	if (mp_verbose) {
		apic_format_redir(ci->ci_dev->dv_xname, "prelint", 0, 0,
		    i82489_readreg(LAPIC_LVINT0));
		apic_format_redir(ci->ci_dev->dv_xname, "prelint", 1, 0,
		    i82489_readreg(LAPIC_LVINT1));
	}
#endif

	if (strcmp(cpu_vendor, "AuthenticAMD") == 0) {
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
				i82489_writereg(LAPIC_LVINT0, mpi->redir);
			else
				i82489_writereg(LAPIC_LVINT1, mpi->redir);
		}
	}

#ifdef MULTIPROCESSOR
	if (mp_verbose) {
		apic_format_redir(ci->ci_dev->dv_xname, "timer", 0, 0,
		    i82489_readreg(LAPIC_LVTT));
		apic_format_redir(ci->ci_dev->dv_xname, "pcint", 0, 0,
		    i82489_readreg(LAPIC_PCINT));
		apic_format_redir(ci->ci_dev->dv_xname, "lint", 0, 0,
		    i82489_readreg(LAPIC_LVINT0));
		apic_format_redir(ci->ci_dev->dv_xname, "lint", 1, 0,
		    i82489_readreg(LAPIC_LVINT1));
		apic_format_redir(ci->ci_dev->dv_xname, "err", 0, 0,
		    i82489_readreg(LAPIC_LVERR));
	}
#endif
}

/*
 * Initialize fixed idt vectors for use by local apic.
 */
void
lapic_boot_init(paddr_t lapic_base)
{
	static int clk_irq = 0;
#ifdef MULTIPROCESSOR
	static int ipi_irq = 0;
#endif

	lapic_map(lapic_base);

#ifdef MULTIPROCESSOR
	idt_vec_set(LAPIC_IPI_VECTOR, Xintripi);
	idt_vec_set(LAPIC_IPI_INVLTLB, Xintripi_invltlb);
	idt_vec_set(LAPIC_IPI_INVLPG, Xintripi_invlpg);
	idt_vec_set(LAPIC_IPI_INVLRANGE, Xintripi_invlrange);
	idt_vec_set(LAPIC_IPI_RELOADCR3, Xintripi_reloadcr3);
#endif
	idt_vec_set(LAPIC_SPURIOUS_VECTOR, Xintrspurious);
	idt_vec_set(LAPIC_TIMER_VECTOR, Xintrltimer);

	evcount_attach(&clk_count, "clock", &clk_irq);
#ifdef MULTIPROCESSOR
	evcount_attach(&ipi_count, "ipi", &ipi_irq);
#endif
}

static __inline u_int32_t
lapic_gettick(void)
{
	return i82489_readreg(LAPIC_CCR_TIMER);
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

struct intrclock lapic_timer_intrclock = {
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
	lapic_timer_oneshot(0, cycles);
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
	i82489_writereg(LAPIC_LVTT, mode | mask | LAPIC_TIMER_VECTOR);
	i82489_writereg(LAPIC_DCR_TIMER, LAPIC_DCRT_DIV1);
	i82489_writereg(LAPIC_ICR_TIMER, cycles);
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
lapic_clockintr(void *frame)
{
	clockintr_dispatch(frame);
	clk_count.ec_count++;
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

	printf("%s: apic clock running at %dMHz\n",
	    ci->ci_dev->dv_xname, lapic_per_second / (1000 * 1000));

	/* XXX What should we do here if the timer frequency is zero? */
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
i386_ipi_init(int target)
{
	if ((target & LAPIC_DEST_MASK) == 0)
		i82489_writereg(LAPIC_ICRHI, target << LAPIC_ID_SHIFT);

	i82489_writereg(LAPIC_ICRLO, (target & LAPIC_DEST_MASK) |
	    LAPIC_DLMODE_INIT | LAPIC_LVL_ASSERT );

	i82489_icr_wait();

	i8254_delay(10000);

	i82489_writereg(LAPIC_ICRLO, (target & LAPIC_DEST_MASK) |
	     LAPIC_DLMODE_INIT | LAPIC_LVL_TRIG | LAPIC_LVL_DEASSERT);

	i82489_icr_wait();
}

void
i386_ipi(int vec, int target, int dl)
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
#endif /* MULTIPROCESSOR */

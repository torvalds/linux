/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
 * Copyright (c) 2013 Neel Natu <neel@freebsd.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <dev/acpica/acpi_hpet.h>

#include <machine/vmm.h>
#include <machine/vmm_dev.h>

#include "vmm_lapic.h"
#include "vatpic.h"
#include "vioapic.h"
#include "vhpet.h"

#include "vmm_ktr.h"

static MALLOC_DEFINE(M_VHPET, "vhpet", "bhyve virtual hpet");

#define	HPET_FREQ	16777216		/* 16.7 (2^24) Mhz */
#define	FS_PER_S	1000000000000000ul

/* Timer N Configuration and Capabilities Register */
#define	HPET_TCAP_RO_MASK	(HPET_TCAP_INT_ROUTE 	|		\
				 HPET_TCAP_FSB_INT_DEL	|		\
				 HPET_TCAP_SIZE		|		\
				 HPET_TCAP_PER_INT)
/*
 * HPET requires at least 3 timers and up to 32 timers per block.
 */
#define	VHPET_NUM_TIMERS	8
CTASSERT(VHPET_NUM_TIMERS >= 3 && VHPET_NUM_TIMERS <= 32);

struct vhpet_callout_arg {
	struct vhpet *vhpet;
	int timer_num;
};

struct vhpet {
	struct vm	*vm;
	struct mtx	mtx;
	sbintime_t	freq_sbt;

	uint64_t	config;		/* Configuration */
	uint64_t	isr;		/* Interrupt Status */
	uint32_t	countbase;	/* HPET counter base value */
	sbintime_t	countbase_sbt;	/* uptime corresponding to base value */

	struct {
		uint64_t	cap_config;	/* Configuration */
		uint64_t	msireg;		/* FSB interrupt routing */
		uint32_t	compval;	/* Comparator */
		uint32_t	comprate;
		struct callout	callout;
		sbintime_t	callout_sbt;	/* time when counter==compval */
		struct vhpet_callout_arg arg;
	} timer[VHPET_NUM_TIMERS];
};

#define	VHPET_LOCK(vhp)		mtx_lock(&((vhp)->mtx))
#define	VHPET_UNLOCK(vhp)	mtx_unlock(&((vhp)->mtx))

static void vhpet_start_timer(struct vhpet *vhpet, int n, uint32_t counter,
    sbintime_t now);

static uint64_t
vhpet_capabilities(void)
{
	uint64_t cap = 0;

	cap |= 0x8086 << 16;			/* vendor id */
	cap |= (VHPET_NUM_TIMERS - 1) << 8;	/* number of timers */
	cap |= 1;				/* revision */
	cap &= ~HPET_CAP_COUNT_SIZE;		/* 32-bit timer */

	cap &= 0xffffffff;
	cap |= (FS_PER_S / HPET_FREQ) << 32;	/* tick period in fs */

	return (cap);
}

static __inline bool
vhpet_counter_enabled(struct vhpet *vhpet)
{

	return ((vhpet->config & HPET_CNF_ENABLE) ? true : false);
}

static __inline bool
vhpet_timer_msi_enabled(struct vhpet *vhpet, int n)
{
	const uint64_t msi_enable = HPET_TCAP_FSB_INT_DEL | HPET_TCNF_FSB_EN;

	if ((vhpet->timer[n].cap_config & msi_enable) == msi_enable)
		return (true);
	else
		return (false);
}

static __inline int
vhpet_timer_ioapic_pin(struct vhpet *vhpet, int n)
{
	/*
	 * If the timer is configured to use MSI then treat it as if the
	 * timer is not connected to the ioapic.
	 */
	if (vhpet_timer_msi_enabled(vhpet, n))
		return (0);

	return ((vhpet->timer[n].cap_config & HPET_TCNF_INT_ROUTE) >> 9);
}

static uint32_t
vhpet_counter(struct vhpet *vhpet, sbintime_t *nowptr)
{
	uint32_t val;
	sbintime_t now, delta;

	val = vhpet->countbase;
	if (vhpet_counter_enabled(vhpet)) {
		now = sbinuptime();
		delta = now - vhpet->countbase_sbt;
		KASSERT(delta >= 0, ("vhpet_counter: uptime went backwards: "
		    "%#lx to %#lx", vhpet->countbase_sbt, now));
		val += delta / vhpet->freq_sbt;
		if (nowptr != NULL)
			*nowptr = now;
	} else {
		/*
		 * The sbinuptime corresponding to the 'countbase' is
		 * meaningless when the counter is disabled. Make sure
		 * that the caller doesn't want to use it.
		 */
		KASSERT(nowptr == NULL, ("vhpet_counter: nowptr must be NULL"));
	}
	return (val);
}

static void
vhpet_timer_clear_isr(struct vhpet *vhpet, int n)
{
	int pin;

	if (vhpet->isr & (1 << n)) {
		pin = vhpet_timer_ioapic_pin(vhpet, n);
		KASSERT(pin != 0, ("vhpet timer %d irq incorrectly routed", n));
		vioapic_deassert_irq(vhpet->vm, pin);
		vhpet->isr &= ~(1 << n);
	}
}

static __inline bool
vhpet_periodic_timer(struct vhpet *vhpet, int n)
{

	return ((vhpet->timer[n].cap_config & HPET_TCNF_TYPE) != 0);
}

static __inline bool
vhpet_timer_interrupt_enabled(struct vhpet *vhpet, int n)
{

	return ((vhpet->timer[n].cap_config & HPET_TCNF_INT_ENB) != 0);
}

static __inline bool
vhpet_timer_edge_trig(struct vhpet *vhpet, int n)
{

	KASSERT(!vhpet_timer_msi_enabled(vhpet, n), ("vhpet_timer_edge_trig: "
	    "timer %d is using MSI", n));

	if ((vhpet->timer[n].cap_config & HPET_TCNF_INT_TYPE) == 0)
		return (true);
	else
		return (false);
}

static void
vhpet_timer_interrupt(struct vhpet *vhpet, int n)
{
	int pin;

	/* If interrupts are not enabled for this timer then just return. */
	if (!vhpet_timer_interrupt_enabled(vhpet, n))
		return;

	/*
	 * If a level triggered interrupt is already asserted then just return.
	 */
	if ((vhpet->isr & (1 << n)) != 0) {
		VM_CTR1(vhpet->vm, "hpet t%d intr is already asserted", n);
		return;
	}

	if (vhpet_timer_msi_enabled(vhpet, n)) {
		lapic_intr_msi(vhpet->vm, vhpet->timer[n].msireg >> 32,
		    vhpet->timer[n].msireg & 0xffffffff);
		return;
	}	

	pin = vhpet_timer_ioapic_pin(vhpet, n);
	if (pin == 0) {
		VM_CTR1(vhpet->vm, "hpet t%d intr is not routed to ioapic", n);
		return;
	}

	if (vhpet_timer_edge_trig(vhpet, n)) {
		vioapic_pulse_irq(vhpet->vm, pin);
	} else {
		vhpet->isr |= 1 << n;
		vioapic_assert_irq(vhpet->vm, pin);
	}
}

static void
vhpet_adjust_compval(struct vhpet *vhpet, int n, uint32_t counter)
{
	uint32_t compval, comprate, compnext;

	KASSERT(vhpet->timer[n].comprate != 0, ("hpet t%d is not periodic", n));

	compval = vhpet->timer[n].compval;
	comprate = vhpet->timer[n].comprate;

	/*
	 * Calculate the comparator value to be used for the next periodic
	 * interrupt.
	 *
	 * This function is commonly called from the callout handler.
	 * In this scenario the 'counter' is ahead of 'compval'. To find
	 * the next value to program into the accumulator we divide the
	 * number space between 'compval' and 'counter' into 'comprate'
	 * sized units. The 'compval' is rounded up such that is "ahead"
	 * of 'counter'.
	 */
	compnext = compval + ((counter - compval) / comprate + 1) * comprate;

	vhpet->timer[n].compval = compnext;
}

static void
vhpet_handler(void *a)
{
	int n;
	uint32_t counter;
	sbintime_t now;
	struct vhpet *vhpet;
	struct callout *callout;
	struct vhpet_callout_arg *arg;

	arg = a;
	vhpet = arg->vhpet;
	n = arg->timer_num;
	callout = &vhpet->timer[n].callout;

	VM_CTR1(vhpet->vm, "hpet t%d fired", n);

	VHPET_LOCK(vhpet);

	if (callout_pending(callout))		/* callout was reset */
		goto done;

	if (!callout_active(callout))		/* callout was stopped */
		goto done;

	callout_deactivate(callout);

	if (!vhpet_counter_enabled(vhpet))
		panic("vhpet(%p) callout with counter disabled", vhpet);

	counter = vhpet_counter(vhpet, &now);
	vhpet_start_timer(vhpet, n, counter, now);
	vhpet_timer_interrupt(vhpet, n);
done:
	VHPET_UNLOCK(vhpet);
	return;
}

static void
vhpet_stop_timer(struct vhpet *vhpet, int n, sbintime_t now)
{

	VM_CTR1(vhpet->vm, "hpet t%d stopped", n);
	callout_stop(&vhpet->timer[n].callout);

	/*
	 * If the callout was scheduled to expire in the past but hasn't
	 * had a chance to execute yet then trigger the timer interrupt
	 * here. Failing to do so will result in a missed timer interrupt
	 * in the guest. This is especially bad in one-shot mode because
	 * the next interrupt has to wait for the counter to wrap around.
	 */
	if (vhpet->timer[n].callout_sbt < now) {
		VM_CTR1(vhpet->vm, "hpet t%d interrupt triggered after "
		    "stopping timer", n);
		vhpet_timer_interrupt(vhpet, n);
	}
}

static void
vhpet_start_timer(struct vhpet *vhpet, int n, uint32_t counter, sbintime_t now)
{
	sbintime_t delta, precision;

	if (vhpet->timer[n].comprate != 0)
		vhpet_adjust_compval(vhpet, n, counter);
	else {
		/*
		 * In one-shot mode it is the guest's responsibility to make
		 * sure that the comparator value is not in the "past". The
		 * hardware doesn't have any belt-and-suspenders to deal with
		 * this so we don't either.
		 */
	}

	delta = (vhpet->timer[n].compval - counter) * vhpet->freq_sbt;
	precision = delta >> tc_precexp;
	vhpet->timer[n].callout_sbt = now + delta;
	callout_reset_sbt(&vhpet->timer[n].callout, vhpet->timer[n].callout_sbt,
	    precision, vhpet_handler, &vhpet->timer[n].arg, C_ABSOLUTE);
}

static void
vhpet_start_counting(struct vhpet *vhpet)
{
	int i;

	vhpet->countbase_sbt = sbinuptime();
	for (i = 0; i < VHPET_NUM_TIMERS; i++) {
		/*
		 * Restart the timers based on the value of the main counter
		 * when it stopped counting.
		 */
		vhpet_start_timer(vhpet, i, vhpet->countbase,
		    vhpet->countbase_sbt);
	}
}

static void
vhpet_stop_counting(struct vhpet *vhpet, uint32_t counter, sbintime_t now)
{
	int i;

	vhpet->countbase = counter;
	for (i = 0; i < VHPET_NUM_TIMERS; i++)
		vhpet_stop_timer(vhpet, i, now);
}

static __inline void
update_register(uint64_t *regptr, uint64_t data, uint64_t mask)
{

	*regptr &= ~mask;
	*regptr |= (data & mask);
}

static void
vhpet_timer_update_config(struct vhpet *vhpet, int n, uint64_t data,
    uint64_t mask)
{
	bool clear_isr;
	int old_pin, new_pin;
	uint32_t allowed_irqs;
	uint64_t oldval, newval;

	if (vhpet_timer_msi_enabled(vhpet, n) ||
	    vhpet_timer_edge_trig(vhpet, n)) {
		if (vhpet->isr & (1 << n))
			panic("vhpet timer %d isr should not be asserted", n);
	}
	old_pin = vhpet_timer_ioapic_pin(vhpet, n);
	oldval = vhpet->timer[n].cap_config;

	newval = oldval;
	update_register(&newval, data, mask);
	newval &= ~(HPET_TCAP_RO_MASK | HPET_TCNF_32MODE);
	newval |= oldval & HPET_TCAP_RO_MASK;

	if (newval == oldval)
		return;

	vhpet->timer[n].cap_config = newval;
	VM_CTR2(vhpet->vm, "hpet t%d cap_config set to 0x%016x", n, newval);

	/*
	 * Validate the interrupt routing in the HPET_TCNF_INT_ROUTE field.
	 * If it does not match the bits set in HPET_TCAP_INT_ROUTE then set
	 * it to the default value of 0.
	 */
	allowed_irqs = vhpet->timer[n].cap_config >> 32;
	new_pin = vhpet_timer_ioapic_pin(vhpet, n);
	if (new_pin != 0 && (allowed_irqs & (1 << new_pin)) == 0) {
		VM_CTR3(vhpet->vm, "hpet t%d configured invalid irq %d, "
		    "allowed_irqs 0x%08x", n, new_pin, allowed_irqs);
		new_pin = 0;
		vhpet->timer[n].cap_config &= ~HPET_TCNF_INT_ROUTE;
	}

	if (!vhpet_periodic_timer(vhpet, n))
		vhpet->timer[n].comprate = 0;

	/*
	 * If the timer's ISR bit is set then clear it in the following cases:
	 * - interrupt is disabled
	 * - interrupt type is changed from level to edge or fsb.
	 * - interrupt routing is changed
	 *
	 * This is to ensure that this timer's level triggered interrupt does
	 * not remain asserted forever.
	 */
	if (vhpet->isr & (1 << n)) {
		KASSERT(old_pin != 0, ("timer %d isr asserted to ioapic pin %d",
		    n, old_pin));
		if (!vhpet_timer_interrupt_enabled(vhpet, n))
			clear_isr = true;
		else if (vhpet_timer_msi_enabled(vhpet, n))
			clear_isr = true;
		else if (vhpet_timer_edge_trig(vhpet, n))
			clear_isr = true;
		else if (vhpet_timer_ioapic_pin(vhpet, n) != old_pin)
			clear_isr = true;
		else
			clear_isr = false;

		if (clear_isr) {
			VM_CTR1(vhpet->vm, "hpet t%d isr cleared due to "
			    "configuration change", n);
			vioapic_deassert_irq(vhpet->vm, old_pin);
			vhpet->isr &= ~(1 << n);
		}
	}
}

int
vhpet_mmio_write(void *vm, int vcpuid, uint64_t gpa, uint64_t val, int size,
    void *arg)
{
	struct vhpet *vhpet;
	uint64_t data, mask, oldval, val64;
	uint32_t isr_clear_mask, old_compval, old_comprate, counter;
	sbintime_t now, *nowptr;
	int i, offset;

	vhpet = vm_hpet(vm);
	offset = gpa - VHPET_BASE;

	VHPET_LOCK(vhpet);

	/* Accesses to the HPET should be 4 or 8 bytes wide */
	switch (size) {
	case 8:
		mask = 0xffffffffffffffff;
		data = val;
		break;
	case 4:
		mask = 0xffffffff;
		data = val;
		if ((offset & 0x4) != 0) {
			mask <<= 32;
			data <<= 32;
		} 
		break;
	default:
		VM_CTR2(vhpet->vm, "hpet invalid mmio write: "
		    "offset 0x%08x, size %d", offset, size);
		goto done;
	}

	/* Access to the HPET should be naturally aligned to its width */
	if (offset & (size - 1)) {
		VM_CTR2(vhpet->vm, "hpet invalid mmio write: "
		    "offset 0x%08x, size %d", offset, size);
		goto done;
	}

	if (offset == HPET_CONFIG || offset == HPET_CONFIG + 4) {
		/*
		 * Get the most recent value of the counter before updating
		 * the 'config' register. If the HPET is going to be disabled
		 * then we need to update 'countbase' with the value right
		 * before it is disabled.
		 */
		nowptr = vhpet_counter_enabled(vhpet) ? &now : NULL;
		counter = vhpet_counter(vhpet, nowptr);
		oldval = vhpet->config;
		update_register(&vhpet->config, data, mask);

		/*
		 * LegacyReplacement Routing is not supported so clear the
		 * bit explicitly.
		 */
		vhpet->config &= ~HPET_CNF_LEG_RT;

		if ((oldval ^ vhpet->config) & HPET_CNF_ENABLE) {
			if (vhpet_counter_enabled(vhpet)) {
				vhpet_start_counting(vhpet);
				VM_CTR0(vhpet->vm, "hpet enabled");
			} else {
				vhpet_stop_counting(vhpet, counter, now);
				VM_CTR0(vhpet->vm, "hpet disabled");
			}
		}
		goto done;
	}

	if (offset == HPET_ISR || offset == HPET_ISR + 4) {
		isr_clear_mask = vhpet->isr & data;
		for (i = 0; i < VHPET_NUM_TIMERS; i++) {
			if ((isr_clear_mask & (1 << i)) != 0) {
				VM_CTR1(vhpet->vm, "hpet t%d isr cleared", i);
				vhpet_timer_clear_isr(vhpet, i);
			}
		}
		goto done;
	}

	if (offset == HPET_MAIN_COUNTER || offset == HPET_MAIN_COUNTER + 4) {
		/* Zero-extend the counter to 64-bits before updating it */
		val64 = vhpet_counter(vhpet, NULL);
		update_register(&val64, data, mask);
		vhpet->countbase = val64;
		if (vhpet_counter_enabled(vhpet))
			vhpet_start_counting(vhpet);
		goto done;
	}

	for (i = 0; i < VHPET_NUM_TIMERS; i++) {
		if (offset == HPET_TIMER_CAP_CNF(i) ||
		    offset == HPET_TIMER_CAP_CNF(i) + 4) {
			vhpet_timer_update_config(vhpet, i, data, mask);
			break;
		}

		if (offset == HPET_TIMER_COMPARATOR(i) ||
		    offset == HPET_TIMER_COMPARATOR(i) + 4) {
			old_compval = vhpet->timer[i].compval;
			old_comprate = vhpet->timer[i].comprate;
			if (vhpet_periodic_timer(vhpet, i)) {
				/*
				 * In periodic mode writes to the comparator
				 * change the 'compval' register only if the
				 * HPET_TCNF_VAL_SET bit is set in the config
				 * register.
				 */
				val64 = vhpet->timer[i].comprate;
				update_register(&val64, data, mask);
				vhpet->timer[i].comprate = val64;
				if ((vhpet->timer[i].cap_config &
				    HPET_TCNF_VAL_SET) != 0) {
					vhpet->timer[i].compval = val64;
				}
			} else {
				KASSERT(vhpet->timer[i].comprate == 0,
				    ("vhpet one-shot timer %d has invalid "
				    "rate %u", i, vhpet->timer[i].comprate));
				val64 = vhpet->timer[i].compval;
				update_register(&val64, data, mask);
				vhpet->timer[i].compval = val64;
			}
			vhpet->timer[i].cap_config &= ~HPET_TCNF_VAL_SET;

			if (vhpet->timer[i].compval != old_compval ||
			    vhpet->timer[i].comprate != old_comprate) {
				if (vhpet_counter_enabled(vhpet)) {
					counter = vhpet_counter(vhpet, &now);
					vhpet_start_timer(vhpet, i, counter,
					    now);
				}
			}
			break;
		}

		if (offset == HPET_TIMER_FSB_VAL(i) ||
		    offset == HPET_TIMER_FSB_ADDR(i)) {
			update_register(&vhpet->timer[i].msireg, data, mask);
			break;
		}
	}
done:
	VHPET_UNLOCK(vhpet);
	return (0);
}

int
vhpet_mmio_read(void *vm, int vcpuid, uint64_t gpa, uint64_t *rval, int size,
    void *arg)
{
	int i, offset;
	struct vhpet *vhpet;
	uint64_t data;

	vhpet = vm_hpet(vm);
	offset = gpa - VHPET_BASE;

	VHPET_LOCK(vhpet);

	/* Accesses to the HPET should be 4 or 8 bytes wide */
	if (size != 4 && size != 8) {
		VM_CTR2(vhpet->vm, "hpet invalid mmio read: "
		    "offset 0x%08x, size %d", offset, size);
		data = 0;
		goto done;
	}

	/* Access to the HPET should be naturally aligned to its width */
	if (offset & (size - 1)) {
		VM_CTR2(vhpet->vm, "hpet invalid mmio read: "
		    "offset 0x%08x, size %d", offset, size);
		data = 0;
		goto done;
	}

	if (offset == HPET_CAPABILITIES || offset == HPET_CAPABILITIES + 4) {
		data = vhpet_capabilities();
		goto done;	
	}

	if (offset == HPET_CONFIG || offset == HPET_CONFIG + 4) {
		data = vhpet->config;
		goto done;
	}

	if (offset == HPET_ISR || offset == HPET_ISR + 4) {
		data = vhpet->isr;
		goto done;
	}

	if (offset == HPET_MAIN_COUNTER || offset == HPET_MAIN_COUNTER + 4) {
		data = vhpet_counter(vhpet, NULL);
		goto done;
	}

	for (i = 0; i < VHPET_NUM_TIMERS; i++) {
		if (offset == HPET_TIMER_CAP_CNF(i) ||
		    offset == HPET_TIMER_CAP_CNF(i) + 4) {
			data = vhpet->timer[i].cap_config;
			break;
		}

		if (offset == HPET_TIMER_COMPARATOR(i) ||
		    offset == HPET_TIMER_COMPARATOR(i) + 4) {
			data = vhpet->timer[i].compval;
			break;
		}

		if (offset == HPET_TIMER_FSB_VAL(i) ||
		    offset == HPET_TIMER_FSB_ADDR(i)) {
			data = vhpet->timer[i].msireg;
			break;
		}
	}

	if (i >= VHPET_NUM_TIMERS)
		data = 0;
done:
	VHPET_UNLOCK(vhpet);

	if (size == 4) {
		if (offset & 0x4)
			data >>= 32;
	}
	*rval = data;
	return (0);
}

struct vhpet *
vhpet_init(struct vm *vm)
{
	int i, pincount;
	struct vhpet *vhpet;
	uint64_t allowed_irqs;
	struct vhpet_callout_arg *arg;
	struct bintime bt;

	vhpet = malloc(sizeof(struct vhpet), M_VHPET, M_WAITOK | M_ZERO);
        vhpet->vm = vm;
	mtx_init(&vhpet->mtx, "vhpet lock", NULL, MTX_DEF);

	FREQ2BT(HPET_FREQ, &bt);
	vhpet->freq_sbt = bttosbt(bt);

	pincount = vioapic_pincount(vm);
	if (pincount >= 32)
		allowed_irqs = 0xff000000;	/* irqs 24-31 */
	else if (pincount >= 20)
		allowed_irqs = 0xf << (pincount - 4);	/* 4 upper irqs */
	else
		allowed_irqs = 0;

	/*
	 * Initialize HPET timer hardware state.
	 */
	for (i = 0; i < VHPET_NUM_TIMERS; i++) {
		vhpet->timer[i].cap_config = allowed_irqs << 32;
		vhpet->timer[i].cap_config |= HPET_TCAP_PER_INT;
		vhpet->timer[i].cap_config |= HPET_TCAP_FSB_INT_DEL;

		vhpet->timer[i].compval = 0xffffffff;
		callout_init(&vhpet->timer[i].callout, 1);

		arg = &vhpet->timer[i].arg;
		arg->vhpet = vhpet;
		arg->timer_num = i;
	}

	return (vhpet);
}

void
vhpet_cleanup(struct vhpet *vhpet)
{
	int i;

	for (i = 0; i < VHPET_NUM_TIMERS; i++)
		callout_drain(&vhpet->timer[i].callout);

	free(vhpet, M_VHPET);
}

int
vhpet_getcap(struct vm_hpet_cap *cap)
{

	cap->capabilities = vhpet_capabilities();
	return (0);
}

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Adrian Chadd
 * Copyright (c) 2012 Spectra Logic Corporation
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
 *
 */

/**
 * \file dev/xen/timer/timer.c
 * \brief A timer driver for the Xen hypervisor's PV clock.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <sys/timeet.h>
#include <sys/smp.h>
#include <sys/limits.h>
#include <sys/clock.h>
#include <sys/proc.h>

#include <xen/xen-os.h>
#include <xen/features.h>
#include <xen/xen_intr.h>
#include <xen/hypervisor.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/vcpu.h>
#include <xen/error.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/clock.h>
#include <machine/_inttypes.h>
#include <machine/smp.h>
#include <machine/pvclock.h>

#include <dev/xen/timer/timer.h>

#include "clock_if.h"

static devclass_t xentimer_devclass;

#define	NSEC_IN_SEC	1000000000ULL
#define	NSEC_IN_USEC	1000ULL
/* 18446744073 = int(2^64 / NSEC_IN_SC) = 1 ns in 64-bit fractions */
#define	FRAC_IN_NSEC	18446744073LL

/* Xen timers may fire up to 100us off */
#define	XENTIMER_MIN_PERIOD_IN_NSEC	100*NSEC_IN_USEC

/*
 * The real resolution of the PV clock is 1ns, but the highest
 * resolution that FreeBSD supports is 1us, so just use that.
 */
#define	XENCLOCK_RESOLUTION		1

#define	XENTIMER_QUALITY	950

struct xentimer_pcpu_data {
	uint64_t timer;
	uint64_t last_processed;
	void *irq_handle;
};

DPCPU_DEFINE(struct xentimer_pcpu_data, xentimer_pcpu);

DPCPU_DECLARE(struct vcpu_info *, vcpu_info);

struct xentimer_softc {
	device_t dev;
	struct timecounter tc;
	struct eventtimer et;
};

static void
xentimer_identify(driver_t *driver, device_t parent)
{
	if (!xen_domain())
		return;

	/* Handle all Xen PV timers in one device instance. */
	if (devclass_get_device(xentimer_devclass, 0))
		return;

	BUS_ADD_CHILD(parent, 0, "xen_et", 0);
}

static int
xentimer_probe(device_t dev)
{
	KASSERT((xen_domain()), ("Trying to use Xen timer on bare metal"));
	/*
	 * In order to attach, this driver requires the following:
	 * - Vector callback support by the hypervisor, in order to deliver
	 *   timer interrupts to the correct CPU for CPUs other than 0.
	 * - Access to the hypervisor shared info page, in order to look up
	 *   each VCPU's timer information and the Xen wallclock time.
	 * - The hypervisor must say its PV clock is "safe" to use.
	 * - The hypervisor must support VCPUOP hypercalls.
	 * - The maximum number of CPUs supported by FreeBSD must not exceed
	 *   the number of VCPUs supported by the hypervisor.
	 */
#define	XTREQUIRES(condition, reason...)	\
	if (!(condition)) {			\
		device_printf(dev, ## reason);	\
		device_detach(dev);		\
		return (ENXIO);			\
	}

	if (xen_hvm_domain()) {
		XTREQUIRES(xen_vector_callback_enabled,
		           "vector callbacks unavailable\n");
		XTREQUIRES(xen_feature(XENFEAT_hvm_safe_pvclock),
		           "HVM safe pvclock unavailable\n");
	}
	XTREQUIRES(HYPERVISOR_shared_info != NULL,
	           "shared info page unavailable\n");
	XTREQUIRES(HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, 0, NULL) == 0,
	           "VCPUOPs interface unavailable\n");
#undef XTREQUIRES
	device_set_desc(dev, "Xen PV Clock");
	return (BUS_PROBE_NOWILDCARD);
}

/**
 * \brief Get the current time, in nanoseconds, since the hypervisor booted.
 *
 * \param vcpu		vcpu_info structure to fetch the time from.
 *
 */
static uint64_t
xen_fetch_vcpu_time(struct vcpu_info *vcpu)
{
	struct pvclock_vcpu_time_info *time;

	time = (struct pvclock_vcpu_time_info *) &vcpu->time;

	return (pvclock_get_timecount(time));
}

static uint32_t
xentimer_get_timecount(struct timecounter *tc)
{
	uint64_t vcpu_time;

	/*
	 * We don't disable preemption here because the worst that can
	 * happen is reading the vcpu_info area of a different CPU than
	 * the one we are currently running on, but that would also
	 * return a valid tc (and we avoid the overhead of
	 * critical_{enter/exit} calls).
	 */
	vcpu_time = xen_fetch_vcpu_time(DPCPU_GET(vcpu_info));

	return (vcpu_time & UINT32_MAX);
}

/**
 * \brief Fetch the hypervisor boot time, known as the "Xen wallclock".
 *
 * \param ts		Timespec to store the current stable value.
 * \param version	Pointer to store the corresponding wallclock version.
 *
 * \note This value is updated when Domain-0 shifts its clock to follow
 *       clock drift, e.g. as detected by NTP.
 */
static void
xen_fetch_wallclock(struct timespec *ts)
{
	shared_info_t *src = HYPERVISOR_shared_info;
	struct pvclock_wall_clock *wc;

	wc = (struct pvclock_wall_clock *) &src->wc_version;

	pvclock_get_wallclock(wc, ts);
}

static void
xen_fetch_uptime(struct timespec *ts)
{
	uint64_t uptime;

	uptime = xen_fetch_vcpu_time(DPCPU_GET(vcpu_info));

	ts->tv_sec = uptime / NSEC_IN_SEC;
	ts->tv_nsec = uptime % NSEC_IN_SEC;
}

static int
xentimer_settime(device_t dev __unused, struct timespec *ts)
{
	struct xen_platform_op settime;
	int ret;

	/*
	 * Don't return EINVAL here; just silently fail if the domain isn't
	 * privileged enough to set the TOD.
	 */
	if (!xen_initial_domain())
		return (0);

	settime.cmd = XENPF_settime64;
	settime.u.settime64.mbz = 0;
	settime.u.settime64.secs = ts->tv_sec;
	settime.u.settime64.nsecs = ts->tv_nsec;
	settime.u.settime64.system_time =
		xen_fetch_vcpu_time(DPCPU_GET(vcpu_info));

	ret = HYPERVISOR_platform_op(&settime);
	ret = ret != 0 ? xen_translate_error(ret) : 0;
	if (ret != 0 && bootverbose)
		device_printf(dev, "failed to set Xen PV clock: %d\n", ret);

	return (ret);
}

/**
 * \brief Return current time according to the Xen Hypervisor wallclock.
 *
 * \param dev	Xentimer device.
 * \param ts	Pointer to store the wallclock time.
 *
 * \note  The Xen time structures document the hypervisor start time and the
 *        uptime-since-hypervisor-start (in nsec.) They need to be combined
 *        in order to calculate a TOD clock.
 */
static int
xentimer_gettime(device_t dev, struct timespec *ts)
{
	struct timespec u_ts;

	timespecclear(ts);
	xen_fetch_wallclock(ts);
	xen_fetch_uptime(&u_ts);
	timespecadd(ts, &u_ts, ts);

	return (0);
}

/**
 * \brief Handle a timer interrupt for the Xen PV timer driver.
 *
 * \param arg	Xen timer driver softc that is expecting the interrupt.
 */
static int
xentimer_intr(void *arg)
{
	struct xentimer_softc *sc = (struct xentimer_softc *)arg;
	struct xentimer_pcpu_data *pcpu = DPCPU_PTR(xentimer_pcpu);

	pcpu->last_processed = xen_fetch_vcpu_time(DPCPU_GET(vcpu_info));
	if (pcpu->timer != 0 && sc->et.et_active)
		sc->et.et_event_cb(&sc->et, sc->et.et_arg);

	return (FILTER_HANDLED);
}

static int
xentimer_vcpu_start_timer(int vcpu, uint64_t next_time)
{
	struct vcpu_set_singleshot_timer single;

	single.timeout_abs_ns = next_time;
	/* Get an event anyway, even if the timeout is already expired */
	single.flags          = 0;
	return (HYPERVISOR_vcpu_op(VCPUOP_set_singleshot_timer, vcpu, &single));
}

static int
xentimer_vcpu_stop_timer(int vcpu)
{

	return (HYPERVISOR_vcpu_op(VCPUOP_stop_singleshot_timer, vcpu, NULL));
}

/**
 * \brief Set the next oneshot time for the current CPU.
 *
 * \param et	Xen timer driver event timer to schedule on.
 * \param first	Delta to the next time to schedule the interrupt for.
 * \param period Not used.
 *
 * \note See eventtimers(9) for more information.
 * \note 
 *
 * \returns 0
 */
static int
xentimer_et_start(struct eventtimer *et,
    sbintime_t first, sbintime_t period)
{
	int error;
	struct xentimer_softc *sc = et->et_priv;
	int cpu = PCPU_GET(vcpu_id);
	struct xentimer_pcpu_data *pcpu = DPCPU_PTR(xentimer_pcpu);
	struct vcpu_info *vcpu = DPCPU_GET(vcpu_info);
	uint64_t first_in_ns, next_time;
#ifdef INVARIANTS
	struct thread *td = curthread;
#endif

	KASSERT(td->td_critnest != 0,
	    ("xentimer_et_start called without preemption disabled"));

	/* See sbttots() for this formula. */
	first_in_ns = (((first >> 32) * NSEC_IN_SEC) +
	               (((uint64_t)NSEC_IN_SEC * (uint32_t)first) >> 32));

	next_time = xen_fetch_vcpu_time(vcpu) + first_in_ns;
	error = xentimer_vcpu_start_timer(cpu, next_time);
	if (error)
		panic("%s: Error %d setting singleshot timer to %"PRIu64"\n",
		    device_get_nameunit(sc->dev), error, next_time);

	pcpu->timer = next_time;
	return (error);
}

/**
 * \brief Cancel the event timer's currently running timer, if any.
 */
static int
xentimer_et_stop(struct eventtimer *et)
{
	int cpu = PCPU_GET(vcpu_id);
	struct xentimer_pcpu_data *pcpu = DPCPU_PTR(xentimer_pcpu);

	pcpu->timer = 0;
	return (xentimer_vcpu_stop_timer(cpu));
}

/**
 * \brief Attach a Xen PV timer driver instance.
 * 
 * \param dev	Bus device object to attach.
 *
 * \note
 * \returns EINVAL 
 */
static int
xentimer_attach(device_t dev)
{
	struct xentimer_softc *sc = device_get_softc(dev);
	int error, i;

	sc->dev = dev;

	/* Bind an event channel to a VIRQ on each VCPU. */
	CPU_FOREACH(i) {
		struct xentimer_pcpu_data *pcpu;

		pcpu = DPCPU_ID_PTR(i, xentimer_pcpu);
		error = HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, i, NULL);
		if (error) {
			device_printf(dev, "Error disabling Xen periodic timer "
			                   "on CPU %d\n", i);
			return (error);
		}

		error = xen_intr_bind_virq(dev, VIRQ_TIMER, i, xentimer_intr,
		    NULL, sc, INTR_TYPE_CLK, &pcpu->irq_handle);
		if (error) {
			device_printf(dev, "Error %d binding VIRQ_TIMER "
			    "to VCPU %d\n", error, i);
			return (error);
		}
		xen_intr_describe(pcpu->irq_handle, "c%d", i);
	}

	/* Register the event timer. */
	sc->et.et_name = "XENTIMER";
	sc->et.et_quality = XENTIMER_QUALITY;
	sc->et.et_flags = ET_FLAGS_ONESHOT | ET_FLAGS_PERCPU;
	sc->et.et_frequency = NSEC_IN_SEC;
	/* See tstosbt() for this formula */
	sc->et.et_min_period = (XENTIMER_MIN_PERIOD_IN_NSEC *
	                        (((uint64_t)1 << 63) / 500000000) >> 32);
	sc->et.et_max_period = ((sbintime_t)4 << 32);
	sc->et.et_start = xentimer_et_start;
	sc->et.et_stop = xentimer_et_stop;
	sc->et.et_priv = sc;
	et_register(&sc->et);

	/* Register the timecounter. */
	sc->tc.tc_name = "XENTIMER";
	sc->tc.tc_quality = XENTIMER_QUALITY;
	/*
	 * FIXME: due to the lack of ordering during resume, FreeBSD cannot
	 * guarantee that the Xen PV timer is resumed before any other device
	 * attempts to make use of it, so mark it as not safe for suspension
	 * (ie: remove the TC_FLAGS_SUSPEND_SAFE flag).
	 *
	 * NB: This was not a problem in previous FreeBSD versions because the
	 * timer was directly attached to the nexus, but it is an issue now
	 * that the timer is attached to the xenpv bus, and thus resumed
	 * later.
	 *
	 * sc->tc.tc_flags = TC_FLAGS_SUSPEND_SAFE;
	 */
    	/*
	 * The underlying resolution is in nanoseconds, since the timer info
	 * scales TSC frequencies using a fraction that represents time in
	 * terms of nanoseconds.
	 */
	sc->tc.tc_frequency = NSEC_IN_SEC;
	sc->tc.tc_counter_mask = ~0u;
	sc->tc.tc_get_timecount = xentimer_get_timecount;
	sc->tc.tc_priv = sc;
	tc_init(&sc->tc);

	/* Register the Hypervisor wall clock */
	clock_register(dev, XENCLOCK_RESOLUTION);

	return (0);
}

static int
xentimer_detach(device_t dev)
{

	/* Implement Xen PV clock teardown - XXX see hpet_detach ? */
	/* If possible:
	 * 1. need to deregister timecounter
	 * 2. need to deregister event timer
	 * 3. need to deregister virtual IRQ event channels
	 */
	return (EBUSY);
}

static void
xentimer_percpu_resume(void *arg)
{
	device_t dev = (device_t) arg;
	struct xentimer_softc *sc = device_get_softc(dev);

	xentimer_et_start(&sc->et, sc->et.et_min_period, 0);
}

static int
xentimer_resume(device_t dev)
{
	int error;
	int i;

	/* Disable the periodic timer */
	CPU_FOREACH(i) {
		error = HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, i, NULL);
		if (error != 0) {
			device_printf(dev,
			    "Error disabling Xen periodic timer on CPU %d\n",
			    i);
			return (error);
		}
	}

	/* Reset the last uptime value */
	pvclock_resume();

	/* Reset the RTC clock */
	inittodr(time_second);

	/* Kick the timers on all CPUs */
	smp_rendezvous(NULL, xentimer_percpu_resume, NULL, dev);

	if (bootverbose)
		device_printf(dev, "resumed operation after suspension\n");

	return (0);
}

static int
xentimer_suspend(device_t dev)
{
	return (0);
}

/*
 * Xen early clock init
 */
void
xen_clock_init(void)
{
}

/*
 * Xen PV DELAY function
 *
 * When running on PVH mode we don't have an emulated i8524, so
 * make use of the Xen time info in order to code a simple DELAY
 * function that can be used during early boot.
 */
void
xen_delay(int n)
{
	struct vcpu_info *vcpu = &HYPERVISOR_shared_info->vcpu_info[0];
	uint64_t end_ns;
	uint64_t current;

	end_ns = xen_fetch_vcpu_time(vcpu);
	end_ns += n * NSEC_IN_USEC;

	for (;;) {
		current = xen_fetch_vcpu_time(vcpu);
		if (current >= end_ns)
			break;
	}
}

static device_method_t xentimer_methods[] = {
	DEVMETHOD(device_identify, xentimer_identify),
	DEVMETHOD(device_probe, xentimer_probe),
	DEVMETHOD(device_attach, xentimer_attach),
	DEVMETHOD(device_detach, xentimer_detach),
	DEVMETHOD(device_suspend, xentimer_suspend),
	DEVMETHOD(device_resume, xentimer_resume),
	/* clock interface */
	DEVMETHOD(clock_gettime, xentimer_gettime),
	DEVMETHOD(clock_settime, xentimer_settime),
	DEVMETHOD_END
};

static driver_t xentimer_driver = {
	"xen_et",
	xentimer_methods,
	sizeof(struct xentimer_softc),
};

DRIVER_MODULE(xentimer, xenpv, xentimer_driver, xentimer_devclass, 0, 0);
MODULE_DEPEND(xentimer, xenpv, 1, 1, 1);

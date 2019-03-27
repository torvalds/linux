/*-
 * Copyright (c) 2015,2016-2017 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/systm.h>
#include <sys/timeet.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/vmbus/hyperv_reg.h>
#include <dev/hyperv/vmbus/hyperv_var.h>
#include <dev/hyperv/vmbus/vmbus_var.h>

#define VMBUS_ET_NAME			"hvet"

#define MSR_HV_STIMER0_CFG_SINT		\
	((((uint64_t)VMBUS_SINT_TIMER) << MSR_HV_STIMER_CFG_SINT_SHIFT) & \
	 MSR_HV_STIMER_CFG_SINT_MASK)

/*
 * Additionally required feature:
 * - SynIC is needed for interrupt generation.
 */
#define CPUID_HV_ET_MASK		(CPUID_HV_MSR_SYNIC |		\
					 CPUID_HV_MSR_SYNTIMER)

static void			vmbus_et_identify(driver_t *, device_t);
static int			vmbus_et_probe(device_t);
static int			vmbus_et_attach(device_t);
static int			vmbus_et_detach(device_t);
static int			vmbus_et_start(struct eventtimer *, sbintime_t,
				    sbintime_t);

static struct eventtimer	vmbus_et;

static device_method_t vmbus_et_methods[] = {
	DEVMETHOD(device_identify,	vmbus_et_identify),
	DEVMETHOD(device_probe,		vmbus_et_probe),
	DEVMETHOD(device_attach,	vmbus_et_attach),
	DEVMETHOD(device_detach,	vmbus_et_detach),

	DEVMETHOD_END
};

static driver_t vmbus_et_driver = {
	VMBUS_ET_NAME,
	vmbus_et_methods,
	0
};

static devclass_t vmbus_et_devclass;

DRIVER_MODULE(hv_et, vmbus, vmbus_et_driver, vmbus_et_devclass, NULL, NULL);
MODULE_VERSION(hv_et, 1);

static __inline uint64_t
hyperv_sbintime2count(sbintime_t time)
{
	struct timespec val;

	val = sbttots(time);
	return (val.tv_sec * HYPERV_TIMER_FREQ) +
	    (val.tv_nsec / HYPERV_TIMER_NS_FACTOR);
}

static int
vmbus_et_start(struct eventtimer *et __unused, sbintime_t first,
    sbintime_t period __unused)
{
	uint64_t current;

	current = hyperv_tc64();
	current += hyperv_sbintime2count(first);
	wrmsr(MSR_HV_STIMER0_COUNT, current);

	return (0);
}

void
vmbus_et_intr(struct trapframe *frame)
{
	struct trapframe *oldframe;
	struct thread *td;

	if (vmbus_et.et_active) {
		td = curthread;
		td->td_intr_nesting_level++;
		oldframe = td->td_intr_frame;
		td->td_intr_frame = frame;
		vmbus_et.et_event_cb(&vmbus_et, vmbus_et.et_arg);
		td->td_intr_frame = oldframe;
		td->td_intr_nesting_level--;
	}
}

static void
vmbus_et_identify(driver_t *driver, device_t parent)
{
	if (device_get_unit(parent) != 0 ||
	    device_find_child(parent, VMBUS_ET_NAME, -1) != NULL ||
	    (hyperv_features & CPUID_HV_ET_MASK) != CPUID_HV_ET_MASK ||
	    hyperv_tc64 == NULL)
		return;

	device_add_child(parent, VMBUS_ET_NAME, -1);
}

static int
vmbus_et_probe(device_t dev)
{
	if (resource_disabled(VMBUS_ET_NAME, 0))
		return (ENXIO);

	device_set_desc(dev, "Hyper-V event timer");

	return (BUS_PROBE_NOWILDCARD);
}

static void
vmbus_et_config(void *arg __unused)
{
	/*
	 * Make sure that STIMER0 is really disabled before writing
	 * to STIMER0_CONFIG.
	 *
	 * "Writing to the configuration register of a timer that
	 *  is already enabled may result in undefined behaviour."
	 */
	for (;;) {
		uint64_t val;

		/* Stop counting, and this also implies disabling STIMER0 */
		wrmsr(MSR_HV_STIMER0_COUNT, 0);

		val = rdmsr(MSR_HV_STIMER0_CONFIG);
		if ((val & MSR_HV_STIMER_CFG_ENABLE) == 0)
			break;
		cpu_spinwait();
	}
	wrmsr(MSR_HV_STIMER0_CONFIG,
	    MSR_HV_STIMER_CFG_AUTOEN | MSR_HV_STIMER0_CFG_SINT);
}

static int
vmbus_et_attach(device_t dev)
{
	/* TODO: use independent IDT vector */

	vmbus_et.et_name = "Hyper-V";
	vmbus_et.et_flags = ET_FLAGS_ONESHOT | ET_FLAGS_PERCPU;
	vmbus_et.et_quality = 1000;
	vmbus_et.et_frequency = HYPERV_TIMER_FREQ;
	vmbus_et.et_min_period = (0x00000001ULL << 32) / HYPERV_TIMER_FREQ;
	vmbus_et.et_max_period = (0xfffffffeULL << 32) / HYPERV_TIMER_FREQ;
	vmbus_et.et_start = vmbus_et_start;

	/*
	 * Delay a bit to make sure that hyperv_tc64 will not return 0,
	 * since writing 0 to STIMER0_COUNT will disable STIMER0.
	 */
	DELAY(100);
	smp_rendezvous(NULL, vmbus_et_config, NULL, NULL);

	return (et_register(&vmbus_et));
}

static int
vmbus_et_detach(device_t dev)
{
	return (et_deregister(&vmbus_et));
}

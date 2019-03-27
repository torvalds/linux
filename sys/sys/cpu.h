/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2007 Nate Lawson (SDG)
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
 * $FreeBSD$
 */

#ifndef _SYS_CPU_H_
#define _SYS_CPU_H_

#include <sys/eventhandler.h>

/*
 * CPU device support.
 */

#define CPU_IVAR_PCPU		1
#define CPU_IVAR_NOMINAL_MHZ	2
#define CPU_IVAR_CPUID_SIZE	3
#define CPU_IVAR_CPUID		4

static __inline struct pcpu *cpu_get_pcpu(device_t dev)
{
	uintptr_t v = 0;
	BUS_READ_IVAR(device_get_parent(dev), dev, CPU_IVAR_PCPU, &v);
	return ((struct pcpu *)v);
}

static __inline int32_t cpu_get_nominal_mhz(device_t dev)
{
	uintptr_t v = 0;
	if (BUS_READ_IVAR(device_get_parent(dev), dev,
	    CPU_IVAR_NOMINAL_MHZ, &v) != 0)
		return (-1);
	return ((int32_t)v);
}

static __inline const uint32_t *cpu_get_cpuid(device_t dev, size_t *count)
{
	uintptr_t v = 0;
	if (BUS_READ_IVAR(device_get_parent(dev), dev,
	    CPU_IVAR_CPUID_SIZE, &v) != 0)
		return (NULL);
	*count = (size_t)v;

	if (BUS_READ_IVAR(device_get_parent(dev), dev,
	    CPU_IVAR_CPUID, &v) != 0)
		return (NULL);
	return ((const uint32_t *)v);
}

/*
 * CPU frequency control interface.
 */

/* Each driver's CPU frequency setting is exported in this format. */
struct cf_setting {
	int	freq;	/* CPU clock in Mhz or 100ths of a percent. */
	int	volts;	/* Voltage in mV. */
	int	power;	/* Power consumed in mW. */
	int	lat;	/* Transition latency in us. */
	device_t dev;	/* Driver providing this setting. */
	int	spec[4];/* Driver-specific storage for non-standard info. */
};

/* Maximum number of settings a given driver can have. */
#define MAX_SETTINGS		256

/* A combination of settings is a level. */
struct cf_level {
	struct cf_setting	total_set;
	struct cf_setting	abs_set;
	struct cf_setting	rel_set[MAX_SETTINGS];
	int			rel_count;
	TAILQ_ENTRY(cf_level)	link;
};

TAILQ_HEAD(cf_level_lst, cf_level);

/* Drivers should set all unknown values to this. */
#define CPUFREQ_VAL_UNKNOWN	(-1)

/*
 * Every driver offers a type of CPU control.  Absolute levels are mutually
 * exclusive while relative levels modify the current absolute level.  There
 * may be multiple absolute and relative drivers available on a given
 * system.
 *
 * For example, consider a system with two absolute drivers that provide
 * frequency settings of 100, 200 and 300, 400 and a relative driver that
 * provides settings of 50%, 100%.  The cpufreq core would export frequency
 * levels of 50, 100, 150, 200, 300, 400.
 *
 * The "info only" flag signifies that settings returned by
 * CPUFREQ_DRV_SETTINGS cannot be passed to the CPUFREQ_DRV_SET method and
 * are only informational.  This is for some drivers that can return
 * information about settings but rely on another machine-dependent driver
 * for actually performing the frequency transition (e.g., ACPI performance
 * states of type "functional fixed hardware.")
 */
#define CPUFREQ_TYPE_MASK	0xffff
#define CPUFREQ_TYPE_RELATIVE	(1<<0)
#define CPUFREQ_TYPE_ABSOLUTE	(1<<1)
#define CPUFREQ_FLAG_INFO_ONLY	(1<<16)

/*
 * When setting a level, the caller indicates the priority of this request.
 * Priorities determine, among other things, whether a level can be
 * overridden by other callers.  For example, if the user sets a level but
 * the system thermal driver needs to override it for emergency cooling,
 * the driver would use a higher priority.  Once the event has passed, the
 * driver would call cpufreq to resume any previous level.
 */
#define CPUFREQ_PRIO_HIGHEST	1000000
#define CPUFREQ_PRIO_KERN	1000
#define CPUFREQ_PRIO_USER	100
#define CPUFREQ_PRIO_LOWEST	0

/*
 * Register and unregister a driver with the cpufreq core.  Once a driver
 * is registered, it must support calls to its CPUFREQ_GET, CPUFREQ_GET_LEVEL,
 * and CPUFREQ_SET methods.  It must also unregister before returning from
 * its DEVICE_DETACH method.
 */
int	cpufreq_register(device_t dev);
int	cpufreq_unregister(device_t dev);

/*
 * Notify the cpufreq core that the number of or values for settings have
 * changed.
 */
int	cpufreq_settings_changed(device_t dev);

/*
 * Eventhandlers that are called before and after a change in frequency.
 * The new level and the result of the change (0 is success) is passed in.
 * If the driver wishes to revoke the change from cpufreq_pre_change, it
 * stores a non-zero error code in the result parameter and the change will
 * not be made.  If the post-change eventhandler gets a non-zero result, 
 * no change was made and the previous level remains in effect.  If a change
 * is revoked, the post-change eventhandler is still called with the error
 * value supplied by the revoking driver.  This gives listeners who cached
 * some data in preparation for a level change a chance to clean up.
 */
typedef void (*cpufreq_pre_notify_fn)(void *, const struct cf_level *, int *);
typedef void (*cpufreq_post_notify_fn)(void *, const struct cf_level *, int);
EVENTHANDLER_DECLARE(cpufreq_pre_change, cpufreq_pre_notify_fn);
EVENTHANDLER_DECLARE(cpufreq_post_change, cpufreq_post_notify_fn);

/*
 * Eventhandler called when the available list of levels changed.
 * The unit number of the device (i.e. "cpufreq0") whose levels changed
 * is provided so the listener can retrieve the new list of levels.
 */
typedef void (*cpufreq_levels_notify_fn)(void *, int);
EVENTHANDLER_DECLARE(cpufreq_levels_changed, cpufreq_levels_notify_fn);

/* Allow values to be +/- a bit since sometimes we have to estimate. */
#define CPUFREQ_CMP(x, y)	(abs((x) - (y)) < 25)

/*
 * Machine-dependent functions.
 */

/* Estimate the current clock rate for the given CPU id. */
int	cpu_est_clockrate(int cpu_id, uint64_t *rate);

#endif /* !_SYS_CPU_H_ */

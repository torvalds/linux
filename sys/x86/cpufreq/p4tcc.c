/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Nate Lawson
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Throttle clock frequency by using the thermal control circuit.  This
 * operates independently of SpeedStep and ACPI throttling and is supported
 * on Pentium 4 and later models (feature TM).
 *
 * Reference:  Intel Developer's manual v.3 #245472-012
 *
 * The original version of this driver was written by Ted Unangst for
 * OpenBSD and imported by Maxim Sobolev.  It was rewritten by Nate Lawson
 * for use with the cpufreq framework.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/md_var.h>
#include <machine/specialreg.h>

#include "cpufreq_if.h"

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>
#include "acpi_if.h"
 
struct p4tcc_softc {
	device_t	dev;
	int		set_count;
	int		lowest_val;
	int		auto_mode;
};

#define TCC_NUM_SETTINGS	8

#define TCC_ENABLE_ONDEMAND	(1<<4)
#define TCC_REG_OFFSET		1
#define TCC_SPEED_PERCENT(x)	((10000 * (x)) / TCC_NUM_SETTINGS)

static int	p4tcc_features(driver_t *driver, u_int *features);
static void	p4tcc_identify(driver_t *driver, device_t parent);
static int	p4tcc_probe(device_t dev);
static int	p4tcc_attach(device_t dev);
static int	p4tcc_detach(device_t dev);
static int	p4tcc_settings(device_t dev, struct cf_setting *sets,
		    int *count);
static int	p4tcc_set(device_t dev, const struct cf_setting *set);
static int	p4tcc_get(device_t dev, struct cf_setting *set);
static int	p4tcc_type(device_t dev, int *type);

static device_method_t p4tcc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	p4tcc_identify),
	DEVMETHOD(device_probe,		p4tcc_probe),
	DEVMETHOD(device_attach,	p4tcc_attach),
	DEVMETHOD(device_detach,	p4tcc_detach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	p4tcc_set),
	DEVMETHOD(cpufreq_drv_get,	p4tcc_get),
	DEVMETHOD(cpufreq_drv_type,	p4tcc_type),
	DEVMETHOD(cpufreq_drv_settings,	p4tcc_settings),

	/* ACPI interface */
	DEVMETHOD(acpi_get_features,	p4tcc_features),

	{0, 0}
};

static driver_t p4tcc_driver = {
	"p4tcc",
	p4tcc_methods,
	sizeof(struct p4tcc_softc),
};

static devclass_t p4tcc_devclass;
DRIVER_MODULE(p4tcc, cpu, p4tcc_driver, p4tcc_devclass, 0, 0);

static int
p4tcc_features(driver_t *driver, u_int *features)
{

	/* Notify the ACPI CPU that we support direct access to MSRs */
	*features = ACPI_CAP_THR_MSRS;
	return (0);
}

static void
p4tcc_identify(driver_t *driver, device_t parent)
{

	if ((cpu_feature & (CPUID_ACPI | CPUID_TM)) != (CPUID_ACPI | CPUID_TM))
		return;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "p4tcc", -1) != NULL)
		return;

	/*
	 * We attach a p4tcc child for every CPU since settings need to
	 * be performed on every CPU in the SMP case.  See section 13.15.3
	 * of the IA32 Intel Architecture Software Developer's Manual,
	 * Volume 3, for more info.
	 */
	if (BUS_ADD_CHILD(parent, 10, "p4tcc", -1) == NULL)
		device_printf(parent, "add p4tcc child failed\n");
}

static int
p4tcc_probe(device_t dev)
{

	if (resource_disabled("p4tcc", 0))
		return (ENXIO);

	device_set_desc(dev, "CPU Frequency Thermal Control");
	return (0);
}

static int
p4tcc_attach(device_t dev)
{
	struct p4tcc_softc *sc;
	struct cf_setting set;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->set_count = TCC_NUM_SETTINGS;

	/*
	 * On boot, the TCC is usually in Automatic mode where reading the
	 * current performance level is likely to produce bogus results.
	 * We record that state here and don't trust the contents of the
	 * status MSR until we've set it ourselves.
	 */
	sc->auto_mode = TRUE;

	/*
	 * XXX: After a cursory glance at various Intel specification
	 * XXX: updates it seems like these tests for errata is bogus.
	 * XXX: As far as I can tell, the failure mode is benign, in
	 * XXX: that cpus with no errata will have their bottom two
	 * XXX: STPCLK# rates disabled, so rather than waste more time
	 * XXX: hunting down intel docs, just document it and punt. /phk
	 */
	switch (cpu_id & 0xff) {
	case 0x22:
	case 0x24:
	case 0x25:
	case 0x27:
	case 0x29:
		/*
		 * These CPU models hang when set to 12.5%.
		 * See Errata O50, P44, and Z21.
		 */
		sc->set_count -= 1;
		break;
	case 0x07:	/* errata N44 and P18 */
	case 0x0a:
	case 0x12:
	case 0x13:
	case 0x62:	/* Pentium D B1: errata AA21 */
	case 0x64:	/* Pentium D C1: errata AA21 */
	case 0x65:	/* Pentium D D0: errata AA21 */
		/*
		 * These CPU models hang when set to 12.5% or 25%.
		 * See Errata N44, P18l and AA21.
		 */
		sc->set_count -= 2;
		break;
	}
	sc->lowest_val = TCC_NUM_SETTINGS - sc->set_count + 1;

	/*
	 * Before we finish attach, switch to 100%.  It's possible the BIOS
	 * set us to a lower rate.  The user can override this after boot.
	 */
	set.freq = 10000;
	p4tcc_set(dev, &set);

	cpufreq_register(dev);
	return (0);
}

static int
p4tcc_detach(device_t dev)
{
	struct cf_setting set;
	int error;

	error = cpufreq_unregister(dev);
	if (error)
		return (error);

	/*
	 * Before we finish detach, switch to Automatic mode.
	 */
	set.freq = 10000;
	p4tcc_set(dev, &set);
	return(0);
}

static int
p4tcc_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct p4tcc_softc *sc;
	int i, val;

	sc = device_get_softc(dev);
	if (sets == NULL || count == NULL)
		return (EINVAL);
	if (*count < sc->set_count)
		return (E2BIG);

	/* Return a list of valid settings for this driver. */
	memset(sets, CPUFREQ_VAL_UNKNOWN, sizeof(*sets) * sc->set_count);
	val = TCC_NUM_SETTINGS;
	for (i = 0; i < sc->set_count; i++, val--) {
		sets[i].freq = TCC_SPEED_PERCENT(val);
		sets[i].dev = dev;
	}
	*count = sc->set_count;

	return (0);
}

static int
p4tcc_set(device_t dev, const struct cf_setting *set)
{
	struct p4tcc_softc *sc;
	uint64_t mask, msr;
	int val;

	if (set == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);

	/*
	 * Validate requested state converts to a setting that is an integer
	 * from [sc->lowest_val .. TCC_NUM_SETTINGS].
	 */
	val = set->freq * TCC_NUM_SETTINGS / 10000;
	if (val * 10000 != set->freq * TCC_NUM_SETTINGS ||
	    val < sc->lowest_val || val > TCC_NUM_SETTINGS)
		return (EINVAL);

	/*
	 * Read the current register and mask off the old setting and
	 * On-Demand bit.  If the new val is < 100%, set it and the On-Demand
	 * bit, otherwise just return to Automatic mode.
	 */
	msr = rdmsr(MSR_THERM_CONTROL);
	mask = (TCC_NUM_SETTINGS - 1) << TCC_REG_OFFSET;
	msr &= ~(mask | TCC_ENABLE_ONDEMAND);
	if (val < TCC_NUM_SETTINGS)
		msr |= (val << TCC_REG_OFFSET) | TCC_ENABLE_ONDEMAND;
	wrmsr(MSR_THERM_CONTROL, msr);

	/*
	 * Record whether we're now in Automatic or On-Demand mode.  We have
	 * to cache this since there is no reliable way to check if TCC is in
	 * Automatic mode (i.e., at 100% or possibly 50%).  Reading bit 4 of
	 * the ACPI Thermal Monitor Control Register produces 0 no matter
	 * what the current mode.
	 */
	if (msr & TCC_ENABLE_ONDEMAND)
		sc->auto_mode = FALSE;
	else
		sc->auto_mode = TRUE;

	return (0);
}

static int
p4tcc_get(device_t dev, struct cf_setting *set)
{
	struct p4tcc_softc *sc;
	uint64_t msr;
	int val;

	if (set == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);

	/*
	 * Read the current register and extract the current setting.  If
	 * in automatic mode, assume we're at TCC_NUM_SETTINGS (100%).
	 *
	 * XXX This is not completely reliable since at high temperatures
	 * the CPU may be automatically throttling to 50% but it's the best
	 * we can do.
	 */
	if (!sc->auto_mode) {
		msr = rdmsr(MSR_THERM_CONTROL);
		val = (msr >> TCC_REG_OFFSET) & (TCC_NUM_SETTINGS - 1);
	} else
		val = TCC_NUM_SETTINGS;

	memset(set, CPUFREQ_VAL_UNKNOWN, sizeof(*set));
	set->freq = TCC_SPEED_PERCENT(val);
	set->dev = dev;

	return (0);
}

static int
p4tcc_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_RELATIVE;
	return (0);
}

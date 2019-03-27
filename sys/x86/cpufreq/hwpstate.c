/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Nate Lawson
 * Copyright (c) 2004 Colin Percival
 * Copyright (c) 2004-2005 Bruno Durcot
 * Copyright (c) 2004 FUKUDA Nobuhiko
 * Copyright (c) 2009 Michael Reifenberger
 * Copyright (c) 2009 Norikatsu Shigemura
 * Copyright (c) 2008-2009 Gen Otsuji
 *
 * This code is depending on kern_cpu.c, est.c, powernow.c, p4tcc.c, smist.c
 * in various parts. The authors of these files are Nate Lawson,
 * Colin Percival, Bruno Durcot, and FUKUDA Nobuhiko.
 * This code contains patches by Michael Reifenberger and Norikatsu Shigemura.
 * Thank you.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * For more info:
 * BIOS and Kernel Developer's Guide(BKDG) for AMD Family 10h Processors
 * 31116 Rev 3.20  February 04, 2009
 * BIOS and Kernel Developer's Guide(BKDG) for AMD Family 11h Processors
 * 41256 Rev 3.00 - July 07, 2008
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/smp.h>
#include <sys/sched.h>

#include <machine/md_var.h>
#include <machine/cputypes.h>
#include <machine/specialreg.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>

#include "acpi_if.h"
#include "cpufreq_if.h"

#define	MSR_AMD_10H_11H_LIMIT	0xc0010061
#define	MSR_AMD_10H_11H_CONTROL	0xc0010062
#define	MSR_AMD_10H_11H_STATUS	0xc0010063
#define	MSR_AMD_10H_11H_CONFIG	0xc0010064

#define	AMD_10H_11H_MAX_STATES	16

/* for MSR_AMD_10H_11H_LIMIT C001_0061 */
#define	AMD_10H_11H_GET_PSTATE_MAX_VAL(msr)	(((msr) >> 4) & 0x7)
#define	AMD_10H_11H_GET_PSTATE_LIMIT(msr)	(((msr)) & 0x7)
/* for MSR_AMD_10H_11H_CONFIG 10h:C001_0064:68 / 11h:C001_0064:6B */
#define	AMD_10H_11H_CUR_VID(msr)		(((msr) >> 9) & 0x7F)
#define	AMD_10H_11H_CUR_DID(msr)		(((msr) >> 6) & 0x07)
#define	AMD_10H_11H_CUR_FID(msr)		((msr) & 0x3F)

#define	AMD_17H_CUR_VID(msr)			(((msr) >> 14) & 0xFF)
#define	AMD_17H_CUR_DID(msr)			(((msr) >> 8) & 0x3F)
#define	AMD_17H_CUR_FID(msr)			((msr) & 0xFF)

#define	HWPSTATE_DEBUG(dev, msg...)			\
	do {						\
		if (hwpstate_verbose)			\
			device_printf(dev, msg);	\
	} while (0)

struct hwpstate_setting {
	int	freq;		/* CPU clock in Mhz or 100ths of a percent. */
	int	volts;		/* Voltage in mV. */
	int	power;		/* Power consumed in mW. */
	int	lat;		/* Transition latency in us. */
	int	pstate_id;	/* P-State id */
};

struct hwpstate_softc {
	device_t		dev;
	struct hwpstate_setting	hwpstate_settings[AMD_10H_11H_MAX_STATES];
	int			cfnum;
};

static void	hwpstate_identify(driver_t *driver, device_t parent);
static int	hwpstate_probe(device_t dev);
static int	hwpstate_attach(device_t dev);
static int	hwpstate_detach(device_t dev);
static int	hwpstate_set(device_t dev, const struct cf_setting *cf);
static int	hwpstate_get(device_t dev, struct cf_setting *cf);
static int	hwpstate_settings(device_t dev, struct cf_setting *sets, int *count);
static int	hwpstate_type(device_t dev, int *type);
static int	hwpstate_shutdown(device_t dev);
static int	hwpstate_features(driver_t *driver, u_int *features);
static int	hwpstate_get_info_from_acpi_perf(device_t dev, device_t perf_dev);
static int	hwpstate_get_info_from_msr(device_t dev);
static int	hwpstate_goto_pstate(device_t dev, int pstate_id);

static int	hwpstate_verbose;
SYSCTL_INT(_debug, OID_AUTO, hwpstate_verbose, CTLFLAG_RWTUN,
    &hwpstate_verbose, 0, "Debug hwpstate");

static int	hwpstate_verify;
SYSCTL_INT(_debug, OID_AUTO, hwpstate_verify, CTLFLAG_RWTUN,
    &hwpstate_verify, 0, "Verify P-state after setting");

static device_method_t hwpstate_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	hwpstate_identify),
	DEVMETHOD(device_probe,		hwpstate_probe),
	DEVMETHOD(device_attach,	hwpstate_attach),
	DEVMETHOD(device_detach,	hwpstate_detach),
	DEVMETHOD(device_shutdown,	hwpstate_shutdown),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	hwpstate_set),
	DEVMETHOD(cpufreq_drv_get,	hwpstate_get),
	DEVMETHOD(cpufreq_drv_settings,	hwpstate_settings),
	DEVMETHOD(cpufreq_drv_type,	hwpstate_type),

	/* ACPI interface */
	DEVMETHOD(acpi_get_features,	hwpstate_features),

	{0, 0}
};

static devclass_t hwpstate_devclass;
static driver_t hwpstate_driver = {
	"hwpstate",
	hwpstate_methods,
	sizeof(struct hwpstate_softc),
};

DRIVER_MODULE(hwpstate, cpu, hwpstate_driver, hwpstate_devclass, 0, 0);

/*
 * Go to Px-state on all cpus considering the limit.
 */
static int
hwpstate_goto_pstate(device_t dev, int id)
{
	sbintime_t sbt;
	uint64_t msr;
	int cpu, i, j, limit;

	/* get the current pstate limit */
	msr = rdmsr(MSR_AMD_10H_11H_LIMIT);
	limit = AMD_10H_11H_GET_PSTATE_LIMIT(msr);
	if (limit > id)
		id = limit;

	cpu = curcpu;
	HWPSTATE_DEBUG(dev, "setting P%d-state on cpu%d\n", id, cpu);
	/* Go To Px-state */
	wrmsr(MSR_AMD_10H_11H_CONTROL, id);

	/*
	 * We are going to the same Px-state on all cpus.
	 * Probably should take _PSD into account.
	 */
	CPU_FOREACH(i) {
		if (i == cpu)
			continue;

		/* Bind to each cpu. */
		thread_lock(curthread);
		sched_bind(curthread, i);
		thread_unlock(curthread);
		HWPSTATE_DEBUG(dev, "setting P%d-state on cpu%d\n", id, i);
		/* Go To Px-state */
		wrmsr(MSR_AMD_10H_11H_CONTROL, id);
	}

	/*
	 * Verify whether each core is in the requested P-state.
	 */
	if (hwpstate_verify) {
		CPU_FOREACH(i) {
			thread_lock(curthread);
			sched_bind(curthread, i);
			thread_unlock(curthread);
			/* wait loop (100*100 usec is enough ?) */
			for (j = 0; j < 100; j++) {
				/* get the result. not assure msr=id */
				msr = rdmsr(MSR_AMD_10H_11H_STATUS);
				if (msr == id)
					break;
				sbt = SBT_1MS / 10;
				tsleep_sbt(dev, PZERO, "pstate_goto", sbt,
				    sbt >> tc_precexp, 0);
			}
			HWPSTATE_DEBUG(dev, "result: P%d-state on cpu%d\n",
			    (int)msr, i);
			if (msr != id) {
				HWPSTATE_DEBUG(dev,
				    "error: loop is not enough.\n");
				return (ENXIO);
			}
		}
	}

	return (0);
}

static int
hwpstate_set(device_t dev, const struct cf_setting *cf)
{
	struct hwpstate_softc *sc;
	struct hwpstate_setting *set;
	int i;

	if (cf == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);
	set = sc->hwpstate_settings;
	for (i = 0; i < sc->cfnum; i++)
		if (CPUFREQ_CMP(cf->freq, set[i].freq))
			break;
	if (i == sc->cfnum)
		return (EINVAL);

	return (hwpstate_goto_pstate(dev, set[i].pstate_id));
}

static int
hwpstate_get(device_t dev, struct cf_setting *cf)
{
	struct hwpstate_softc *sc;
	struct hwpstate_setting set;
	uint64_t msr;

	sc = device_get_softc(dev);
	if (cf == NULL)
		return (EINVAL);
	msr = rdmsr(MSR_AMD_10H_11H_STATUS);
	if (msr >= sc->cfnum)
		return (EINVAL);
	set = sc->hwpstate_settings[msr];

	cf->freq = set.freq;
	cf->volts = set.volts;
	cf->power = set.power;
	cf->lat = set.lat;
	cf->dev = dev;
	return (0);
}

static int
hwpstate_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct hwpstate_softc *sc;
	struct hwpstate_setting set;
	int i;

	if (sets == NULL || count == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);
	if (*count < sc->cfnum)
		return (E2BIG);
	for (i = 0; i < sc->cfnum; i++, sets++) {
		set = sc->hwpstate_settings[i];
		sets->freq = set.freq;
		sets->volts = set.volts;
		sets->power = set.power;
		sets->lat = set.lat;
		sets->dev = dev;
	}
	*count = sc->cfnum;

	return (0);
}

static int
hwpstate_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_ABSOLUTE;
	return (0);
}

static void
hwpstate_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "hwpstate", -1) != NULL)
		return;

	if (cpu_vendor_id != CPU_VENDOR_AMD || CPUID_TO_FAMILY(cpu_id) < 0x10)
		return;

	/*
	 * Check if hardware pstate enable bit is set.
	 */
	if ((amd_pminfo & AMDPM_HW_PSTATE) == 0) {
		HWPSTATE_DEBUG(parent, "hwpstate enable bit is not set.\n");
		return;
	}

	if (resource_disabled("hwpstate", 0))
		return;

	if (BUS_ADD_CHILD(parent, 10, "hwpstate", -1) == NULL)
		device_printf(parent, "hwpstate: add child failed\n");
}

static int
hwpstate_probe(device_t dev)
{
	struct hwpstate_softc *sc;
	device_t perf_dev;
	uint64_t msr;
	int error, type;

	/*
	 * Only hwpstate0.
	 * It goes well with acpi_throttle.
	 */
	if (device_get_unit(dev) != 0)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->dev = dev;

	/*
	 * Check if acpi_perf has INFO only flag.
	 */
	perf_dev = device_find_child(device_get_parent(dev), "acpi_perf", -1);
	error = TRUE;
	if (perf_dev && device_is_attached(perf_dev)) {
		error = CPUFREQ_DRV_TYPE(perf_dev, &type);
		if (error == 0) {
			if ((type & CPUFREQ_FLAG_INFO_ONLY) == 0) {
				/*
				 * If acpi_perf doesn't have INFO_ONLY flag,
				 * it will take care of pstate transitions.
				 */
				HWPSTATE_DEBUG(dev, "acpi_perf will take care of pstate transitions.\n");
				return (ENXIO);
			} else {
				/*
				 * If acpi_perf has INFO_ONLY flag, (_PCT has FFixedHW)
				 * we can get _PSS info from acpi_perf
				 * without going into ACPI.
				 */
				HWPSTATE_DEBUG(dev, "going to fetch info from acpi_perf\n");
				error = hwpstate_get_info_from_acpi_perf(dev, perf_dev);
			}
		}
	}

	if (error == 0) {
		/*
		 * Now we get _PSS info from acpi_perf without error.
		 * Let's check it.
		 */
		msr = rdmsr(MSR_AMD_10H_11H_LIMIT);
		if (sc->cfnum != 1 + AMD_10H_11H_GET_PSTATE_MAX_VAL(msr)) {
			HWPSTATE_DEBUG(dev, "MSR (%jd) and ACPI _PSS (%d)"
			    " count mismatch\n", (intmax_t)msr, sc->cfnum);
			error = TRUE;
		}
	}

	/*
	 * If we cannot get info from acpi_perf,
	 * Let's get info from MSRs.
	 */
	if (error)
		error = hwpstate_get_info_from_msr(dev);
	if (error)
		return (error);

	device_set_desc(dev, "Cool`n'Quiet 2.0");
	return (0);
}

static int
hwpstate_attach(device_t dev)
{

	return (cpufreq_register(dev));
}

static int
hwpstate_get_info_from_msr(device_t dev)
{
	struct hwpstate_softc *sc;
	struct hwpstate_setting *hwpstate_set;
	uint64_t msr;
	int family, i, fid, did;

	family = CPUID_TO_FAMILY(cpu_id);
	sc = device_get_softc(dev);
	/* Get pstate count */
	msr = rdmsr(MSR_AMD_10H_11H_LIMIT);
	sc->cfnum = 1 + AMD_10H_11H_GET_PSTATE_MAX_VAL(msr);
	hwpstate_set = sc->hwpstate_settings;
	for (i = 0; i < sc->cfnum; i++) {
		msr = rdmsr(MSR_AMD_10H_11H_CONFIG + i);
		if ((msr & ((uint64_t)1 << 63)) == 0) {
			HWPSTATE_DEBUG(dev, "msr is not valid.\n");
			return (ENXIO);
		}
		did = AMD_10H_11H_CUR_DID(msr);
		fid = AMD_10H_11H_CUR_FID(msr);

		/* Convert fid/did to frequency. */
		switch (family) {
		case 0x11:
			hwpstate_set[i].freq = (100 * (fid + 0x08)) >> did;
			break;
		case 0x10:
		case 0x12:
		case 0x15:
		case 0x16:
			hwpstate_set[i].freq = (100 * (fid + 0x10)) >> did;
			break;
		case 0x17:
			did = AMD_17H_CUR_DID(msr);
			if (did == 0) {
				HWPSTATE_DEBUG(dev, "unexpected did: 0\n");
				did = 1;
			}
			fid = AMD_17H_CUR_FID(msr);
			hwpstate_set[i].freq = (200 * fid) / did;
			break;
		default:
			HWPSTATE_DEBUG(dev, "get_info_from_msr: AMD family"
			    " 0x%02x CPUs are not supported yet\n", family);
			return (ENXIO);
		}
		hwpstate_set[i].pstate_id = i;
		/* There was volts calculation, but deleted it. */
		hwpstate_set[i].volts = CPUFREQ_VAL_UNKNOWN;
		hwpstate_set[i].power = CPUFREQ_VAL_UNKNOWN;
		hwpstate_set[i].lat = CPUFREQ_VAL_UNKNOWN;
	}
	return (0);
}

static int
hwpstate_get_info_from_acpi_perf(device_t dev, device_t perf_dev)
{
	struct hwpstate_softc *sc;
	struct cf_setting *perf_set;
	struct hwpstate_setting *hwpstate_set;
	int count, error, i;

	perf_set = malloc(MAX_SETTINGS * sizeof(*perf_set), M_TEMP, M_NOWAIT);
	if (perf_set == NULL) {
		HWPSTATE_DEBUG(dev, "nomem\n");
		return (ENOMEM);
	}
	/*
	 * Fetch settings from acpi_perf.
	 * Now it is attached, and has info only flag.
	 */
	count = MAX_SETTINGS;
	error = CPUFREQ_DRV_SETTINGS(perf_dev, perf_set, &count);
	if (error) {
		HWPSTATE_DEBUG(dev, "error: CPUFREQ_DRV_SETTINGS.\n");
		goto out;
	}
	sc = device_get_softc(dev);
	sc->cfnum = count;
	hwpstate_set = sc->hwpstate_settings;
	for (i = 0; i < count; i++) {
		if (i == perf_set[i].spec[0]) {
			hwpstate_set[i].pstate_id = i;
			hwpstate_set[i].freq = perf_set[i].freq;
			hwpstate_set[i].volts = perf_set[i].volts;
			hwpstate_set[i].power = perf_set[i].power;
			hwpstate_set[i].lat = perf_set[i].lat;
		} else {
			HWPSTATE_DEBUG(dev, "ACPI _PSS object mismatch.\n");
			error = ENXIO;
			goto out;
		}
	}
out:
	if (perf_set)
		free(perf_set, M_TEMP);
	return (error);
}

static int
hwpstate_detach(device_t dev)
{

	hwpstate_goto_pstate(dev, 0);
	return (cpufreq_unregister(dev));
}

static int
hwpstate_shutdown(device_t dev)
{

	/* hwpstate_goto_pstate(dev, 0); */
	return (0);
}

static int
hwpstate_features(driver_t *driver, u_int *features)
{

	/* Notify the ACPI CPU that we support direct access to MSRs */
	*features = ACPI_CAP_PERF_MSRS;
	return (0);
}

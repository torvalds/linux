/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007, 2008 Rui Paulo <rpaulo@FreeBSD.org>
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Device driver for Intel's On Die thermal sensor via MSR.
 * First introduced in Intel's Core line of processors.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>	/* for curthread */
#include <sys/sched.h>

#include <machine/specialreg.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>

#define	TZ_ZEROC			2731

#define	THERM_STATUS_LOG		0x02
#define	THERM_STATUS			0x01
#define	THERM_STATUS_TEMP_SHIFT		16
#define	THERM_STATUS_TEMP_MASK		0x7f
#define	THERM_STATUS_RES_SHIFT		27
#define	THERM_STATUS_RES_MASK		0x0f
#define	THERM_STATUS_VALID_SHIFT	31
#define	THERM_STATUS_VALID_MASK		0x01

struct coretemp_softc {
	device_t	sc_dev;
	int		sc_tjmax;
	unsigned int	sc_throttle_log;
};

/*
 * Device methods.
 */
static void	coretemp_identify(driver_t *driver, device_t parent);
static int	coretemp_probe(device_t dev);
static int	coretemp_attach(device_t dev);
static int	coretemp_detach(device_t dev);

static uint64_t	coretemp_get_thermal_msr(int cpu);
static void	coretemp_clear_thermal_msr(int cpu);
static int	coretemp_get_val_sysctl(SYSCTL_HANDLER_ARGS);
static int	coretemp_throttle_log_sysctl(SYSCTL_HANDLER_ARGS);

static device_method_t coretemp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	coretemp_identify),
	DEVMETHOD(device_probe,		coretemp_probe),
	DEVMETHOD(device_attach,	coretemp_attach),
	DEVMETHOD(device_detach,	coretemp_detach),

	DEVMETHOD_END
};

static driver_t coretemp_driver = {
	"coretemp",
	coretemp_methods,
	sizeof(struct coretemp_softc),
};

enum therm_info {
	CORETEMP_TEMP,
	CORETEMP_DELTA,
	CORETEMP_RESOLUTION,
	CORETEMP_TJMAX,
};

static devclass_t coretemp_devclass;
DRIVER_MODULE(coretemp, cpu, coretemp_driver, coretemp_devclass, NULL,
    NULL);

static void
coretemp_identify(driver_t *driver, device_t parent)
{
	device_t child;
	u_int regs[4];

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "coretemp", -1) != NULL)
		return;

	/* Check that CPUID 0x06 is supported and the vendor is Intel.*/
	if (cpu_high < 6 || cpu_vendor_id != CPU_VENDOR_INTEL)
		return;
	/*
	 * CPUID 0x06 returns 1 if the processor has on-die thermal
	 * sensors. EBX[0:3] contains the number of sensors.
	 */
	do_cpuid(0x06, regs);
	if ((regs[0] & 0x1) != 1)
		return;

	/*
	 * We add a child for each CPU since settings must be performed
	 * on each CPU in the SMP case.
	 */
	child = device_add_child(parent, "coretemp", -1);
	if (child == NULL)
		device_printf(parent, "add coretemp child failed\n");
}

static int
coretemp_probe(device_t dev)
{
	if (resource_disabled("coretemp", 0))
		return (ENXIO);

	device_set_desc(dev, "CPU On-Die Thermal Sensors");

	if (!bootverbose && device_get_unit(dev) != 0)
		device_quiet(dev);

	return (BUS_PROBE_GENERIC);
}

static int
coretemp_attach(device_t dev)
{
	struct coretemp_softc *sc = device_get_softc(dev);
	device_t pdev;
	uint64_t msr;
	int cpu_model, cpu_stepping;
	int ret, tjtarget;
	struct sysctl_oid *oid;
	struct sysctl_ctx_list *ctx;

	sc->sc_dev = dev;
	pdev = device_get_parent(dev);
	cpu_model = CPUID_TO_MODEL(cpu_id);
	cpu_stepping = cpu_id & CPUID_STEPPING;

	/*
	 * Some CPUs, namely the PIII, don't have thermal sensors, but
	 * report them when the CPUID check is performed in
	 * coretemp_identify(). This leads to a later GPF when the sensor
	 * is queried via a MSR, so we stop here.
	 */
	if (cpu_model < 0xe)
		return (ENXIO);

#if 0 /*
       * XXXrpaulo: I have this CPU model and when it returns from C3
       * coretemp continues to function properly.
       */
	 
	/*
	 * Check for errata AE18.
	 * "Processor Digital Thermal Sensor (DTS) Readout stops
	 *  updating upon returning from C3/C4 state."
	 *
	 * Adapted from the Linux coretemp driver.
	 */
	if (cpu_model == 0xe && cpu_stepping < 0xc) {
		msr = rdmsr(MSR_BIOS_SIGN);
		msr = msr >> 32;
		if (msr < 0x39) {
			device_printf(dev, "not supported (Intel errata "
			    "AE18), try updating your BIOS\n");
			return (ENXIO);
		}
	}
#endif

	/*
	 * Use 100C as the initial value.
	 */
	sc->sc_tjmax = 100;

	if ((cpu_model == 0xf && cpu_stepping >= 2) || cpu_model == 0xe) {
		/*
		 * On some Core 2 CPUs, there's an undocumented MSR that
		 * can tell us if Tj(max) is 100 or 85.
		 *
		 * The if-clause for CPUs having the MSR_IA32_EXT_CONFIG was adapted
		 * from the Linux coretemp driver.
		 */
		msr = rdmsr(MSR_IA32_EXT_CONFIG);
		if (msr & (1 << 30))
			sc->sc_tjmax = 85;
	} else if (cpu_model == 0x17) {
		switch (cpu_stepping) {
		case 0x6:	/* Mobile Core 2 Duo */
			sc->sc_tjmax = 105;
			break;
		default:	/* Unknown stepping */
			break;
		}
	} else if (cpu_model == 0x1c) {
		switch (cpu_stepping) {
		case 0xa:	/* 45nm Atom D400, N400 and D500 series */
			sc->sc_tjmax = 100;
			break;
		default:
			sc->sc_tjmax = 90;
			break;
		}
	} else {
		/*
		 * Attempt to get Tj(max) from MSR IA32_TEMPERATURE_TARGET.
		 *
		 * This method is described in Intel white paper "CPU
		 * Monitoring With DTS/PECI". (#322683)
		 */
		ret = rdmsr_safe(MSR_IA32_TEMPERATURE_TARGET, &msr);
		if (ret == 0) {
			tjtarget = (msr >> 16) & 0xff;

			/*
			 * On earlier generation of processors, the value
			 * obtained from IA32_TEMPERATURE_TARGET register is
			 * an offset that needs to be summed with a model
			 * specific base.  It is however not clear what
			 * these numbers are, with the publicly available
			 * documents from Intel.
			 *
			 * For now, we consider [70, 110]C range, as
			 * described in #322683, as "reasonable" and accept
			 * these values whenever the MSR is available for
			 * read, regardless the CPU model.
			 */
			if (tjtarget >= 70 && tjtarget <= 110)
				sc->sc_tjmax = tjtarget;
			else
				device_printf(dev, "Tj(target) value %d "
				    "does not seem right.\n", tjtarget);
		} else
			device_printf(dev, "Can not get Tj(target) "
			    "from your CPU, using 100C.\n");
	}

	if (bootverbose)
		device_printf(dev, "Setting TjMax=%d\n", sc->sc_tjmax);

	ctx = device_get_sysctl_ctx(dev);

	oid = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(pdev)), OID_AUTO,
	    "coretemp", CTLFLAG_RD, NULL, "Per-CPU thermal information");

	/*
	 * Add the MIBs to dev.cpu.N and dev.cpu.N.coretemp.
	 */
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(device_get_sysctl_tree(pdev)),
	    OID_AUTO, "temperature", CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    dev, CORETEMP_TEMP, coretemp_get_val_sysctl, "IK",
	    "Current temperature");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "delta",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, dev, CORETEMP_DELTA,
	    coretemp_get_val_sysctl, "I",
	    "Delta between TCC activation and current temperature");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "resolution",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, dev, CORETEMP_RESOLUTION,
	    coretemp_get_val_sysctl, "I",
	    "Resolution of CPU thermal sensor");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "tjmax",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, dev, CORETEMP_TJMAX,
	    coretemp_get_val_sysctl, "IK",
	    "TCC activation temperature");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
	    "throttle_log", CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, dev, 0,
	    coretemp_throttle_log_sysctl, "I",
	    "Set to 1 if the thermal sensor has tripped");

	return (0);
}

static int
coretemp_detach(device_t dev)
{
	return (0);
}

static uint64_t
coretemp_get_thermal_msr(int cpu)
{
	uint64_t msr;

	thread_lock(curthread);
	sched_bind(curthread, cpu);
	thread_unlock(curthread);

	/*
	 * The digital temperature reading is located at bit 16
	 * of MSR_THERM_STATUS.
	 *
	 * There is a bit on that MSR that indicates whether the
	 * temperature is valid or not.
	 *
	 * The temperature is computed by subtracting the temperature
	 * reading by Tj(max).
	 */
	msr = rdmsr(MSR_THERM_STATUS);

	thread_lock(curthread);
	sched_unbind(curthread);
	thread_unlock(curthread);

	return (msr);
}

static void
coretemp_clear_thermal_msr(int cpu)
{
	thread_lock(curthread);
	sched_bind(curthread, cpu);
	thread_unlock(curthread);

	wrmsr(MSR_THERM_STATUS, 0);

	thread_lock(curthread);
	sched_unbind(curthread);
	thread_unlock(curthread);
}

static int
coretemp_get_val_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev;
	uint64_t msr;
	int val, tmp;
	struct coretemp_softc *sc;
	enum therm_info type;
	char stemp[16];

	dev = (device_t) arg1;
	msr = coretemp_get_thermal_msr(device_get_unit(dev));
	sc = device_get_softc(dev);
	type = arg2;

	if (((msr >> THERM_STATUS_VALID_SHIFT) & THERM_STATUS_VALID_MASK) != 1) {
		val = -1;
	} else {
		switch (type) {
		case CORETEMP_TEMP:
			tmp = (msr >> THERM_STATUS_TEMP_SHIFT) &
			    THERM_STATUS_TEMP_MASK;
			val = (sc->sc_tjmax - tmp) * 10 + TZ_ZEROC;
			break;
		case CORETEMP_DELTA:
			val = (msr >> THERM_STATUS_TEMP_SHIFT) &
			    THERM_STATUS_TEMP_MASK;
			break;
		case CORETEMP_RESOLUTION:
			val = (msr >> THERM_STATUS_RES_SHIFT) &
			    THERM_STATUS_RES_MASK;
			break;
		case CORETEMP_TJMAX:
			val = sc->sc_tjmax * 10 + TZ_ZEROC;
			break;
		}
	}

	if (msr & THERM_STATUS_LOG) {
		coretemp_clear_thermal_msr(device_get_unit(dev));
		sc->sc_throttle_log = 1;

		/*
		 * Check for Critical Temperature Status and Critical
		 * Temperature Log.  It doesn't really matter if the
		 * current temperature is invalid because the "Critical
		 * Temperature Log" bit will tell us if the Critical
		 * Temperature has * been reached in past. It's not
		 * directly related to the current temperature.
		 *
		 * If we reach a critical level, allow devctl(4)
		 * to catch this and shutdown the system.
		 */
		if (msr & THERM_STATUS) {
			tmp = (msr >> THERM_STATUS_TEMP_SHIFT) &
			    THERM_STATUS_TEMP_MASK;
			tmp = (sc->sc_tjmax - tmp) * 10 + TZ_ZEROC;
			device_printf(dev, "critical temperature detected, "
			    "suggest system shutdown\n");
			snprintf(stemp, sizeof(stemp), "%d", tmp);
			devctl_notify("coretemp", "Thermal", stemp,
			    "notify=0xcc");
		}
	}

	return (sysctl_handle_int(oidp, &val, 0, req));
}

static int
coretemp_throttle_log_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev;
	uint64_t msr;
	int error, val;
	struct coretemp_softc *sc;

	dev = (device_t) arg1;
	msr = coretemp_get_thermal_msr(device_get_unit(dev));
	sc = device_get_softc(dev);

	if (msr & THERM_STATUS_LOG) {
		coretemp_clear_thermal_msr(device_get_unit(dev));
		sc->sc_throttle_log = 1;
	}

	val = sc->sc_throttle_log;

	error = sysctl_handle_int(oidp, &val, 0, req);

	if (error || !req->newptr)
		return (error);
	else if (val != 0)
		return (EINVAL);

	coretemp_clear_thermal_msr(device_get_unit(dev));
	sc->sc_throttle_log = 0;

	return (0);
}

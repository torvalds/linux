/*-
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/refcount.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/bhnd/cores/chipc/chipc.h>
#include <dev/bhnd/cores/chipc/pwrctl/bhnd_pwrctl.h>

#include "siba_eromvar.h"

#include "sibareg.h"
#include "sibavar.h"

/* RID used when allocating EROM resources */
#define	SIBA_EROM_RID	0

static bhnd_erom_class_t *
siba_get_erom_class(driver_t *driver)
{
	return (&siba_erom_parser);
}

int
siba_probe(device_t dev)
{
	device_set_desc(dev, "SIBA BHND bus");
	return (BUS_PROBE_DEFAULT);
}

/**
 * Default siba(4) bus driver implementation of DEVICE_ATTACH().
 * 
 * This implementation initializes internal siba(4) state and performs
 * bus enumeration, and must be called by subclassing drivers in
 * DEVICE_ATTACH() before any other bus methods.
 */
int
siba_attach(device_t dev)
{
	struct siba_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	SIBA_LOCK_INIT(sc);

	/* Enumerate children */
	if ((error = siba_add_children(dev))) {
		device_delete_children(dev);
		SIBA_LOCK_DESTROY(sc);
		return (error);
	}

	return (0);
}

int
siba_detach(device_t dev)
{
	struct siba_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	if ((error = bhnd_generic_detach(dev)))
		return (error);

	SIBA_LOCK_DESTROY(sc);

	return (0);
}

int
siba_resume(device_t dev)
{
	return (bhnd_generic_resume(dev));
}

int
siba_suspend(device_t dev)
{
	return (bhnd_generic_suspend(dev));
}

static int
siba_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct siba_softc		*sc;
	const struct siba_devinfo	*dinfo;
	const struct bhnd_core_info	*cfg;

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);
	cfg = &dinfo->core_id.core_info;
	
	switch (index) {
	case BHND_IVAR_VENDOR:
		*result = cfg->vendor;
		return (0);
	case BHND_IVAR_DEVICE:
		*result = cfg->device;
		return (0);
	case BHND_IVAR_HWREV:
		*result = cfg->hwrev;
		return (0);
	case BHND_IVAR_DEVICE_CLASS:
		*result = bhnd_core_class(cfg);
		return (0);
	case BHND_IVAR_VENDOR_NAME:
		*result = (uintptr_t) bhnd_vendor_name(cfg->vendor);
		return (0);
	case BHND_IVAR_DEVICE_NAME:
		*result = (uintptr_t) bhnd_core_name(cfg);
		return (0);
	case BHND_IVAR_CORE_INDEX:
		*result = cfg->core_idx;
		return (0);
	case BHND_IVAR_CORE_UNIT:
		*result = cfg->unit;
		return (0);
	case BHND_IVAR_PMU_INFO:
		SIBA_LOCK(sc);
		switch (dinfo->pmu_state) {
		case SIBA_PMU_NONE:
			*result = (uintptr_t)NULL;
			SIBA_UNLOCK(sc);
			return (0);

		case SIBA_PMU_BHND:
			*result = (uintptr_t)dinfo->pmu.bhnd_info;
			SIBA_UNLOCK(sc);
			return (0);

		case SIBA_PMU_PWRCTL:
		case SIBA_PMU_FIXED:
			*result = (uintptr_t)NULL;
			SIBA_UNLOCK(sc);
			return (0);
		}

		panic("invalid PMU state: %d", dinfo->pmu_state);
		return (ENXIO);

	default:
		return (ENOENT);
	}
}

static int
siba_write_ivar(device_t dev, device_t child, int index, uintptr_t value)
{
	struct siba_softc	*sc;
	struct siba_devinfo	*dinfo;

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);

	switch (index) {
	case BHND_IVAR_VENDOR:
	case BHND_IVAR_DEVICE:
	case BHND_IVAR_HWREV:
	case BHND_IVAR_DEVICE_CLASS:
	case BHND_IVAR_VENDOR_NAME:
	case BHND_IVAR_DEVICE_NAME:
	case BHND_IVAR_CORE_INDEX:
	case BHND_IVAR_CORE_UNIT:
		return (EINVAL);
	case BHND_IVAR_PMU_INFO:
		SIBA_LOCK(sc);
		switch (dinfo->pmu_state) {
		case SIBA_PMU_NONE:
		case SIBA_PMU_BHND:
			dinfo->pmu.bhnd_info = (void *)value;
			dinfo->pmu_state = SIBA_PMU_BHND;
			SIBA_UNLOCK(sc);
			return (0);

		case SIBA_PMU_PWRCTL:
		case SIBA_PMU_FIXED:
			panic("bhnd_set_pmu_info() called with siba PMU state "
			    "%d", dinfo->pmu_state);
			return (ENXIO);
		}

		panic("invalid PMU state: %d", dinfo->pmu_state);
		return (ENXIO);

	default:
		return (ENOENT);
	}
}

static struct resource_list *
siba_get_resource_list(device_t dev, device_t child)
{
	struct siba_devinfo *dinfo = device_get_ivars(child);
	return (&dinfo->resources);
}

/* BHND_BUS_ALLOC_PMU() */
static int
siba_alloc_pmu(device_t dev, device_t child)
{
	struct siba_softc	*sc;
	struct siba_devinfo	*dinfo;
	device_t		 chipc;
	device_t		 pwrctl;
	struct chipc_caps	 ccaps;
	siba_pmu_state		 pmu_state;
	int			 error;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);
	pwrctl = NULL;

	/* Fetch ChipCommon capability flags */
	chipc = bhnd_retain_provider(child, BHND_SERVICE_CHIPC);
	if (chipc != NULL) {
		ccaps = *BHND_CHIPC_GET_CAPS(chipc);
		bhnd_release_provider(child, chipc, BHND_SERVICE_CHIPC);
	} else {
		memset(&ccaps, 0, sizeof(ccaps));
	}

	/* Defer to bhnd(4)'s PMU implementation if ChipCommon exists and
	 * advertises PMU support */
	if (ccaps.pmu) {
		if ((error = bhnd_generic_alloc_pmu(dev, child)))
			return (error);

		KASSERT(dinfo->pmu_state == SIBA_PMU_BHND,
		    ("unexpected PMU state: %d", dinfo->pmu_state));

		return (0);
	}

	/*
	 * This is either a legacy PWRCTL chipset, or the device does not
	 * support dynamic clock control.
	 * 
	 * We need to map all bhnd(4) bus PMU to PWRCTL or no-op operations.
	 */
	if (ccaps.pwr_ctrl) {
		pmu_state = SIBA_PMU_PWRCTL;
		pwrctl = bhnd_retain_provider(child, BHND_SERVICE_PWRCTL);
		if (pwrctl == NULL) {
			device_printf(dev, "PWRCTL not found\n");
			return (ENODEV);
		}
	} else {
		pmu_state = SIBA_PMU_FIXED;
		pwrctl = NULL;
	}

	SIBA_LOCK(sc);

	/* Per-core PMU state already allocated? */
	if (dinfo->pmu_state != SIBA_PMU_NONE) {
		panic("duplicate PMU allocation for %s",
		    device_get_nameunit(child));
	}

	/* Update the child's PMU allocation state, and transfer ownership of
	 * the PWRCTL provider reference (if any) */
	dinfo->pmu_state = pmu_state;
	dinfo->pmu.pwrctl = pwrctl;

	SIBA_UNLOCK(sc);

	return (0);
}

/* BHND_BUS_RELEASE_PMU() */
static int
siba_release_pmu(device_t dev, device_t child)
{
	struct siba_softc	*sc;
	struct siba_devinfo	*dinfo;
	device_t		 pwrctl;
	int			 error;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);

	SIBA_LOCK(sc);
	switch(dinfo->pmu_state) {
	case SIBA_PMU_NONE:
		panic("pmu over-release for %s", device_get_nameunit(child));
		SIBA_UNLOCK(sc);
		return (ENXIO);

	case SIBA_PMU_BHND:
		SIBA_UNLOCK(sc);
		return (bhnd_generic_release_pmu(dev, child));

	case SIBA_PMU_PWRCTL:
		/* Requesting BHND_CLOCK_DYN releases any outstanding clock
		 * reservations */
		pwrctl = dinfo->pmu.pwrctl;
		error = bhnd_pwrctl_request_clock(pwrctl, child,
		    BHND_CLOCK_DYN);
		if (error) {
			SIBA_UNLOCK(sc);
			return (error);
		}

		/* Clean up the child's PMU state */
		dinfo->pmu_state = SIBA_PMU_NONE;
		dinfo->pmu.pwrctl = NULL;
		SIBA_UNLOCK(sc);

		/* Release the provider reference */
		bhnd_release_provider(child, pwrctl, BHND_SERVICE_PWRCTL);
		return (0);

	case SIBA_PMU_FIXED:
		/* Clean up the child's PMU state */
		KASSERT(dinfo->pmu.pwrctl == NULL,
		    ("PWRCTL reference with FIXED state"));

		dinfo->pmu_state = SIBA_PMU_NONE;
		dinfo->pmu.pwrctl = NULL;
		SIBA_UNLOCK(sc);
	}

	panic("invalid PMU state: %d", dinfo->pmu_state);
}

/* BHND_BUS_GET_CLOCK_LATENCY() */
static int
siba_get_clock_latency(device_t dev, device_t child, bhnd_clock clock,
    u_int *latency)
{
	struct siba_softc	*sc;
	struct siba_devinfo	*dinfo;
	int			 error;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);

	SIBA_LOCK(sc);
	switch(dinfo->pmu_state) {
	case SIBA_PMU_NONE:
		panic("no active PMU request state");

		SIBA_UNLOCK(sc);
		return (ENXIO);

	case SIBA_PMU_BHND:
		SIBA_UNLOCK(sc);
		return (bhnd_generic_get_clock_latency(dev, child, clock,
		    latency));

	case SIBA_PMU_PWRCTL:
		 error = bhnd_pwrctl_get_clock_latency(dinfo->pmu.pwrctl, clock,
		    latency);
		 SIBA_UNLOCK(sc);

		 return (error);

	case SIBA_PMU_FIXED:
		SIBA_UNLOCK(sc);

		/* HT clock is always available, and incurs no transition
		 * delay. */
		switch (clock) {
		case BHND_CLOCK_HT:
			*latency = 0;
			return (0);

		default:
			return (ENODEV);
		}

		return (ENODEV);
	}

	panic("invalid PMU state: %d", dinfo->pmu_state);
}

/* BHND_BUS_GET_CLOCK_FREQ() */
static int
siba_get_clock_freq(device_t dev, device_t child, bhnd_clock clock,
    u_int *freq)
{
	struct siba_softc	*sc;
	struct siba_devinfo	*dinfo;
	int			 error;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);

	SIBA_LOCK(sc);
	switch(dinfo->pmu_state) {
	case SIBA_PMU_NONE:
		panic("no active PMU request state");

		SIBA_UNLOCK(sc);
		return (ENXIO);

	case SIBA_PMU_BHND:
		SIBA_UNLOCK(sc);
		return (bhnd_generic_get_clock_freq(dev, child, clock, freq));

	case SIBA_PMU_PWRCTL:
		error = bhnd_pwrctl_get_clock_freq(dinfo->pmu.pwrctl, clock,
		    freq);
		SIBA_UNLOCK(sc);

		return (error);

	case SIBA_PMU_FIXED:
		SIBA_UNLOCK(sc);

		return (ENODEV);
	}

	panic("invalid PMU state: %d", dinfo->pmu_state);
}

/* BHND_BUS_REQUEST_EXT_RSRC() */
static int
siba_request_ext_rsrc(device_t dev, device_t child, u_int rsrc)
{
	struct siba_softc	*sc;
	struct siba_devinfo	*dinfo;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);

	SIBA_LOCK(sc);
	switch(dinfo->pmu_state) {
	case SIBA_PMU_NONE:
		panic("no active PMU request state");

		SIBA_UNLOCK(sc);
		return (ENXIO);

	case SIBA_PMU_BHND:
		SIBA_UNLOCK(sc);
		return (bhnd_generic_request_ext_rsrc(dev, child, rsrc));

	case SIBA_PMU_PWRCTL:
	case SIBA_PMU_FIXED:
		/* HW does not support per-core external resources */
		SIBA_UNLOCK(sc);
		return (ENODEV);
	}

	panic("invalid PMU state: %d", dinfo->pmu_state);
}

/* BHND_BUS_RELEASE_EXT_RSRC() */
static int
siba_release_ext_rsrc(device_t dev, device_t child, u_int rsrc)
{
	struct siba_softc	*sc;
	struct siba_devinfo	*dinfo;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);

	SIBA_LOCK(sc);
	switch(dinfo->pmu_state) {
	case SIBA_PMU_NONE:
		panic("no active PMU request state");

		SIBA_UNLOCK(sc);
		return (ENXIO);

	case SIBA_PMU_BHND:
		SIBA_UNLOCK(sc);
		return (bhnd_generic_release_ext_rsrc(dev, child, rsrc));

	case SIBA_PMU_PWRCTL:
	case SIBA_PMU_FIXED:
		/* HW does not support per-core external resources */
		SIBA_UNLOCK(sc);
		return (ENODEV);
	}

	panic("invalid PMU state: %d", dinfo->pmu_state);
}

/* BHND_BUS_REQUEST_CLOCK() */
static int
siba_request_clock(device_t dev, device_t child, bhnd_clock clock)
{
	struct siba_softc	*sc;
	struct siba_devinfo	*dinfo;
	int			 error;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);

	SIBA_LOCK(sc);
	switch(dinfo->pmu_state) {
	case SIBA_PMU_NONE:
		panic("no active PMU request state");

		SIBA_UNLOCK(sc);
		return (ENXIO);

	case SIBA_PMU_BHND:
		SIBA_UNLOCK(sc);
		return (bhnd_generic_request_clock(dev, child, clock));

	case SIBA_PMU_PWRCTL:
		error = bhnd_pwrctl_request_clock(dinfo->pmu.pwrctl, child,
		    clock);
		SIBA_UNLOCK(sc);

		return (error);

	case SIBA_PMU_FIXED:
		SIBA_UNLOCK(sc);

		/* HT clock is always available, and fulfills any of the
		 * following clock requests */
		switch (clock) {
		case BHND_CLOCK_DYN:
		case BHND_CLOCK_ILP:
		case BHND_CLOCK_ALP:
		case BHND_CLOCK_HT:
			return (0);

		default:
			return (ENODEV);
		}
	}

	panic("invalid PMU state: %d", dinfo->pmu_state);
}

/* BHND_BUS_ENABLE_CLOCKS() */
static int
siba_enable_clocks(device_t dev, device_t child, uint32_t clocks)
{
	struct siba_softc	*sc;
	struct siba_devinfo	*dinfo;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);

	SIBA_LOCK(sc);
	switch(dinfo->pmu_state) {
	case SIBA_PMU_NONE:
		panic("no active PMU request state");

		SIBA_UNLOCK(sc);
		return (ENXIO);

	case SIBA_PMU_BHND:
		SIBA_UNLOCK(sc);
		return (bhnd_generic_enable_clocks(dev, child, clocks));

	case SIBA_PMU_PWRCTL:
	case SIBA_PMU_FIXED:
		SIBA_UNLOCK(sc);

		/* All (supported) clocks are already enabled by default */
		clocks &= ~(BHND_CLOCK_DYN |
			    BHND_CLOCK_ILP |
			    BHND_CLOCK_ALP |
			    BHND_CLOCK_HT);

		if (clocks != 0) {
			device_printf(dev, "%s requested unknown clocks: %#x\n",
			    device_get_nameunit(child), clocks);
			return (ENODEV);
		}

		return (0);
	}

	panic("invalid PMU state: %d", dinfo->pmu_state);
}

static int
siba_read_iost(device_t dev, device_t child, uint16_t *iost)
{
	uint32_t	tmhigh;
	int		error;

	error = bhnd_read_config(child, SIBA_CFG0_TMSTATEHIGH, &tmhigh, 4);
	if (error)
		return (error);

	*iost = (SIBA_REG_GET(tmhigh, TMH_SISF));
	return (0);
}

static int
siba_read_ioctl(device_t dev, device_t child, uint16_t *ioctl)
{
	uint32_t	ts_low;
	int		error;

	if ((error = bhnd_read_config(child, SIBA_CFG0_TMSTATELOW, &ts_low, 4)))
		return (error);

	*ioctl = (SIBA_REG_GET(ts_low, TML_SICF));
	return (0);
}

static int
siba_write_ioctl(device_t dev, device_t child, uint16_t value, uint16_t mask)
{
	struct siba_devinfo	*dinfo;
	struct bhnd_resource	*r;
	uint32_t		 ts_low, ts_mask;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	/* Fetch CFG0 mapping */
	dinfo = device_get_ivars(child);
	if ((r = dinfo->cfg_res[0]) == NULL)
		return (ENODEV);

	/* Mask and set TMSTATELOW core flag bits */
	ts_mask = (mask << SIBA_TML_SICF_SHIFT) & SIBA_TML_SICF_MASK;
	ts_low = (value << SIBA_TML_SICF_SHIFT) & ts_mask;

	siba_write_target_state(child, dinfo, SIBA_CFG0_TMSTATELOW,
	    ts_low, ts_mask);
	return (0);
}

static bool
siba_is_hw_suspended(device_t dev, device_t child)
{
	uint32_t		ts_low;
	uint16_t		ioctl;
	int			error;

	/* Fetch target state */
	error = bhnd_read_config(child, SIBA_CFG0_TMSTATELOW, &ts_low, 4);
	if (error) {
		device_printf(child, "error reading HW reset state: %d\n",
		    error);
		return (true);
	}

	/* Is core held in RESET? */
	if (ts_low & SIBA_TML_RESET)
		return (true);

	/* Is target reject enabled? */
	if (ts_low & SIBA_TML_REJ_MASK)
		return (true);

	/* Is core clocked? */
	ioctl = SIBA_REG_GET(ts_low, TML_SICF);
	if (!(ioctl & BHND_IOCTL_CLK_EN))
		return (true);

	return (false);
}

static int
siba_reset_hw(device_t dev, device_t child, uint16_t ioctl,
    uint16_t reset_ioctl)
{
	struct siba_devinfo		*dinfo;
	struct bhnd_resource		*r;
	uint32_t			 ts_low, imstate;
	uint16_t			 clkflags;
	int				 error;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	dinfo = device_get_ivars(child);

	/* Can't suspend the core without access to the CFG0 registers */
	if ((r = dinfo->cfg_res[0]) == NULL)
		return (ENODEV);

	/* We require exclusive control over BHND_IOCTL_CLK_(EN|FORCE) */
	clkflags = BHND_IOCTL_CLK_EN | BHND_IOCTL_CLK_FORCE;
	if (ioctl & clkflags)
		return (EINVAL);

	/* Place core into known RESET state */
	if ((error = bhnd_suspend_hw(child, reset_ioctl)))
		return (error);

	/* Set RESET, clear REJ, set the caller's IOCTL flags, and
	 * force clocks to ensure the signal propagates throughout the
	 * core. */
	ts_low = SIBA_TML_RESET |
		 (ioctl << SIBA_TML_SICF_SHIFT) |
		 (BHND_IOCTL_CLK_EN << SIBA_TML_SICF_SHIFT) |
		 (BHND_IOCTL_CLK_FORCE << SIBA_TML_SICF_SHIFT);

	siba_write_target_state(child, dinfo, SIBA_CFG0_TMSTATELOW,
	    ts_low, UINT32_MAX);

	/* Clear any target errors */
	if (bhnd_bus_read_4(r, SIBA_CFG0_TMSTATEHIGH) & SIBA_TMH_SERR) {
		siba_write_target_state(child, dinfo, SIBA_CFG0_TMSTATEHIGH,
		    0x0, SIBA_TMH_SERR);
	}

	/* Clear any initiator errors */
	imstate = bhnd_bus_read_4(r, SIBA_CFG0_IMSTATE);
	if (imstate & (SIBA_IM_IBE|SIBA_IM_TO)) {
		siba_write_target_state(child, dinfo, SIBA_CFG0_IMSTATE, 0x0,
		    SIBA_IM_IBE|SIBA_IM_TO);
	}

	/* Release from RESET while leaving clocks forced, ensuring the
	 * signal propagates throughout the core */
	siba_write_target_state(child, dinfo, SIBA_CFG0_TMSTATELOW, 0x0,
	    SIBA_TML_RESET);

	/* The core should now be active; we can clear the BHND_IOCTL_CLK_FORCE
	 * bit and allow the core to manage clock gating. */
	siba_write_target_state(child, dinfo, SIBA_CFG0_TMSTATELOW, 0x0,
	    (BHND_IOCTL_CLK_FORCE << SIBA_TML_SICF_SHIFT));

	return (0);
}

static int
siba_suspend_hw(device_t dev, device_t child, uint16_t ioctl)
{
	struct siba_softc		*sc;
	struct siba_devinfo		*dinfo;
	struct bhnd_resource		*r;
	uint32_t			 idl, ts_low, ts_mask;
	uint16_t			 cflags, clkflags;
	int				 error;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);

	/* Can't suspend the core without access to the CFG0 registers */
	if ((r = dinfo->cfg_res[0]) == NULL)
		return (ENODEV);

	/* We require exclusive control over BHND_IOCTL_CLK_(EN|FORCE) */
	clkflags = BHND_IOCTL_CLK_EN | BHND_IOCTL_CLK_FORCE;
	if (ioctl & clkflags)
		return (EINVAL);

	/* Already in RESET? */
	ts_low = bhnd_bus_read_4(r, SIBA_CFG0_TMSTATELOW);
	if (ts_low & SIBA_TML_RESET)
		return (0);

	/* If clocks are already disabled, we can place the core directly
	 * into RESET|REJ while setting the caller's IOCTL flags. */
	cflags = SIBA_REG_GET(ts_low, TML_SICF);
	if (!(cflags & BHND_IOCTL_CLK_EN)) {
		ts_low = SIBA_TML_RESET | SIBA_TML_REJ |
			 (ioctl << SIBA_TML_SICF_SHIFT);
		ts_mask = SIBA_TML_RESET | SIBA_TML_REJ | SIBA_TML_SICF_MASK;

		siba_write_target_state(child, dinfo, SIBA_CFG0_TMSTATELOW,
		    ts_low, ts_mask);
		return (0);
	}

	/* Reject further transactions reaching this core */
	siba_write_target_state(child, dinfo, SIBA_CFG0_TMSTATELOW,
	    SIBA_TML_REJ, SIBA_TML_REJ);

	/* Wait for transaction busy flag to clear for all transactions
	 * initiated by this core */
	error = siba_wait_target_state(child, dinfo, SIBA_CFG0_TMSTATEHIGH,
	    0x0, SIBA_TMH_BUSY, 100000);
	if (error)
		return (error);

	/* If this is an initiator core, we need to reject initiator
	 * transactions too. */
	idl = bhnd_bus_read_4(r, SIBA_CFG0_IDLOW);
	if (idl & SIBA_IDL_INIT) {
		/* Reject further initiator transactions */
		siba_write_target_state(child, dinfo, SIBA_CFG0_IMSTATE,
		    SIBA_IM_RJ, SIBA_IM_RJ);

		/* Wait for initiator busy flag to clear */
		error = siba_wait_target_state(child, dinfo, SIBA_CFG0_IMSTATE,
		    0x0, SIBA_IM_BY, 100000);
		if (error)
			return (error);
	}

	/* Put the core into RESET, set the caller's IOCTL flags, and
	 * force clocks to ensure the RESET signal propagates throughout the
	 * core. */
	ts_low = SIBA_TML_RESET |
		 (ioctl << SIBA_TML_SICF_SHIFT) |
		 (BHND_IOCTL_CLK_EN << SIBA_TML_SICF_SHIFT) |
		 (BHND_IOCTL_CLK_FORCE << SIBA_TML_SICF_SHIFT);
	ts_mask = SIBA_TML_RESET |
		  SIBA_TML_SICF_MASK;

	siba_write_target_state(child, dinfo, SIBA_CFG0_TMSTATELOW, ts_low,
	    ts_mask);

	/* Give RESET ample time */
	DELAY(10);

	/* Clear previously asserted initiator reject */
	if (idl & SIBA_IDL_INIT) {
		siba_write_target_state(child, dinfo, SIBA_CFG0_IMSTATE, 0x0,
		    SIBA_IM_RJ);
	}

	/* Disable all clocks, leaving RESET and REJ asserted */
	siba_write_target_state(child, dinfo, SIBA_CFG0_TMSTATELOW, 0x0,
	    (BHND_IOCTL_CLK_EN | BHND_IOCTL_CLK_FORCE) << SIBA_TML_SICF_SHIFT);

	/*
	 * Core is now in RESET.
	 *
	 * If the core holds any PWRCTL clock reservations, we need to release
	 * those now. This emulates the standard bhnd(4) PMU behavior of RESET
	 * automatically clearing clkctl
	 */
	SIBA_LOCK(sc);
	if (dinfo->pmu_state == SIBA_PMU_PWRCTL) {
		error = bhnd_pwrctl_request_clock(dinfo->pmu.pwrctl, child,
		    BHND_CLOCK_DYN);
		SIBA_UNLOCK(sc);

		if (error) {
			device_printf(child, "failed to release clock request: "
			    "%d", error);
			return (error);
		}

		return (0);
	} else {
		SIBA_UNLOCK(sc);
		return (0);
	}
}

static int
siba_read_config(device_t dev, device_t child, bus_size_t offset, void *value,
    u_int width)
{
	struct siba_devinfo	*dinfo;
	rman_res_t		 r_size;

	/* Must be directly attached */
	if (device_get_parent(child) != dev)
		return (EINVAL);

	/* CFG0 registers must be available */
	dinfo = device_get_ivars(child);
	if (dinfo->cfg_res[0] == NULL)
		return (ENODEV);

	/* Offset must fall within CFG0 */
	r_size = rman_get_size(dinfo->cfg_res[0]->res);
	if (r_size < offset || r_size - offset < width)
		return (EFAULT);

	switch (width) {
	case 1:
		*((uint8_t *)value) = bhnd_bus_read_1(dinfo->cfg_res[0],
		    offset);
		return (0);
	case 2:
		*((uint16_t *)value) = bhnd_bus_read_2(dinfo->cfg_res[0],
		    offset);
		return (0);
	case 4:
		*((uint32_t *)value) = bhnd_bus_read_4(dinfo->cfg_res[0],
		    offset);
		return (0);
	default:
		return (EINVAL);
	}
}

static int
siba_write_config(device_t dev, device_t child, bus_size_t offset,
    const void *value, u_int width)
{
	struct siba_devinfo	*dinfo;
	struct bhnd_resource	*r;
	rman_res_t		 r_size;

	/* Must be directly attached */
	if (device_get_parent(child) != dev)
		return (EINVAL);

	/* CFG0 registers must be available */
	dinfo = device_get_ivars(child);
	if ((r = dinfo->cfg_res[0]) == NULL)
		return (ENODEV);

	/* Offset must fall within CFG0 */
	r_size = rman_get_size(r->res);
	if (r_size < offset || r_size - offset < width)
		return (EFAULT);

	switch (width) {
	case 1:
		bhnd_bus_write_1(r, offset, *(const uint8_t *)value);
		return (0);
	case 2:
		bhnd_bus_write_2(r, offset, *(const uint8_t *)value);
		return (0);
	case 4:
		bhnd_bus_write_4(r, offset, *(const uint8_t *)value);
		return (0);
	default:
		return (EINVAL);
	}
}

static u_int
siba_get_port_count(device_t dev, device_t child, bhnd_port_type type)
{
	struct siba_devinfo *dinfo;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev)
		return (BHND_BUS_GET_PORT_COUNT(device_get_parent(dev), child,
		    type));

	dinfo = device_get_ivars(child);
	return (siba_port_count(&dinfo->core_id, type));
}

static u_int
siba_get_region_count(device_t dev, device_t child, bhnd_port_type type,
    u_int port)
{
	struct siba_devinfo	*dinfo;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev)
		return (BHND_BUS_GET_REGION_COUNT(device_get_parent(dev), child,
		    type, port));

	dinfo = device_get_ivars(child);
	return (siba_port_region_count(&dinfo->core_id, type, port));
}

static int
siba_get_port_rid(device_t dev, device_t child, bhnd_port_type port_type,
    u_int port_num, u_int region_num)
{
	struct siba_devinfo	*dinfo;
	struct siba_addrspace	*addrspace;
	struct siba_cfg_block	*cfg;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev)
		return (BHND_BUS_GET_PORT_RID(device_get_parent(dev), child,
		    port_type, port_num, region_num));

	dinfo = device_get_ivars(child);

	/* Look for a matching addrspace entry */
	addrspace = siba_find_addrspace(dinfo, port_type, port_num, region_num);
	if (addrspace != NULL)
		return (addrspace->sa_rid);

	/* Try the config blocks */
	cfg = siba_find_cfg_block(dinfo, port_type, port_num, region_num);
	if (cfg != NULL)
		return (cfg->cb_rid);

	/* Not found */
	return (-1);
}

static int
siba_decode_port_rid(device_t dev, device_t child, int type, int rid,
    bhnd_port_type *port_type, u_int *port_num, u_int *region_num)
{
	struct siba_devinfo	*dinfo;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev)
		return (BHND_BUS_DECODE_PORT_RID(device_get_parent(dev), child,
		    type, rid, port_type, port_num, region_num));

	dinfo = device_get_ivars(child);

	/* Ports are always memory mapped */
	if (type != SYS_RES_MEMORY)
		return (EINVAL);

	/* Look for a matching addrspace entry */
	for (u_int i = 0; i < dinfo->core_id.num_admatch; i++) {
		if (dinfo->addrspace[i].sa_rid != rid)
			continue;

		*port_type = BHND_PORT_DEVICE;
		*port_num = siba_addrspace_device_port(i);
		*region_num = siba_addrspace_device_region(i);
		return (0);
	}

	/* Try the config blocks */
	for (u_int i = 0; i < dinfo->core_id.num_cfg_blocks; i++) {
		if (dinfo->cfg[i].cb_rid != rid)
			continue;

		*port_type = BHND_PORT_AGENT;
		*port_num = siba_cfg_agent_port(i);
		*region_num = siba_cfg_agent_region(i);
		return (0);
	}

	/* Not found */
	return (ENOENT);
}

static int
siba_get_region_addr(device_t dev, device_t child, bhnd_port_type port_type,
    u_int port_num, u_int region_num, bhnd_addr_t *addr, bhnd_size_t *size)
{
	struct siba_devinfo	*dinfo;
	struct siba_addrspace	*addrspace;
	struct siba_cfg_block	*cfg;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev) {
		return (BHND_BUS_GET_REGION_ADDR(device_get_parent(dev), child,
		    port_type, port_num, region_num, addr, size));
	}

	dinfo = device_get_ivars(child);

	/* Look for a matching addrspace */
	addrspace = siba_find_addrspace(dinfo, port_type, port_num, region_num);
	if (addrspace != NULL) {
		*addr = addrspace->sa_base;
		*size = addrspace->sa_size - addrspace->sa_bus_reserved;
		return (0);
	}

	/* Look for a matching cfg block */
	cfg = siba_find_cfg_block(dinfo, port_type, port_num, region_num);
	if (cfg != NULL) {
		*addr = cfg->cb_base;
		*size = cfg->cb_size;
		return (0);
	}

	/* Not found */
	return (ENOENT);
}

/**
 * Default siba(4) bus driver implementation of BHND_BUS_GET_INTR_COUNT().
 */
u_int
siba_get_intr_count(device_t dev, device_t child)
{
	struct siba_devinfo	*dinfo;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev)
		return (BHND_BUS_GET_INTR_COUNT(device_get_parent(dev), child));

	dinfo = device_get_ivars(child);
	if (!dinfo->core_id.intr_en) {
		/* No interrupts */
		return (0);
	} else {
		/* One assigned interrupt */
		return (1);
	}
}

/**
 * Default siba(4) bus driver implementation of BHND_BUS_GET_INTR_IVEC().
 */
int
siba_get_intr_ivec(device_t dev, device_t child, u_int intr, u_int *ivec)
{
	struct siba_devinfo	*dinfo;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev)
		return (BHND_BUS_GET_INTR_IVEC(device_get_parent(dev), child,
		    intr, ivec));

	/* Must be a valid interrupt ID */
	if (intr >= siba_get_intr_count(dev, child))
		return (ENXIO);

	KASSERT(intr == 0, ("invalid ivec %u", intr));

	dinfo = device_get_ivars(child);

	KASSERT(dinfo->core_id.intr_en,
	    ("core does not have an interrupt assigned"));

	*ivec = dinfo->core_id.intr_flag;
	return (0);
}

/**
 * Map per-core configuration blocks for @p dinfo.
 *
 * @param dev The siba bus device.
 * @param dinfo The device info instance on which to map all per-core
 * configuration blocks.
 */
static int
siba_map_cfg_resources(device_t dev, struct siba_devinfo *dinfo)
{
	struct siba_addrspace	*addrspace;
	rman_res_t		 r_start, r_count, r_end;
	uint8_t			 num_cfg;
	int			 rid;

	num_cfg = dinfo->core_id.num_cfg_blocks;
	if (num_cfg > SIBA_MAX_CFG) {
		device_printf(dev, "config block count %hhu out of range\n",
		    num_cfg);
		return (ENXIO);
	}
	
	/* Fetch the core register address space */
	addrspace = siba_find_addrspace(dinfo, BHND_PORT_DEVICE, 0, 0);
	if (addrspace == NULL) {
		device_printf(dev, "missing device registers\n");
		return (ENXIO);
	}

	/*
	 * Map the per-core configuration blocks
	 */
	for (uint8_t i = 0; i < num_cfg; i++) {
		/* Add to child's resource list */
		r_start = addrspace->sa_base + SIBA_CFG_OFFSET(i);
		r_count = SIBA_CFG_SIZE;
		r_end = r_start + r_count - 1;

		rid = resource_list_add_next(&dinfo->resources, SYS_RES_MEMORY,
		    r_start, r_end, r_count);

		/* Initialize config block descriptor */
		dinfo->cfg[i] = ((struct siba_cfg_block) {
			.cb_base = r_start,
			.cb_size = SIBA_CFG_SIZE,
			.cb_rid = rid
		});

		/* Map the config resource for bus-level access */
		dinfo->cfg_rid[i] = SIBA_CFG_RID(dinfo, i);
		dinfo->cfg_res[i] = BHND_BUS_ALLOC_RESOURCE(dev, dev,
		    SYS_RES_MEMORY, &dinfo->cfg_rid[i], r_start, r_end,
		    r_count, RF_ACTIVE|RF_SHAREABLE);

		if (dinfo->cfg_res[i] == NULL) {
			device_printf(dev, "failed to allocate SIBA_CFG%hhu\n",
			    i);
			return (ENXIO);
		}
	}

	return (0);
}

static device_t
siba_add_child(device_t dev, u_int order, const char *name, int unit)
{
	struct siba_devinfo	*dinfo;
	device_t		 child;

	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL)
		return (NULL);

	if ((dinfo = siba_alloc_dinfo(dev)) == NULL) {
		device_delete_child(dev, child);
		return (NULL);
	}

	device_set_ivars(child, dinfo);

	return (child);
}

static void
siba_child_deleted(device_t dev, device_t child)
{
	struct bhnd_softc	*sc;
	struct siba_devinfo	*dinfo;

	sc = device_get_softc(dev);

	/* Call required bhnd(4) implementation */
	bhnd_generic_child_deleted(dev, child);

	/* Free siba device info */
	if ((dinfo = device_get_ivars(child)) != NULL)
		siba_free_dinfo(dev, child, dinfo);

	device_set_ivars(child, NULL);
}

/**
 * Scan the core table and add all valid discovered cores to
 * the bus.
 * 
 * @param dev The siba bus device.
 */
int
siba_add_children(device_t dev)
{
	bhnd_erom_t			*erom;
	struct siba_erom		*siba_erom;
	struct bhnd_erom_io		*eio;
	const struct bhnd_chipid	*cid;
	struct siba_core_id		*cores;
	device_t			*children;
	int				 error;

	cid = BHND_BUS_GET_CHIPID(dev, dev);

	/* Allocate our EROM parser */
	eio = bhnd_erom_iores_new(dev, SIBA_EROM_RID);
	erom = bhnd_erom_alloc(&siba_erom_parser, cid, eio);
	if (erom == NULL) {
		bhnd_erom_io_fini(eio);
		return (ENODEV);
	}

	/* Allocate our temporary core and device table */
	cores = malloc(sizeof(*cores) * cid->ncores, M_BHND, M_WAITOK);
	children = malloc(sizeof(*children) * cid->ncores, M_BHND,
	    M_WAITOK | M_ZERO);

	/*
	 * Add child devices for all discovered cores.
	 * 
	 * On bridged devices, we'll exhaust our available register windows if
	 * we map config blocks on unpopulated/disabled cores. To avoid this, we
	 * defer mapping of the per-core siba(4) config blocks until all cores
	 * have been enumerated and otherwise configured.
	 */
	siba_erom = (struct siba_erom *)erom;
	for (u_int i = 0; i < cid->ncores; i++) {
		struct siba_devinfo	*dinfo;
		device_t		 child;

		if ((error = siba_erom_get_core_id(siba_erom, i, &cores[i])))
			goto failed;

		/* Add the child device */
		child = BUS_ADD_CHILD(dev, 0, NULL, -1);
		if (child == NULL) {
			error = ENXIO;
			goto failed;
		}

		children[i] = child;

		/* Initialize per-device bus info */
		if ((dinfo = device_get_ivars(child)) == NULL) {
			error = ENXIO;
			goto failed;
		}

		if ((error = siba_init_dinfo(dev, child, dinfo, &cores[i])))
			goto failed;

		/* If pins are floating or the hardware is otherwise
		 * unpopulated, the device shouldn't be used. */
		if (bhnd_is_hw_disabled(child))
			device_disable(child);
	}

	/* Free EROM (and any bridge register windows it might hold) */
	bhnd_erom_free(erom);
	erom = NULL;

	/* Map all valid core's config register blocks and perform interrupt
	 * assignment */
	for (u_int i = 0; i < cid->ncores; i++) {
		struct siba_devinfo	*dinfo;
		device_t		 child;

		child = children[i];

		/* Skip if core is disabled */
		if (bhnd_is_hw_disabled(child))
			continue;

		dinfo = device_get_ivars(child);

		/* Map the core's config blocks */
		if ((error = siba_map_cfg_resources(dev, dinfo)))
			goto failed;

		/* Issue bus callback for fully initialized child. */
		BHND_BUS_CHILD_ADDED(dev, child);
	}

	free(cores, M_BHND);
	free(children, M_BHND);

	return (0);

failed:
	for (u_int i = 0; i < cid->ncores; i++) {
		if (children[i] == NULL)
			continue;

		device_delete_child(dev, children[i]);
	}

	free(cores, M_BHND);
	free(children, M_BHND);
	if (erom != NULL)
		bhnd_erom_free(erom);

	return (error);
}

static device_method_t siba_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			siba_probe),
	DEVMETHOD(device_attach,		siba_attach),
	DEVMETHOD(device_detach,		siba_detach),
	DEVMETHOD(device_resume,		siba_resume),
	DEVMETHOD(device_suspend,		siba_suspend),
	
	/* Bus interface */
	DEVMETHOD(bus_add_child,		siba_add_child),
	DEVMETHOD(bus_child_deleted,		siba_child_deleted),
	DEVMETHOD(bus_read_ivar,		siba_read_ivar),
	DEVMETHOD(bus_write_ivar,		siba_write_ivar),
	DEVMETHOD(bus_get_resource_list,	siba_get_resource_list),

	/* BHND interface */
	DEVMETHOD(bhnd_bus_get_erom_class,	siba_get_erom_class),
	DEVMETHOD(bhnd_bus_alloc_pmu,		siba_alloc_pmu),
	DEVMETHOD(bhnd_bus_release_pmu,		siba_release_pmu),
	DEVMETHOD(bhnd_bus_request_clock,	siba_request_clock),
	DEVMETHOD(bhnd_bus_enable_clocks,	siba_enable_clocks),
	DEVMETHOD(bhnd_bus_request_ext_rsrc,	siba_request_ext_rsrc),
	DEVMETHOD(bhnd_bus_release_ext_rsrc,	siba_release_ext_rsrc),
	DEVMETHOD(bhnd_bus_get_clock_freq,	siba_get_clock_freq),
	DEVMETHOD(bhnd_bus_get_clock_latency,	siba_get_clock_latency),
	DEVMETHOD(bhnd_bus_read_ioctl,		siba_read_ioctl),
	DEVMETHOD(bhnd_bus_write_ioctl,		siba_write_ioctl),
	DEVMETHOD(bhnd_bus_read_iost,		siba_read_iost),
	DEVMETHOD(bhnd_bus_is_hw_suspended,	siba_is_hw_suspended),
	DEVMETHOD(bhnd_bus_reset_hw,		siba_reset_hw),
	DEVMETHOD(bhnd_bus_suspend_hw,		siba_suspend_hw),
	DEVMETHOD(bhnd_bus_read_config,		siba_read_config),
	DEVMETHOD(bhnd_bus_write_config,	siba_write_config),
	DEVMETHOD(bhnd_bus_get_port_count,	siba_get_port_count),
	DEVMETHOD(bhnd_bus_get_region_count,	siba_get_region_count),
	DEVMETHOD(bhnd_bus_get_port_rid,	siba_get_port_rid),
	DEVMETHOD(bhnd_bus_decode_port_rid,	siba_decode_port_rid),
	DEVMETHOD(bhnd_bus_get_region_addr,	siba_get_region_addr),
	DEVMETHOD(bhnd_bus_get_intr_count,	siba_get_intr_count),
	DEVMETHOD(bhnd_bus_get_intr_ivec,	siba_get_intr_ivec),

	DEVMETHOD_END
};

DEFINE_CLASS_1(bhnd, siba_driver, siba_methods, sizeof(struct siba_softc), bhnd_driver);

MODULE_VERSION(siba, 1);
MODULE_DEPEND(siba, bhnd, 1, 1, 1);

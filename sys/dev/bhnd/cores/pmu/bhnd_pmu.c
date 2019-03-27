/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/bhnd/bhndreg.h>
#include <dev/bhnd/bhndvar.h>
#include <dev/bhnd/cores/chipc/chipc.h>

#include "bhnd_nvram_map.h"

#include "bhnd_pmureg.h"
#include "bhnd_pmuvar.h"

#include "bhnd_pmu_private.h"

/*
 * Broadcom PMU driver.
 * 
 * On modern BHND chipsets, the PMU, GCI, and SRENG (Save/Restore Engine?)
 * register blocks are found within a dedicated PMU core (attached via
 * the AHB 'always on bus').
 * 
 * On earlier chipsets, these register blocks are found at the same
 * offsets within the ChipCommon core.
 */

devclass_t bhnd_pmu_devclass;	/**< bhnd(4) PMU device class */

static int	bhnd_pmu_sysctl_bus_freq(SYSCTL_HANDLER_ARGS);
static int	bhnd_pmu_sysctl_cpu_freq(SYSCTL_HANDLER_ARGS);
static int	bhnd_pmu_sysctl_mem_freq(SYSCTL_HANDLER_ARGS);

static uint32_t	bhnd_pmu_read_4(bus_size_t reg, void *ctx);
static void	bhnd_pmu_write_4(bus_size_t reg, uint32_t val, void *ctx);
static uint32_t	bhnd_pmu_read_chipst(void *ctx);

static const struct bhnd_pmu_io bhnd_pmu_res_io = {
	.rd4		= bhnd_pmu_read_4,
	.wr4		= bhnd_pmu_write_4,
	.rd_chipst	= bhnd_pmu_read_chipst
};

/**
 * Default bhnd_pmu driver implementation of DEVICE_PROBE().
 */
int
bhnd_pmu_probe(device_t dev)
{
	return (BUS_PROBE_DEFAULT);
}

/**
 * Default bhnd_pmu driver implementation of DEVICE_ATTACH().
 * 
 * @param dev PMU device.
 * @param res The PMU device registers. The driver will maintain a borrowed
 * reference to this resource for the lifetime of the device.
 */
int
bhnd_pmu_attach(device_t dev, struct bhnd_resource *res)
{
	struct bhnd_pmu_softc	*sc;
	struct sysctl_ctx_list	*ctx;
	struct sysctl_oid	*tree;
	devclass_t		 bhnd_class;
	device_t		 core, bus;
	int			 error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->res = res;

	/* Fetch capability flags */
	sc->caps = bhnd_bus_read_4(sc->res, BHND_PMU_CAP);

	/* Find the bus and bus-attached core */
	bhnd_class = devclass_find("bhnd");
	core = sc->dev;
	while ((bus = device_get_parent(core)) != NULL) {
		if (device_get_devclass(bus) == bhnd_class)
			break;

		core = bus;
	}

	if (core == NULL) {
		device_printf(sc->dev, "bhnd bus not found\n");
		return (ENXIO);
	}

	/* Fetch chip and board info */
	sc->cid = *bhnd_get_chipid(core);
	if ((error = bhnd_read_board_info(core, &sc->board))) {
		device_printf(sc->dev, "error fetching board info: %d\n",
		    error);
		return (ENXIO);
	}

	/* Initialize query state */
	error = bhnd_pmu_query_init(&sc->query, dev, sc->cid, &bhnd_pmu_res_io,
	    sc);
	if (error)
		return (error);
	sc->io = sc->query.io; 
	sc->io_ctx = sc->query.io_ctx;

	BPMU_LOCK_INIT(sc);

	/* Allocate our own core clkctl state directly; we use this to wait on
	 * PMU state transitions, avoiding a cyclic dependency between bhnd(4)'s
	 * clkctl handling and registration of this device as a PMU */
	sc->clkctl = bhnd_alloc_core_clkctl(core, dev, sc->res, BHND_CLK_CTL_ST,
	    BHND_PMU_MAX_TRANSITION_DLY);
	if (sc->clkctl == NULL) {
		device_printf(sc->dev, "failed to allocate clkctl for %s\n",
		    device_get_nameunit(core));
		error = ENOMEM;
		goto failed;
	}

	/* Locate ChipCommon device */
	sc->chipc_dev = bhnd_retain_provider(dev, BHND_SERVICE_CHIPC);
	if (sc->chipc_dev == NULL) {
		device_printf(sc->dev, "chipcommon device not found\n");
		error = ENXIO;
		goto failed;
	}

	/* Initialize PMU */
	if ((error = bhnd_pmu_init(sc))) {
		device_printf(sc->dev, "PMU init failed: %d\n", error);
		goto failed;
	}

	/* Register ourselves with the bus */
	if ((error = bhnd_register_provider(dev, BHND_SERVICE_PMU))) {
		device_printf(sc->dev, "failed to register PMU with bus : %d\n",
		    error);
		goto failed;
	}

	/* Set up sysctl nodes */
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "bus_freq", CTLTYPE_UINT | CTLFLAG_RD, sc, 0,
	    bhnd_pmu_sysctl_bus_freq, "IU", "Bus clock frequency");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "cpu_freq", CTLTYPE_UINT | CTLFLAG_RD, sc, 0,
	    bhnd_pmu_sysctl_cpu_freq, "IU", "CPU clock frequency");
	
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "mem_freq", CTLTYPE_UINT | CTLFLAG_RD, sc, 0,
	    bhnd_pmu_sysctl_mem_freq, "IU", "Memory clock frequency");

	return (0);

failed:
	BPMU_LOCK_DESTROY(sc);
	bhnd_pmu_query_fini(&sc->query);

	if (sc->clkctl != NULL)
		bhnd_free_core_clkctl(sc->clkctl);

	if (sc->chipc_dev != NULL) {
		bhnd_release_provider(sc->dev, sc->chipc_dev,
		    BHND_SERVICE_CHIPC);
	}

	return (error);
}

/**
 * Default bhnd_pmu driver implementation of DEVICE_DETACH().
 */
int
bhnd_pmu_detach(device_t dev)
{
	struct bhnd_pmu_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	if ((error = bhnd_deregister_provider(dev, BHND_SERVICE_ANY)))
		return (error);

	BPMU_LOCK_DESTROY(sc);
	bhnd_pmu_query_fini(&sc->query);
	bhnd_free_core_clkctl(sc->clkctl);
	bhnd_release_provider(sc->dev, sc->chipc_dev, BHND_SERVICE_CHIPC);
	
	return (0);
}

/**
 * Default bhnd_pmu driver implementation of DEVICE_SUSPEND().
 */
int
bhnd_pmu_suspend(device_t dev)
{
	return (0);
}

/**
 * Default bhnd_pmu driver implementation of DEVICE_RESUME().
 */
int
bhnd_pmu_resume(device_t dev)
{
	struct bhnd_pmu_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	/* Re-initialize PMU */
	if ((error = bhnd_pmu_init(sc))) {
		device_printf(sc->dev, "PMU init failed: %d\n", error);
		return (error);
	}

	return (0);
}

static int
bhnd_pmu_sysctl_bus_freq(SYSCTL_HANDLER_ARGS)
{
	struct bhnd_pmu_softc	*sc;
	uint32_t		 freq;
	
	sc = arg1;

	BPMU_LOCK(sc);
	freq = bhnd_pmu_si_clock(&sc->query);
	BPMU_UNLOCK(sc);

	return (sysctl_handle_32(oidp, NULL, freq, req));
}

static int
bhnd_pmu_sysctl_cpu_freq(SYSCTL_HANDLER_ARGS)
{
	struct bhnd_pmu_softc	*sc;
	uint32_t		 freq;
	
	sc = arg1;

	BPMU_LOCK(sc);
	freq = bhnd_pmu_cpu_clock(&sc->query);
	BPMU_UNLOCK(sc);

	return (sysctl_handle_32(oidp, NULL, freq, req));
}

static int
bhnd_pmu_sysctl_mem_freq(SYSCTL_HANDLER_ARGS)
{
	struct bhnd_pmu_softc	*sc;
	uint32_t		 freq;
	
	sc = arg1;

	BPMU_LOCK(sc);
	freq = bhnd_pmu_mem_clock(&sc->query);
	BPMU_UNLOCK(sc);

	return (sysctl_handle_32(oidp, NULL, freq, req));
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_READ_CHIPCTRL().
 */
static uint32_t
bhnd_pmu_read_chipctrl_method(device_t dev, uint32_t reg)
{
	struct bhnd_pmu_softc *sc;
	uint32_t rval;

	sc = device_get_softc(dev);

	BPMU_LOCK(sc);
	rval = BHND_PMU_CCTRL_READ(sc, reg);
	BPMU_UNLOCK(sc);

	return (rval);
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_WRITE_CHIPCTRL().
 */
static void
bhnd_pmu_write_chipctrl_method(device_t dev, uint32_t reg, uint32_t value,
    uint32_t mask)
{
	struct bhnd_pmu_softc *sc = device_get_softc(dev);

	BPMU_LOCK(sc);
	BHND_PMU_CCTRL_WRITE(sc, reg, value, mask);
	BPMU_UNLOCK(sc);
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_READ_REGCTRL().
 */
static uint32_t
bhnd_pmu_read_regctrl_method(device_t dev, uint32_t reg)
{
	struct bhnd_pmu_softc *sc;
	uint32_t rval;

	sc = device_get_softc(dev);

	BPMU_LOCK(sc);
	rval = BHND_PMU_REGCTRL_READ(sc, reg);
	BPMU_UNLOCK(sc);

	return (rval);
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_WRITE_REGCTRL().
 */
static void
bhnd_pmu_write_regctrl_method(device_t dev, uint32_t reg, uint32_t value,
    uint32_t mask)
{
	struct bhnd_pmu_softc *sc = device_get_softc(dev);

	BPMU_LOCK(sc);
	BHND_PMU_REGCTRL_WRITE(sc, reg, value, mask);
	BPMU_UNLOCK(sc);
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_READ_PLLCTRL().
 */
static uint32_t
bhnd_pmu_read_pllctrl_method(device_t dev, uint32_t reg)
{
	struct bhnd_pmu_softc *sc;
	uint32_t rval;

	sc = device_get_softc(dev);

	BPMU_LOCK(sc);
	rval = BHND_PMU_PLL_READ(sc, reg);
	BPMU_UNLOCK(sc);

	return (rval);
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_WRITE_PLLCTRL().
 */
static void
bhnd_pmu_write_pllctrl_method(device_t dev, uint32_t reg, uint32_t value,
    uint32_t mask)
{
	struct bhnd_pmu_softc *sc = device_get_softc(dev);

	BPMU_LOCK(sc);
	BHND_PMU_PLL_WRITE(sc, reg, value, mask);
	BPMU_UNLOCK(sc);
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_SET_VOLTAGE_RAW().
 */
static int
bhnd_pmu_set_voltage_raw_method(device_t dev, bhnd_pmu_regulator regulator,
    uint32_t value)
{
	struct bhnd_pmu_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	switch (regulator) {
	case BHND_REGULATOR_PAREF_LDO:
		if (value > UINT8_MAX)
			return (EINVAL);
	
		BPMU_LOCK(sc);
		error = bhnd_pmu_set_ldo_voltage(sc, SET_LDO_VOLTAGE_PAREF,
		    value);
		BPMU_UNLOCK(sc);

		return (error);

	default:
		return (ENODEV);
	}
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_ENABLE_REGULATOR().
 */
static int
bhnd_pmu_enable_regulator_method(device_t dev, bhnd_pmu_regulator regulator)
{
	struct bhnd_pmu_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	switch (regulator) {
	case BHND_REGULATOR_PAREF_LDO:
		BPMU_LOCK(sc);
		error = bhnd_pmu_paref_ldo_enable(sc, true);
		BPMU_UNLOCK(sc);

		return (error);

	default:
		return (ENODEV);
	}
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_DISABLE_REGULATOR().
 */
static int
bhnd_pmu_disable_regulator_method(device_t dev, bhnd_pmu_regulator regulator)
{
	struct bhnd_pmu_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	switch (regulator) {
	case BHND_REGULATOR_PAREF_LDO:
		BPMU_LOCK(sc);
		error = bhnd_pmu_paref_ldo_enable(sc, false);
		BPMU_UNLOCK(sc);

		return (error);

	default:
		return (ENODEV);
	}
}


/**
 * Default bhnd_pmu driver implementation of BHND_PMU_GET_CLOCK_LATENCY().
 */
static int
bhnd_pmu_get_clock_latency_method(device_t dev, bhnd_clock clock,
    u_int *latency)
{
	struct bhnd_pmu_softc	*sc;
	u_int			 pwrup_delay;
	int			 error;

	sc = device_get_softc(dev);

	switch (clock) {
	case BHND_CLOCK_HT:
		BPMU_LOCK(sc);
		error = bhnd_pmu_fast_pwrup_delay(sc, &pwrup_delay);
		BPMU_UNLOCK(sc);

		if (error)
			return (error);

		*latency = pwrup_delay;
		return (0);

	default:
		return (ENODEV);
	}
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_GET_CLOCK_FREQ().
 */
static int
bhnd_pmu_get_clock_freq_method(device_t dev, bhnd_clock clock, uint32_t *freq)
{
	struct bhnd_pmu_softc	*sc = device_get_softc(dev);

	BPMU_LOCK(sc);
	switch (clock) {
	case BHND_CLOCK_HT:
		*freq = bhnd_pmu_si_clock(&sc->query);
		break;

	case BHND_CLOCK_ALP:
		*freq = bhnd_pmu_alp_clock(&sc->query);
		break;

	case BHND_CLOCK_ILP:
		*freq = bhnd_pmu_ilp_clock(&sc->query);
		break;

	case BHND_CLOCK_DYN:
	default:
		BPMU_UNLOCK(sc);
		return (ENODEV);
	}

	BPMU_UNLOCK(sc);
	return (0);
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_REQUEST_SPURAVOID().
 */
static int
bhnd_pmu_request_spuravoid_method(device_t dev, bhnd_pmu_spuravoid spuravoid)
{
	struct bhnd_pmu_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	BPMU_LOCK(sc);
	error = bhnd_pmu_set_spuravoid(sc, spuravoid);
	BPMU_UNLOCK(sc);

	return (error);
}

/**
 * Default bhnd_pmu driver implementation of BHND_PMU_GET_TRANSITION_LATENCY().
 */
static u_int
bhnd_pmu_get_max_transition_latency_method(device_t dev)
{
	return (BHND_PMU_MAX_TRANSITION_DLY);
}

/* bhnd_pmu_query read_4 callback */
static uint32_t
bhnd_pmu_read_4(bus_size_t reg, void *ctx)
{
	struct bhnd_pmu_softc *sc = ctx;
	return (bhnd_bus_read_4(sc->res, reg));
}

/* bhnd_pmu_query write_4 callback */
static void
bhnd_pmu_write_4(bus_size_t reg, uint32_t val, void *ctx)
{
	struct bhnd_pmu_softc *sc = ctx;
	return (bhnd_bus_write_4(sc->res, reg, val));
}

/* bhnd_pmu_query read_chipst callback */
static uint32_t
bhnd_pmu_read_chipst(void *ctx)
{
	struct bhnd_pmu_softc *sc = ctx;
	return (BHND_CHIPC_READ_CHIPST(sc->chipc_dev));
}

static device_method_t bhnd_pmu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,				bhnd_pmu_probe),
	DEVMETHOD(device_detach,			bhnd_pmu_detach),
	DEVMETHOD(device_suspend,			bhnd_pmu_suspend),
	DEVMETHOD(device_resume,			bhnd_pmu_resume),

	/* BHND PMU interface */
	DEVMETHOD(bhnd_pmu_read_chipctrl,		bhnd_pmu_read_chipctrl_method),
	DEVMETHOD(bhnd_pmu_write_chipctrl,		bhnd_pmu_write_chipctrl_method),
	DEVMETHOD(bhnd_pmu_read_regctrl,		bhnd_pmu_read_regctrl_method),
	DEVMETHOD(bhnd_pmu_write_regctrl,		bhnd_pmu_write_regctrl_method),
	DEVMETHOD(bhnd_pmu_read_pllctrl,		bhnd_pmu_read_pllctrl_method),
	DEVMETHOD(bhnd_pmu_write_pllctrl,		bhnd_pmu_write_pllctrl_method),
	DEVMETHOD(bhnd_pmu_set_voltage_raw,		bhnd_pmu_set_voltage_raw_method),
	DEVMETHOD(bhnd_pmu_enable_regulator,		bhnd_pmu_enable_regulator_method),
	DEVMETHOD(bhnd_pmu_disable_regulator,		bhnd_pmu_disable_regulator_method),

	DEVMETHOD(bhnd_pmu_get_clock_latency,		bhnd_pmu_get_clock_latency_method),
	DEVMETHOD(bhnd_pmu_get_clock_freq,		bhnd_pmu_get_clock_freq_method),

	DEVMETHOD(bhnd_pmu_get_max_transition_latency,	bhnd_pmu_get_max_transition_latency_method),
	DEVMETHOD(bhnd_pmu_request_spuravoid,		bhnd_pmu_request_spuravoid_method),
	
	DEVMETHOD_END
};

DEFINE_CLASS_0(bhnd_pmu, bhnd_pmu_driver, bhnd_pmu_methods, sizeof(struct bhnd_pmu_softc));
MODULE_VERSION(bhnd_pmu, 1);

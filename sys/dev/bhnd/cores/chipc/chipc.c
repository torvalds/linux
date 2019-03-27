/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2016 Michael Zhilin <mizhka@gmail.com>
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

/*
 * Broadcom ChipCommon driver.
 * 
 * With the exception of some very early chipsets, the ChipCommon core
 * has been included in all HND SoCs and chipsets based on the siba(4) 
 * and bcma(4) interconnects, providing a common interface to chipset 
 * identification, bus enumeration, UARTs, clocks, watchdog interrupts,
 * GPIO, flash, etc.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/bhndvar.h>

#include "chipcreg.h"
#include "chipcvar.h"

#include "chipc_private.h"

devclass_t bhnd_chipc_devclass;	/**< bhnd(4) chipcommon device class */

static struct bhnd_device_quirk chipc_quirks[];

/* Supported device identifiers */
static const struct bhnd_device chipc_devices[] = {
	BHND_DEVICE(BCM, CC, NULL, chipc_quirks),
	BHND_DEVICE(BCM, 4706_CC, NULL, chipc_quirks),
	BHND_DEVICE_END
};


/* Device quirks table */
static struct bhnd_device_quirk chipc_quirks[] = {
	/* HND OTP controller revisions */
	BHND_CORE_QUIRK	(HWREV_EQ (12),		CHIPC_QUIRK_OTP_HND), /* (?) */
	BHND_CORE_QUIRK	(HWREV_EQ (17),		CHIPC_QUIRK_OTP_HND), /* BCM4311 */
	BHND_CORE_QUIRK	(HWREV_EQ (22),		CHIPC_QUIRK_OTP_HND), /* BCM4312 */

	/* IPX OTP controller revisions */
	BHND_CORE_QUIRK	(HWREV_EQ (21),		CHIPC_QUIRK_OTP_IPX),
	BHND_CORE_QUIRK	(HWREV_GTE(23),		CHIPC_QUIRK_OTP_IPX),
	
	BHND_CORE_QUIRK	(HWREV_GTE(32),		CHIPC_QUIRK_SUPPORTS_SPROM),
	BHND_CORE_QUIRK	(HWREV_GTE(35),		CHIPC_QUIRK_SUPPORTS_CAP_EXT),
	BHND_CORE_QUIRK	(HWREV_GTE(49),		CHIPC_QUIRK_IPX_OTPL_SIZE),

	/* 4706 variant quirks */
	BHND_CORE_QUIRK	(HWREV_EQ (38),		CHIPC_QUIRK_4706_NFLASH), /* BCM5357? */
	BHND_CHIP_QUIRK	(4706,	HWREV_ANY,	CHIPC_QUIRK_4706_NFLASH),

	/* 4331 quirks*/
	BHND_CHIP_QUIRK	(4331,	HWREV_ANY,	CHIPC_QUIRK_4331_EXTPA_MUX_SPROM),
	BHND_PKG_QUIRK	(4331,	TN,		CHIPC_QUIRK_4331_GPIO2_5_MUX_SPROM),
	BHND_PKG_QUIRK	(4331,	TNA0,		CHIPC_QUIRK_4331_GPIO2_5_MUX_SPROM),
	BHND_PKG_QUIRK	(4331,	TT,		CHIPC_QUIRK_4331_EXTPA2_MUX_SPROM),

	/* 4360 quirks */
	BHND_CHIP_QUIRK	(4352,	HWREV_LTE(2),	CHIPC_QUIRK_4360_FEM_MUX_SPROM),
	BHND_CHIP_QUIRK	(43460,	HWREV_LTE(2),	CHIPC_QUIRK_4360_FEM_MUX_SPROM),
	BHND_CHIP_QUIRK	(43462,	HWREV_LTE(2),	CHIPC_QUIRK_4360_FEM_MUX_SPROM),
	BHND_CHIP_QUIRK	(43602,	HWREV_LTE(2),	CHIPC_QUIRK_4360_FEM_MUX_SPROM),

	BHND_DEVICE_QUIRK_END
};

static int		 chipc_add_children(struct chipc_softc *sc);

static bhnd_nvram_src	 chipc_find_nvram_src(struct chipc_softc *sc,
			     struct chipc_caps *caps);
static int		 chipc_read_caps(struct chipc_softc *sc,
			     struct chipc_caps *caps);

static bool		 chipc_should_enable_muxed_sprom(
			     struct chipc_softc *sc);
static int		 chipc_enable_otp_power(struct chipc_softc *sc);
static void		 chipc_disable_otp_power(struct chipc_softc *sc);
static int		 chipc_enable_sprom_pins(struct chipc_softc *sc);
static void		 chipc_disable_sprom_pins(struct chipc_softc *sc);

static int		 chipc_try_activate_resource(struct chipc_softc *sc,
			     device_t child, int type, int rid,
			     struct resource *r, bool req_direct);

static int		 chipc_init_rman(struct chipc_softc *sc);
static void		 chipc_free_rman(struct chipc_softc *sc);
static struct rman	*chipc_get_rman(struct chipc_softc *sc, int type);

/* quirk and capability flag convenience macros */
#define	CHIPC_QUIRK(_sc, _name)	\
    ((_sc)->quirks & CHIPC_QUIRK_ ## _name)
    
#define CHIPC_CAP(_sc, _name)	\
    ((_sc)->caps._name)

#define	CHIPC_ASSERT_QUIRK(_sc, name)	\
    KASSERT(CHIPC_QUIRK((_sc), name), ("quirk " __STRING(_name) " not set"))

#define	CHIPC_ASSERT_CAP(_sc, name)	\
    KASSERT(CHIPC_CAP((_sc), name), ("capability " __STRING(_name) " not set"))

static int
chipc_probe(device_t dev)
{
	const struct bhnd_device *id;

	id = bhnd_device_lookup(dev, chipc_devices, sizeof(chipc_devices[0]));
	if (id == NULL)
		return (ENXIO);

	bhnd_set_default_core_desc(dev);
	return (BUS_PROBE_DEFAULT);
}

static int
chipc_attach(device_t dev)
{
	struct chipc_softc		*sc;
	int				 error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->quirks = bhnd_device_quirks(dev, chipc_devices,
	    sizeof(chipc_devices[0]));
	sc->sprom_refcnt = 0;

	CHIPC_LOCK_INIT(sc);
	STAILQ_INIT(&sc->mem_regions);

	/* Set up resource management */
	if ((error = chipc_init_rman(sc))) {
		device_printf(sc->dev,
		    "failed to initialize chipc resource state: %d\n", error);
		goto failed;
	}

	/* Allocate the region containing the chipc register block */
	if ((sc->core_region = chipc_find_region_by_rid(sc, 0)) == NULL) {
		error = ENXIO;
		goto failed;
	}

	error = chipc_retain_region(sc, sc->core_region,
	    RF_ALLOCATED|RF_ACTIVE);
	if (error) {
		sc->core_region = NULL;
		goto failed;
	}

	/* Save a direct reference to our chipc registers */
	sc->core = sc->core_region->cr_res;

	/* Fetch and parse capability register(s) */
	if ((error = chipc_read_caps(sc, &sc->caps)))
		goto failed;

	if (bootverbose)
		chipc_print_caps(sc->dev, &sc->caps);

	/* Attach all supported child devices */
	if ((error = chipc_add_children(sc)))
		goto failed;

	/*
	 * Register ourselves with the bus; we're fully initialized and can
	 * response to ChipCommin API requests.
	 * 
	 * Since our children may need access to ChipCommon, this must be done
	 * before attaching our children below (via bus_generic_attach).
	 */
	if ((error = bhnd_register_provider(dev, BHND_SERVICE_CHIPC)))
		goto failed;

	if ((error = bus_generic_attach(dev)))
		goto failed;

	return (0);
	
failed:
	device_delete_children(sc->dev);

	if (sc->core_region != NULL) {
		chipc_release_region(sc, sc->core_region,
		    RF_ALLOCATED|RF_ACTIVE);
	}

	chipc_free_rman(sc);
	CHIPC_LOCK_DESTROY(sc);
	return (error);
}

static int
chipc_detach(device_t dev)
{
	struct chipc_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	if ((error = bus_generic_detach(dev)))
		return (error);

	if ((error = device_delete_children(dev)))
		return (error);

	if ((error = bhnd_deregister_provider(dev, BHND_SERVICE_ANY)))
		return (error);

	chipc_release_region(sc, sc->core_region, RF_ALLOCATED|RF_ACTIVE);
	chipc_free_rman(sc);

	CHIPC_LOCK_DESTROY(sc);

	return (0);
}

static int
chipc_add_children(struct chipc_softc *sc)
{
	device_t	 child;
	const char	*flash_bus;
	int		 error;

	/* SPROM/OTP */
	if (sc->caps.nvram_src == BHND_NVRAM_SRC_SPROM ||
	    sc->caps.nvram_src == BHND_NVRAM_SRC_OTP)
	{
		child = BUS_ADD_CHILD(sc->dev, 0, "bhnd_nvram", -1);
		if (child == NULL) {
			device_printf(sc->dev, "failed to add nvram device\n");
			return (ENXIO);
		}

		/* Both OTP and external SPROM are mapped at CHIPC_SPROM_OTP */
		error = chipc_set_mem_resource(sc, child, 0, CHIPC_SPROM_OTP,
		    CHIPC_SPROM_OTP_SIZE, 0, 0);
		if (error) {
			device_printf(sc->dev, "failed to set OTP memory "
			    "resource: %d\n", error);
			return (error);
		}
	}

	/*
	 * PMU/PWR_CTRL
	 * 
	 * On AOB ("Always on Bus") devices, the PMU core (if it exists) is
	 * attached directly to the bhnd(4) bus -- not chipc.
	 */
	if (sc->caps.pmu && !sc->caps.aob) {
		child = BUS_ADD_CHILD(sc->dev, 0, "bhnd_pmu", -1);
		if (child == NULL) {
			device_printf(sc->dev, "failed to add pmu\n");
			return (ENXIO);
		}
	} else if (sc->caps.pwr_ctrl) {
		child = BUS_ADD_CHILD(sc->dev, 0, "bhnd_pwrctl", -1);
		if (child == NULL) {
			device_printf(sc->dev, "failed to add pwrctl\n");
			return (ENXIO);
		}
	}

	/* GPIO */
	child = BUS_ADD_CHILD(sc->dev, 0, "gpio", -1);
	if (child == NULL) {
		device_printf(sc->dev, "failed to add gpio\n");
		return (ENXIO);
	}

	error = chipc_set_mem_resource(sc, child, 0, 0, RM_MAX_END, 0, 0);
	if (error) {
		device_printf(sc->dev, "failed to set gpio memory resource: "
		    "%d\n", error);
		return (error);
	}

	/* All remaining devices are SoC-only */
	if (bhnd_get_attach_type(sc->dev) != BHND_ATTACH_NATIVE)
		return (0);

	/* UARTs */
	for (u_int i = 0; i < min(sc->caps.num_uarts, CHIPC_UART_MAX); i++) {
		int irq_rid, mem_rid;

		irq_rid = 0;
		mem_rid = 0;

		child = BUS_ADD_CHILD(sc->dev, 0, "uart", -1);
		if (child == NULL) {
			device_printf(sc->dev, "failed to add uart%u\n", i);
			return (ENXIO);
		}

		/* Shared IRQ */
		error = chipc_set_irq_resource(sc, child, irq_rid, 0);
		if (error) {
			device_printf(sc->dev, "failed to set uart%u irq %u\n",
			    i, 0);
			return (error);
		}

		/* UART registers are mapped sequentially */
		error = chipc_set_mem_resource(sc, child, mem_rid,
		    CHIPC_UART(i), CHIPC_UART_SIZE, 0, 0);
		if (error) {
			device_printf(sc->dev, "failed to set uart%u memory "
			    "resource: %d\n", i, error);
			return (error);
		}
	}

	/* Flash */
	flash_bus = chipc_flash_bus_name(sc->caps.flash_type);
	if (flash_bus != NULL) {
		int rid;

		child = BUS_ADD_CHILD(sc->dev, 0, flash_bus, -1);
		if (child == NULL) {
			device_printf(sc->dev, "failed to add %s device\n",
			    flash_bus);
			return (ENXIO);
		}

		/* flash memory mapping */
		rid = 0;
		error = chipc_set_mem_resource(sc, child, rid, 0, RM_MAX_END, 1,
		    1);
		if (error) {
			device_printf(sc->dev, "failed to set flash memory "
			    "resource %d: %d\n", rid, error);
			return (error);
		}

		/* flashctrl registers */
		rid++;
		error = chipc_set_mem_resource(sc, child, rid,
		    CHIPC_SFLASH_BASE, CHIPC_SFLASH_SIZE, 0, 0);
		if (error) {
			device_printf(sc->dev, "failed to set flash memory "
			    "resource %d: %d\n", rid, error);
			return (error);
		}
	}

	return (0);
}

/**
 * Determine the NVRAM data source for this device.
 * 
 * The SPROM, OTP, and flash capability flags must be fully populated in
 * @p caps.
 *
 * @param sc chipc driver state.
 * @param caps capability flags to be used to derive NVRAM configuration.
 */
static bhnd_nvram_src
chipc_find_nvram_src(struct chipc_softc *sc, struct chipc_caps *caps)
{
	uint32_t		 otp_st, srom_ctrl;

	/*
	 * We check for hardware presence in order of precedence. For example,
	 * SPROM is is always used in preference to internal OTP if found.
	 */
	if (CHIPC_QUIRK(sc, SUPPORTS_SPROM) && caps->sprom) {
		srom_ctrl = bhnd_bus_read_4(sc->core, CHIPC_SPROM_CTRL);
		if (srom_ctrl & CHIPC_SRC_PRESENT)
			return (BHND_NVRAM_SRC_SPROM);
	}

	/* Check for programmed OTP H/W subregion (contains SROM data) */
	if (CHIPC_QUIRK(sc, SUPPORTS_OTP) && caps->otp_size > 0) {
		/* TODO: need access to HND-OTP device */
		if (!CHIPC_QUIRK(sc, OTP_HND)) {
			device_printf(sc->dev,
			    "NVRAM unavailable: unsupported OTP controller.\n");
			return (BHND_NVRAM_SRC_UNKNOWN);
		}

		otp_st = bhnd_bus_read_4(sc->core, CHIPC_OTPST);
		if (otp_st & CHIPC_OTPS_GUP_HW)
			return (BHND_NVRAM_SRC_OTP);
	}

	/* Check for flash */
	if (caps->flash_type != CHIPC_FLASH_NONE)
		return (BHND_NVRAM_SRC_FLASH);

	/* No NVRAM hardware capability declared */
	return (BHND_NVRAM_SRC_UNKNOWN);
}

/* Read and parse chipc capabilities */
static int
chipc_read_caps(struct chipc_softc *sc, struct chipc_caps *caps)
{
	uint32_t	cap_reg;
	uint32_t	cap_ext_reg;
	uint32_t	regval;

	/* Fetch cap registers */
	cap_reg = bhnd_bus_read_4(sc->core, CHIPC_CAPABILITIES);
	cap_ext_reg = 0;
	if (CHIPC_QUIRK(sc, SUPPORTS_CAP_EXT))
		cap_ext_reg = bhnd_bus_read_4(sc->core, CHIPC_CAPABILITIES_EXT);

	/* Extract values */
	caps->num_uarts		= CHIPC_GET_BITS(cap_reg, CHIPC_CAP_NUM_UART);
	caps->mipseb		= CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_MIPSEB);
	caps->uart_gpio		= CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_UARTGPIO);
	caps->uart_clock	= CHIPC_GET_BITS(cap_reg, CHIPC_CAP_UCLKSEL);

	caps->extbus_type	= CHIPC_GET_BITS(cap_reg, CHIPC_CAP_EXTBUS);
	caps->pwr_ctrl		= CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_PWR_CTL);
	caps->jtag_master	= CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_JTAGP);

	caps->pll_type		= CHIPC_GET_BITS(cap_reg, CHIPC_CAP_PLL);
	caps->backplane_64	= CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_BKPLN64);
	caps->boot_rom		= CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_ROM);
	caps->pmu		= CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_PMU);
	caps->eci		= CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_ECI);
	caps->sprom		= CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_SPROM);
	caps->otp_size		= CHIPC_GET_BITS(cap_reg, CHIPC_CAP_OTP_SIZE);

	caps->seci		= CHIPC_GET_FLAG(cap_ext_reg, CHIPC_CAP2_SECI);
	caps->gsio		= CHIPC_GET_FLAG(cap_ext_reg, CHIPC_CAP2_GSIO);
	caps->aob		= CHIPC_GET_FLAG(cap_ext_reg, CHIPC_CAP2_AOB);

	/* Fetch OTP size for later IPX controller revisions */
	if (CHIPC_QUIRK(sc, IPX_OTPL_SIZE)) {
		regval = bhnd_bus_read_4(sc->core, CHIPC_OTPLAYOUT);
		caps->otp_size = CHIPC_GET_BITS(regval, CHIPC_OTPL_SIZE);
	}

	/* Determine flash type and parameters */
	caps->cfi_width = 0;
	switch (CHIPC_GET_BITS(cap_reg, CHIPC_CAP_FLASH)) {
	case CHIPC_CAP_SFLASH_ST:
		caps->flash_type = CHIPC_SFLASH_ST;
		break;
	case CHIPC_CAP_SFLASH_AT:
		caps->flash_type = CHIPC_SFLASH_AT;
		break;
	case CHIPC_CAP_NFLASH:
		/* unimplemented */
		caps->flash_type = CHIPC_NFLASH;
		break;
	case CHIPC_CAP_PFLASH:
		caps->flash_type = CHIPC_PFLASH_CFI;

		/* determine cfi width */
		regval = bhnd_bus_read_4(sc->core, CHIPC_FLASH_CFG);
		if (CHIPC_GET_FLAG(regval, CHIPC_FLASH_CFG_DS))
			caps->cfi_width = 2;
		else
			caps->cfi_width = 1;

		break;
	case CHIPC_CAP_FLASH_NONE:
		caps->flash_type = CHIPC_FLASH_NONE;
		break;
			
	}

	/* Handle 4706_NFLASH fallback */
	if (CHIPC_QUIRK(sc, 4706_NFLASH) &&
	    CHIPC_GET_FLAG(cap_reg, CHIPC_CAP_4706_NFLASH))
	{
		caps->flash_type = CHIPC_NFLASH_4706;
	}


	/* Determine NVRAM source. Must occur after the SPROM/OTP/flash
	 * capability flags have been populated. */
	caps->nvram_src = chipc_find_nvram_src(sc, caps);

	/* Determine the SPROM offset within OTP (if any). SPROM-formatted
	 * data is placed within the OTP general use region. */
	caps->sprom_offset = 0;
	if (caps->nvram_src == BHND_NVRAM_SRC_OTP) {
		CHIPC_ASSERT_QUIRK(sc, OTP_IPX);

		/* Bit offset to GUP HW subregion containing SPROM data */
		regval = bhnd_bus_read_4(sc->core, CHIPC_OTPLAYOUT);
		caps->sprom_offset = CHIPC_GET_BITS(regval, CHIPC_OTPL_GUP);

		/* Convert to bytes */
		caps->sprom_offset /= 8;
	}

	return (0);
}

static int
chipc_suspend(device_t dev)
{
	return (bus_generic_suspend(dev));
}

static int
chipc_resume(device_t dev)
{
	return (bus_generic_resume(dev));
}

static void
chipc_probe_nomatch(device_t dev, device_t child)
{
	struct resource_list	*rl;
	const char		*name;

	name = device_get_name(child);
	if (name == NULL)
		name = "unknown device";

	device_printf(dev, "<%s> at", name);

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (rl != NULL) {
		resource_list_print_type(rl, "mem", SYS_RES_MEMORY, "%#jx");
		resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%jd");
	}

	printf(" (no driver attached)\n");
}

static int
chipc_print_child(device_t dev, device_t child)
{
	struct resource_list	*rl;
	int			 retval = 0;

	retval += bus_print_child_header(dev, child);

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (rl != NULL) {
		retval += resource_list_print_type(rl, "mem", SYS_RES_MEMORY,
		    "%#jx");
		retval += resource_list_print_type(rl, "irq", SYS_RES_IRQ,
		    "%jd");
	}

	retval += bus_print_child_domain(dev, child);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static int
chipc_child_pnpinfo_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	if (buflen == 0)
		return (EOVERFLOW);

	*buf = '\0';
	return (0);
}

static int
chipc_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	if (buflen == 0)
		return (EOVERFLOW);

	*buf = '\0';
	return (ENXIO);
}

static device_t
chipc_add_child(device_t dev, u_int order, const char *name, int unit)
{
	struct chipc_softc	*sc;
	struct chipc_devinfo	*dinfo;
	device_t		 child;

	sc = device_get_softc(dev);

	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL)
		return (NULL);

	dinfo = malloc(sizeof(struct chipc_devinfo), M_BHND, M_NOWAIT);
	if (dinfo == NULL) {
		device_delete_child(dev, child);
		return (NULL);
	}

	resource_list_init(&dinfo->resources);
	dinfo->irq_mapped = false;
	device_set_ivars(child, dinfo);

	return (child);
}

static void
chipc_child_deleted(device_t dev, device_t child)
{
	struct chipc_devinfo *dinfo = device_get_ivars(child);

	if (dinfo != NULL) {
		/* Free the child's resource list */
		resource_list_free(&dinfo->resources);

		/* Unmap the child's IRQ */
		if (dinfo->irq_mapped) {
			bhnd_unmap_intr(dev, dinfo->irq);
			dinfo->irq_mapped = false;
		}

		free(dinfo, M_BHND);
	}

	device_set_ivars(child, NULL);
}

static struct resource_list *
chipc_get_resource_list(device_t dev, device_t child)
{
	struct chipc_devinfo *dinfo = device_get_ivars(child);
	return (&dinfo->resources);
}


/* Allocate region records for the given port, and add the port's memory
 * range to the mem_rman */
static int
chipc_rman_init_regions (struct chipc_softc *sc, bhnd_port_type type,
    u_int port)
{
	struct	chipc_region	*cr;
	rman_res_t		 start, end;
	u_int			 num_regions;
	int			 error;

	num_regions = bhnd_get_region_count(sc->dev, type, port);
	for (u_int region = 0; region < num_regions; region++) {
		/* Allocate new region record */
		cr = chipc_alloc_region(sc, type, port, region);
		if (cr == NULL)
			return (ENODEV);

		/* Can't manage regions that cannot be allocated */
		if (cr->cr_rid < 0) {
			BHND_DEBUG_DEV(sc->dev, "no rid for chipc region "
			    "%s%u.%u", bhnd_port_type_name(type), port, region);
			chipc_free_region(sc, cr);
			continue;
		}

		/* Add to rman's managed range */
		start = cr->cr_addr;
		end = cr->cr_end;
		if ((error = rman_manage_region(&sc->mem_rman, start, end))) {
			chipc_free_region(sc, cr);
			return (error);
		}

		/* Add to region list */
		STAILQ_INSERT_TAIL(&sc->mem_regions, cr, cr_link);
	}

	return (0);
}

/* Initialize memory state for all chipc port regions */
static int
chipc_init_rman(struct chipc_softc *sc)
{
	u_int	num_ports;
	int	error;

	/* Port types for which we'll register chipc_region mappings */
	bhnd_port_type types[] = {
	    BHND_PORT_DEVICE
	};

	/* Initialize resource manager */
	sc->mem_rman.rm_start = 0;
	sc->mem_rman.rm_end = BUS_SPACE_MAXADDR;
	sc->mem_rman.rm_type = RMAN_ARRAY;
	sc->mem_rman.rm_descr = "ChipCommon Device Memory";
	if ((error = rman_init(&sc->mem_rman))) {
		device_printf(sc->dev, "could not initialize mem_rman: %d\n",
		    error);
		return (error);
	}

	/* Populate per-port-region state */
	for (u_int i = 0; i < nitems(types); i++) {
		num_ports = bhnd_get_port_count(sc->dev, types[i]);
		for (u_int port = 0; port < num_ports; port++) {
			error = chipc_rman_init_regions(sc, types[i], port);
			if (error) {
				device_printf(sc->dev,
				    "region init failed for %s%u: %d\n",
				     bhnd_port_type_name(types[i]), port,
				     error);

				goto failed;
			}
		}
	}

	return (0);

failed:
	chipc_free_rman(sc);
	return (error);
}

/* Free memory management state */
static void
chipc_free_rman(struct chipc_softc *sc)
{
	struct chipc_region *cr, *cr_next;

	STAILQ_FOREACH_SAFE(cr, &sc->mem_regions, cr_link, cr_next)
		chipc_free_region(sc, cr);

	rman_fini(&sc->mem_rman);
}

/**
 * Return the rman instance for a given resource @p type, if any.
 * 
 * @param sc The chipc device state.
 * @param type The resource type (e.g. SYS_RES_MEMORY, SYS_RES_IRQ, ...)
 */
static struct rman *
chipc_get_rman(struct chipc_softc *sc, int type)
{	
	switch (type) {
	case SYS_RES_MEMORY:
		return (&sc->mem_rman);

	case SYS_RES_IRQ:
		/* We delegate IRQ resource management to the parent bus */
		return (NULL);

	default:
		return (NULL);
	};
}

static struct resource *
chipc_alloc_resource(device_t dev, device_t child, int type,
    int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct chipc_softc		*sc;
	struct chipc_region		*cr;
	struct resource_list_entry	*rle;
	struct resource			*rv;
	struct rman			*rm;
	int				 error;
	bool				 passthrough, isdefault;

	sc = device_get_softc(dev);
	passthrough = (device_get_parent(child) != dev);
	isdefault = RMAN_IS_DEFAULT_RANGE(start, end);
	rle = NULL;

	/* Fetch the resource manager, delegate request if necessary */
	rm = chipc_get_rman(sc, type);
	if (rm == NULL) {
		/* Requested resource type is delegated to our parent */
		rv = bus_generic_rl_alloc_resource(dev, child, type, rid,
		    start, end, count, flags);
		return (rv);
	}

	/* Populate defaults */
	if (!passthrough && isdefault) {
		/* Fetch the resource list entry. */
		rle = resource_list_find(BUS_GET_RESOURCE_LIST(dev, child),
		    type, *rid);
		if (rle == NULL) {
			device_printf(dev,
			    "default resource %#x type %d for child %s "
			    "not found\n", *rid, type,
			    device_get_nameunit(child));			
			return (NULL);
		}
		
		if (rle->res != NULL) {
			device_printf(dev,
			    "resource entry %#x type %d for child %s is busy "
			    "[%d]\n",
			    *rid, type, device_get_nameunit(child),
			    rman_get_flags(rle->res));
			
			return (NULL);
		}

		start = rle->start;
		end = rle->end;
		count = ulmax(count, rle->count);
	}

	/* Locate a mapping region */
	if ((cr = chipc_find_region(sc, start, end)) == NULL) {
		/* Resource requests outside our shared port regions can be
		 * delegated to our parent. */
		rv = bus_generic_rl_alloc_resource(dev, child, type, rid,
		    start, end, count, flags);
		return (rv);
	}

	/*
	 * As a special case, children that map the complete ChipCommon register
	 * block are delegated to our parent.
	 *
	 * The rman API does not support sharing resources that are not
	 * identical in size; since we allocate subregions to various children,
	 * any children that need to map the entire register block (e.g. because
	 * they require access to discontiguous register ranges) must make the
	 * allocation through our parent, where we hold a compatible
	 * RF_SHAREABLE allocation.
	 */
	if (cr == sc->core_region && cr->cr_addr == start &&
	    cr->cr_end == end && cr->cr_count == count)
	{
		rv = bus_generic_rl_alloc_resource(dev, child, type, rid,
		    start, end, count, flags);
		return (rv);
	}

	/* Try to retain a region reference */
	if ((error = chipc_retain_region(sc, cr, RF_ALLOCATED)))
		return (NULL);

	/* Make our rman reservation */
	rv = rman_reserve_resource(rm, start, end, count, flags & ~RF_ACTIVE,
	    child);
	if (rv == NULL) {
		chipc_release_region(sc, cr, RF_ALLOCATED);
		return (NULL);
	}

	rman_set_rid(rv, *rid);

	/* Activate */
	if (flags & RF_ACTIVE) {
		error = bus_activate_resource(child, type, *rid, rv);
		if (error) {
			device_printf(dev,
			    "failed to activate entry %#x type %d for "
				"child %s: %d\n",
			     *rid, type, device_get_nameunit(child), error);

			chipc_release_region(sc, cr, RF_ALLOCATED);
			rman_release_resource(rv);

			return (NULL);
		}
	}

	/* Update child's resource list entry */
	if (rle != NULL) {
		rle->res = rv;
		rle->start = rman_get_start(rv);
		rle->end = rman_get_end(rv);
		rle->count = rman_get_size(rv);
	}

	return (rv);
}

static int
chipc_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	struct chipc_softc		*sc;
	struct chipc_region		*cr;
	struct rman			*rm;
	struct resource_list_entry	*rle;
	int			 	 error;

	sc = device_get_softc(dev);

	/* Handled by parent bus? */
	rm = chipc_get_rman(sc, type);
	if (rm == NULL || !rman_is_region_manager(r, rm)) {
		return (bus_generic_rl_release_resource(dev, child, type, rid,
		    r));
	}

	/* Locate the mapping region */
	cr = chipc_find_region(sc, rman_get_start(r), rman_get_end(r));
	if (cr == NULL)
		return (EINVAL);

	/* Deactivate resources */
	if (rman_get_flags(r) & RF_ACTIVE) {
		error = BUS_DEACTIVATE_RESOURCE(dev, child, type, rid, r);
		if (error)
			return (error);
	}

	if ((error = rman_release_resource(r)))
		return (error);

	/* Drop allocation reference */
	chipc_release_region(sc, cr, RF_ALLOCATED);

	/* Clear reference from the resource list entry if exists */
	rle = resource_list_find(BUS_GET_RESOURCE_LIST(dev, child), type, rid);
	if (rle != NULL)
		rle->res = NULL;

	return (0);
}

static int
chipc_adjust_resource(device_t dev, device_t child, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct chipc_softc		*sc;
	struct chipc_region		*cr;
	struct rman			*rm;
	
	sc = device_get_softc(dev);

	/* Handled by parent bus? */
	rm = chipc_get_rman(sc, type);
	if (rm == NULL || !rman_is_region_manager(r, rm)) {
		return (bus_generic_adjust_resource(dev, child, type, r, start,
		    end));
	}

	/* The range is limited to the existing region mapping */
	cr = chipc_find_region(sc, rman_get_start(r), rman_get_end(r));
	if (cr == NULL)
		return (EINVAL);
	
	if (end <= start)
		return (EINVAL);

	if (start < cr->cr_addr || end > cr->cr_end)
		return (EINVAL);

	/* Range falls within the existing region */
	return (rman_adjust_resource(r, start, end));
}

/**
 * Retain an RF_ACTIVE reference to the region mapping @p r, and
 * configure @p r with its subregion values.
 *
 * @param sc Driver instance state.
 * @param child Requesting child device.
 * @param type resource type of @p r.
 * @param rid resource id of @p r
 * @param r resource to be activated.
 * @param req_direct If true, failure to allocate a direct bhnd resource
 * will be treated as an error. If false, the resource will not be marked
 * as RF_ACTIVE if bhnd direct resource allocation fails.
 */
static int
chipc_try_activate_resource(struct chipc_softc *sc, device_t child, int type,
    int rid, struct resource *r, bool req_direct)
{
	struct rman		*rm;
	struct chipc_region	*cr;
	bhnd_size_t		 cr_offset;
	rman_res_t		 r_start, r_end, r_size;
	int			 error;

	rm = chipc_get_rman(sc, type);
	if (rm == NULL || !rman_is_region_manager(r, rm))
		return (EINVAL);

	r_start = rman_get_start(r);
	r_end = rman_get_end(r);
	r_size = rman_get_size(r);

	/* Find the corresponding chipc region */
	cr = chipc_find_region(sc, r_start, r_end);
	if (cr == NULL)
		return (EINVAL);
	
	/* Calculate subregion offset within the chipc region */
	cr_offset = r_start - cr->cr_addr;

	/* Retain (and activate, if necessary) the chipc region */
	if ((error = chipc_retain_region(sc, cr, RF_ACTIVE)))
		return (error);

	/* Configure child resource with its subregion values. */
	if (cr->cr_res->direct) {
		error = chipc_init_child_resource(r, cr->cr_res->res,
		    cr_offset, r_size);
		if (error)
			goto cleanup;

		/* Mark active */
		if ((error = rman_activate_resource(r)))
			goto cleanup;
	} else if (req_direct) {
		error = ENOMEM;
		goto cleanup;
	}

	return (0);

cleanup:
	chipc_release_region(sc, cr, RF_ACTIVE);
	return (error);
}

static int
chipc_activate_bhnd_resource(device_t dev, device_t child, int type,
    int rid, struct bhnd_resource *r)
{
	struct chipc_softc	*sc;
	struct rman		*rm;
	int			 error;

	sc = device_get_softc(dev);
	
	/* Delegate non-locally managed resources to parent */
	rm = chipc_get_rman(sc, type);
	if (rm == NULL || !rman_is_region_manager(r->res, rm)) {
		return (bhnd_bus_generic_activate_resource(dev, child, type,
		    rid, r));
	}

	/* Try activating the chipc region resource */
	error = chipc_try_activate_resource(sc, child, type, rid, r->res,
	    false);
	if (error)
		return (error);

	/* Mark the child resource as direct according to the returned resource
	 * state */
	if (rman_get_flags(r->res) & RF_ACTIVE)
		r->direct = true;

	return (0);
}

static int
chipc_activate_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	struct chipc_softc	*sc;
	struct rman		*rm;

	sc = device_get_softc(dev);

	/* Delegate non-locally managed resources to parent */
	rm = chipc_get_rman(sc, type);
	if (rm == NULL || !rman_is_region_manager(r, rm)) {
		return (bus_generic_activate_resource(dev, child, type, rid,
		    r));
	}

	/* Try activating the chipc region-based resource */
	return (chipc_try_activate_resource(sc, child, type, rid, r, true));
}

/**
 * Default bhndb(4) implementation of BUS_DEACTIVATE_RESOURCE().
 */
static int
chipc_deactivate_resource(device_t dev, device_t child, int type,
    int rid, struct resource *r)
{
	struct chipc_softc	*sc;
	struct chipc_region	*cr;
	struct rman		*rm;
	int			 error;

	sc = device_get_softc(dev);

	/* Handled by parent bus? */
	rm = chipc_get_rman(sc, type);
	if (rm == NULL || !rman_is_region_manager(r, rm)) {
		return (bus_generic_deactivate_resource(dev, child, type, rid,
		    r));
	}

	/* Find the corresponding chipc region */
	cr = chipc_find_region(sc, rman_get_start(r), rman_get_end(r));
	if (cr == NULL)
		return (EINVAL);

	/* Mark inactive */
	if ((error = rman_deactivate_resource(r)))
		return (error);

	/* Drop associated RF_ACTIVE reference */
	chipc_release_region(sc, cr, RF_ACTIVE);

	return (0);
}

/**
 * Examine bus state and make a best effort determination of whether it's
 * likely safe to enable the muxed SPROM pins.
 * 
 * On devices that do not use SPROM pin muxing, always returns true.
 * 
 * @param sc chipc driver state.
 */
static bool
chipc_should_enable_muxed_sprom(struct chipc_softc *sc)
{
	device_t	*devs;
	device_t	 hostb;
	device_t	 parent;
	int		 devcount;
	int		 error;
	bool		 result;

	/* Nothing to do? */
	if (!CHIPC_QUIRK(sc, MUX_SPROM))
		return (true);

	mtx_lock(&Giant);	/* for newbus */

	parent = device_get_parent(sc->dev);
	hostb = bhnd_bus_find_hostb_device(parent);

	if ((error = device_get_children(parent, &devs, &devcount))) {
		mtx_unlock(&Giant);
		return (false);
	}

	/* Reject any active devices other than ChipCommon, or the
	 * host bridge (if any). */
	result = true;
	for (int i = 0; i < devcount; i++) {
		if (devs[i] == hostb || devs[i] == sc->dev)
			continue;

		if (!device_is_attached(devs[i]))
			continue;

		if (device_is_suspended(devs[i]))
			continue;

		/* Active device; assume SPROM is busy */
		result = false;
		break;
	}

	free(devs, M_TEMP);
	mtx_unlock(&Giant);
	return (result);
}

static int
chipc_enable_sprom(device_t dev)
{
	struct chipc_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);
	CHIPC_LOCK(sc);

	/* Already enabled? */
	if (sc->sprom_refcnt >= 1) {
		sc->sprom_refcnt++;
		CHIPC_UNLOCK(sc);

		return (0);
	}

	switch (sc->caps.nvram_src) {
	case BHND_NVRAM_SRC_SPROM:
		error = chipc_enable_sprom_pins(sc);
		break;
	case BHND_NVRAM_SRC_OTP:
		error = chipc_enable_otp_power(sc);
		break;
	default:
		error = 0;
		break;
	}

	/* Bump the reference count */
	if (error == 0)
		sc->sprom_refcnt++;

	CHIPC_UNLOCK(sc);
	return (error);
}

static void
chipc_disable_sprom(device_t dev)
{
	struct chipc_softc	*sc;

	sc = device_get_softc(dev);
	CHIPC_LOCK(sc);

	/* Check reference count, skip disable if in-use. */
	KASSERT(sc->sprom_refcnt > 0, ("sprom refcnt overrelease"));
	sc->sprom_refcnt--;
	if (sc->sprom_refcnt > 0) {
		CHIPC_UNLOCK(sc);
		return;
	}

	switch (sc->caps.nvram_src) {
	case BHND_NVRAM_SRC_SPROM:
		chipc_disable_sprom_pins(sc);
		break;
	case BHND_NVRAM_SRC_OTP:
		chipc_disable_otp_power(sc);
		break;
	default:
		break;
	}


	CHIPC_UNLOCK(sc);
}

static int
chipc_enable_otp_power(struct chipc_softc *sc)
{
	// TODO: Enable OTP resource via PMU, and wait up to 100 usec for
	// OTPS_READY to be set in `optstatus`.
	return (0);
}

static void
chipc_disable_otp_power(struct chipc_softc *sc)
{
	// TODO: Disable OTP resource via PMU
}

/**
 * If required by this device, enable access to the SPROM.
 * 
 * @param sc chipc driver state.
 */
static int
chipc_enable_sprom_pins(struct chipc_softc *sc)
{
	uint32_t		 cctrl;

	CHIPC_LOCK_ASSERT(sc, MA_OWNED);
	KASSERT(sc->sprom_refcnt == 0, ("sprom pins already enabled"));

	/* Nothing to do? */
	if (!CHIPC_QUIRK(sc, MUX_SPROM))
		return (0);

	/* Check whether bus is busy */
	if (!chipc_should_enable_muxed_sprom(sc))
		return (EBUSY);

	cctrl = bhnd_bus_read_4(sc->core, CHIPC_CHIPCTRL);

	/* 4331 devices */
	if (CHIPC_QUIRK(sc, 4331_EXTPA_MUX_SPROM)) {
		cctrl &= ~CHIPC_CCTRL4331_EXTPA_EN;

		if (CHIPC_QUIRK(sc, 4331_GPIO2_5_MUX_SPROM))
			cctrl &= ~CHIPC_CCTRL4331_EXTPA_ON_GPIO2_5;

		if (CHIPC_QUIRK(sc, 4331_EXTPA2_MUX_SPROM))
			cctrl &= ~CHIPC_CCTRL4331_EXTPA_EN2;

		bhnd_bus_write_4(sc->core, CHIPC_CHIPCTRL, cctrl);
		return (0);
	}

	/* 4360 devices */
	if (CHIPC_QUIRK(sc, 4360_FEM_MUX_SPROM)) {
		/* Unimplemented */
	}

	/* Refuse to proceed on unsupported devices with muxed SPROM pins */
	device_printf(sc->dev, "muxed sprom lines on unrecognized device\n");
	return (ENXIO);
}

/**
 * If required by this device, revert any GPIO/pin configuration applied
 * to allow SPROM access.
 * 
 * @param sc chipc driver state.
 */
static void
chipc_disable_sprom_pins(struct chipc_softc *sc)
{
	uint32_t		 cctrl;

	/* Nothing to do? */
	if (!CHIPC_QUIRK(sc, MUX_SPROM))
		return;

	CHIPC_LOCK_ASSERT(sc, MA_OWNED);
	KASSERT(sc->sprom_refcnt == 0, ("sprom pins in use"));

	cctrl = bhnd_bus_read_4(sc->core, CHIPC_CHIPCTRL);

	/* 4331 devices */
	if (CHIPC_QUIRK(sc, 4331_EXTPA_MUX_SPROM)) {
		cctrl |= CHIPC_CCTRL4331_EXTPA_EN;

		if (CHIPC_QUIRK(sc, 4331_GPIO2_5_MUX_SPROM))
			cctrl |= CHIPC_CCTRL4331_EXTPA_ON_GPIO2_5;

		if (CHIPC_QUIRK(sc, 4331_EXTPA2_MUX_SPROM))
			cctrl |= CHIPC_CCTRL4331_EXTPA_EN2;

		bhnd_bus_write_4(sc->core, CHIPC_CHIPCTRL, cctrl);
		return;
	}

	/* 4360 devices */
	if (CHIPC_QUIRK(sc, 4360_FEM_MUX_SPROM)) {
		/* Unimplemented */
	}
}

static uint32_t
chipc_read_chipst(device_t dev)
{
	struct chipc_softc *sc = device_get_softc(dev);
	return (bhnd_bus_read_4(sc->core, CHIPC_CHIPST));
}

static void
chipc_write_chipctrl(device_t dev, uint32_t value, uint32_t mask)
{
	struct chipc_softc	*sc;
	uint32_t		 cctrl;

	sc = device_get_softc(dev);

	CHIPC_LOCK(sc);

	cctrl = bhnd_bus_read_4(sc->core, CHIPC_CHIPCTRL);
	cctrl = (cctrl & ~mask) | (value | mask);
	bhnd_bus_write_4(sc->core, CHIPC_CHIPCTRL, cctrl);

	CHIPC_UNLOCK(sc);
}

static struct chipc_caps *
chipc_get_caps(device_t dev)
{
	struct chipc_softc	*sc;

	sc = device_get_softc(dev);
	return (&sc->caps);
}

static device_method_t chipc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			chipc_probe),
	DEVMETHOD(device_attach,		chipc_attach),
	DEVMETHOD(device_detach,		chipc_detach),
	DEVMETHOD(device_suspend,		chipc_suspend),
	DEVMETHOD(device_resume,		chipc_resume),

	/* Bus interface */
	DEVMETHOD(bus_probe_nomatch,		chipc_probe_nomatch),
	DEVMETHOD(bus_print_child,		chipc_print_child),
	DEVMETHOD(bus_child_pnpinfo_str,	chipc_child_pnpinfo_str),
	DEVMETHOD(bus_child_location_str,	chipc_child_location_str),

	DEVMETHOD(bus_add_child,		chipc_add_child),
	DEVMETHOD(bus_child_deleted,		chipc_child_deleted),

	DEVMETHOD(bus_set_resource,		bus_generic_rl_set_resource),
	DEVMETHOD(bus_get_resource,		bus_generic_rl_get_resource),
	DEVMETHOD(bus_delete_resource,		bus_generic_rl_delete_resource),
	DEVMETHOD(bus_alloc_resource,		chipc_alloc_resource),
	DEVMETHOD(bus_release_resource,		chipc_release_resource),
	DEVMETHOD(bus_adjust_resource,		chipc_adjust_resource),
	DEVMETHOD(bus_activate_resource,	chipc_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	chipc_deactivate_resource),
	DEVMETHOD(bus_get_resource_list,	chipc_get_resource_list),

	DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,		bus_generic_teardown_intr),
	DEVMETHOD(bus_config_intr,		bus_generic_config_intr),
	DEVMETHOD(bus_bind_intr,		bus_generic_bind_intr),
	DEVMETHOD(bus_describe_intr,		bus_generic_describe_intr),

	/* BHND bus inteface */
	DEVMETHOD(bhnd_bus_activate_resource,	chipc_activate_bhnd_resource),

	/* ChipCommon interface */
	DEVMETHOD(bhnd_chipc_read_chipst,	chipc_read_chipst),
	DEVMETHOD(bhnd_chipc_write_chipctrl,	chipc_write_chipctrl),
	DEVMETHOD(bhnd_chipc_enable_sprom,	chipc_enable_sprom),
	DEVMETHOD(bhnd_chipc_disable_sprom,	chipc_disable_sprom),
	DEVMETHOD(bhnd_chipc_get_caps,		chipc_get_caps),

	DEVMETHOD_END
};

DEFINE_CLASS_0(bhnd_chipc, bhnd_chipc_driver, chipc_methods, sizeof(struct chipc_softc));
EARLY_DRIVER_MODULE(bhnd_chipc, bhnd, bhnd_chipc_driver, bhnd_chipc_devclass, 0, 0,
    BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
MODULE_DEPEND(bhnd_chipc, bhnd, 1, 1, 1);
MODULE_VERSION(bhnd_chipc, 1);

/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * SoC misc configuration and indentification driver.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/nvidia/tegra_efuse.h>

#define	PMC_STRAPPING_OPT_A	0  	/* 0x464 */

#define	PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT	4
#define	PMC_STRAPPING_OPT_A_RAM_CODE_MASK_LONG	\
	(0xf << PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT)
#define	PMC_STRAPPING_OPT_A_RAM_CODE_MASK_SHORT	\
	(0x3 << PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT)


#define	ABP_RD4(_sc, _r)	bus_read_4((_sc)->abp_misc_res, (_r))
#define	STR_RD4(_sc, _r)	bus_read_4((_sc)->strap_opt_res, (_r))

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-apbmisc",	1},
	{NULL,				0}
};

struct tegra_abpmisc_softc {
	device_t		dev;

	struct resource		*abp_misc_res;
	struct resource		*strap_opt_res;
};

static struct tegra_abpmisc_softc *dev_sc;

static void
tegra_abpmisc_read_revision(struct tegra_abpmisc_softc *sc)
{
	uint32_t id, chip_id, minor_rev;
	int rev;

	id = ABP_RD4(sc, 4);
	chip_id = (id >> 8) & 0xff;
	minor_rev = (id >> 16) & 0xf;

	switch (minor_rev) {
	case 1:
		rev = TEGRA_REVISION_A01;
		break;
	case 2:
		rev = TEGRA_REVISION_A02;
		break;
	case 3:
		rev = TEGRA_REVISION_A03;
		break;
	case 4:
		rev = TEGRA_REVISION_A04;
		break;
	default:
		rev = TEGRA_REVISION_UNKNOWN;
	}

	tegra_sku_info.chip_id = chip_id;
	tegra_sku_info.revision = rev;
}

static int
tegra_abpmisc_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static int
tegra_abpmisc_attach(device_t dev)
{
	int rid;
	struct tegra_abpmisc_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	rid = 0;
	sc->abp_misc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->abp_misc_res == NULL) {
		device_printf(dev, "Cannot map ABP misc registers.\n");
		goto fail;
	}

	rid = 1;
	sc->strap_opt_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->strap_opt_res == NULL) {
		device_printf(dev, "Cannot map strapping options registers.\n");
		goto fail;
	}

	tegra_abpmisc_read_revision(sc);

	/* XXX - Hack - address collision with pinmux. */
	if (sc->abp_misc_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->abp_misc_res);
		sc->abp_misc_res = NULL;
	}

	dev_sc = sc;
	return (bus_generic_attach(dev));

fail:
	if (sc->abp_misc_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->abp_misc_res);
	if (sc->strap_opt_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 1, sc->strap_opt_res);

	return (ENXIO);
}

static int
tegra_abpmisc_detach(device_t dev)
{
	struct tegra_abpmisc_softc *sc;

	sc = device_get_softc(dev);
	if (sc->abp_misc_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->abp_misc_res);
	if (sc->strap_opt_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 1, sc->strap_opt_res);
	return (bus_generic_detach(dev));
}

static device_method_t tegra_abpmisc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tegra_abpmisc_probe),
	DEVMETHOD(device_attach,	tegra_abpmisc_attach),
	DEVMETHOD(device_detach,	tegra_abpmisc_detach),

	DEVMETHOD_END
};

static devclass_t tegra_abpmisc_devclass;
static DEFINE_CLASS_0(abpmisc, tegra_abpmisc_driver, tegra_abpmisc_methods,
    sizeof(struct tegra_abpmisc_softc));
EARLY_DRIVER_MODULE(tegra_abpmisc, simplebus, tegra_abpmisc_driver,
    tegra_abpmisc_devclass, NULL, NULL, BUS_PASS_TIMER);

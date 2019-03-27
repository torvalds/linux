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
 * SDHCI driver glue for NVIDIA Tegra family
 *
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/gpio/gpiobusvar.h>
#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcbrvar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/sdhci/sdhci.h>
#include <dev/sdhci/sdhci_fdt_gpio.h>

#include "sdhci_if.h"

#include "opt_mmccam.h"

/* Tegra SDHOST controller vendor register definitions */
#define	SDMMC_VENDOR_CLOCK_CNTRL		0x100
#define	 VENDOR_CLOCK_CNTRL_CLK_SHIFT			8
#define	 VENDOR_CLOCK_CNTRL_CLK_MASK			0xFF
#define	SDMMC_VENDOR_SYS_SW_CNTRL		0x104
#define	SDMMC_VENDOR_CAP_OVERRIDES		0x10C
#define	SDMMC_VENDOR_BOOT_CNTRL			0x110
#define	SDMMC_VENDOR_BOOT_ACK_TIMEOUT		0x114
#define	SDMMC_VENDOR_BOOT_DAT_TIMEOUT		0x118
#define	SDMMC_VENDOR_DEBOUNCE_COUNT		0x11C
#define	SDMMC_VENDOR_MISC_CNTRL			0x120
#define	 VENDOR_MISC_CTRL_ENABLE_SDR104			0x8
#define	 VENDOR_MISC_CTRL_ENABLE_SDR50			0x10
#define	 VENDOR_MISC_CTRL_ENABLE_SDHCI_SPEC_300		0x20
#define	 VENDOR_MISC_CTRL_ENABLE_DDR50			0x200
#define	SDMMC_MAX_CURRENT_OVERRIDE		0x124
#define	SDMMC_MAX_CURRENT_OVERRIDE_HI		0x128
#define	SDMMC_VENDOR_CLK_GATE_HYSTERESIS_COUNT 	0x1D0
#define	SDMMC_VENDOR_PHWRESET_VAL0		0x1D4
#define	SDMMC_VENDOR_PHWRESET_VAL1		0x1D8
#define	SDMMC_VENDOR_PHWRESET_VAL2		0x1DC
#define	SDMMC_SDMEMCOMPPADCTRL_0		0x1E0
#define	SDMMC_AUTO_CAL_CONFIG			0x1E4
#define	SDMMC_AUTO_CAL_INTERVAL			0x1E8
#define	SDMMC_AUTO_CAL_STATUS			0x1EC
#define	SDMMC_SDMMC_MCCIF_FIFOCTRL		0x1F4
#define	SDMMC_TIMEOUT_WCOAL_SDMMC		0x1F8

/* Compatible devices. */
static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-sdhci",	1},
	{NULL,				0},
};

struct tegra_sdhci_softc {
	device_t		dev;
	struct resource *	mem_res;
	struct resource *	irq_res;
	void *			intr_cookie;
	u_int			quirks;	/* Chip specific quirks */
	u_int			caps;	/* If we override SDHCI_CAPABILITIES */
	uint32_t		max_clk; /* Max possible freq */
	clk_t			clk;
	hwreset_t 		reset;
	gpio_pin_t		gpio_power;
	struct sdhci_fdt_gpio	*gpio;

	int			force_card_present;
	struct sdhci_slot	slot;

};

static inline uint32_t
RD4(struct tegra_sdhci_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off));
}

static uint8_t
tegra_sdhci_read_1(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct tegra_sdhci_softc *sc;

	sc = device_get_softc(dev);
	return (bus_read_1(sc->mem_res, off));
}

static uint16_t
tegra_sdhci_read_2(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct tegra_sdhci_softc *sc;

	sc = device_get_softc(dev);
	return (bus_read_2(sc->mem_res, off));
}

static uint32_t
tegra_sdhci_read_4(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct tegra_sdhci_softc *sc;
	uint32_t val32;

	sc = device_get_softc(dev);
	val32 = bus_read_4(sc->mem_res, off);
	/* Force the card-present state if necessary. */
	if (off == SDHCI_PRESENT_STATE && sc->force_card_present)
		val32 |= SDHCI_CARD_PRESENT;
	return (val32);
}

static void
tegra_sdhci_read_multi_4(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint32_t *data, bus_size_t count)
{
	struct tegra_sdhci_softc *sc;

	sc = device_get_softc(dev);
	bus_read_multi_4(sc->mem_res, off, data, count);
}

static void
tegra_sdhci_write_1(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint8_t val)
{
	struct tegra_sdhci_softc *sc;

	sc = device_get_softc(dev);
	bus_write_1(sc->mem_res, off, val);
}

static void
tegra_sdhci_write_2(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint16_t val)
{
	struct tegra_sdhci_softc *sc;

	sc = device_get_softc(dev);
	bus_write_2(sc->mem_res, off, val);
}

static void
tegra_sdhci_write_4(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint32_t val)
{
	struct tegra_sdhci_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->mem_res, off, val);
}

static void
tegra_sdhci_write_multi_4(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint32_t *data, bus_size_t count)
{
	struct tegra_sdhci_softc *sc;

	sc = device_get_softc(dev);
	bus_write_multi_4(sc->mem_res, off, data, count);
}

static void
tegra_sdhci_intr(void *arg)
{
	struct tegra_sdhci_softc *sc = arg;

	sdhci_generic_intr(&sc->slot);
	RD4(sc, SDHCI_INT_STATUS);
}

static int
tegra_sdhci_get_ro(device_t brdev, device_t reqdev)
{
	struct tegra_sdhci_softc *sc = device_get_softc(brdev);

	return (sdhci_fdt_gpio_get_readonly(sc->gpio));
}

static bool
tegra_sdhci_get_card_present(device_t dev, struct sdhci_slot *slot)
{
	struct tegra_sdhci_softc *sc = device_get_softc(dev);

	return (sdhci_fdt_gpio_get_present(sc->gpio));
}

static int
tegra_sdhci_probe(device_t dev)
{
	struct tegra_sdhci_softc *sc;
	phandle_t node;
	pcell_t cid;
	const struct ofw_compat_data *cd;

	sc = device_get_softc(dev);
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "nvidia,tegra124-sdhci")) {
		device_set_desc(dev, "Tegra SDHCI controller");
	} else
		return (ENXIO);
	cd = ofw_bus_search_compatible(dev, compat_data);
	if (cd->ocd_data == 0)
		return (ENXIO);

	node = ofw_bus_get_node(dev);

	/* Allow dts to patch quirks, slots, and max-frequency. */
	if ((OF_getencprop(node, "quirks", &cid, sizeof(cid))) > 0)
		sc->quirks = cid;
	if ((OF_getencprop(node, "max-frequency", &cid, sizeof(cid))) > 0)
		sc->max_clk = cid;

	return (BUS_PROBE_DEFAULT);
}

static int
tegra_sdhci_attach(device_t dev)
{
	struct tegra_sdhci_softc *sc;
	int rid, rv;
	uint64_t freq;
	phandle_t node, prop;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->mem_res) {
		device_printf(dev, "cannot allocate memory window\n");
		rv = ENXIO;
		goto fail;
	}

	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (!sc->irq_res) {
		device_printf(dev, "cannot allocate interrupt\n");
		rv = ENXIO;
		goto fail;
	}

	if (bus_setup_intr(dev, sc->irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, tegra_sdhci_intr, sc, &sc->intr_cookie)) {
		device_printf(dev, "cannot setup interrupt handler\n");
		rv = ENXIO;
		goto fail;
	}

	rv = hwreset_get_by_ofw_name(sc->dev, 0, "sdhci", &sc->reset);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'sdhci' reset\n");
		goto fail;
	}
	rv = hwreset_deassert(sc->reset);
	if (rv != 0) {
		device_printf(dev, "Cannot unreset 'sdhci' reset\n");
		goto fail;
	}

	gpio_pin_get_by_ofw_property(sc->dev, node, "power-gpios", &sc->gpio_power);

	rv = clk_get_by_ofw_index(dev, 0, 0, &sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot get clock\n");
		goto fail;
	}
	rv = clk_enable(sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot enable clock\n");
		goto fail;
	}
	rv = clk_set_freq(sc->clk, 48000000, CLK_SET_ROUND_DOWN);
	if (rv != 0) {
		device_printf(dev, "Cannot set clock\n");
	}
	rv = clk_get_freq(sc->clk, &freq);
	if (rv != 0) {
		device_printf(dev, "Cannot get clock frequency\n");
		goto fail;
	}
	if (bootverbose)
		device_printf(dev, " Base MMC clock: %lld\n", freq);

	/* Fill slot information. */
	sc->max_clk = (int)freq;
	sc->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
	    SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
	    SDHCI_QUIRK_MISSING_CAPS;

	/* Limit real slot capabilities. */
	sc->caps = RD4(sc, SDHCI_CAPABILITIES);
	if (OF_getencprop(node, "bus-width", &prop, sizeof(prop)) > 0) {
		sc->caps &= ~(MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA);
		switch (prop) {
		case 8:
			sc->caps |= MMC_CAP_8_BIT_DATA;
			/* FALLTHROUGH */
		case 4:
			sc->caps |= MMC_CAP_4_BIT_DATA;
			break;
		case 1:
			break;
		default:
			device_printf(dev, "Bad bus-width value %u\n", prop);
			break;
		}
	}
	if (OF_hasprop(node, "non-removable"))
		sc->force_card_present = 1;
	/*
	 * Clear clock field, so SDHCI driver uses supplied frequency.
	 * in sc->slot.max_clk
	 */
	sc->caps &= ~SDHCI_CLOCK_V3_BASE_MASK;

	sc->slot.quirks = sc->quirks;
	sc->slot.max_clk = sc->max_clk;
	sc->slot.caps = sc->caps;

	rv = sdhci_init_slot(dev, &sc->slot, 0);
	if (rv != 0) {
		goto fail;
	}

	sc->gpio = sdhci_fdt_gpio_setup(sc->dev, &sc->slot);

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	sdhci_start_slot(&sc->slot);

	return (0);

fail:
	if (sc->gpio != NULL)
		sdhci_fdt_gpio_teardown(sc->gpio);
	if (sc->intr_cookie != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	if (sc->gpio_power != NULL)
		gpio_pin_release(sc->gpio_power);
	if (sc->clk != NULL)
		clk_release(sc->clk);
	if (sc->reset != NULL)
		hwreset_release(sc->reset);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	return (rv);
}

static int
tegra_sdhci_detach(device_t dev)
{
	struct tegra_sdhci_softc *sc = device_get_softc(dev);
	struct sdhci_slot *slot = &sc->slot;

	bus_generic_detach(dev);
	sdhci_fdt_gpio_teardown(sc->gpio);
	clk_release(sc->clk);
	bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(sc->irq_res),
			     sc->irq_res);

	sdhci_cleanup_slot(slot);
	bus_release_resource(dev, SYS_RES_MEMORY,
			     rman_get_rid(sc->mem_res),
			     sc->mem_res);
	return (0);
}

static device_method_t tegra_sdhci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tegra_sdhci_probe),
	DEVMETHOD(device_attach,	tegra_sdhci_attach),
	DEVMETHOD(device_detach,	tegra_sdhci_detach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	sdhci_generic_read_ivar),
	DEVMETHOD(bus_write_ivar,	sdhci_generic_write_ivar),

	/* MMC bridge interface */
	DEVMETHOD(mmcbr_update_ios,	sdhci_generic_update_ios),
	DEVMETHOD(mmcbr_request,	sdhci_generic_request),
	DEVMETHOD(mmcbr_get_ro,		tegra_sdhci_get_ro),
	DEVMETHOD(mmcbr_acquire_host,	sdhci_generic_acquire_host),
	DEVMETHOD(mmcbr_release_host,	sdhci_generic_release_host),

	/* SDHCI registers accessors */
	DEVMETHOD(sdhci_read_1,		tegra_sdhci_read_1),
	DEVMETHOD(sdhci_read_2,		tegra_sdhci_read_2),
	DEVMETHOD(sdhci_read_4,		tegra_sdhci_read_4),
	DEVMETHOD(sdhci_read_multi_4,	tegra_sdhci_read_multi_4),
	DEVMETHOD(sdhci_write_1,	tegra_sdhci_write_1),
	DEVMETHOD(sdhci_write_2,	tegra_sdhci_write_2),
	DEVMETHOD(sdhci_write_4,	tegra_sdhci_write_4),
	DEVMETHOD(sdhci_write_multi_4,	tegra_sdhci_write_multi_4),
	DEVMETHOD(sdhci_get_card_present, tegra_sdhci_get_card_present),

	DEVMETHOD_END
};

static devclass_t tegra_sdhci_devclass;
static DEFINE_CLASS_0(sdhci, tegra_sdhci_driver, tegra_sdhci_methods,
    sizeof(struct tegra_sdhci_softc));
DRIVER_MODULE(sdhci_tegra, simplebus, tegra_sdhci_driver, tegra_sdhci_devclass,
    NULL, NULL);
SDHCI_DEPEND(sdhci_tegra);
#ifndef MMCCAM
MMC_DECLARE_BRIDGE(sdhci);
#endif

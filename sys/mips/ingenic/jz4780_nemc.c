/*-
 * Copyright 2015 Alexander Kabaev <kan@FreeBSD.org>
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

/*
 * Ingenic JZ4780 NAND and External Memory Controller (NEMC) driver.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>

#include <dev/fdt/fdt_common.h>
#include <dev/fdt/simplebus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/ingenic/jz4780_regs.h>

struct jz4780_nemc_devinfo {
	struct simplebus_devinfo sinfo;
	uint32_t                 bank;
};

struct jz4780_nemc_softc {
	struct simplebus_softc	simplebus_sc;
	device_t		dev;
	struct resource		*res[1];
	uint32_t		banks;
	uint32_t		clock_tick_psecs;
	clk_t			clk;
};

static struct resource_spec jz4780_nemc_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ -1, 0 }
};

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)

static int jz4780_nemc_probe(device_t dev);
static int jz4780_nemc_attach(device_t dev);
static int jz4780_nemc_detach(device_t dev);

static int
jz4780_nemc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-nemc"))
		return (ENXIO);

	device_set_desc(dev, "Ingenic JZ4780 NEMC");

	return (BUS_PROBE_DEFAULT);
}

#define JZ4780_NEMC_NS_TO_TICKS(sc, val) howmany((val) * 1000,  (sc)->clock_tick_psecs)

/* Use table from JZ4780 programmers manual to convert ticks to tBP/tAW register values */
static const uint8_t ticks_to_tBP_tAW[32] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,  /* 1:1 mapping */
	11, 11,                            /* 12 cycles */
	12, 12, 12,                        /* 15 cycles */
	13, 13, 13, 13, 13,                /* 20 cycles */
	14, 14, 14, 14, 14,                /* 25 cycles */
	15, 15, 15, 15, 15, 15             /* 31 cycles */
};

static int
jz4780_nemc_configure_bank(struct jz4780_nemc_softc *sc,
        device_t dev, u_int bank)
{
	uint32_t smcr, cycles;
	phandle_t node;
	pcell_t   val;

	/* Check if bank is configured already */
	if (sc->banks & (1 << bank))
		return 0;

	smcr = CSR_READ_4(sc, JZ_NEMC_SMCR(bank));

	smcr &= ~JZ_NEMC_SMCR_SMT_MASK;
	smcr |= JZ_NEMC_SMCR_SMT_NORMAL << JZ_NEMC_SMCR_SMT_SHIFT;

	node = ofw_bus_get_node(dev);
	if (OF_getencprop(node, "ingenic,nemc-tAS", &val, sizeof(val)) > 0) {
		cycles = JZ4780_NEMC_NS_TO_TICKS(sc, val);
		if (cycles > 15) {
			device_printf(sc->dev,
			    "invalid value of %s %u (%u cycles), maximum %u cycles supported\n",
			    "ingenic,nemc-tAS", val, cycles, 15);
			return -1;
		}
		smcr &= ~JZ_NEMC_SMCR_TAS_MASK;
		smcr |= cycles << JZ_NEMC_SMCR_TAS_SHIFT;
	}

	if (OF_getencprop(node, "ingenic,nemc-tAH", &val, sizeof(val)) > 0) {
		cycles = JZ4780_NEMC_NS_TO_TICKS(sc, val);
		if (cycles > 15) {
			device_printf(sc->dev,
			    "invalid value of %s %u (%u cycles), maximum %u cycles supported\n",
			    "ingenic,nemc-tAH", val, cycles, 15);
			return -1;
		}
		smcr &= ~JZ_NEMC_SMCR_TAH_MASK;
		smcr |= cycles << JZ_NEMC_SMCR_TAH_SHIFT;
	}

	if (OF_getencprop(node, "ingenic,nemc-tBP", &val, sizeof(val)) > 0) {
		cycles = JZ4780_NEMC_NS_TO_TICKS(sc, val);
		if (cycles > 31) {
			device_printf(sc->dev,
			    "invalid value of %s %u (%u cycles), maximum %u cycles supported\n",
			    "ingenic,nemc-tBP", val, cycles, 15);
			return -1;
		}
		smcr &= ~JZ_NEMC_SMCR_TBP_MASK;
		smcr |= ticks_to_tBP_tAW[cycles] << JZ_NEMC_SMCR_TBP_SHIFT;
	}

	if (OF_getencprop(node, "ingenic,nemc-tAW", &val, sizeof(val)) > 0) {
		cycles = JZ4780_NEMC_NS_TO_TICKS(sc, val);
		if (cycles > 31) {
			device_printf(sc->dev,
			    "invalid value of %s %u (%u cycles), maximum %u cycles supported\n",
			    "ingenic,nemc-tAW", val, cycles, 15);
			return -1;
		}
		smcr &= ~JZ_NEMC_SMCR_TAW_MASK;
		smcr |= ticks_to_tBP_tAW[cycles] << JZ_NEMC_SMCR_TAW_SHIFT;
	}

	if (OF_getencprop(node, "ingenic,nemc-tSTRV", &val, sizeof(val)) > 0) {
		cycles = JZ4780_NEMC_NS_TO_TICKS(sc, val);
		if (cycles > 63) {
			device_printf(sc->dev,
			    "invalid value of %s %u (%u cycles), maximum %u cycles supported\n",
			    "ingenic,nemc-tSTRV", val, cycles, 15);
			return -1;
		}
		smcr &= ~JZ_NEMC_SMCR_STRV_MASK;
		smcr |= cycles << JZ_NEMC_SMCR_STRV_SHIFT;
	}
	CSR_WRITE_4(sc, JZ_NEMC_SMCR(bank), smcr);
	sc->banks |= (1 << bank);
	return 0;
}

/* Wholesale copy of simplebus routine */
static int
jz4780_nemc_fill_ranges(phandle_t node, struct simplebus_softc *sc)
{
	int host_address_cells;
	cell_t *base_ranges;
	ssize_t nbase_ranges;
	int err;
	int i, j, k;

	err = OF_searchencprop(OF_parent(node), "#address-cells",
	    &host_address_cells, sizeof(host_address_cells));
	if (err <= 0)
		return (-1);

	nbase_ranges = OF_getproplen(node, "ranges");
	if (nbase_ranges < 0)
		return (-1);
	sc->nranges = nbase_ranges / sizeof(cell_t) /
	    (sc->acells + host_address_cells + sc->scells);
	if (sc->nranges == 0)
		return (0);

	sc->ranges = malloc(sc->nranges * sizeof(sc->ranges[0]),
	    M_DEVBUF, M_WAITOK);
	base_ranges = malloc(nbase_ranges, M_DEVBUF, M_WAITOK);
	OF_getencprop(node, "ranges", base_ranges, nbase_ranges);

	for (i = 0, j = 0; i < sc->nranges; i++) {
		sc->ranges[i].bus = 0;
		for (k = 0; k < sc->acells; k++) {
			sc->ranges[i].bus <<= 32;
			sc->ranges[i].bus |= base_ranges[j++];
		}
		sc->ranges[i].host = 0;
		for (k = 0; k < host_address_cells; k++) {
			sc->ranges[i].host <<= 32;
			sc->ranges[i].host |= base_ranges[j++];
		}
		sc->ranges[i].size = 0;
		for (k = 0; k < sc->scells; k++) {
			sc->ranges[i].size <<= 32;
			sc->ranges[i].size |= base_ranges[j++];
		}
	}

	free(base_ranges, M_DEVBUF);
	return (sc->nranges);
}

static int
jz4780_nemc_attach(device_t dev)
{
	struct jz4780_nemc_softc *sc = device_get_softc(dev);
	phandle_t node;
	uint64_t freq;

	sc->dev = dev;

	if (bus_alloc_resources(dev, jz4780_nemc_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);

	/* Initialize simplebus and enumerate resources */
	simplebus_init(dev, node);

	if (jz4780_nemc_fill_ranges(node, &sc->simplebus_sc) < 0)
		goto error;

	/* Figure our underlying clock rate. */
	if (clk_get_by_ofw_index(dev, 0, 0, &sc->clk) != 0) {
		device_printf(dev, "could not lookup device clock\n");
		goto error;
	}
	if (clk_enable(sc->clk) != 0) {
		device_printf(dev, "could not enable device clock\n");
		goto error;
	}
	if (clk_get_freq(sc->clk, &freq) != 0) {
		device_printf(dev, "could not determine clock speed\n");
		goto error;
	}

	/* Convert clock frequency to picoseconds-per-tick value. */
	sc->clock_tick_psecs = (uint32_t)(1000000000000ULL / freq);

	/*
	 * Allow devices to identify.
	 */
	bus_generic_probe(dev);

	/*
	 * Now walk the tree and attach top level devices
	 */
	for (node = OF_child(node); node > 0; node = OF_peer(node))
		simplebus_add_device(dev, node, 0, NULL, -1, NULL);

	return (bus_generic_attach(dev));
error:
	jz4780_nemc_detach(dev);
	return (ENXIO);
}

static int
jz4780_nemc_detach(device_t dev)
{
	struct jz4780_nemc_softc *sc = device_get_softc(dev);

	bus_generic_detach(dev);
	if (sc->clk != NULL)
		clk_release(sc->clk);
	bus_release_resources(dev, jz4780_nemc_spec, sc->res);
	return (0);
}

static int
jz4780_nemc_decode_bank(struct simplebus_softc *sc, struct resource *r,
    u_int *bank)
{
	rman_res_t start, end;
	int i;

	start = rman_get_start(r);
	end = rman_get_end(r);

	/* Remap through ranges property */
	for (i = 0; i < sc->nranges; i++) {
		if (start >= sc->ranges[i].host && end <
		    sc->ranges[i].host + sc->ranges[i].size) {
			*bank = (sc->ranges[i].bus >> 32);
			return (0);
		}
	}
	return (1);
}

static int
jz4780_nemc_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	struct jz4780_nemc_softc *sc;
	u_int bank;
	int err;

	if (type == SYS_RES_MEMORY) {
		sc = device_get_softc(bus);

		/* Figure out on what bank device is residing */
		err = jz4780_nemc_decode_bank(&sc->simplebus_sc, r, &bank);
		if (err == 0) {
			/* Attempt to configure the bank if not done already */
			err = jz4780_nemc_configure_bank(sc, child, bank);
			if (err != 0)
				return (err);
		}
	}

	/* Call default implementation to finish the work */
	return (bus_generic_activate_resource(bus, child,
		type, rid, r));
}

static device_method_t jz4780_nemc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_nemc_probe),
	DEVMETHOD(device_attach,	jz4780_nemc_attach),
	DEVMETHOD(device_detach,	jz4780_nemc_detach),

	/* Overrides to configure bank on resource activation */
	DEVMETHOD(bus_activate_resource, jz4780_nemc_activate_resource),

	DEVMETHOD_END
};

static devclass_t jz4780_nemc_devclass;
DEFINE_CLASS_1(nemc, jz4780_nemc_driver, jz4780_nemc_methods,
        sizeof(struct jz4780_nemc_softc), simplebus_driver);
DRIVER_MODULE(jz4780_nemc, simplebus, jz4780_nemc_driver,
    jz4780_nemc_devclass, 0, 0);

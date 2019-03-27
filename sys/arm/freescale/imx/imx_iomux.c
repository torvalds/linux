/*-
 * Copyright (c) 2014 Ian Lepore
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

/*
 * Pin mux and pad control driver for imx5 and imx6.
 *
 * This driver implements the fdt_pinctrl interface for configuring the gpio and
 * peripheral pins based on fdt configuration data.
 *
 * When the driver attaches, it walks the entire fdt tree and automatically
 * configures the pins for each device which has a pinctrl-0 property and whose
 * status is "okay".  In addition it implements the fdt_pinctrl_configure()
 * method which any other driver can call at any time to reconfigure its pins.
 *
 * The nature of the fsl,pins property in fdt data makes this driver's job very
 * easy.  Instead of representing each pin and pad configuration using symbolic
 * properties such as pullup-enable="true" and so on, the data simply contains
 * the addresses of the registers that control the pins, and the raw values to
 * store in those registers.
 *
 * The imx5 and imx6 SoCs also have a small number of "general purpose
 * registers" in the iomuxc device which are used to control an assortment
 * of completely unrelated aspects of SoC behavior.  This driver provides other
 * drivers with direct access to those registers via simple accessor functions.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/fdt/fdt_pinctrl.h>

#include <arm/freescale/imx/imx_iomuxvar.h>
#include <arm/freescale/imx/imx_machdep.h>

struct iomux_softc {
	device_t	dev;
	struct resource	*mem_res;
	u_int		last_gpregaddr;
};

static struct iomux_softc *iomux_sc;

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6dl-iomuxc",	true},
	{"fsl,imx6q-iomuxc",	true},
	{"fsl,imx6sl-iomuxc",	true},
	{"fsl,imx6ul-iomuxc",	true},
	{"fsl,imx6sx-iomuxc",	true},
	{"fsl,imx53-iomuxc",	true},
	{"fsl,imx51-iomuxc",	true},
	{NULL,			false},
};

/*
 * Each tuple in an fsl,pins property contains these fields.
 */
struct pincfg {
	uint32_t mux_reg;
	uint32_t padconf_reg;
	uint32_t input_reg;
	uint32_t mux_val;
	uint32_t input_val;
	uint32_t padconf_val;
};

#define	PADCONF_NONE	(1U << 31)	/* Do not configure pad. */
#define	PADCONF_SION	(1U << 30)	/* Force SION bit in mux register. */
#define	PADMUX_SION	(1U <<  4)	/* The SION bit in the mux register. */

static inline uint32_t
RD4(struct iomux_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off));
}

static inline void
WR4(struct iomux_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->mem_res, off, val);
}

static void
iomux_configure_input(struct iomux_softc *sc, uint32_t reg, uint32_t val)
{
	u_int select, mask, shift, width;

	/* If register and value are zero, there is nothing to configure. */
	if (reg == 0 && val == 0)
		return;

	/*
	 * If the config value has 0xff in the high byte it is encoded:
	 * 	31     23      15      7        0
	 *      | 0xff | shift | width | select |
	 * We need to mask out the old select value and OR in the new, using a
	 * mask of the given width and shifting the values up by shift.
	 */
	if ((val & 0xff000000) == 0xff000000) {
		select = val & 0x000000ff;
		width = (val & 0x0000ff00) >> 8;
		shift = (val & 0x00ff0000) >> 16;
		mask  = ((1u << width) - 1) << shift;
		val = (RD4(sc, reg) & ~mask) | (select << shift);
	}
	WR4(sc, reg, val);
}

static int
iomux_configure_pins(device_t dev, phandle_t cfgxref)
{
	struct iomux_softc *sc;
	struct pincfg *cfgtuples, *cfg;
	phandle_t cfgnode;
	int i, ntuples;
	uint32_t sion;

	sc = device_get_softc(dev);
	cfgnode = OF_node_from_xref(cfgxref);
	ntuples = OF_getencprop_alloc_multi(cfgnode, "fsl,pins",
	    sizeof(*cfgtuples), (void **)&cfgtuples);
	if (ntuples < 0)
		return (ENOENT);
	if (ntuples == 0)
		return (0); /* Empty property is not an error. */
	for (i = 0, cfg = cfgtuples; i < ntuples; i++, cfg++) {
		sion = (cfg->padconf_val & PADCONF_SION) ? PADMUX_SION : 0;
		WR4(sc, cfg->mux_reg, cfg->mux_val | sion);
		iomux_configure_input(sc, cfg->input_reg, cfg->input_val);
		if ((cfg->padconf_val & PADCONF_NONE) == 0)
			WR4(sc, cfg->padconf_reg, cfg->padconf_val);
		if (bootverbose) {
			char name[32]; 
			OF_getprop(cfgnode, "name", &name, sizeof(name));
			printf("%16s: muxreg 0x%04x muxval 0x%02x "
			    "inpreg 0x%04x inpval 0x%02x "
			    "padreg 0x%04x padval 0x%08x\n",
			    name, cfg->mux_reg, cfg->mux_val | sion,
			    cfg->input_reg, cfg->input_val,
			    cfg->padconf_reg, cfg->padconf_val);
		}
	}
	OF_prop_free(cfgtuples);
	return (0);
}

static int
iomux_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Freescale i.MX pin configuration");
	return (BUS_PROBE_DEFAULT);
}

static int
iomux_detach(device_t dev)
{

        /* This device is always present. */
	return (EBUSY);
}

static int
iomux_attach(device_t dev)
{
	struct iomux_softc * sc;
	int rid;

	sc = device_get_softc(dev);
	sc->dev = dev;

	switch (imx_soc_type()) {
	case IMXSOC_51:
		sc->last_gpregaddr = 1 * sizeof(uint32_t);
		break;
	case IMXSOC_53:
		sc->last_gpregaddr = 2 * sizeof(uint32_t);
		break;
	case IMXSOC_6DL:
	case IMXSOC_6S:
	case IMXSOC_6SL:
	case IMXSOC_6Q:
		sc->last_gpregaddr = 13 * sizeof(uint32_t);
		break;
	case IMXSOC_6UL:
		sc->last_gpregaddr = 14 * sizeof(uint32_t);
		break;
	default:
		device_printf(dev, "Unknown SoC type\n");
		return (ENXIO);
	}

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		return (ENXIO);
	}

	iomux_sc = sc;

	/*
	 * Register as a pinctrl device, and call the convenience function that
	 * walks the entire device tree invoking FDT_PINCTRL_CONFIGURE() on any
	 * pinctrl-0 property cells whose xref phandle refers to a configuration
	 * that is a child node of our node in the tree.
	 *
	 * The pinctrl bindings documentation specifically mentions that the
	 * pinctrl device itself may have a pinctrl-0 property which contains
	 * static configuration to be applied at device init time.  The tree
	 * walk will automatically handle this for us when it passes through our
	 * node in the tree.
	 */
	fdt_pinctrl_register(dev, "fsl,pins");
	fdt_pinctrl_configure_tree(dev);

	return (0);
}

uint32_t
imx_iomux_gpr_get(u_int regaddr)
{
	struct iomux_softc * sc;

	sc = iomux_sc;
	KASSERT(sc != NULL, ("%s called before attach", __FUNCTION__));
	KASSERT(regaddr >= 0 && regaddr <= sc->last_gpregaddr, 
	    ("%s bad regaddr %u, max %u", __FUNCTION__, regaddr,
	    sc->last_gpregaddr));

	return (RD4(iomux_sc, regaddr));
}

void
imx_iomux_gpr_set(u_int regaddr, uint32_t val)
{
	struct iomux_softc * sc;

	sc = iomux_sc;
	KASSERT(sc != NULL, ("%s called before attach", __FUNCTION__));
	KASSERT(regaddr >= 0 && regaddr <= sc->last_gpregaddr, 
	    ("%s bad regaddr %u, max %u", __FUNCTION__, regaddr,
	    sc->last_gpregaddr));

	WR4(iomux_sc, regaddr, val);
}

void
imx_iomux_gpr_set_masked(u_int regaddr, uint32_t clrbits, uint32_t setbits)
{
	struct iomux_softc * sc;
	uint32_t val;

	sc = iomux_sc;
	KASSERT(sc != NULL, ("%s called before attach", __FUNCTION__));
	KASSERT(regaddr >= 0 && regaddr <= sc->last_gpregaddr, 
	    ("%s bad regaddr %u, max %u", __FUNCTION__, regaddr,
	    sc->last_gpregaddr));

	val = RD4(iomux_sc, regaddr * 4);
	val = (val & ~clrbits) | setbits;
	WR4(iomux_sc, regaddr, val);
}

static device_method_t imx_iomux_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         iomux_probe),
	DEVMETHOD(device_attach,        iomux_attach),
	DEVMETHOD(device_detach,        iomux_detach),

        /* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure,iomux_configure_pins),

	DEVMETHOD_END
};

static driver_t imx_iomux_driver = {
	"imx_iomux",
	imx_iomux_methods,
	sizeof(struct iomux_softc),
};

static devclass_t imx_iomux_devclass;

EARLY_DRIVER_MODULE(imx_iomux, simplebus, imx_iomux_driver, 
    imx_iomux_devclass, 0, 0, BUS_PASS_CPU + BUS_PASS_ORDER_LATE);


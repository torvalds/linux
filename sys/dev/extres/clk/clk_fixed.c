/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/extres/clk/clk_fixed.h>

#define	CLK_TYPE_FIXED		1
#define	CLK_TYPE_FIXED_FACTOR	2

static int clknode_fixed_init(struct clknode *clk, device_t dev);
static int clknode_fixed_recalc(struct clknode *clk, uint64_t *freq);
static int clknode_fixed_set_freq(struct clknode *clk, uint64_t fin,
    uint64_t *fout, int flags, int *stop);

struct clknode_fixed_sc {
	int		fixed_flags;
	uint64_t	freq;
	uint32_t	mult;
	uint32_t	div;
};

static clknode_method_t clknode_fixed_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,	   clknode_fixed_init),
	CLKNODEMETHOD(clknode_recalc_freq, clknode_fixed_recalc),
	CLKNODEMETHOD(clknode_set_freq,    clknode_fixed_set_freq),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(clknode_fixed, clknode_fixed_class, clknode_fixed_methods,
   sizeof(struct clknode_fixed_sc), clknode_class);

static int
clknode_fixed_init(struct clknode *clk, device_t dev)
{
	struct clknode_fixed_sc *sc;

	sc = clknode_get_softc(clk);
	if (sc->freq == 0)
		clknode_init_parent_idx(clk, 0);
	return(0);
}

static int
clknode_fixed_recalc(struct clknode *clk, uint64_t *freq)
{
	struct clknode_fixed_sc *sc;

	sc = clknode_get_softc(clk);

	if ((sc->mult != 0) && (sc->div != 0))
		*freq = (*freq / sc->div) * sc->mult;
	else
		*freq = sc->freq;
	return (0);
}

static int
clknode_fixed_set_freq(struct clknode *clk, uint64_t fin, uint64_t *fout,
    int flags, int *stop)
{
	struct clknode_fixed_sc *sc;

	sc = clknode_get_softc(clk);
	if (sc->mult == 0 || sc->div == 0) {
		/* Fixed frequency clock. */
		*stop = 1;
		if (*fout != sc->freq)
			return (ERANGE);
		return (0);
	}
	/* Fixed factor clock. */
	*stop = 0;
	*fout = (*fout / sc->mult) *  sc->div;
	return (0);
}

int
clknode_fixed_register(struct clkdom *clkdom, struct clk_fixed_def *clkdef)
{
	struct clknode *clk;
	struct clknode_fixed_sc *sc;

	clk = clknode_create(clkdom, &clknode_fixed_class, &clkdef->clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);
	sc->fixed_flags = clkdef->fixed_flags;
	sc->freq = clkdef->freq;
	sc->mult = clkdef->mult;
	sc->div = clkdef->div;

	clknode_register(clkdom, clk);
	return (0);
}

#ifdef FDT

static struct ofw_compat_data compat_data[] = {
	{"fixed-clock",		CLK_TYPE_FIXED},
	{"fixed-factor-clock",  CLK_TYPE_FIXED_FACTOR},
	{NULL,		 	0},
};

struct clk_fixed_softc {
	device_t	dev;
	struct clkdom	*clkdom;
};

static int
clk_fixed_probe(device_t dev)
{
	intptr_t clk_type;

	clk_type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	switch (clk_type) {
	case CLK_TYPE_FIXED:
		device_set_desc(dev, "Fixed clock");
		return (BUS_PROBE_DEFAULT);
	case CLK_TYPE_FIXED_FACTOR:
		device_set_desc(dev, "Fixed factor clock");
		return (BUS_PROBE_DEFAULT);
	default:
		return (ENXIO);
	}
}

static int
clk_fixed_init_fixed(struct clk_fixed_softc *sc, phandle_t node,
    struct clk_fixed_def *def)
{
	uint32_t freq;
	int rv;

	def->clkdef.id = 1;
	rv = OF_getencprop(node, "clock-frequency", &freq,  sizeof(freq));
	if (rv <= 0)
		return (ENXIO);
	def->freq = freq;
	return (0);
}

static int
clk_fixed_init_fixed_factor(struct clk_fixed_softc *sc, phandle_t node,
    struct clk_fixed_def *def)
{
	int rv;
	clk_t  parent;

	def->clkdef.id = 1;
	rv = OF_getencprop(node, "clock-mult", &def->mult,  sizeof(def->mult));
	if (rv <= 0)
		return (ENXIO);
	rv = OF_getencprop(node, "clock-div", &def->div,  sizeof(def->div));
	if (rv <= 0)
		return (ENXIO);
	/* Get name of parent clock */
	rv = clk_get_by_ofw_index(sc->dev, 0, 0, &parent);
	if (rv != 0)
		return (ENXIO);
	def->clkdef.parent_names = malloc(sizeof(char *), M_OFWPROP, M_WAITOK);
	def->clkdef.parent_names[0] = clk_get_name(parent);
	def->clkdef.parent_cnt  = 1;
	clk_release(parent);
	return (0);
}

static int
clk_fixed_attach(device_t dev)
{
	struct clk_fixed_softc *sc;
	intptr_t clk_type;
	phandle_t node;
	struct clk_fixed_def def;
	int rv;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node  = ofw_bus_get_node(dev);
	clk_type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	bzero(&def, sizeof(def));
	if (clk_type == CLK_TYPE_FIXED)
		rv = clk_fixed_init_fixed(sc, node, &def);
	else if (clk_type == CLK_TYPE_FIXED_FACTOR)
		rv = clk_fixed_init_fixed_factor(sc, node, &def);
	else
		rv = ENXIO;
	if (rv != 0) {
		device_printf(sc->dev, "Cannot FDT parameters.\n");
		goto fail;
	}
	rv = clk_parse_ofw_clk_name(dev, node, &def.clkdef.name);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot parse clock name.\n");
		goto fail;
	}
	sc->clkdom = clkdom_create(dev);
	KASSERT(sc->clkdom != NULL, ("Clock domain is NULL"));

	rv = clknode_fixed_register(sc->clkdom, &def);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot register fixed clock.\n");
		rv = ENXIO;
		goto fail;
	}

	rv = clkdom_finit(sc->clkdom);
	if (rv != 0) {
		device_printf(sc->dev, "Clk domain finit fails.\n");
		rv = ENXIO;
		goto fail;
	}
#ifdef CLK_DEBUG
	clkdom_dump(sc->clkdom);
#endif
	OF_prop_free(__DECONST(char *, def.clkdef.name));
	OF_prop_free(def.clkdef.parent_names);
	return (bus_generic_attach(dev));

fail:
	OF_prop_free(__DECONST(char *, def.clkdef.name));
	OF_prop_free(def.clkdef.parent_names);
	return (rv);
}

static device_method_t clk_fixed_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		clk_fixed_probe),
	DEVMETHOD(device_attach,	clk_fixed_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(clk_fixed, clk_fixed_driver, clk_fixed_methods,
    sizeof(struct clk_fixed_softc));
static devclass_t clk_fixed_devclass;
EARLY_DRIVER_MODULE(clk_fixed, simplebus, clk_fixed_driver,
    clk_fixed_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(clk_fixed, 1);

#endif

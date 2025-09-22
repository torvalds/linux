/*	$OpenBSD: simplebus.c,v 1.3 2023/09/22 01:10:43 jsg Exp $	*/

/*
 * Copyright (c) 2016 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <machine/fdt.h>
#include <machine/simplebusvar.h>

int simplebus_match(struct device *, void *, void *);
void simplebus_attach(struct device *, struct device *, void *);

void simplebus_attach_node(struct device *, int);
int simplebus_bs_map(bus_space_tag_t, bus_addr_t, bus_size_t,
    int, bus_space_handle_t *);

const struct cfattach simplebus_ca = {
	sizeof(struct simplebus_softc), simplebus_match, simplebus_attach
};

struct cfdriver simplebus_cd = {
	NULL, "simplebus", DV_DULL
};

/*
 * Simplebus is a generic bus with no special casings.
 */
int
simplebus_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *fa = (struct fdt_attach_args *)aux;

	/* Guard against non-fdt attach on iobus(4). */
	if (strlen(fa->fa_name) > 0)
		return (0);

	if (fa->fa_node == 0)
		return (0);

	if (!OF_is_compatible(fa->fa_node, "simple-bus"))
		return (0);

	return (1);
}

void
simplebus_attach(struct device *parent, struct device *self, void *aux)
{
	struct simplebus_softc *sc = (struct simplebus_softc *)self;
	struct fdt_attach_args *fa = (struct fdt_attach_args *)aux;
	char name[32];
	int node;

	sc->sc_node = fa->fa_node;
	sc->sc_iot = fa->fa_iot;
	sc->sc_dmat = fa->fa_dmat;
	sc->sc_acells = OF_getpropint(sc->sc_node, "#address-cells",
	    fa->fa_acells);
	sc->sc_scells = OF_getpropint(sc->sc_node, "#size-cells",
	    fa->fa_scells);
	sc->sc_pacells = fa->fa_acells;
	sc->sc_pscells = fa->fa_scells;

	if (OF_getprop(sc->sc_node, "name", name, sizeof(name)) > 0) {
		name[sizeof(name) - 1] = 0;
		printf(": \"%s\"", name);
	}

	printf("\n");

	memcpy(&sc->sc_bus, sc->sc_iot, sizeof(sc->sc_bus));
	sc->sc_bus.bus_private = sc;
	sc->sc_bus._space_map = simplebus_bs_map;

	sc->sc_rangeslen = OF_getproplen(sc->sc_node, "ranges");
	if (sc->sc_rangeslen > 0 && !(sc->sc_rangeslen % sizeof(uint32_t))) {
		sc->sc_ranges = malloc(sc->sc_rangeslen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "ranges", sc->sc_ranges,
		    sc->sc_rangeslen);
	}

	/* Scan the whole tree. */
	sc->sc_early = 1;
	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) 
		simplebus_attach_node(self, node);

	sc->sc_early = 0;
	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) 
		simplebus_attach_node(self, node);
}

int
simplebus_submatch(struct device *self, void *match, void *aux)
{
	struct simplebus_softc	*sc = (struct simplebus_softc *)self;
	struct cfdata *cf = match;

	if (cf->cf_loc[0] == sc->sc_early)
		return (*cf->cf_attach->ca_match)(self, match, aux);
	return 0;
}

int
simplebus_print(void *aux, const char *pnp)
{
	struct fdt_attach_args *fa = aux;
	char name[32];

	if (!pnp)
		return (QUIET);

	if (OF_getprop(fa->fa_node, "name", name, sizeof(name)) > 0) {
		name[sizeof(name) - 1] = 0;
		printf("\"%s\"", name);
	} else
		printf("node %u", fa->fa_node);

	printf(" at %s", pnp);

	return (UNCONF);
}

/*
 * Look for a driver that wants to be attached to this node.
 */
void
simplebus_attach_node(struct device *self, int node)
{
	struct simplebus_softc	*sc = (struct simplebus_softc *)self;
	struct fdt_attach_args	 fa;
	char			 buffer[128];
	int			 i, len, line;
	uint32_t		*cell, *reg;

	if (!OF_getprop(node, "compatible", buffer, sizeof(buffer)))
		return;

	if (OF_getprop(node, "status", buffer, sizeof(buffer)))
		if (!strcmp(buffer, "disabled"))
			return;

	memset(&fa, 0, sizeof(fa));
	fa.fa_name = "";
	fa.fa_node = node;
	fa.fa_iot = &sc->sc_bus;
	fa.fa_dmat = sc->sc_dmat;
	fa.fa_acells = sc->sc_acells;
	fa.fa_scells = sc->sc_scells;

	len = OF_getproplen(node, "reg");
	line = (sc->sc_acells + sc->sc_scells) * sizeof(uint32_t);
	if (len > 0 && line > 0 && (len % line) == 0) {
		reg = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(node, "reg", reg, len);

		fa.fa_reg = malloc((len / line) * sizeof(struct fdt_reg),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		fa.fa_nreg = (len / line);

		for (i = 0, cell = reg; i < len / line; i++) {
			if (sc->sc_acells >= 1)
				fa.fa_reg[i].addr = cell[0];
			if (sc->sc_acells == 2) {
				fa.fa_reg[i].addr <<= 32;
				fa.fa_reg[i].addr |= cell[1];
			}
			cell += sc->sc_acells;
			if (sc->sc_scells >= 1)
				fa.fa_reg[i].size = cell[0];
			if (sc->sc_scells == 2) {
				fa.fa_reg[i].size <<= 32;
				fa.fa_reg[i].size |= cell[1];
			}
			cell += sc->sc_scells;
		}

		free(reg, M_TEMP, len);
	}

	len = OF_getproplen(node, "interrupts");
	if (len > 0 && (len % sizeof(uint32_t)) == 0) {
		fa.fa_intr = malloc(len, M_DEVBUF, M_WAITOK);
		fa.fa_nintr = len / sizeof(uint32_t);

		OF_getpropintarray(node, "interrupts", fa.fa_intr, len);
	}

	config_found_sm(self, &fa, sc->sc_early ? NULL : simplebus_print,
	    simplebus_submatch);

	free(fa.fa_reg, M_DEVBUF, fa.fa_nreg * sizeof(struct fdt_reg));
	free(fa.fa_intr, M_DEVBUF, fa.fa_nintr * sizeof(uint32_t));
}

/*
 * Translate memory address if needed.
 */
int
simplebus_bs_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size,
    int flag, bus_space_handle_t *bshp)
{
	struct simplebus_softc *sc = t->bus_private;
	uint64_t addr, rfrom, rto, rsize;
	uint32_t *range;
	int parent, rlen, rone;

	addr = bpa;
	parent = OF_parent(sc->sc_node);
	if (parent == 0)
		return bus_space_map(sc->sc_iot, addr, size, flag, bshp);

	if (sc->sc_rangeslen < 0)
		return EINVAL;
	if (sc->sc_rangeslen == 0)
		return bus_space_map(sc->sc_iot, addr, size, flag, bshp);

	rlen = sc->sc_rangeslen / sizeof(uint32_t);
	rone = sc->sc_pacells + sc->sc_acells + sc->sc_scells;

	/* For each range. */
	for (range = sc->sc_ranges; rlen >= rone; rlen -= rone, range += rone) {
		/* Extract from and size, so we can see if we fit. */
		rfrom = range[0];
		if (sc->sc_acells == 2)
			rfrom = (rfrom << 32) + range[1];
		rsize = range[sc->sc_acells + sc->sc_pacells];
		if (sc->sc_scells == 2)
			rsize = (rsize << 32) +
			    range[sc->sc_acells + sc->sc_pacells + 1];

		/* Try next, if we're not in the range. */
		if (addr < rfrom || (addr + size) > (rfrom + rsize))
			continue;

		/* All good, extract to address and translate. */
		rto = range[sc->sc_acells];
		if (sc->sc_pacells == 2)
			rto = (rto << 32) + range[sc->sc_acells + 1];

		addr -= rfrom;
		addr += rto;

		return bus_space_map(sc->sc_iot, addr, size, flag, bshp);
	}

	return ESRCH;
}

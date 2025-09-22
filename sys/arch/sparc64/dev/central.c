/*	$OpenBSD: central.c,v 1.12 2022/10/16 01:22:39 jsg Exp $	*/

/*
 * Copyright (c) 2004 Jason L. Wright (jason@thought.net)
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/timeout.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/centralvar.h>

struct central_softc {
	struct device sc_dv;
	bus_space_tag_t sc_bt;
	bus_space_tag_t sc_cbt;
	int sc_node;
	int sc_nrange;
	struct central_range *sc_range;
};

int central_match(struct device *, void *, void *);
void central_attach(struct device *, struct device *, void *);

int central_print(void *, const char *);
int central_get_string(int, char *, char **);

bus_space_tag_t central_alloc_bus_tag(struct central_softc *);
int _central_bus_map(bus_space_tag_t, bus_space_tag_t, bus_addr_t, bus_size_t,
    int, bus_space_handle_t *);

int
central_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "central") == 0)
		return (1);
	return (0);
}

void
central_attach(struct device *parent, struct device *self, void *aux)
{
	struct central_softc *sc = (struct central_softc *)self;
	struct mainbus_attach_args *ma = aux;
	int node0, node;

	sc->sc_bt = ma->ma_bustag;
	sc->sc_node = ma->ma_node;
	sc->sc_cbt = central_alloc_bus_tag(sc);

	getprop(sc->sc_node, "ranges", sizeof(struct central_range),
	    &sc->sc_nrange, (void **)&sc->sc_range);

	printf("\n");

	node0 = firstchild(sc->sc_node);
	for (node = node0; node; node = nextsibling(node)) {
		struct central_attach_args ca;

		bzero(&ca, sizeof(ca));
		ca.ca_node = node;
		ca.ca_bustag = sc->sc_cbt;
		if (central_get_string(ca.ca_node, "name", &ca.ca_name)) {
			printf("can't fetch name for node 0x%x\n", node);
			continue;
		}

		getprop(node, "reg", sizeof(struct central_reg),
		    &ca.ca_nreg, (void **)&ca.ca_reg);

		(void)config_found(&sc->sc_dv, (void *)&ca, central_print);

		free(ca.ca_name, M_DEVBUF, 0);
	}
}

int
central_get_string(int node, char *name, char **buf)
{
	int len;

	len = getproplen(node, name);
	if (len < 0)
		return (len);
	*buf = (char *)malloc(len + 1, M_DEVBUF, M_NOWAIT);
	if (*buf == NULL)
		return (-1);

	if (len != 0)
		getpropstringA(node, name, *buf);
	(*buf)[len] = '\0';
	return (0);
}

int
central_print(void *args, const char *busname)
{
	struct central_attach_args *ca = args;
	char *class;

	if (busname != NULL) {
		printf("\"%s\" at %s", ca->ca_name, busname);
		class = getpropstring(ca->ca_node, "device_type");
		if (*class != '\0')
			printf(" class %s", class);
	}
	return (UNCONF);
}

bus_space_tag_t
central_alloc_bus_tag(struct central_softc *sc)
{
	struct sparc_bus_space_tag *bt;

	bt = malloc(sizeof(*bt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bt == NULL)
		panic("central: couldn't alloc bus tag");

	strlcpy(bt->name, sc->sc_dv.dv_xname, sizeof(bt->name));
	bt->cookie = sc;
	bt->parent = sc->sc_bt;
	bt->asi = bt->parent->asi;
	bt->sasi = bt->parent->sasi;
	bt->sparc_bus_map = _central_bus_map;
	/* XXX bt->sparc_bus_mmap = central_bus_mmap; */
	/* XXX bt->sparc_intr_establish = upa_intr_establish; */
	return (bt);
}

int
_central_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t addr,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	struct central_softc *sc = t->cookie;
	int64_t slot = BUS_ADDR_IOSPACE(addr);
	int64_t offset = BUS_ADDR_PADDR(addr);
	int i;

	if (t->parent == NULL || t->parent->sparc_bus_map == NULL) {
		printf("\ncentral_bus_map: invalid parent");
		return (EINVAL);
	}

	if (flags & BUS_SPACE_MAP_PROMADDRESS)
		return ((*t->parent->sparc_bus_map)(t, t0, addr,
		    size, flags, hp));

	for (i = 0; i < sc->sc_nrange; i++) {
		bus_addr_t paddr;

		if (sc->sc_range[i].cspace != slot)
			continue;

		paddr = offset - sc->sc_range[i].coffset;
		paddr += sc->sc_range[i].poffset;
		paddr |= ((bus_addr_t)sc->sc_range[i].pspace << 32);

		return ((*t->parent->sparc_bus_map)(t->parent, t0, paddr,
		    size, flags, hp));
	}

	return (EINVAL);
}

const struct cfattach central_ca = {
	sizeof(struct central_softc), central_match, central_attach
};

struct cfdriver central_cd = {
	NULL, "central", DV_DULL
};

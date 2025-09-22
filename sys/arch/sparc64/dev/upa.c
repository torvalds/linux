/*	$OpenBSD: upa.c,v 1.11 2021/10/24 17:05:04 mpi Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/timeout.h>
#include <sys/malloc.h>

#define _SPARC_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

struct upa_range {
	u_int64_t	ur_space;
	u_int64_t	ur_addr;
	u_int64_t	ur_len;
};

struct upa_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_reg[3];
	struct upa_range	*sc_range;
	int			sc_node;
	int			sc_nrange;
	bus_space_tag_t		sc_cbt;
};

int	upa_match(struct device *, void *, void *);
void	upa_attach(struct device *, struct device *, void *);

const struct cfattach upa_ca = {
	sizeof(struct upa_softc), upa_match, upa_attach
};

struct cfdriver upa_cd = {
	NULL, "upa", DV_DULL
};

int upa_print(void *, const char *);
bus_space_tag_t upa_alloc_bus_tag(struct upa_softc *);
int upa_bus_map(bus_space_tag_t, bus_space_tag_t, bus_addr_t,
    bus_size_t, int, bus_space_handle_t *);
paddr_t upa_bus_mmap(bus_space_tag_t, bus_space_tag_t,
    bus_addr_t, off_t, int, int);

int
upa_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "upa") == 0)
		return (1);

	return (0);
}

void
upa_attach(struct device *parent, struct device *self, void *aux)
{
	struct upa_softc *sc = (void *)self;
	struct mainbus_attach_args *ma = aux;
	int i, node;

	sc->sc_bt = ma->ma_bustag;
	sc->sc_node = ma->ma_node;

	for (i = 0; i < 3; i++) {
		if (i >= ma->ma_nreg) {
			printf(": no register %d\n", i);
			return;
		}
		if (bus_space_map(sc->sc_bt, ma->ma_reg[i].ur_paddr,
		    ma->ma_reg[i].ur_len, 0, &sc->sc_reg[i])) {
			printf(": failed to map reg%d\n", i);
			return;
		}
	}

	if (getprop(sc->sc_node, "ranges", sizeof(struct upa_range),
	    &sc->sc_nrange, (void **)&sc->sc_range))
		panic("upa: can't get ranges");

	printf("\n");

	sc->sc_cbt = upa_alloc_bus_tag(sc);

	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) {
		char buf[32];
		struct mainbus_attach_args map;

		bzero(&map, sizeof(map));
		if (OF_getprop(node, "device_type", buf, sizeof(buf)) <= 0)
			continue;
		if (getprop(node, "reg", sizeof(*map.ma_reg),
		    &map.ma_nreg, (void **)&map.ma_reg) != 0)
			continue;
		if (OF_getprop(node, "name", buf, sizeof(buf)) <= 0)
			continue;
		map.ma_node = node;
		map.ma_name = buf;
		map.ma_bustag = sc->sc_cbt;
		map.ma_dmatag = ma->ma_dmatag;
		config_found(&sc->sc_dev, &map, upa_print);
	}
}

int
upa_print(void *args, const char *name)
{
	struct mainbus_attach_args *ma = args;

	if (name)
		printf("\"%s\" at %s", ma->ma_name, name);
	return (UNCONF);
}

bus_space_tag_t
upa_alloc_bus_tag(struct upa_softc *sc)
{
	struct sparc_bus_space_tag *bt;

	bt = malloc(sizeof(*bt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bt == NULL)
		panic("upa: couldn't alloc bus tag");

	strlcpy(bt->name, sc->sc_dev.dv_xname, sizeof(bt->name));
	bt->cookie = sc;
	bt->parent = sc->sc_bt;
	bt->asi = bt->parent->asi;
	bt->sasi = bt->parent->sasi;
	bt->sparc_bus_map = upa_bus_map;
	bt->sparc_bus_mmap = upa_bus_mmap;
	/* XXX bt->sparc_intr_establish = upa_intr_establish; */
	return (bt);
}

int
upa_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t offset,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	struct upa_softc *sc = t->cookie;
	int i;

	if (t->parent == 0 || t->parent->sparc_bus_map == 0) {
		printf("\n__upa_bus_map: invalid parent");
		return (EINVAL);
	}

	t = t->parent;

        if (flags & BUS_SPACE_MAP_PROMADDRESS) {
		return ((*t->sparc_bus_map)
		    (t, t0, offset, size, flags, hp));
	}

	for (i = 0; i < sc->sc_nrange; i++) {
		if (offset < sc->sc_range[i].ur_space)
			continue;
		if (offset >= (sc->sc_range[i].ur_space +
		    sc->sc_range[i].ur_space))
			continue;
		break;
	}
	if (i == sc->sc_nrange)
		return (EINVAL);

	offset -= sc->sc_range[i].ur_space;
	offset += sc->sc_range[i].ur_addr;

	return ((*t->sparc_bus_map)(t, t0, offset, size, flags, hp));
}

paddr_t
upa_bus_mmap(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t paddr,
    off_t off, int prot, int flags)
{
	struct upa_softc *sc = t->cookie;
	int i;

	if (t->parent == 0 || t->parent->sparc_bus_map == 0) {
		printf("\n__upa_bus_map: invalid parent");
		return (EINVAL);
	}

	t = t->parent;

        if (flags & BUS_SPACE_MAP_PROMADDRESS)
		return ((*t->sparc_bus_mmap)(t, t0, paddr, off, prot, flags));

	for (i = 0; i < sc->sc_nrange; i++) {
		if (paddr + off < sc->sc_range[i].ur_space)
			continue;
		if (paddr + off >= (sc->sc_range[i].ur_space +
		    sc->sc_range[i].ur_space))
			continue;
		break;
	}
	if (i == sc->sc_nrange)
		return (EINVAL);

	paddr -= sc->sc_range[i].ur_space;
	paddr += sc->sc_range[i].ur_addr;

	return ((*t->sparc_bus_mmap)(t, t0, paddr, off, prot, flags));
}

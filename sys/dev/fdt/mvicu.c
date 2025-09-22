/*	$OpenBSD: mvicu.c,v 1.8 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define ICU_SETSPI_NSR_AL	0x10
#define ICU_SETSPI_NSR_AH	0x14
#define ICU_CLRSPI_NSR_AL	0x18
#define ICU_CLRSPI_NSR_AH	0x1c
#define ICU_SET_SEI_AL		0x50
#define ICU_SET_SEI_AH		0x54
#define ICU_CLR_SEI_AL		0x58
#define ICU_CLR_SEI_AH		0x5c
#define ICU_INT_CFG(x)	(0x100 + (x) * 4)
#define  ICU_INT_ENABLE		(1 << 24)
#define  ICU_INT_EDGE		(1 << 28)
#define  ICU_INT_GROUP_SHIFT	29
#define  ICU_INT_MASK		0x3ff

#define GICP_SETSPI_NSR		0x00
#define GICP_CLRSPI_NSR		0x08

/* Devices */
#define ICU_DEVICE_SATA0	109
#define ICU_DEVICE_SATA1	107
#define ICU_DEVICE_NIRQ		207

/* Groups. */
#define ICU_GRP_NSR		0x0
#define ICU_GRP_SR		0x1
#define ICU_GRP_SEI		0x4
#define ICU_GRP_REI		0x5

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct mvicu_softc;
struct mvicu_subnode {
	struct mvicu_softc	*sn_sc;
	int			sn_group;
	struct interrupt_controller sn_ic;
	struct interrupt_controller *sn_parent_ic;
};

struct mvicu_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint64_t		sc_nsr_addr;
	uint64_t		sc_sei_addr;

	int			sc_legacy;
	struct mvicu_subnode	*sc_nodes;
};

int mvicu_match(struct device *, void *, void *);
void mvicu_attach(struct device *, struct device *, void *);

const struct cfattach	mvicu_ca = {
	sizeof (struct mvicu_softc), mvicu_match, mvicu_attach
};

struct cfdriver mvicu_cd = {
	NULL, "mvicu", DV_DULL
};

void	mvicu_register(struct mvicu_softc *, int, int);
void	*mvicu_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	mvicu_intr_disestablish(void *);
void	mvicu_intr_barrier(void *);

int
mvicu_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,cp110-icu");
}

void
mvicu_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvicu_softc *sc = (struct mvicu_softc *)self;
	struct fdt_attach_args *faa = aux;
	int i, node, nchildren;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	if (OF_child(faa->fa_node) == 0) {
		sc->sc_legacy = 1;
		sc->sc_nodes = mallocarray(1, sizeof(*sc->sc_nodes),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		mvicu_register(sc, faa->fa_node, 0);
	} else {
		for (node = OF_child(faa->fa_node), nchildren = 0;
		    node; node = OF_peer(node))
			nchildren++;
		sc->sc_nodes = mallocarray(nchildren, sizeof(*sc->sc_nodes),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		for (node = OF_child(faa->fa_node), i = 0; node;
		    node = OF_peer(node))
			mvicu_register(sc, node, i++);
	}
}

void
mvicu_register(struct mvicu_softc *sc, int node, int idx)
{
	struct mvicu_subnode *sn = &sc->sc_nodes[idx];
	struct interrupt_controller *ic;
	uint32_t phandle = 0;
	uint32_t group;
	int i;

	sn->sn_group = -1;
	if (OF_is_compatible(node, "marvell,cp110-icu") ||
	    OF_is_compatible(node, "marvell,cp110-icu-nsr"))
		sn->sn_group = ICU_GRP_NSR;
	if (OF_is_compatible(node, "marvell,cp110-icu-sei"))
		sn->sn_group = ICU_GRP_SEI;

	for (i = 0; i < ICU_DEVICE_NIRQ; i++) {
		group = HREAD4(sc, ICU_INT_CFG(i)) >> ICU_INT_GROUP_SHIFT;
		if ((sn->sn_group == ICU_GRP_NSR && group == ICU_GRP_NSR) ||
		    (sn->sn_group == ICU_GRP_SEI && group == ICU_GRP_SEI))
			HWRITE4(sc, ICU_INT_CFG(i), 0);
	}

	sn->sn_sc = sc;
	sn->sn_ic.ic_node = node;
	sn->sn_ic.ic_cookie = sn;
	sn->sn_ic.ic_establish = mvicu_intr_establish;
	sn->sn_ic.ic_disestablish = mvicu_intr_disestablish;
	sn->sn_ic.ic_barrier = mvicu_intr_barrier;

	while (node && !phandle) {
		phandle = OF_getpropint(node, "msi-parent", 0);
		node = OF_parent(node);
	}
	if (phandle == 0)
		return;

	extern LIST_HEAD(, interrupt_controller) interrupt_controllers;
	LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
		if (ic->ic_phandle == phandle)
			break;
	}
	if (ic == NULL)
		return;

	sn->sn_parent_ic = ic;
	fdt_intr_register(&sn->sn_ic);
}

void *
mvicu_intr_establish(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct mvicu_subnode *sn = cookie;
	struct mvicu_softc *sc = sn->sn_sc;
	struct interrupt_controller *ic = sn->sn_parent_ic;
	struct machine_intr_handle *ih;
	uint32_t idx, flags;
	uint64_t addr, data;
	int edge = 0;

	if (sc->sc_legacy) {
		if (cell[0] != ICU_GRP_NSR)
			return NULL;
		idx = cell[1];
		flags = cell[2];
		edge = ((flags & 0xf) == 0x1);
	} else if (sn->sn_group == ICU_GRP_NSR) {
		idx = cell[0];
		flags = cell[1];
		edge = ((flags & 0xf) == 0x1);
	} else if (sn->sn_group == ICU_GRP_SEI) {
		idx = cell[0];
		flags = cell[1];
		edge = 1;
	} else {
		return NULL;
	}

	data = flags;
	cookie = ic->ic_establish_msi(ic->ic_cookie, &addr, &data,
	    level, ci, func, arg, name);
	if (cookie == NULL)
		return NULL;

	if (sn->sn_group == ICU_GRP_NSR && !sc->sc_nsr_addr) {
		sc->sc_nsr_addr = addr;
		HWRITE4(sc, ICU_SETSPI_NSR_AL,
		    (addr + GICP_SETSPI_NSR) & 0xffffffff);
		HWRITE4(sc, ICU_SETSPI_NSR_AH,
		    (addr + GICP_SETSPI_NSR) >> 32);
		HWRITE4(sc, ICU_CLRSPI_NSR_AL,
		    (addr + GICP_CLRSPI_NSR) & 0xffffffff);
		HWRITE4(sc, ICU_CLRSPI_NSR_AH,
		    (addr + GICP_CLRSPI_NSR) >> 32);
	}

	if (sn->sn_group == ICU_GRP_SEI && !sc->sc_sei_addr) {
		sc->sc_sei_addr = addr;
		HWRITE4(sc, ICU_SET_SEI_AL, addr & 0xffffffff);
		HWRITE4(sc, ICU_SET_SEI_AH, addr >> 32);
	}

	/* Configure ICU. */
	HWRITE4(sc, ICU_INT_CFG(idx), data | ICU_INT_ENABLE |
	    (sn->sn_group << ICU_INT_GROUP_SHIFT) | (edge ? ICU_INT_EDGE : 0));

	/* Need to configure interrupt for both SATA ports. */
	if (idx == ICU_DEVICE_SATA0 || idx == ICU_DEVICE_SATA1) {
		HWRITE4(sc, ICU_INT_CFG(ICU_DEVICE_SATA0), data |
		    ICU_INT_ENABLE | (sn->sn_group << ICU_INT_GROUP_SHIFT) |
		    (edge ? ICU_INT_EDGE : 0));
		HWRITE4(sc, ICU_INT_CFG(ICU_DEVICE_SATA1), data |
		    ICU_INT_ENABLE | (sn->sn_group << ICU_INT_GROUP_SHIFT) |
		    (edge ? ICU_INT_EDGE : 0));
	}

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_ic = ic;
	ih->ih_ih = cookie;

	return ih;
}

void
mvicu_intr_disestablish(void *cookie)
{
	panic("%s", __func__);
}

void
mvicu_intr_barrier(void *cookie)
{
	struct machine_intr_handle *ih = cookie;
	struct interrupt_controller *ic = ih->ih_ic;

	ic->ic_barrier(ih->ih_ih);
}

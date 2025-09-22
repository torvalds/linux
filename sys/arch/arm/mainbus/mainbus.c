/* $OpenBSD: mainbus.c,v 1.26 2025/08/11 07:18:40 miod Exp $ */
/*
 * Copyright (c) 2016 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/ofw_thermal.h>

#include <arm/mainbus/mainbus.h>

int mainbus_match(struct device *, void *, void *);
void mainbus_attach(struct device *, struct device *, void *);

void mainbus_attach_node(struct device *, int, cfmatch_t);
int mainbus_match_status(struct device *, void *, void *);
void mainbus_attach_cpus(struct device *, cfmatch_t);
int mainbus_match_primary(struct device *, void *, void *);
int mainbus_match_secondary(struct device *, void *, void *);
void mainbus_attach_framebuffer(struct device *);

struct mainbus_softc {
	struct device		 sc_dev;
	int			 sc_node;
	bus_space_tag_t		 sc_iot;
	bus_dma_tag_t		 sc_dmat;
	int			 sc_acells;
	int			 sc_scells;
	int			*sc_ranges;
	int			 sc_rangeslen;
	int			 sc_early;
};

const struct cfattach mainbus_ca = {
	sizeof(struct mainbus_softc), mainbus_match, mainbus_attach
};

struct cfdriver mainbus_cd = {
	NULL, "mainbus", DV_DULL
};

struct arm32_bus_dma_tag mainbus_dma_tag = {
	NULL,
	_bus_dmamap_create,
	_bus_dmamap_destroy,
	_bus_dmamap_load,
	_bus_dmamap_load_mbuf,
	_bus_dmamap_load_uio,
	_bus_dmamap_load_raw,
	_bus_dmamap_load_buffer,
	_bus_dmamap_unload,
	_bus_dmamap_sync,
	_bus_dmamem_alloc,
	_bus_dmamem_alloc_range,
	_bus_dmamem_free,
	_bus_dmamem_map,
	_bus_dmamem_unmap,
	_bus_dmamem_mmap,
};

/*
 * Mainbus takes care of FDT and non-FDT machines, so we
 * always attach.
 */
int
mainbus_match(struct device *parent, void *cfdata, void *aux)
{
	return (1);
}

extern struct bus_space armv7_bs_tag;
void platform_init_mainbus(struct device *);

void
mainbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct mainbus_softc *sc = (struct mainbus_softc *)self;
	char prop[128];
	int node, len;

	arm_intr_init_fdt();

	sc->sc_node = OF_peer(0);
	sc->sc_iot = &armv7_bs_tag;
	sc->sc_dmat = &mainbus_dma_tag;
	sc->sc_acells = OF_getpropint(OF_peer(0), "#address-cells", 1);
	sc->sc_scells = OF_getpropint(OF_peer(0), "#size-cells", 1);

	len = OF_getprop(sc->sc_node, "model", prop, sizeof(prop));
	if (len > 0) {
		printf(": %s\n", prop);
		hw_prod = malloc(len, M_DEVBUF, M_NOWAIT);
		if (hw_prod)
			strlcpy(hw_prod, prop, len);
	} else
		printf(": unknown model\n");

	len = OF_getprop(sc->sc_node, "serial-number", prop, sizeof(prop));
	if (len > 0) {
		hw_serial = malloc(len, M_DEVBUF, M_NOWAIT);
		if (hw_serial)
			strlcpy(hw_serial, prop, len);
	}

	/* Attach primary CPU first. */
	mainbus_attach_cpus(self, mainbus_match_primary);

	platform_init_mainbus(self);

	sc->sc_rangeslen = OF_getproplen(OF_peer(0), "ranges");
	if (sc->sc_rangeslen > 0 && !(sc->sc_rangeslen % sizeof(uint32_t))) {
		sc->sc_ranges = malloc(sc->sc_rangeslen, M_TEMP, M_WAITOK);
		OF_getpropintarray(OF_peer(0), "ranges", sc->sc_ranges,
		    sc->sc_rangeslen);
	}

	/* Scan the whole tree. */
	sc->sc_early = 1;
	for (node = OF_child(sc->sc_node); node != 0; node = OF_peer(node))
		mainbus_attach_node(self, node, NULL);

	sc->sc_early = 0;
	for (node = OF_child(sc->sc_node); node != 0; node = OF_peer(node))
		mainbus_attach_node(self, node, NULL);
	
	mainbus_attach_framebuffer(self);

	/* Attach secondary CPUs. */
	mainbus_attach_cpus(self, mainbus_match_secondary);

	thermal_init();
}

/*
 * Look for a driver that wants to be attached to this node.
 */
void
mainbus_attach_node(struct device *self, int node, cfmatch_t submatch)
{
	struct mainbus_softc	*sc = (struct mainbus_softc *)self;
	struct fdt_attach_args	 fa;
	int			 i, len, line;
	uint32_t		*cell, *reg;

	memset(&fa, 0, sizeof(fa));
	fa.fa_name = "";
	fa.fa_node = node;
	fa.fa_iot = sc->sc_iot;
	fa.fa_dmat = sc->sc_dmat;
	fa.fa_acells = sc->sc_acells;
	fa.fa_scells = sc->sc_scells;

	len = OF_getproplen(node, "reg");
	line = (sc->sc_acells + sc->sc_scells) * sizeof(uint32_t);
	if (len > 0 && (len % line) == 0) {
		reg = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(node, "reg", reg, len);

		fa.fa_reg = malloc((len / line) * sizeof(struct fdt_reg),
		    M_DEVBUF, M_WAITOK);
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

	if (submatch == NULL)
		submatch = mainbus_match_status;
	config_found_sm(self, &fa, NULL, submatch);

	free(fa.fa_reg, M_DEVBUF, fa.fa_nreg * sizeof(struct fdt_reg));
	free(fa.fa_intr, M_DEVBUF, fa.fa_nintr * sizeof(uint32_t));
}

int
mainbus_match_status(struct device *parent, void *match, void *aux)
{
	struct mainbus_softc *sc = (struct mainbus_softc *)parent;
	struct fdt_attach_args *fa = aux;
	struct cfdata *cf = match;
	char buf[32];

	if (OF_getprop(fa->fa_node, "status", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "disabled") == 0)
		return 0;

	if (cf->cf_loc[0] == sc->sc_early)
		return (*cf->cf_attach->ca_match)(parent, match, aux);
	return 0;
}

void
mainbus_attach_cpus(struct device *self, cfmatch_t match)
{
	struct mainbus_softc *sc = (struct mainbus_softc *)self;
	int node = OF_finddevice("/cpus");
	int acells, scells;

	if (node == 0)
		return;

	acells = sc->sc_acells;
	scells = sc->sc_scells;
	sc->sc_acells = OF_getpropint(node, "#address-cells", 1);
	sc->sc_scells = OF_getpropint(node, "#size-cells", 0);

	for (node = OF_child(node); node != 0; node = OF_peer(node))
		mainbus_attach_node(self, node, match);

	sc->sc_acells = acells;
	sc->sc_scells = scells;
}

int
mainbus_match_primary(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *fa = aux;
	struct cfdata *cf = match;
	uint32_t mpidr;

	__asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r" (mpidr));

	if (fa->fa_nreg < 1 || fa->fa_reg[0].addr != (mpidr & MPIDR_AFF))
		return 0;

	return (*cf->cf_attach->ca_match)(parent, match, aux);
}

int
mainbus_match_secondary(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *fa = aux;
	struct cfdata *cf = match;
	uint32_t mpidr;

	__asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r" (mpidr));

	if (fa->fa_nreg < 1 || fa->fa_reg[0].addr == (mpidr & MPIDR_AFF))
		return 0;

	return (*cf->cf_attach->ca_match)(parent, match, aux);
}

void
mainbus_attach_framebuffer(struct device *self)
{
	int node = OF_finddevice("/chosen");

	if (node == 0)
		return;

	for (node = OF_child(node); node != 0; node = OF_peer(node))
		mainbus_attach_node(self, node, NULL);
}

/*
 * Legacy support for SoCs that do not fully use FDT.
 */
void
mainbus_legacy_found(struct device *self, char *name)
{
	union mainbus_attach_args ma;

	memset(&ma, 0, sizeof(ma));
	ma.ma_name = name;

	config_found(self, &ma, NULL);
}

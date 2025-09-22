/* $OpenBSD: armv7.c,v 1.20 2025/08/11 07:18:40 miod Exp $ */
/*
 * Copyright (c) 2005,2008 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2012-2013 Patrick Wildt <patrick@blueri.se>
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

#define _ARM32_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <arm/armv7/armv7var.h>
#include <armv7/armv7/armv7var.h>
#include <armv7/armv7/armv7_machdep.h>

struct arm32_bus_dma_tag armv7_bus_dma_tag = {
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

struct armv7_dev *armv7_devs = NULL;

#define DEVNAME(sc)	(sc)->sc_dv.dv_xname

/*
 * We do direct configuration of devices on this SoC "bus", so we
 * never call the child device's match function at all (it can be
 * NULL in the struct cfattach).
 */
int
armv7_submatch(struct device *parent, void *child, void *aux)
{
	struct cfdata *cf = child;
	struct armv7_attach_args *aa = aux;

	if (strcmp(cf->cf_driver->cd_name, aa->aa_dev->name) == 0)
		return (1);

	/* "These are not the droids you are looking for." */
	return (0);
}

void
armv7_set_devs(struct armv7_dev *devs)
{
	armv7_devs = devs;
}

struct armv7_dev *
armv7_find_dev(const char *name, int unit)
{
	struct armv7_dev *ad;

	if (armv7_devs == NULL)
		panic("%s: armv7_devs == NULL", __func__);

	for (ad = armv7_devs; ad->name != NULL; ad++) {
		if (ad->unit == unit && strcmp(ad->name, name) == 0)
			return (ad);
	}

	return (NULL);
}

void
armv7_attach(struct device *parent, struct device *self, void *aux)
{
	struct armv7_softc *sc = (struct armv7_softc *)self;
	struct board_dev *bd;

	printf("\n");

	sc->sc_board_devs = platform_board_devs();

	/* Directly configure on-board devices (dev* in config file). */
	for (bd = sc->sc_board_devs; bd->name != NULL; bd++) {
		struct armv7_dev *ad = armv7_find_dev(bd->name, bd->unit);
		struct armv7_attach_args aa;

		if (ad == NULL) {
			printf("%s: device %s unit %d not found\n",
			    DEVNAME(sc), bd->name, bd->unit);
			continue;
		}

		memset(&aa, 0, sizeof(aa));
		aa.aa_dev = ad;
		aa.aa_iot = &armv7_bs_tag;
		aa.aa_dmat = &armv7_bus_dma_tag;

		if (config_found_sm(self, &aa, NULL, armv7_submatch) == NULL)
			printf("%s: device %s unit %d not configured\n",
			    DEVNAME(sc), bd->name, bd->unit);
	}
}

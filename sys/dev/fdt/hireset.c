/*	$OpenBSD: hireset.c,v 1.2 2021/10/24 17:52:26 mpi Exp $	*/
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

struct hireset_softc {
	struct device		sc_dev;
	uint32_t		sc_rst_syscon;

	struct reset_device	sc_rd;
};

int hireset_match(struct device *, void *, void *);
void hireset_attach(struct device *, struct device *, void *);

const struct cfattach	hireset_ca = {
	sizeof (struct hireset_softc), hireset_match, hireset_attach
};

struct cfdriver hireset_cd = {
	NULL, "hireset", DV_DULL
};

void	hireset_reset(void *, uint32_t *, int);

int
hireset_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "hisilicon,hi3660-reset");
}

void
hireset_attach(struct device *parent, struct device *self, void *aux)
{
	struct hireset_softc *sc = (struct hireset_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_rst_syscon = OF_getpropint(faa->fa_node, "hisi,rst-syscon", 0);

	printf("\n");

	sc->sc_rd.rd_node = faa->fa_node;
	sc->sc_rd.rd_cookie = sc;
	sc->sc_rd.rd_reset = hireset_reset;
	reset_register(&sc->sc_rd);
}

void
hireset_reset(void *cookie, uint32_t *cells, int on)
{
	struct hireset_softc *sc = cookie;
	struct regmap *rm;
	uint32_t offset = cells[0];
	uint32_t bit = cells[1];

	rm = regmap_byphandle(sc->sc_rst_syscon);
	if (rm == NULL) {
		printf("%s: can't find regmap\n", sc->sc_dev.dv_xname);
		return;
	}

	if (on)
		regmap_write_4(rm, offset + 0, (1 << bit));
	else
		regmap_write_4(rm, offset + 4, (1 << bit));
}

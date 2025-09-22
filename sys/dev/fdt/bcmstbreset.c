/*	$OpenBSD: bcmstbreset.c,v 1.1 2025/08/20 12:01:02 kettenis Exp $	*/
/*
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/fdt.h>

/* Registers. */
#define SW_INIT_SET(_bank)	(0x00 + (_bank) * 24)
#define SW_INIT_CLR(_bank)	(0x04 + (_bank) * 24)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct bcmstbreset_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	u_int			sc_nbanks;

	struct reset_device	sc_rd;
};

int	bcmstbreset_match(struct device *, void *, void *);
void	bcmstbreset_attach(struct device *, struct device *, void *);

const struct cfattach	bcmstbreset_ca = {
	sizeof (struct bcmstbreset_softc),
	bcmstbreset_match, bcmstbreset_attach
};

struct cfdriver bcmstbreset_cd = {
	NULL, "bcmstbreset", DV_DULL
};

void	bcmstbreset_reset(void *, uint32_t *, int);

int
bcmstbreset_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,brcmstb-reset");
}

void
bcmstbreset_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmstbreset_softc *sc = (struct bcmstbreset_softc *)self;
	struct fdt_attach_args *faa = aux;

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
	sc->sc_nbanks = faa->fa_reg[0].size / 24;

	printf("\n");

	sc->sc_rd.rd_node = faa->fa_node;
	sc->sc_rd.rd_cookie = sc;
	sc->sc_rd.rd_reset = bcmstbreset_reset;
	reset_register(&sc->sc_rd);
}

void
bcmstbreset_reset(void *cookie, uint32_t *cells, int assert)
{
	struct bcmstbreset_softc *sc = cookie;
	uint32_t bank = cells[0] / 32;;
	uint32_t bit = cells[0] % 32;

	if (bank >= sc->sc_nbanks)
		return;

	if (assert)
		HWRITE4(sc, SW_INIT_SET(bank), 1U << bit);
	else
		HWRITE4(sc, SW_INIT_CLR(bank), 1U << bit);
}

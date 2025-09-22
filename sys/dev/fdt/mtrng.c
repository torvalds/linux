/*	$OpenBSD: mtrng.c,v 1.1 2025/02/14 03:11:05 hastings Exp $	*/
/*
 * Copyright (c) 2025 James Hastings <hastings@openbsd.org>
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
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define RNG_CONF		0x00
#define  RNG_READY		(1U << 31)
#define  RNG_EN			(1U << 0)
#define RNG_DATA		0x08

struct mtrng_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct timeout		sc_to;
};

int	mtrng_match(struct device *, void *, void *);
void	mtrng_attach(struct device *, struct device *, void *);

const struct cfattach	mtrng_ca = {
	sizeof (struct mtrng_softc), mtrng_match, mtrng_attach
};

struct cfdriver mtrng_cd = {
	NULL, "mtrng", DV_DULL
};

void	mtrng_rnd(void *);

int
mtrng_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "mediatek,mt7623-rng");
}

void
mtrng_attach(struct device *parent, struct device *self, void *aux)
{
	struct mtrng_softc *sc = (struct mtrng_softc *)self;
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

	clock_enable_all(faa->fa_node);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, RNG_CONF, RNG_EN);

	printf("\n");

	timeout_set(&sc->sc_to, mtrng_rnd, sc);
	mtrng_rnd(sc);
}

void
mtrng_rnd(void *arg)
{
	struct mtrng_softc *sc = arg;
	uint32_t sta;

	sta = bus_space_read_4(sc->sc_iot, sc->sc_ioh, RNG_CONF);
	if ((sta & RNG_READY) == RNG_READY)
		enqueue_randomness(bus_space_read_4(sc->sc_iot,
		    sc->sc_ioh, RNG_DATA));

	timeout_add_sec(&sc->sc_to, 1);
}

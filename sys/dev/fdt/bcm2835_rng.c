/*	$OpenBSD: bcm2835_rng.c,v 1.4 2021/10/24 17:52:26 mpi Exp $	*/
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
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define RNG_CTRL		0x0000
#define  RNG_CTRL_EN		(1 << 0)
#define RNG_STATUS		0x0004
#define  RNG_STATUS_COUNT(x)	((x) >> 24)
#define RNG_DATA		0x0008

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct bcmrng_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct timeout		sc_to;
};

int	bcmrng_match(struct device *, void *, void *);
void	bcmrng_attach(struct device *, struct device *, void *);

const struct cfattach	bcmrng_ca = {
	sizeof (struct bcmrng_softc), bcmrng_match, bcmrng_attach
};

struct cfdriver bcmrng_cd = {
	NULL, "bcmrng", DV_DULL
};

void	bcmrng_rnd(void *);

int
bcmrng_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,bcm2835-rng");
}

void
bcmrng_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmrng_softc *sc = (struct bcmrng_softc *)self;
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

	printf("\n");

	/* Discard initial numbers which may be "less" random. */
	HWRITE4(sc, RNG_STATUS, 250000);
	HWRITE4(sc, RNG_CTRL, RNG_CTRL_EN);

	timeout_set(&sc->sc_to, bcmrng_rnd, sc);
	bcmrng_rnd(sc);
}

void
bcmrng_rnd(void *arg)
{
	struct bcmrng_softc *sc = arg;
	uint32_t status, data;
	int i, count;

	status = HREAD4(sc, RNG_STATUS);
	count = MIN(4, RNG_STATUS_COUNT(status));
	for (i = 0; i < count; i++) {
		data = HREAD4(sc, RNG_DATA);
		enqueue_randomness(data);
	}

	timeout_add_sec(&sc->sc_to, 1);
}

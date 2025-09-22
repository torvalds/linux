/*	$OpenBSD: bcm2711_rng.c,v 1.3 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#define  RNG_CTRL_RBGEN_MASK	(0x1fff << 0)
#define  RNG_CTRL_RBGEN_EN	(1 << 0)
#define RNG_FIFO_DATA		0x0020
#define RNG_FIFO_COUNT		0x0024
#define  RNG_FIFO_COUNT_MASK	(0xff << 0)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct bcmirng_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct timeout		sc_to;
};

int	bcmirng_match(struct device *, void *, void *);
void	bcmirng_attach(struct device *, struct device *, void *);

const struct cfattach bcmirng_ca = {
	sizeof (struct bcmirng_softc), bcmirng_match, bcmirng_attach
};

struct cfdriver bcmirng_cd = {
	NULL, "bcmirng", DV_DULL
};

void	bcmirng_rnd(void *);

int
bcmirng_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "brcm,bcm2711-rng200") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2838-rng200"));
}

void
bcmirng_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmirng_softc *sc = (struct bcmirng_softc *)self;
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

	HWRITE4(sc, RNG_CTRL, RNG_CTRL_RBGEN_EN);

	timeout_set(&sc->sc_to, bcmirng_rnd, sc);
	bcmirng_rnd(sc);
}

void
bcmirng_rnd(void *arg)
{
	struct bcmirng_softc *sc = arg;
	uint32_t data;
	int count, i;

	count = MAX(4, HREAD4(sc, RNG_FIFO_COUNT) & RNG_FIFO_COUNT_MASK);
	for (i = 0; i < count; i++) {
		data = HREAD4(sc, RNG_FIFO_DATA);
		enqueue_randomness(data);
	}

	timeout_add_sec(&sc->sc_to, 1);
}

/*	$OpenBSD: mvrng.c,v 1.4 2021/10/24 17:52:26 mpi Exp $	*/
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
#define RNG_OUTPUT0		0x0000
#define RNG_OUTPUT1		0x0004
#define RNG_OUTPUT2		0x0008
#define RNG_OUTPUT3		0x000c
#define RNG_STATUS		0x0010
#define  RNG_STATUS_READY	(1 << 0)
#define  RNG_STATUS_SHUTDOWN	(1 << 1)
#define RNG_CONTROL		0x0014
#define  RNG_CONTROL_TRNG_EN	(1 << 10)
#define RNG_CONFIG		0x0018
#define  RNG_CONFIG_MIN_CYCLES_SHIFT	0
#define  RNG_CONFIG_MAX_CYCLES_SHIFT	16
#define RNG_FROENABLE		0x0020
#define  RNG_FROENABLE_MASK	0xffffff
#define RNG_FRODETUNE		0x0024
#define RNG_ALARMMASK		0x0028
#define RNG_ALARMSTOP		0x002c

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct mvrng_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct timeout		sc_to;
};

int	mvrng_match(struct device *, void *, void *);
void	mvrng_attach(struct device *, struct device *, void *);

const struct cfattach	mvrng_ca = {
	sizeof (struct mvrng_softc), mvrng_match, mvrng_attach
};

struct cfdriver mvrng_cd = {
	NULL, "mvrng", DV_DULL
};

void	mvrng_rnd(void *);

int
mvrng_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-8k-rng");
}

void
mvrng_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvrng_softc *sc = (struct mvrng_softc *)self;
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

	/* Configure and enable the RNG. */
	HWRITE4(sc, RNG_CONFIG, 0x5 << RNG_CONFIG_MIN_CYCLES_SHIFT |
	    0x22 << RNG_CONFIG_MAX_CYCLES_SHIFT);
	HWRITE4(sc, RNG_FRODETUNE, 0);
	HWRITE4(sc, RNG_FROENABLE, RNG_FROENABLE_MASK);
	HSET4(sc, RNG_CONTROL, RNG_CONTROL_TRNG_EN);

	timeout_set(&sc->sc_to, mvrng_rnd, sc);
	mvrng_rnd(sc);
}

void
mvrng_rnd(void *arg)
{
	struct mvrng_softc *sc = arg;
	uint32_t status, detune;

	status = HREAD4(sc, RNG_STATUS);
	if (status & RNG_STATUS_SHUTDOWN) {
		/* Clear alarms. */
		HWRITE4(sc, RNG_ALARMMASK, 0);
		HWRITE4(sc, RNG_ALARMSTOP, 0);

		/* Detune FROs that are shutdown. */
		detune = ~HREAD4(sc, RNG_FROENABLE) & RNG_FROENABLE_MASK;
		HSET4(sc, RNG_FRODETUNE, detune);

		/* Re-enable them. */
		HWRITE4(sc, RNG_FROENABLE, RNG_FROENABLE_MASK);
		HWRITE4(sc, RNG_STATUS, RNG_STATUS_SHUTDOWN);
	}
	if (status & RNG_STATUS_READY) {
		enqueue_randomness(HREAD4(sc, RNG_OUTPUT0));
		enqueue_randomness(HREAD4(sc, RNG_OUTPUT1));
		enqueue_randomness(HREAD4(sc, RNG_OUTPUT2));
		enqueue_randomness(HREAD4(sc, RNG_OUTPUT3));
		HWRITE4(sc, RNG_STATUS, RNG_STATUS_READY);
	}

	timeout_add_sec(&sc->sc_to, 1);
}

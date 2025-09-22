/*	$OpenBSD: stfrng.c,v 1.1 2023/09/23 18:29:55 kettenis Exp $	*/
/*
 * Copyright (c) 2023 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/ofw_clock.h>

/* Registers */
#define RNG_CTRL		0x0000
#define  RNG_CTRL_RANDOMIZE	0x1
#define  RNG_CTRL_RESEED	0x2
#define RNG_STAT		0x0004
#define  RNG_STAT_SEEDED	(1 << 9)
#define RNG_MODE		0x000c
#define  RNG_MODE_R256		(1 << 3)
#define RNG_ISTAT		0x0014
#define  RNG_ISTAT_RAND_RDY	(1 << 0)
#define  RNG_ISTAT_LFSR_LOCKUP	(1 << 4)
#define RNG_DATA0		0x0020
#define RNG_DATA1		0x0024
#define RNG_DATA2		0x0028
#define RNG_DATA3		0x002c
#define RNG_DATA4		0x0030
#define RNG_DATA5		0x0034
#define RNG_DATA6		0x0038
#define RNG_DATA7		0x003c

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct stfrng_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct timeout		sc_to;
};

int	stfrng_match(struct device *, void *, void *);
void	stfrng_attach(struct device *, struct device *, void *);

const struct cfattach	stfrng_ca = {
	sizeof (struct stfrng_softc), stfrng_match, stfrng_attach
};

struct cfdriver stfrng_cd = {
	NULL, "stfrng", DV_DULL
};

void	stfrng_rnd(void *);

int
stfrng_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "starfive,jh7110-trng");
}

void
stfrng_attach(struct device *parent, struct device *self, void *aux)
{
	struct stfrng_softc *sc = (struct stfrng_softc *)self;
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

	clock_enable(faa->fa_node, "hclk");
	clock_enable(faa->fa_node, "ahb");
	reset_deassert(faa->fa_node, NULL);

	/* Clear all interrupts. */
	HWRITE4(sc, RNG_ISTAT, 0xffffffff);

	HWRITE4(sc, RNG_MODE, RNG_MODE_R256);
	HWRITE4(sc, RNG_CTRL, RNG_CTRL_RESEED);

	timeout_set(&sc->sc_to, stfrng_rnd, sc);
	stfrng_rnd(sc);
}

void
stfrng_rnd(void *arg)
{
	struct stfrng_softc *sc = arg;
	uint32_t stat, istat;

	stat = HREAD4(sc, RNG_STAT);
	if (stat & RNG_STAT_SEEDED) {
		istat = HREAD4(sc, RNG_ISTAT);
		if (istat & RNG_ISTAT_RAND_RDY) {
			HWRITE4(sc, RNG_ISTAT, RNG_ISTAT_RAND_RDY);
			enqueue_randomness(HREAD4(sc, RNG_DATA0));
			enqueue_randomness(HREAD4(sc, RNG_DATA1));
			enqueue_randomness(HREAD4(sc, RNG_DATA2));
			enqueue_randomness(HREAD4(sc, RNG_DATA3));
			enqueue_randomness(HREAD4(sc, RNG_DATA4));
			enqueue_randomness(HREAD4(sc, RNG_DATA5));
			enqueue_randomness(HREAD4(sc, RNG_DATA6));
			enqueue_randomness(HREAD4(sc, RNG_DATA7));
		}

		if (istat & RNG_ISTAT_LFSR_LOCKUP)
			HWRITE4(sc, RNG_CTRL, RNG_CTRL_RESEED);
		else
			HWRITE4(sc, RNG_CTRL, RNG_CTRL_RANDOMIZE);
	}

	timeout_add_sec(&sc->sc_to, 1);
}

/* $OpenBSD: mvagc.c,v 1.2 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2016 Patrick Wildt <patrick@blueri.se>
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
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct mvagc_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	int			 sc_node;
	struct clock_device	 sc_cd;
};

int	 mvagc_match(struct device *, void *, void *);
void	 mvagc_attach(struct device *, struct device *, void *);

void	 mvagc_enable(void *, uint32_t *, int);
uint32_t mvagc_gen_get_frequency(void *, uint32_t *);

const struct cfattach mvagc_ca = {
	sizeof (struct mvagc_softc), mvagc_match, mvagc_attach
};

struct cfdriver mvagc_cd = {
	NULL, "mvagc", DV_DULL
};

int
mvagc_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node,
	    "marvell,armada-380-gating-clock"));
}

void
mvagc_attach(struct device *parent, struct device *self, void *args)
{
	struct mvagc_softc *sc = (struct mvagc_softc *)self;
	struct fdt_attach_args *faa = args;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	sc->sc_cd.cd_node = sc->sc_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_frequency = mvagc_gen_get_frequency;
	sc->sc_cd.cd_enable = mvagc_enable;
	clock_register(&sc->sc_cd);
}

/*
 * A "generic" function that simply gets the clock frequency from the
 * parent clock.  Useful for clock gating devices that don't scale
 * their clocks.
 */
uint32_t
mvagc_gen_get_frequency(void *cookie, uint32_t *cells)
{
	struct mvagc_softc *sc = cookie;

	return clock_get_frequency(sc->sc_node, NULL);
}

void
mvagc_enable(void *cookie, uint32_t *cells, int on)
{
	struct mvagc_softc *sc = cookie;
	uint32_t id = cells[0];

	if (id >= 32)
		return;

	if (on)
		HSET4(sc, 0, (1 << id));
	else
		HCLR4(sc, 0, (1 << id));
}

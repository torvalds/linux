/* $OpenBSD: mvsysctrl.c,v 1.2 2021/10/24 17:52:27 mpi Exp $ */
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
#include <armv7/armv7/armv7_machdep.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define RSTOUTN				0x60
#define  RSTOUTN_GLOBAL_SOFT_RSTOUT_EN	1
#define SYSSOFTRST			0x64
#define  SYSSOFTRST_GLOBAL_SOFT_RST	1

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct mvsysctrl_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
};

int	 mvsysctrl_match(struct device *, void *, void *);
void	 mvsysctrl_attach(struct device *, struct device *, void *);

void	 mvsysctrl_reset(void);

struct mvsysctrl_softc *mvsysctrl_sc;

const struct cfattach	mvsysctrl_ca = {
	sizeof (struct mvsysctrl_softc), mvsysctrl_match, mvsysctrl_attach
};

struct cfdriver mvsysctrl_cd = {
	NULL, "mvsysctrl", DV_DULL
};

int
mvsysctrl_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node,
	    "marvell,armada-370-xp-system-controller");
}

void
mvsysctrl_attach(struct device *parent, struct device *self, void *args)
{
	struct mvsysctrl_softc *sc = (struct mvsysctrl_softc *)self;
	struct fdt_attach_args *faa = args;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	mvsysctrl_sc = sc;
	cpuresetfn = mvsysctrl_reset;
}

void
mvsysctrl_reset(void)
{
	struct mvsysctrl_softc *sc = mvsysctrl_sc;

	HWRITE4(sc, RSTOUTN, RSTOUTN_GLOBAL_SOFT_RSTOUT_EN);
	HWRITE4(sc, SYSSOFTRST, SYSSOFTRST_GLOBAL_SOFT_RST);

	for (;;)
		continue;
}

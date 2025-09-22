/*	$OpenBSD: aplrtk.c,v 1.4 2024/10/29 21:19:25 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <arm64/dev/rtkit.h>

#define CPU_CTRL		0x0044
#define CPU_CTRL_RUN		(1 << 4)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct aplrtk_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;

	int			sc_node;
	uint32_t		sc_phandle;

	struct rtkit		sc_rtkit;
	struct rtkit_state	*sc_state;
};

int	aplrtk_match(struct device *, void *, void *);
void	aplrtk_attach(struct device *, struct device *, void *);

const struct cfattach aplrtk_ca = {
	sizeof (struct aplrtk_softc), aplrtk_match, aplrtk_attach
};

struct cfdriver aplrtk_cd = {
	NULL, "aplrtk", DV_DULL
};

int
aplrtk_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,rtk-helper-asc4");
}

void
aplrtk_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplrtk_softc *sc = (struct aplrtk_softc *)self;
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

	sc->sc_dmat = faa->fa_dmat;
	sc->sc_node = faa->fa_node;
	sc->sc_phandle = OF_getpropint(faa->fa_node, "phandle", 0);

	printf("\n");
}

int
aplrtk_do_start(struct aplrtk_softc *sc)
{
	uint32_t ctrl;

	ctrl = HREAD4(sc, CPU_CTRL);
	HWRITE4(sc, CPU_CTRL, ctrl | CPU_CTRL_RUN);

	sc->sc_rtkit.rk_cookie = sc;
	sc->sc_rtkit.rk_dmat = sc->sc_dmat;
	sc->sc_state = rtkit_init(sc->sc_node, NULL, 0, &sc->sc_rtkit);
	if (sc->sc_state == NULL)
		return EIO;

	return rtkit_boot(sc->sc_state);
}

int
aplrtk_start(uint32_t phandle)
{
	struct aplrtk_softc *sc;
	int i;

	for (i = 0; i < aplrtk_cd.cd_ndevs; i++) {
		sc = aplrtk_cd.cd_devs[i];
		if (sc == NULL)
			continue;
		if (sc->sc_phandle == phandle)
			return aplrtk_do_start(sc);
	}

	return ENXIO;
}

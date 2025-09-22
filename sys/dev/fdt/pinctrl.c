/*	$OpenBSD: pinctrl.c,v 1.5 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2018, 2019 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#define HREAD2(sc, reg)							\
	(bus_space_read_2((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE2(sc, reg, val)						\
	bus_space_write_2((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct pinctrl_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_reg_width;
	uint32_t		sc_func_mask;
	uint32_t		sc_ncells;
};

int	pinctrl_match(struct device *, void *, void *);
void	pinctrl_attach(struct device *, struct device *, void *);

const struct cfattach	pinctrl_ca = {
	sizeof (struct pinctrl_softc), pinctrl_match, pinctrl_attach
};

struct cfdriver pinctrl_cd = {
	NULL, "pinctrl", DV_DULL
};

int	pinctrl_pinctrl(uint32_t, void *);

int
pinctrl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "pinctrl-single") ||
	    OF_is_compatible(faa->fa_node, "pinconf-single"));
}

void
pinctrl_attach(struct device *parent, struct device *self, void *aux)
{
	struct pinctrl_softc *sc = (struct pinctrl_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_reg_width = OF_getpropint(faa->fa_node,
	    "pinctrl-single,register-width", 0);
	if (sc->sc_reg_width != 16 &&
	    sc->sc_reg_width != 32) {
		printf(": unsupported register width\n");
		return;
	}

	sc->sc_ncells = OF_getpropint(faa->fa_node, "#pinctrl-cells", 1);
	sc->sc_func_mask = OF_getpropint(faa->fa_node,
	    "pinctrl-single,function-mask", 0);

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	pinctrl_register(faa->fa_node, pinctrl_pinctrl, sc);

	printf("\n");
}

uint32_t
pinctrl_set2(int node, char *setting, uint32_t val)
{
	uint32_t values[2];

	if (OF_getpropintarray(node, setting, values, sizeof(values)) !=
	    sizeof(values))
		return val;

	val &= ~values[1];
	val |= (values[0] & values[1]);
	return val;
}

uint32_t
pinctrl_set4(int node, char *setting, uint32_t val)
{
	uint32_t values[4];

	if (OF_getpropintarray(node, setting, values, sizeof(values)) !=
	    sizeof(values))
		return val;

	val &= ~values[3];
	val |= (values[0] & values[3]);
	return val;
}

int
pinctrl_pinctrl(uint32_t phandle, void *cookie)
{
	struct pinctrl_softc *sc = cookie;
	uint32_t *pins;
	int node, len, i;
	
	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "pinctrl-single,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "pinctrl-single,pins", pins, len);

	for (i = 0; i < len / sizeof(uint32_t); i += (1 + sc->sc_ncells)) {
		uint32_t reg = pins[i];
		uint32_t func = pins[i + 1];
		uint32_t val = 0;

		if (sc->sc_ncells == 2)
			func |= pins[i + 2];
		
		if (sc->sc_reg_width == 16)
			val = HREAD2(sc, reg);
		else if (sc->sc_reg_width == 32)
			val = HREAD4(sc, reg);

		val &= ~sc->sc_func_mask;
		val |= (func & sc->sc_func_mask);

		val = pinctrl_set2(node, "pinctrl-single,drive-strength", val);
		val = pinctrl_set4(node, "pinctrl-single,bias-pulldown", val);
		val = pinctrl_set4(node, "pinctrl-single,bias-pullup", val);

		if (sc->sc_reg_width == 16)
			HWRITE2(sc, reg, val);
		else if (sc->sc_reg_width == 32)
			HWRITE4(sc, reg, val);
	}

	free(pins, M_TEMP, len);
	return 0;
}

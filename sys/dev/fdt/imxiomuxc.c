/* $OpenBSD: imxiomuxc.c,v 1.8 2023/08/15 08:27:30 miod Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2016 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/socket.h>
#include <sys/timeout.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#define IOMUX_CONFIG_SION		(1 << 4)

#define IMX_PINCTRL_NO_PAD_CTL		(1U << 31)
#define IMX_PINCTRL_SION		(1 << 30)

struct imxiomuxc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

struct imxiomuxc_softc *imxiomuxc_sc;

int	imxiomuxc_match(struct device *, void *, void *);
void	imxiomuxc_attach(struct device *, struct device *, void *);

const struct cfattach imxiomuxc_ca = {
	sizeof (struct imxiomuxc_softc), imxiomuxc_match, imxiomuxc_attach
};

struct cfdriver imxiomuxc_cd = {
	NULL, "imxiomuxc", DV_DULL
};

int	imxiomuxc_pinctrl(uint32_t, void *);

int
imxiomuxc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "fsl,imx6q-iomuxc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6dl-iomuxc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6sl-iomuxc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6sx-iomuxc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6ul-iomuxc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx7d-iomuxc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mm-iomuxc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mp-iomuxc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mq-iomuxc"));
}

void
imxiomuxc_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxiomuxc_softc *sc = (struct imxiomuxc_softc *)self;
	struct fdt_attach_args *faa = aux;

	KASSERT(faa->fa_nreg >= 1);

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	pinctrl_register(faa->fa_node, imxiomuxc_pinctrl, sc);
	pinctrl_byname(faa->fa_node, "default");
	imxiomuxc_sc = sc;
	printf("\n");
}

int
imxiomuxc_pinctrl(uint32_t phandle, void *cookie)
{
	struct imxiomuxc_softc *sc = cookie;
	char name[31];
	uint32_t *pins;
	int npins;
	int node;
	int len;
	int i;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	OF_getprop(node, "name", name, sizeof(name));
	name[sizeof(name) - 1] = 0;

	len = OF_getproplen(node, "fsl,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "fsl,pins", pins, len);
	npins = len / (6 * sizeof(uint32_t));

	for (i = 0; i < npins; i++) {
		uint32_t mux_reg = pins[6 * i + 0];
		uint32_t conf_reg = pins[6 * i + 1];
		uint32_t input_reg = pins[6 * i + 2];
		uint32_t mux_val = pins[6 * i + 3];
		uint32_t conf_val = pins[6 * i + 5];
		uint32_t input_val = pins[6 * i + 4];
		uint32_t val;

		/* Set MUX mode. */
		if (conf_val & IMX_PINCTRL_SION)
			mux_val |= IOMUX_CONFIG_SION;
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, mux_reg, mux_val);

		/* Set PAD config. */
		if ((conf_val & IMX_PINCTRL_NO_PAD_CTL) == 0)
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    conf_reg, conf_val);

		/* Set input select. */
		if ((input_val >> 24) == 0xff) {
			/*
			 * Magic value used to clear or set specific
			 * bits in the general purpose registers.
			 */
			uint8_t shift = (input_val >> 16) & 0xff;
			uint8_t width = (input_val >> 8) & 0xff;
			uint32_t clr = ((1 << width) - 1) << shift;
			uint32_t set = (input_val & 0xff) << shift;

			val = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
			    input_reg);
			val &= ~clr;
			val |= set;
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    input_reg, val);
		} else if (input_reg != 0) {
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    input_reg, input_val);
		}
	}

	free(pins, M_TEMP, len);
	return 0;
}

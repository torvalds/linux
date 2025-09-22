/* $OpenBSD: imxiic_fdt.c,v 1.3 2022/04/06 18:59:28 naddy Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include <dev/ic/imxiicvar.h>

struct imxiic_fdt_softc {
	struct imxiic_softc	fc_sc;
	int			fc_node;
};

int imxiic_fdt_match(struct device *, void *, void *);
void imxiic_fdt_attach(struct device *, struct device *, void *);

void imxiic_fdt_bus_scan(struct device *, struct i2cbus_attach_args *, void *);

const struct cfattach imxiic_fdt_ca = {
	sizeof(struct imxiic_fdt_softc), imxiic_fdt_match, imxiic_fdt_attach
};

int
imxiic_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "fsl,imx21-i2c") ||
	    OF_is_compatible(faa->fa_node, "fsl,vf610-i2c"));
}

void
imxiic_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxiic_fdt_softc *fc = (struct imxiic_fdt_softc *)self;
	struct imxiic_softc *sc = &fc->fc_sc;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	fc->fc_node = faa->fa_node;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("imxiic_attach: bus_space_map failed!");

	sc->sc_reg_shift = 2;
	sc->sc_clk_div = imxiic_imx21_clk_div;
	sc->sc_clk_ndiv = nitems(imxiic_imx21_clk_div);
	sc->sc_type = I2C_TYPE_IMX21;

	if (OF_is_compatible(faa->fa_node, "fsl,vf610-i2c")) {
		sc->sc_reg_shift = 0;
		sc->sc_clk_div = imxiic_vf610_clk_div;
		sc->sc_clk_ndiv = nitems(imxiic_vf610_clk_div);
		sc->sc_type = I2C_TYPE_VF610;
	}

	printf("\n");

	clock_enable(faa->fa_node, NULL);
	pinctrl_byname(faa->fa_node, "default");

	/* set speed */
	sc->sc_clkrate = clock_get_frequency(fc->fc_node, NULL) / 1000;
	sc->sc_bitrate = OF_getpropint(fc->fc_node,
	    "clock-frequency", 100000) / 1000;
	imxiic_setspeed(sc, sc->sc_bitrate);

	/* reset */
	imxiic_enable(sc, 0);

	sc->stopped = 1;
	rw_init(&sc->sc_buslock, sc->sc_dev.dv_xname);

	struct i2cbus_attach_args iba;

	sc->i2c_tag.ic_cookie = sc;
	sc->i2c_tag.ic_acquire_bus = imxiic_i2c_acquire_bus;
	sc->i2c_tag.ic_release_bus = imxiic_i2c_release_bus;
	sc->i2c_tag.ic_exec = imxiic_i2c_exec;

	bzero(&iba, sizeof iba);
	iba.iba_name = "iic";
	iba.iba_tag = &sc->i2c_tag;
	iba.iba_bus_scan = imxiic_fdt_bus_scan;
	iba.iba_bus_scan_arg = &fc->fc_node;
	config_found(&sc->sc_dev, &iba, iicbus_print);
}

void
imxiic_fdt_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *aux)
{
	int iba_node = *(int *)aux;
	extern int iic_print(void *, const char *);
	struct i2c_attach_args ia;
	char name[32], status[32];
	uint32_t reg[1];
	int node;

	for (node = OF_child(iba_node); node; node = OF_peer(node)) {
		memset(name, 0, sizeof(name));
		memset(status, 0, sizeof(status));
		memset(reg, 0, sizeof(reg));

		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		if (OF_getprop(node, "status", status, sizeof(status)) > 0 &&
		    strcmp(status, "disabled") == 0)
			continue;

		if (OF_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			continue;

		memset(&ia, 0, sizeof(ia));
		ia.ia_tag = iba->iba_tag;
		ia.ia_addr = bemtoh32(&reg[0]);
		ia.ia_name = name;
		ia.ia_cookie = &node;

		config_found(self, &ia, iic_print);
	}
}

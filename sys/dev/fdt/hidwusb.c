/*	$OpenBSD: hidwusb.c,v 1.4 2023/09/22 01:10:44 jsg Exp $	*/
/*
 * Copyright (c) 2017, 2018 Mark Kettenis <kettenis@openbsd.org>
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
#include <machine/simplebusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

/*
 * This driver is based on preliminary device tree bindings and will
 * almost certainly need changes once the official bindings land in
 * mainline Linux.  Support for these preliminary bindings will be
 * dropped as soon as official bindings are available.
 */

struct hidwusb_softc {
	struct simplebus_softc	sc_sbus;
};

int	hidwusb_match(struct device *, void *, void *);
void	hidwusb_attach(struct device *, struct device *, void *);

const struct cfattach hidwusb_ca = {
	sizeof(struct hidwusb_softc), hidwusb_match, hidwusb_attach
};

struct cfdriver hidwusb_cd = {
	NULL, "hidwusb", DV_DULL
};

int
hidwusb_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "hisilicon,kirin970-dwc3");
}

void
hidwusb_attach(struct device *parent, struct device *self, void *aux)
{
	struct hidwusb_softc *sc = (struct hidwusb_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t gpio[3];
	int node;

	/*
	 * The HiKey970 has a switch to select between the Type-C and
	 * a hub with Type-A connectors.  Switch to the USB Type-C
	 * connector as we can't power up the hub yet.
	 */
	node = OF_finddevice("/soc/hikey_usbhub");
	if (node) {
		if (OF_getpropintarray(node, "typc_vbus_int_gpio,typec-gpios",
		    gpio, sizeof(gpio)) == sizeof(gpio)) {
			gpio_controller_config_pin(gpio, GPIO_CONFIG_OUTPUT);
			gpio_controller_set_pin(gpio, 1);
		}
	}

	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}

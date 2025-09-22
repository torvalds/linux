/*	$OpenBSD: rkdwusb.c,v 1.5 2023/09/22 01:10:44 jsg Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/fdt.h>

struct rkdwusb_softc {
	struct simplebus_softc	sc_sbus;
};

int	rkdwusb_match(struct device *, void *, void *);
void	rkdwusb_attach(struct device *, struct device *, void *);

const struct cfattach rkdwusb_ca = {
	sizeof(struct rkdwusb_softc), rkdwusb_match, rkdwusb_attach
};

struct cfdriver rkdwusb_cd = {
	NULL, "rkdwusb", DV_DULL
};

int
rkdwusb_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,rk3399-dwc3");
}

void
rkdwusb_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkdwusb_softc *sc = (struct rkdwusb_softc *)self;
	struct fdt_attach_args *faa = aux;

	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}

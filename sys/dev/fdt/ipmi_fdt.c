/*	$OpenBSD: ipmi_fdt.c,v 1.3 2024/10/09 00:38:26 jsg Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/fdt.h>

#include <dev/ipmivar.h>

int	ipmi_fdt_match(struct device *, void *, void *);
void	ipmi_fdt_attach(struct device *, struct device *, void *);

const struct cfattach ipmi_fdt_ca = {
	sizeof (struct ipmi_softc), ipmi_fdt_match, ipmi_fdt_attach,
	NULL, ipmi_activate
};

int
ipmi_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ipmi-kcs");
}

void
ipmi_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct ipmi_softc *sc = (struct ipmi_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct ipmi_attach_args ia;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	memset(&ia, 0, sizeof(ia));
	ia.iaa_memt = faa->fa_iot;
	ia.iaa_if_type = IPMI_IF_KCS;
	ia.iaa_if_rev = 0x20;
	ia.iaa_if_irq = -1;
	ia.iaa_if_irqlvl = 0;
	ia.iaa_if_iosize = OF_getpropint(faa->fa_node, "reg-size", 1);
	ia.iaa_if_iospacing = OF_getpropint(faa->fa_node, "reg-spacing", 1);
	ia.iaa_if_iobase = faa->fa_reg[0].addr;
	ia.iaa_if_iotype = 'm';

	ipmi_attach_common(sc, &ia);
}

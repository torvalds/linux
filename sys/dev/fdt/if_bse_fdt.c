/*	$OpenBSD: if_bse_fdt.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/
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

#include <net/if.h>
#include <net/if_media.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/mii/miivar.h>
#include <dev/ic/bcmgenetvar.h>

int	bse_fdt_match(struct device *, void *, void *);
void	bse_fdt_attach(struct device *, struct device *, void *);

const struct cfattach bse_fdt_ca = {
	sizeof (struct genet_softc), bse_fdt_match, bse_fdt_attach
};

int
bse_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "brcm,bcm2711-genet-v5") ||
	    OF_is_compatible(faa->fa_node, "brcm,genet-v5"));
}

void
bse_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct genet_softc *sc = (struct genet_softc *)self;
	struct fdt_attach_args *faa = aux;
	char phy_mode[16] = { 0 };
	uint32_t phy;
	int node, error;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_bst = faa->fa_iot;
	sc->sc_dmat = faa->fa_dmat;

	if (bus_space_map(sc->sc_bst, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_bsh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_NET,
	    genet_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	if (OF_getprop(faa->fa_node, "local-mac-address",
	    &sc->sc_lladdr, ETHER_ADDR_LEN) != ETHER_ADDR_LEN)
		genet_lladdr_read(sc, sc->sc_lladdr);

	OF_getprop(faa->fa_node, "phy-mode", phy_mode, sizeof(phy_mode));
	if (strcmp(phy_mode, "rgmii-id") == 0)
		sc->sc_phy_mode = GENET_PHY_MODE_RGMII_ID;
	else if (strcmp(phy_mode, "rgmii-rxid") == 0)
		sc->sc_phy_mode = GENET_PHY_MODE_RGMII_RXID;
	else if (strcmp(phy_mode, "rgmii-txid") == 0)
		sc->sc_phy_mode = GENET_PHY_MODE_RGMII_TXID;
	else
		sc->sc_phy_mode = GENET_PHY_MODE_RGMII;

	/* Lookup PHY. */
	phy = OF_getpropint(faa->fa_node, "phy-handle", 0);
	node = OF_getnodebyphandle(phy);
	if (node)
		sc->sc_phy_id = OF_getpropint(node, "reg", MII_PHY_ANY);
	else
		sc->sc_phy_id = MII_PHY_ANY;

	error = genet_attach(sc);
	if (error)
		goto disestablish;

	return;

disestablish:
	fdt_intr_disestablish(sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc_bst, sc->sc_bsh, faa->fa_reg[0].size);
}
